/* Native SDL2 frontend for DKC1Recomp.
 *
 * The recompiled cartridge/runtime stays identical to the Win32 and headless
 * hosts. This file owns only host presentation, input, queued audio, timing,
 * and user-facing save/repro shortcuts. macOS adds native menus, file pickers,
 * and Metal scanout; other Unix hosts retain the portable SDL path.
 */
#include "dkc1_blank_scan.h"
#include "dkc1_baby_kong.h"
#include "dkc1_debug_dump.h"
#include "dkc1_flight_recorder.h"
#include "dkc1_game.h"
#include "dkc1_invariant_monitor.h"
#include "dkc1_haptics.h"
#include "dkc1_msu1.h"
#include "dkc1_video.h"
#include "input_playback.h"
#include "macos_file_picker.h"
#include "macos_metal_presenter.h"
#include "verified_rom.h"
#include "wram_dump.h"

#include "common_cpu_infra.h"
#include "common_rtl.h"
#include "audio_trace.h"
#include "snes/snes.h"

#include <SDL.h>
#ifdef __APPLE__
#include <SDL_syswm.h>
#endif

#include <float.h>
#include <limits.h>
#ifdef __APPLE__
#include <mach/mach_time.h>
#include <pthread.h>
#endif
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#ifndef DKC1_BUILD_COMMIT
#define DKC1_BUILD_COMMIT "untracked"
#endif
#ifndef DKC1_BUILD_CONFIG
#define DKC1_BUILD_CONFIG "sdl-dev"
#endif

#ifdef __APPLE__
#define DKC1_HOST_PLATFORM "macos"
#define DKC1_HOST_CLOCK "mach_absolute_time"
#else
#define DKC1_HOST_PLATFORM "linux"
#define DKC1_HOST_CLOCK "SDL_GetPerformanceCounter"
#endif

enum {
  kWindowScale = 3,
  kSnesPixelAspectNumerator = 7,
  kSnesPixelAspectDenominator = 6,
  kAudioRate = 32040,
  kAudioChannels = 2,
  kAudioScratchFrames = 1024,
  kAudioFramesPerBlock = 536,
  kAudioRingStartFrames = 2136,
  kAudioMaximumQueuedFrames = kAudioRate / 4,
};

static const double kHostPresentationFramesPerSecond = 60.0;
static const double kMacNativeDisplayFramesPerSecond = 120.0;
static const double kHostWorkGuardSeconds = 0.006;
static const double kMacSubmitLeadSeconds = 0.004;
static const double kMacFinalSpinSeconds = 0.0015;

enum {
  kHapticRequestNone = 0,
  kHapticRequestPulse,
  kHapticRequestStop,
};

typedef struct Dkc1HapticWorker {
  SDL_mutex *mutex;
  SDL_cond *condition;
  SDL_Thread *thread;
  int request;
  int busy;
  int shutdown;
} Dkc1HapticWorker;

typedef struct Dkc1FramePacer {
  double frequency;
  double ticks_per_frame;
  double next_deadline;
  double estimated_work_ticks;
  double previous_present;
  double title_window_start;
  double interval_sum;
  double interval_min;
  double interval_max;
  double work_sum;
  double work_max;
  double present_wait_sum;
  double present_wait_max;
  double last_work_ticks;
  double last_present_wait_ticks;
  double last_wake_lateness_ticks;
  double last_work_reserve_ticks;
  uint64_t presented_frames;
  uint64_t interval_count;
  uint64_t present_wait_count;
  uint64_t title_window_intervals;
  uint64_t long_intervals;
  uint64_t reanchors;
} Dkc1FramePacer;

typedef struct Dkc1DisplayPacer {
  double previous_timestamp;
  double callback_interval;
  double interval_sum;
  double interval_min;
  double interval_max;
  unsigned long long previous_callback_number;
  uint64_t interval_count;
  uint64_t skipped_callbacks;
  uint64_t wait_timeouts;
} Dkc1DisplayPacer;

typedef struct Dkc1FrameWorkProfile {
  double events;
  double input;
  double emulation;
  double ppu;
  double diagnostics;
  double audio;
  double title;
} Dkc1FrameWorkProfile;

typedef struct Dkc1PacingLog {
  FILE *stream;
  double last_submit;
  double last_present;
  double target;
  double wait_ms;
  double setup_ms;
  double emulation_ms;
  double render_ms;
  double diagnostics_ms;
  double audio_ms;
  long test_stall_frame;
  unsigned test_stall_ms;
  int header_written;
  int test_stall_fired;
} Dkc1PacingLog;

static uint8_t s_pixels[kDkc1VideoWidescreenWidth * kDkc1VideoHeight * 4];
static SDL_Window *s_window;
static SDL_Renderer *s_renderer;
static SDL_Texture *s_texture;
static SDL_AudioDeviceID s_audio_device;
static SDL_GameController *s_controller;
static Dkc1Msu1 *s_msu1;
static Dkc1StompProbe s_stomp_probe;
static int16_t s_audio_scratch[kAudioScratchFrames * kAudioChannels];
static double s_audio_accumulator;
static unsigned s_audio_preroll_blocks = 2;
static unsigned s_audio_ring_start_threshold = kAudioRingStartFrames;
static unsigned s_audio_last_queued_frames;
static unsigned s_audio_ring_frames;
static unsigned long s_audio_starvations;
static unsigned long s_audio_drops;
static unsigned long long s_audio_internal_underflows;
static int s_audio_started;
static int s_audio_waiting_for_ring = 1;
static int s_audio_collect_stats;
static int s_audio_recovery_requested;
static int s_haptics_enabled = 1;
static int s_running = 1;
static int s_paused;
static int s_step_once;
static int s_fullscreen;
static Dkc1MacFullscreenScaling s_fullscreen_scaling;
static int s_width;
static int s_presentation_output_width;
static int s_presentation_output_height;
static int s_renderer_vsync;
static int s_display_link_active;
static int s_metal_presenter_active;
static long s_host_frame;
static long s_smoke_test_frames;
static int s_reanchor_pacer;
static double s_present_fps;
static double s_audio_pacing_fps = 60.0;
static char s_status[256] = "ready";
static Dkc1WramDump s_wram_dump;
static Dkc1InputPlayback s_input_playback;
static Dkc1HapticWorker s_haptic_worker;

static int EnvironmentEnabled(const char *name) {
  const char *value = getenv(name);
  return value && *value && *value != '0';
}

static double FramePacerNow(void) {
#ifdef __APPLE__
  return (double)mach_absolute_time();
#else
  return (double)SDL_GetPerformanceCounter();
#endif
}

static void FramePacerCpuRelax(void) {
#if defined(__aarch64__) || defined(__arm64__)
  __asm__ volatile("yield");
#elif defined(__x86_64__)
  __asm__ volatile("pause");
#else
  SDL_Delay(0);
#endif
}

/* Sleep on an absolute Mach deadline, retaining the final 1.5 ms for a bounded
 * CPU spin. On Apple Silicon the scheduler can coalesce an otherwise idle
 * main-thread wake by more than the old 250 us margin, which turns an exact
 * display target into alternating short/long Metal submissions. Relative
 * millisecond sleeps still accumulate phase error, so keep the deadline
 * absolute and absorb only the observed final-wake variance here. */
static void FramePacerWaitUntil(double deadline, double frequency) {
  const double spin_ticks = frequency * kMacFinalSpinSeconds;
  double now = FramePacerNow();
#ifdef __APPLE__
  if (deadline - now > spin_ticks)
    (void)mach_wait_until((uint64_t)(deadline - spin_ticks));
#else
  while (deadline - now > spin_ticks) {
    const double coarse_ticks = deadline - now - spin_ticks;
    const Uint32 coarse_ms =
        (Uint32)(coarse_ticks * 1000.0 / frequency);
    if (!coarse_ms)
      break;
    SDL_Delay(coarse_ms);
    now = FramePacerNow();
  }
#endif
  while (FramePacerNow() < deadline)
    FramePacerCpuRelax();
}

static void FramePacerInit(Dkc1FramePacer *pacer) {
#ifdef __APPLE__
  mach_timebase_info_data_t timebase = {0, 0};
  mach_timebase_info(&timebase);
  if (!timebase.numer || !timebase.denom) {
    timebase.numer = 1;
    timebase.denom = 1;
  }
  memset(pacer, 0, sizeof *pacer);
  pacer->frequency = 1000000000.0 * (double)timebase.denom /
                     (double)timebase.numer;
#else
  memset(pacer, 0, sizeof *pacer);
  pacer->frequency = (double)SDL_GetPerformanceFrequency();
  if (pacer->frequency <= 0.0)
    pacer->frequency = 1000.0;
#endif
  pacer->ticks_per_frame =
      pacer->frequency / kHostPresentationFramesPerSecond;
  pacer->next_deadline = FramePacerNow() + pacer->ticks_per_frame;
  pacer->estimated_work_ticks = pacer->frequency / 500.0;
  pacer->previous_present = FramePacerNow();
  pacer->title_window_start = pacer->previous_present;
  pacer->interval_min = DBL_MAX;
}

static void FramePacerReanchor(Dkc1FramePacer *pacer, double now) {
  /* next_deadline is the intended scanout boundary, while submission occurs
   * kMacSubmitLeadSeconds earlier. Include that lead when anchoring from the
   * last completed submit so a stall is followed by one normal 16.67 ms
   * interval rather than a short catch-up interval. */
  pacer->next_deadline =
      now + pacer->ticks_per_frame +
      pacer->frequency * kMacSubmitLeadSeconds;
  pacer->previous_present = 0.0;
  pacer->title_window_start = 0.0;
  pacer->title_window_intervals = 0;
  pacer->reanchors++;
}

/* Keep input sampling close to presentation: coarse-wait until the estimated
 * emulation/render workload should begin, then use the exact presentation
 * deadline for the final wait. The estimate rises immediately and decays
 * toward steady state over a short window so a one-off host stall does not
 * create persistent input latency. */
static void FramePacerWaitForWorkWindow(Dkc1FramePacer *pacer) {
  /* Leave six milliseconds beyond the adaptive work estimate. macOS can vary
   * texture upload, title/menu, and compositor preparation cost by more than
   * the old 250 us guard even when the complete frame remains inexpensive;
   * the measured steady-state high-water mark was 6.707 ms while the estimate
   * had decayed to 2.168 ms. The wider guard absorbs that real variance.
   * Starting that work slightly earlier lets the same vsync absorb the
   * variance instead of turning it into a dropped presentation slot. */
  double reserve = pacer->estimated_work_ticks +
                   pacer->frequency * kHostWorkGuardSeconds;
  const double maximum = pacer->ticks_per_frame * 0.75;
  if (reserve > maximum)
    reserve = maximum;
  const double work_deadline = pacer->next_deadline - reserve;
  pacer->last_work_reserve_ticks = reserve;
  FramePacerWaitUntil(work_deadline, pacer->frequency);
  const double wake_lateness = FramePacerNow() - work_deadline;
  pacer->last_wake_lateness_ticks = wake_lateness;
  if (wake_lateness > pacer->frequency / 500.0 &&
      EnvironmentEnabled("DKC1_FPS_STATS")) {
    fprintf(stderr,
            "[fps-stall] frame=%ld phase=work_wake lateness_ms=%.3f\n",
            s_host_frame + 1,
            wake_lateness * 1000.0 / pacer->frequency);
  }
}

static void FramePacerRecordWork(Dkc1FramePacer *pacer, double work_ticks) {
  const double minimum = pacer->frequency / 2000.0;
  const double maximum = pacer->ticks_per_frame * 0.75;
  pacer->work_sum += work_ticks;
  pacer->last_work_ticks = work_ticks;
  if (work_ticks > pacer->work_max)
    pacer->work_max = work_ticks;
  if (work_ticks > pacer->estimated_work_ticks)
    pacer->estimated_work_ticks = work_ticks;
  else
    pacer->estimated_work_ticks =
        pacer->estimated_work_ticks * 0.9 + work_ticks * 0.1;
  if (pacer->estimated_work_ticks < minimum)
    pacer->estimated_work_ticks = minimum;
  if (pacer->estimated_work_ticks > maximum)
    pacer->estimated_work_ticks = maximum;
  if (work_ticks > pacer->ticks_per_frame &&
      EnvironmentEnabled("DKC1_FPS_STATS")) {
    fprintf(stderr, "[fps-stall] frame=%ld phase=work duration_ms=%.3f\n",
            s_host_frame, work_ticks * 1000.0 / pacer->frequency);
  }
}

static void FramePacerRecordWorkProfile(const Dkc1FramePacer *pacer,
                                        const Dkc1FrameWorkProfile *profile,
                                        double total_ticks) {
  if (!EnvironmentEnabled("DKC1_FPS_STATS") ||
      total_ticks <= pacer->frequency / 125.0)
    return;
  fprintf(stderr,
          "[fps-work] frame=%ld total_ms=%.3f events_ms=%.3f input_ms=%.3f "
          "emulation_ms=%.3f ppu_ms=%.3f diagnostics_ms=%.3f "
          "audio_ms=%.3f title_ms=%.3f\n",
          s_host_frame, total_ticks * 1000.0 / pacer->frequency,
          profile->events * 1000.0 / pacer->frequency,
          profile->input * 1000.0 / pacer->frequency,
          profile->emulation * 1000.0 / pacer->frequency,
          profile->ppu * 1000.0 / pacer->frequency,
          profile->diagnostics * 1000.0 / pacer->frequency,
          profile->audio * 1000.0 / pacer->frequency,
          profile->title * 1000.0 / pacer->frequency);
}

static void DisplayPacerInit(Dkc1DisplayPacer *display) {
  memset(display, 0, sizeof *display);
  display->interval_min = DBL_MAX;
}

static void DisplayPacerRecord(Dkc1DisplayPacer *display,
                               unsigned long long callback_number,
                               double timestamp, double duration) {
  if (display->previous_timestamp > 0.0 &&
      callback_number > display->previous_callback_number) {
    const unsigned long long callback_delta =
        callback_number - display->previous_callback_number;
    const double elapsed = timestamp - display->previous_timestamp;
    const double interval = elapsed / (double)callback_delta;
    if (interval > 0.0 && interval < 0.100) {
      display->interval_count++;
      display->interval_sum += interval;
      if (interval < display->interval_min)
        display->interval_min = interval;
      if (interval > display->interval_max)
        display->interval_max = interval;
      if (interval > 1.0 / 75.0 && interval < 1.0 / 50.0) {
        if (display->callback_interval <= 0.0)
          display->callback_interval = interval;
        else
          display->callback_interval =
              display->callback_interval * 0.95 + interval * 0.05;
        s_audio_pacing_fps = 1.0 / display->callback_interval;
      }
    }
    if (callback_delta > 1) {
      display->skipped_callbacks += callback_delta - 1;
      if (callback_delta >= 4 && s_audio_started)
        s_audio_recovery_requested = 1;
      if (EnvironmentEnabled("DKC1_FPS_STATS")) {
        fprintf(stderr,
                "[display-stall] frame=%ld callbacks_skipped=%llu "
                "elapsed_ms=%.3f callback_duration_ms=%.3f\n",
                s_host_frame + 1, callback_delta - 1,
                elapsed * 1000.0, duration * 1000.0);
      }
    }
  }
  display->previous_timestamp = timestamp;
  display->previous_callback_number = callback_number;
}

static int DisplayPacerWaitForTarget(Dkc1FramePacer *pacer,
                                     Dkc1DisplayPacer *display) {
  for (;;) {
    double timestamp = 0.0;
    double target_timestamp = 0.0;
    double duration = 0.0;
    unsigned long long callback_number = 0;
    const unsigned long long previous_callback_number =
        display->previous_callback_number;
    const double previous_timestamp = display->previous_timestamp;
    if (!Dkc1MacDisplayLinkWait(
            previous_callback_number, 0.050,
            &timestamp, &target_timestamp, &duration, &callback_number)) {
      display->wait_timeouts++;
      if (s_audio_started)
        s_audio_recovery_requested = 1;
      return 0;
    }
    DisplayPacerRecord(display, callback_number, timestamp, duration);
    pacer->next_deadline = target_timestamp * pacer->frequency;
    const double target_lead = pacer->next_deadline - FramePacerNow();
    const double callback_elapsed =
        previous_timestamp > 0.0 &&
                callback_number == previous_callback_number + 1
            ? (timestamp - previous_timestamp) * pacer->frequency
            : pacer->ticks_per_frame;
    const bool short_callback =
        callback_elapsed < pacer->ticks_per_frame * 0.75;
    double work_budget = pacer->estimated_work_ticks;
    if (work_budget > pacer->frequency * kHostWorkGuardSeconds)
      work_budget = pacer->frequency * kHostWorkGuardSeconds;
    const double minimum_target_lead =
        work_budget + pacer->frequency * (kMacSubmitLeadSeconds + 0.001);
    if (!short_callback && target_lead >= minimum_target_lead)
      return 1;
    /* Consume a real ProMotion half-interval or a callback that arrived too
     * late to finish the measured work before the submit lead. A normally
     * spaced 60 Hz callback that was delivered a few milliseconds late still
     * has enough budget and must not be converted into a dropped frame. */
    display->skipped_callbacks++;
    if (EnvironmentEnabled("DKC1_FPS_STATS")) {
      fprintf(stderr,
              "[display-stale] frame=%ld target_lead_ms=%.3f\n",
              s_host_frame + 1, target_lead * 1000.0 / pacer->frequency);
    }
  }
}

static void DisplayPacerPrintStats(const Dkc1DisplayPacer *display) {
  if (!EnvironmentEnabled("DKC1_FPS_STATS") || !s_display_link_active)
    return;
  fprintf(stderr,
          "[display] intervals=%llu fps=%.6f average_interval_ms=%.3f "
          "min_interval_ms=%.3f max_interval_ms=%.3f "
          "skipped_callbacks=%llu wait_timeouts=%llu\n",
          (unsigned long long)display->interval_count,
          display->interval_sum > 0.0
              ? (double)display->interval_count / display->interval_sum : 0.0,
          display->interval_count
              ? display->interval_sum * 1000.0 /
                    (double)display->interval_count : 0.0,
          display->interval_count ? display->interval_min * 1000.0 : 0.0,
          display->interval_max * 1000.0,
          (unsigned long long)display->skipped_callbacks,
          (unsigned long long)display->wait_timeouts);
}

static void PacingLogInit(Dkc1PacingLog *log) {
  memset(log, 0, sizeof *log);
  const char *path = getenv("DKC1_PACING_LOG");
  if (path && *path) {
    log->stream = fopen(path, "wb");
    if (!log->stream)
      fprintf(stderr, "warning: unable to open pacing log: %s\n", path);
    else
      s_audio_collect_stats = 1;
  }
  const char *frame_text = getenv("DKC1_PACING_TEST_STALL_FRAME");
  const char *ms_text = getenv("DKC1_PACING_TEST_STALL_MS");
  if (frame_text && *frame_text && ms_text && *ms_text) {
    char *frame_end = NULL;
    char *ms_end = NULL;
    const long frame = strtol(frame_text, &frame_end, 10);
    const unsigned long milliseconds = strtoul(ms_text, &ms_end, 10);
    if (frame_end && !*frame_end && ms_end && !*ms_end && frame > 0 &&
        milliseconds > 0 && milliseconds <= 1000) {
      log->test_stall_frame = frame;
      log->test_stall_ms = (unsigned)milliseconds;
    }
  }
}

static int PacingLogWriteHeader(Dkc1PacingLog *log,
                                const Dkc1DisplayPacer *display) {
  if (!log->stream)
    return 0;
  if (log->header_written)
    return 1;
  /* The second CADisplayLink callback supplies the first measured interval.
   * Defer the header until then so refresh_hz describes the actual panel
   * cadence rather than the requested preferred rate. */
  if (s_display_link_active && display->interval_count == 0)
    return 0;
  const double refresh_hz = display->callback_interval > 0.0
      ? 1.0 / display->callback_interval
      : kHostPresentationFramesPerSecond;
  fprintf(log->stream,
          "{\"schema\":\"dkc1.pacing.v3\",\"platform\":\"%s\","
          "\"refresh_hz\":%.9f,\"display_hz\":%.9f,"
          "\"clock_source\":\"%s\",\"submit_lead_ms\":%.4f,"
          "\"audio_preroll\":%u,\"audio_ring_start_frames\":%u,"
          "\"test_stall_frame\":%ld,\"test_stall_ms\":%u}\n",
          DKC1_HOST_PLATFORM, refresh_hz, refresh_hz,
          s_display_link_active ? "CADisplayLink" : DKC1_HOST_CLOCK,
          kMacSubmitLeadSeconds * 1000.0,
          s_audio_preroll_blocks, s_audio_ring_start_threshold,
          log->test_stall_frame, log->test_stall_ms);
  log->header_written = 1;
  return 1;
}

static void PacingLogInjectTestStall(Dkc1PacingLog *log, long host_frame) {
  if (!log->test_stall_fired && log->test_stall_ms &&
      host_frame == log->test_stall_frame) {
    log->test_stall_fired = 1;
    SDL_Delay(log->test_stall_ms);
  }
}

static void PacingLogPresented(Dkc1PacingLog *log,
                               const Dkc1FramePacer *pacer,
                               const Dkc1DisplayPacer *display,
                               double work_start, double work_end,
                               double submit,
                               double presented) {
  if (!PacingLogWriteHeader(log, display))
    return;
  const double scale = 1000.0 / pacer->frequency;
  const double present_interval = log->last_present > 0.0
      ? (presented - log->last_present) * scale : 0.0;
  const double submit_interval = log->last_submit > 0.0
      ? (submit - log->last_submit) * scale : 0.0;
  const double submit_error = log->target > 0.0
      ? (submit - log->target) * scale : 0.0;
  const double late = submit_error > 0.0 ? submit_error : 0.0;
  const unsigned long long overruns = s_display_link_active
      ? display->skipped_callbacks + display->wait_timeouts
      : pacer->long_intervals;
  fprintf(log->stream,
          "{\"frame\":%ld,\"work_ms\":%.4f,\"wait_ms\":%.4f,"
          "\"late_ms\":%.4f,\"present_interval_ms\":%.4f,"
          "\"submit_interval_ms\":%.4f,\"submit_error_ms\":%.4f,"
          "\"present_ms\":%.4f,\"setup_ms\":%.4f,"
          "\"emulation_ms\":%.4f,\"render_ms\":%.4f,"
          "\"diagnostics_ms\":%.4f,\"audio_ms\":%.4f,"
          "\"audio_queued_frames\":%u,\"audio_starvations\":%lu,"
          "\"audio_drops\":%lu,\"audio_ring_frames\":%u,"
          "\"audio_internal_underflows\":%llu,\"overruns\":%llu}\n",
          s_host_frame, (work_end - work_start) * scale, log->wait_ms,
          late, present_interval, submit_interval, submit_error,
          (presented - submit) * scale, log->setup_ms,
          log->emulation_ms, log->render_ms, log->diagnostics_ms,
          log->audio_ms, s_audio_last_queued_frames,
          s_audio_starvations, s_audio_drops, s_audio_ring_frames,
          s_audio_internal_underflows, overruns);
  log->last_submit = submit;
  log->last_present = presented;
  if ((s_host_frame % 60) == 0)
    fflush(log->stream);
}

static void PacingLogClose(Dkc1PacingLog *log) {
  if (log->stream) {
    fflush(log->stream);
    fclose(log->stream);
    log->stream = NULL;
  }
  s_audio_collect_stats = 0;
}

static void FramePacerRecordPresent(Dkc1FramePacer *pacer, double now) {
  pacer->presented_frames++;
  if (pacer->previous_present > 0.0) {
    const double interval = now - pacer->previous_present;
    pacer->interval_count++;
    pacer->interval_sum += interval;
    if (interval < pacer->interval_min)
      pacer->interval_min = interval;
    if (interval > pacer->interval_max)
      pacer->interval_max = interval;
    if (interval > pacer->ticks_per_frame * 1.25)
      pacer->long_intervals++;
    if (interval > pacer->ticks_per_frame * 1.25 &&
        EnvironmentEnabled("DKC1_FPS_STATS")) {
      fprintf(stderr,
              "[fps-stall] frame=%ld phase=present_interval duration_ms=%.3f "
              "work_ms=%.3f reserve_ms=%.3f wake_late_ms=%.3f "
              "present_wait_ms=%.3f deadline_late_ms=%.3f\n",
              s_host_frame, interval * 1000.0 / pacer->frequency,
              pacer->last_work_ticks * 1000.0 / pacer->frequency,
              pacer->last_work_reserve_ticks * 1000.0 / pacer->frequency,
              pacer->last_wake_lateness_ticks * 1000.0 / pacer->frequency,
              pacer->last_present_wait_ticks * 1000.0 / pacer->frequency,
              (now - pacer->next_deadline) * 1000.0 / pacer->frequency);
    }
    if (pacer->title_window_start <= 0.0)
      pacer->title_window_start = pacer->previous_present;
    pacer->title_window_intervals++;
    if (pacer->title_window_intervals >= 30) {
      s_present_fps =
          (double)pacer->title_window_intervals * pacer->frequency /
          (now - pacer->title_window_start);
      pacer->title_window_start = now;
      pacer->title_window_intervals = 0;
    }
  } else {
    pacer->title_window_start = now;
  }
  pacer->previous_present = now;
}

static void FramePacerRecordPresentWait(Dkc1FramePacer *pacer,
                                        double present_wait_ticks) {
  pacer->present_wait_count++;
  pacer->present_wait_sum += present_wait_ticks;
  pacer->last_present_wait_ticks = present_wait_ticks;
  if (present_wait_ticks > pacer->present_wait_max)
    pacer->present_wait_max = present_wait_ticks;
  if (present_wait_ticks > pacer->ticks_per_frame * 1.25 &&
      EnvironmentEnabled("DKC1_FPS_STATS")) {
    fprintf(stderr,
            "[fps-stall] frame=%ld phase=render_present duration_ms=%.3f\n",
            s_host_frame,
            present_wait_ticks * 1000.0 / pacer->frequency);
  }
}

static void FramePacerAdvance(Dkc1FramePacer *pacer, double presented_at,
                              int force_reanchor) {
  const double lateness = presented_at - pacer->next_deadline;
  if (lateness >= pacer->ticks_per_frame * 3.0 && s_audio_started)
    s_audio_recovery_requested = 1;
  if (force_reanchor || lateness > pacer->frequency / 500.0)
    FramePacerReanchor(pacer, presented_at);
  else
    pacer->next_deadline += pacer->ticks_per_frame;
}

static void FramePacerPrintStats(const Dkc1FramePacer *pacer) {
  if (!EnvironmentEnabled("DKC1_FPS_STATS"))
    return;
  const double active_seconds = pacer->interval_sum / pacer->frequency;
  fprintf(stderr,
          "[fps] frames=%llu active_seconds=%.6f fps=%.3f target=%.6f "
          "average_interval_ms=%.3f min_interval_ms=%.3f "
          "max_interval_ms=%.3f long_intervals=%llu reanchors=%llu "
          "average_work_ms=%.3f max_work_ms=%.3f "
          "average_present_wait_ms=%.3f max_present_wait_ms=%.3f\n",
          (unsigned long long)pacer->presented_frames, active_seconds,
          active_seconds > 0.0
              ? (double)pacer->interval_count / active_seconds : 0.0,
          kHostPresentationFramesPerSecond,
          pacer->interval_count
              ? pacer->interval_sum * 1000.0 /
                    (pacer->frequency * (double)pacer->interval_count) : 0.0,
          pacer->interval_count
              ? pacer->interval_min * 1000.0 / pacer->frequency : 0.0,
          pacer->interval_max * 1000.0 / pacer->frequency,
          (unsigned long long)pacer->long_intervals,
          (unsigned long long)pacer->reanchors,
          pacer->presented_frames
              ? pacer->work_sum * 1000.0 /
                    (pacer->frequency * (double)pacer->presented_frames) : 0.0,
          pacer->work_max * 1000.0 / pacer->frequency,
          pacer->present_wait_count
              ? pacer->present_wait_sum * 1000.0 /
                    (pacer->frequency * (double)pacer->present_wait_count) : 0.0,
          pacer->present_wait_max * 1000.0 / pacer->frequency);
}

static void ShowError(const char *title, const char *message) {
  SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, title, message, s_window);
  fprintf(stderr, "%s: %s\n", title, message);
}

static const char *LayerName(uint8_t mask) {
  switch (mask) {
    case 0x01: return "BG1";
    case 0x02: return "BG2";
    case 0x04: return "BG3";
    case 0x10: return "OBJ";
    default: return "composite";
  }
}

static const char *AspectName(Dkc1VideoAspect aspect) {
  switch (aspect) {
    case kDkc1VideoAspect16x10: return "16:10";
    case kDkc1VideoAspect16x9: return "16:9";
    default: return "4:3";
  }
}

static void UpdateWindowTitle(void) {
  if (!s_window)
    return;
  char title[512];
  if (!EnvironmentEnabled("DKC1_LIVE_TITLE")) {
    snprintf(title, sizeof title,
             "DKC1Recomp %s | %s | %s | %s | %s",
             DKC1_BUILD_COMMIT, s_paused ? "PAUSED" : "running",
             AspectName(Dkc1VideoGetAspect()),
             LayerName(Dkc1DebugLayerMask()), s_status);
  } else if (s_present_fps > 0.0) {
    snprintf(title, sizeof title,
             "DKC1Recomp %s | frame %ld | %.1f FPS | %s | %s | %s | %s",
             DKC1_BUILD_COMMIT, s_host_frame, s_present_fps,
             s_paused ? "PAUSED" : "running",
             AspectName(Dkc1VideoGetAspect()),
             LayerName(Dkc1DebugLayerMask()), s_status);
  } else {
    snprintf(title, sizeof title,
             "DKC1Recomp %s | frame %ld | %s | %s | %s | %s",
             DKC1_BUILD_COMMIT, s_host_frame,
             s_paused ? "PAUSED" : "running",
             AspectName(Dkc1VideoGetAspect()),
             LayerName(Dkc1DebugLayerMask()), s_status);
  }
  SDL_SetWindowTitle(s_window, title);
}

static void UpdateTitle(void) {
  UpdateWindowTitle();
  Dkc1MacUpdateMenuState(s_paused, s_fullscreen,
                         s_fullscreen_scaling,
                         Dkc1VideoGetAspect(), Dkc1VideoGetEdgePolicy(),
                         Dkc1DebugLayerMask(),
                         Dkc1DebugProvenanceOverlay(), s_msu1 != NULL,
                         Dkc1BabyKongEnabled(), Dkc1BabyKongReady());
}

static char *ConfiguredMusicPackPath(void) {
  if (EnvironmentEnabled("DKC1_MSU1_DISABLE"))
    return NULL;
  const char *configured = getenv("DKC1_MSU1_PACK");
  if (configured && *configured) {
    size_t size = strlen(configured) + 1;
    char *copy = malloc(size);
    if (copy)
      memcpy(copy, configured, size);
    return copy;
  }
  return Dkc1MacSavedMsu1();
}

static void ChooseBabyKongRom(void) {
  char *path = Dkc1MacChooseBabyKongRom();
  if (!path)
    return;
  char error[192];
  if (Dkc1BabyKongLoadRom(path, error, sizeof error)) {
    Dkc1BabyKongSetEnabled(true);
    Dkc1MacSetBabyKongRom(path);
    Dkc1MacSetBabyKongEnabled(1);
    snprintf(s_status, sizeof s_status, "Baby Kong enabled | %zu frames",
             Dkc1BabyKongFrameCount());
  } else {
    ShowError("Unsupported DKC3 ROM", error);
    snprintf(s_status, sizeof s_status, "Baby Kong: %.160s", error);
  }
  free(path);
}

static uint16_t ReadWram16(size_t address) {
  return (uint16_t)(g_ram[address] | ((uint16_t)g_ram[address + 1] << 8));
}

static void ObserveMsu1MusicState(void) {
  if (!s_msu1)
    return;
  const unsigned previous_track = Dkc1Msu1CurrentTrack(s_msu1);
  Dkc1Msu1ObserveMusicState(s_msu1, ReadWram16(0x0521),
                            ReadWram16(0x051d));
  const unsigned current_track = Dkc1Msu1CurrentTrack(s_msu1);
  if (current_track == previous_track)
    return;
  if (current_track) {
    fprintf(stderr, "msu1: playing track %u (DKC music $%02x)\n",
            current_track, ReadWram16(0x0521));
    snprintf(s_status, sizeof s_status, "MSU-1 track %u | %s",
             current_track,
             s_haptics_enabled ? "controller stomp haptics on" : "haptics off");
  } else {
    fprintf(stderr, "msu1: stopped\n");
    snprintf(s_status, sizeof s_status, "MSU-1 waiting for music cue | %s",
             s_haptics_enabled ? "controller stomp haptics on" : "haptics off");
  }
}

static int ResolveRomPath(int argc, char **argv, char output[PATH_MAX]) {
  const char *candidate = argc > 1 ? argv[1] : getenv("DKC1_ROM");
  char *picked = NULL;
  if (!candidate || !*candidate) {
#ifdef __APPLE__
    picked = Dkc1MacChooseRom();
    candidate = picked;
#else
    fprintf(stderr, "usage: DKC1Recomp <rom.sfc>\n");
    return -1;
#endif
  }
  if (!candidate) {
    output[0] = 0;
    return 0;
  }
  char *resolved = realpath(candidate, output);
  if (!resolved)
    snprintf(output, PATH_MAX, "%s", candidate);
  free(picked);
  return output[0] != 0;
}

static void PrepareUserDirectory(void) {
  char *path = SDL_GetPrefPath("Flat2VR", "DKC1Recomp");
  if (!path)
    return;
  if (chdir(path) != 0)
    fprintf(stderr, "warning: could not use app data directory: %s\n", path);
  SDL_free(path);
  mkdir("build", 0755);
  mkdir("build/tier2", 0755);
  if (!getenv("SNESRECOMP_TIER2_DIR") &&
      !getenv("SNESRECOMP_TIER2_MANIFEST"))
    setenv("SNESRECOMP_TIER2_DIR", "build/tier2", 0);
}

static int PresentationWidth(void) {
  return (s_width * kSnesPixelAspectNumerator +
          kSnesPixelAspectDenominator / 2) /
         kSnesPixelAspectDenominator;
}

static void ApplyPresentationGeometry(void) {
  s_presentation_output_width = 0;
  s_presentation_output_height = 0;
  if (s_metal_presenter_active) {
    Dkc1MacMetalPresenterSetGeometry(PresentationWidth(), s_fullscreen);
    Dkc1MacMetalPresenterSetScaling(s_fullscreen_scaling);
  }
  if (s_texture) {
    /* Retain exact source pixels. Smooth avoids differently sized output
     * columns at a fractional Retina scale; Pixel Sharp keeps hard nearest-
     * neighbor edges. This preference changes only the host sampler. */
    (void)SDL_SetTextureScaleMode(
        s_texture,
        s_fullscreen &&
                s_fullscreen_scaling != kDkc1MacFullscreenPixelSharp
            ? SDL_ScaleModeLinear
            : SDL_ScaleModeNearest);
  }
  if (s_fullscreen) {
    /* macOS fullscreen Spaces resize asynchronously. Use the live drawable
     * as a 1:1 logical target, then fit the texture explicitly in Present().
     * This prevents SDL from retaining the previous integer-sized viewport. */
    SDL_RenderSetLogicalSize(s_renderer, 0, 0);
    SDL_RenderSetIntegerScale(s_renderer, SDL_FALSE);
    SDL_RenderSetScale(s_renderer, 1.0f, 1.0f);
    SDL_RenderSetViewport(s_renderer, NULL);
    if (SDL_GetRendererOutputSize(s_renderer, &s_presentation_output_width,
                                  &s_presentation_output_height) == 0 &&
        s_presentation_output_width > 0 &&
        s_presentation_output_height > 0)
      return;
  }
  SDL_RenderSetLogicalSize(s_renderer, PresentationWidth(),
                           kDkc1VideoHeight);
  SDL_RenderSetIntegerScale(s_renderer, SDL_TRUE);
}

static void ApplyWindowedSize(void) {
  SDL_SetWindowSize(s_window, PresentationWidth() * kWindowScale,
                    kDkc1VideoHeight * kWindowScale);
}

static bool InitVideo(void) {
  const int window_width = PresentationWidth() * kWindowScale;
  const int window_height = kDkc1VideoHeight * kWindowScale;
  s_window = SDL_CreateWindow(
      "DKC1Recomp", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
      window_width, window_height,
      SDL_WINDOW_ALLOW_HIGHDPI | SDL_WINDOW_RESIZABLE);
  if (!s_window)
    return false;

  SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");
  /* The fixed Mach clock is the default and sole presentation authority.
   * SDL's blocking Metal vsync is retained only as an explicit diagnostic;
   * the macOS compositor still prevents direct-scanout tearing. */
  const int request_vsync =
      EnvironmentEnabled("DKC1_KEEP_RENDERER_VSYNC") &&
      !EnvironmentEnabled("DKC1_DISABLE_VSYNC");
  const Uint32 renderer_flags =
      SDL_RENDERER_ACCELERATED |
      (request_vsync ? SDL_RENDERER_PRESENTVSYNC : 0);
  s_renderer = SDL_CreateRenderer(s_window, -1, renderer_flags);
  if (!s_renderer && request_vsync) {
    fprintf(stderr, "warning: accelerated vsync unavailable: %s\n",
            SDL_GetError());
    s_renderer = SDL_CreateRenderer(s_window, -1, SDL_RENDERER_ACCELERATED);
  }
  if (!s_renderer)
    s_renderer = SDL_CreateRenderer(s_window, -1, SDL_RENDERER_SOFTWARE);
  if (!s_renderer)
    return false;

  if (EnvironmentEnabled("DKC1_FPS_STATS")) {
    SDL_RendererInfo info;
    if (SDL_GetRendererInfo(s_renderer, &info) == 0) {
      s_renderer_vsync =
          (info.flags & SDL_RENDERER_PRESENTVSYNC) != 0;
      fprintf(stderr, "[fps-renderer] name=%s accelerated=%d vsync=%d\n",
              info.name ? info.name : "unknown",
              (info.flags & SDL_RENDERER_ACCELERATED) != 0,
              s_renderer_vsync);
    }
  } else {
    SDL_RendererInfo info;
    if (SDL_GetRendererInfo(s_renderer, &info) == 0)
      s_renderer_vsync =
          (info.flags & SDL_RENDERER_PRESENTVSYNC) != 0;
  }

  s_texture = SDL_CreateTexture(s_renderer, SDL_PIXELFORMAT_ARGB8888,
                                SDL_TEXTUREACCESS_STREAMING, s_width,
                                kDkc1VideoHeight);
  if (!s_texture)
    return false;
  SDL_SetTextureBlendMode(s_texture, SDL_BLENDMODE_NONE);
  ApplyPresentationGeometry();
  SDL_SetRenderDrawColor(s_renderer, 0, 0, 0, 255);
  return true;
}

static void InitDisplayLink(void) {
#ifdef __APPLE__
  SDL_SysWMinfo window_info;
  SDL_VERSION(&window_info.version);
  const int have_native_window =
      SDL_GetWindowWMInfo(s_window, &window_info) != 0;
  if (!EnvironmentEnabled("DKC1_DISABLE_METAL_PRESENTER")) {
    if (!have_native_window) {
      fprintf(stderr,
              "warning: native window unavailable for Metal presenter: %s\n",
              SDL_GetError());
    } else {
      s_metal_presenter_active = Dkc1MacMetalPresenterStart(
          window_info.info.cocoa.window, kMacNativeDisplayFramesPerSecond,
          s_fullscreen_scaling, s_fullscreen);
    }
  }
  const int request_display_link =
      !s_metal_presenter_active &&
      EnvironmentEnabled("DKC1_USE_DISPLAY_LINK_PACING") &&
      !EnvironmentEnabled("DKC1_DISABLE_DISPLAY_LINK");
  if (request_display_link) {
    if (!have_native_window) {
      fprintf(stderr,
              "warning: native window unavailable for display link: %s\n",
              SDL_GetError());
    } else {
      s_display_link_active =
          Dkc1MacDisplayLinkStart(window_info.info.cocoa.window,
                                  kHostPresentationFramesPerSecond);
    }
  }
  if (s_display_link_active && s_renderer_vsync &&
      !EnvironmentEnabled("DKC1_KEEP_RENDERER_VSYNC")) {
    /* CADisplayLink is the opted-in scanout authority. A second blocking-vsync gate in
     * SDL can quantize an otherwise timely Metal submission onto the following
     * refresh. macOS remains composited; disabling this wait does not permit
     * direct scanout tearing. */
    if (SDL_RenderSetVSync(s_renderer, 0) == 0) {
      s_renderer_vsync = 0;
    } else {
      fprintf(stderr, "warning: unable to disable renderer vsync: %s\n",
              SDL_GetError());
    }
  }
  if (EnvironmentEnabled("DKC1_FPS_STATS")) {
    fprintf(stderr,
            "[metal-presenter] active=%d requested_hz=%.6f\n",
            s_metal_presenter_active,
            kMacNativeDisplayFramesPerSecond);
    fprintf(stderr, "[display-link] active=%d requested_fps=%.6f\n",
            s_display_link_active, kHostPresentationFramesPerSecond);
    fprintf(stderr, "[display-authority] display_link=%d renderer_vsync=%d\n",
            s_display_link_active, s_renderer_vsync);
  }
#else
  if (EnvironmentEnabled("DKC1_FPS_STATS")) {
    fprintf(stderr,
            "[display-authority] fixed_clock=1 renderer_vsync=%d\n",
            s_renderer_vsync);
  }
#endif
}

static void PreparePresentation(void) {
  if (s_metal_presenter_active) {
    Dkc1MacPresentationFrameInfo info = {
      .host_frame = s_host_frame,
      .camera_x = ReadWram16(0x088b),
      .camera_y = ReadWram16(0x0895),
    };
    for (int layer = 0; layer < 4; layer++) {
      info.bg_hscroll[layer] = g_ppu->hScroll[layer];
      info.bg_vscroll[layer] = g_ppu->vScroll[layer];
    }
    Dkc1MacMetalPresenterQueueFrame(
        (const uint32_t *)s_pixels, s_width, kDkc1VideoHeight,
        PresentationWidth(), &info);
    return;
  }
  SDL_Rect destination;
  SDL_Rect *destination_ptr = NULL;
  if (s_fullscreen) {
    int output_width = 0;
    int output_height = 0;
    if (SDL_GetRendererOutputSize(s_renderer, &output_width,
                                  &output_height) == 0 &&
        output_width > 0 && output_height > 0) {
      if (output_width != s_presentation_output_width ||
          output_height != s_presentation_output_height)
        ApplyPresentationGeometry();
      const int presentation_width = PresentationWidth();
      if ((int64_t)output_width * kDkc1VideoHeight <=
          (int64_t)output_height * presentation_width) {
        destination.w = output_width;
        destination.h = (output_width * kDkc1VideoHeight +
                         presentation_width / 2) / presentation_width;
      } else {
        destination.h = output_height;
        destination.w = (output_height * presentation_width +
                         kDkc1VideoHeight / 2) / kDkc1VideoHeight;
      }
      destination.x = (output_width - destination.w) / 2;
      destination.y = (output_height - destination.h) / 2;
      destination_ptr = &destination;
    }
  }
  SDL_UpdateTexture(s_texture, NULL, s_pixels, s_width * 4);
  SDL_RenderClear(s_renderer);
  SDL_RenderCopy(s_renderer, s_texture, NULL, destination_ptr);
}

static void SubmitPresentation(void) {
  if (!s_metal_presenter_active)
    SDL_RenderPresent(s_renderer);
}

static void Present(void) {
  PreparePresentation();
  SubmitPresentation();
}

static void OpenFirstController(void) {
  if (s_controller)
    return;
  for (int i = 0; i < SDL_NumJoysticks(); i++) {
    if (SDL_IsGameController(i)) {
      s_controller = SDL_GameControllerOpen(i);
      if (s_controller) {
        snprintf(s_status, sizeof s_status, "controller: %.160s",
                 SDL_GameControllerName(s_controller));
        UpdateTitle();
        return;
      }
    }
  }
}

static int SDLCALL HapticWorkerMain(void *unused) {
  (void)unused;
  Dkc1HapticWorker *worker = &s_haptic_worker;
  SDL_LockMutex(worker->mutex);
  while (!worker->shutdown) {
    while (!worker->shutdown && worker->request == kHapticRequestNone)
      SDL_CondWait(worker->condition, worker->mutex);
    if (worker->shutdown)
      break;
    const int request = worker->request;
    SDL_GameController *controller = s_controller;
    worker->request = kHapticRequestNone;
    worker->busy = 1;
    SDL_UnlockMutex(worker->mutex);
    if (controller) {
      if (request == kHapticRequestPulse)
        (void)SDL_GameControllerRumble(controller, 0x2800, 0x5000, 55);
      else
        (void)SDL_GameControllerRumble(controller, 0, 0, 0);
    }
    SDL_LockMutex(worker->mutex);
    worker->busy = 0;
    SDL_CondBroadcast(worker->condition);
  }
  worker->busy = 0;
  SDL_CondBroadcast(worker->condition);
  SDL_UnlockMutex(worker->mutex);
  return 0;
}

static bool HapticWorkerStart(void) {
  if (!s_haptics_enabled)
    return true;
  Dkc1HapticWorker *worker = &s_haptic_worker;
  worker->mutex = SDL_CreateMutex();
  worker->condition = SDL_CreateCond();
  if (!worker->mutex || !worker->condition)
    goto fail;
  worker->thread = SDL_CreateThread(HapticWorkerMain, "DKC1 haptics", NULL);
  if (!worker->thread)
    goto fail;
  return true;

fail:
  if (worker->condition)
    SDL_DestroyCond(worker->condition);
  if (worker->mutex)
    SDL_DestroyMutex(worker->mutex);
  *worker = (Dkc1HapticWorker){0};
  return false;
}

static void HapticWorkerRequest(int request) {
  Dkc1HapticWorker *worker = &s_haptic_worker;
  if (!worker->thread || !worker->mutex)
    return;
  SDL_LockMutex(worker->mutex);
  worker->request = request;
  SDL_CondSignal(worker->condition);
  SDL_UnlockMutex(worker->mutex);
}

static void HapticWorkerDetachController(void) {
  Dkc1HapticWorker *worker = &s_haptic_worker;
  if (!worker->mutex)
    return;
  SDL_LockMutex(worker->mutex);
  worker->request = kHapticRequestNone;
  while (worker->busy)
    SDL_CondWait(worker->condition, worker->mutex);
  SDL_UnlockMutex(worker->mutex);
}

static void HapticWorkerStop(void) {
  Dkc1HapticWorker *worker = &s_haptic_worker;
  if (!worker->thread)
    return;
  SDL_LockMutex(worker->mutex);
  worker->request = kHapticRequestNone;
  while (worker->busy)
    SDL_CondWait(worker->condition, worker->mutex);
  worker->shutdown = 1;
  SDL_CondSignal(worker->condition);
  SDL_UnlockMutex(worker->mutex);
  SDL_WaitThread(worker->thread, NULL);
  SDL_DestroyCond(worker->condition);
  SDL_DestroyMutex(worker->mutex);
  *worker = (Dkc1HapticWorker){0};
}

static void StopControllerRumble(void) {
  if (s_haptic_worker.thread)
    HapticWorkerRequest(kHapticRequestStop);
  else if (s_controller)
    (void)SDL_GameControllerRumble(s_controller, 0, 0, 0);
}

static void PulseStompHaptic(void) {
  if (!s_haptics_enabled || !s_controller)
    return;
  /* A short, brighter high-frequency pulse reads as a stomp without masking
   * controller input or blocking the frame-critical thread on Bluetooth I/O. */
  HapticWorkerRequest(kHapticRequestPulse);
}

static void ControllerRemoved(SDL_JoystickID instance) {
  if (!s_controller)
    return;
  SDL_Joystick *joystick = SDL_GameControllerGetJoystick(s_controller);
  if (SDL_JoystickInstanceID(joystick) == instance) {
    HapticWorkerDetachController();
    (void)SDL_GameControllerRumble(s_controller, 0, 0, 0);
    SDL_GameControllerClose(s_controller);
    s_controller = NULL;
    snprintf(s_status, sizeof s_status, "controller disconnected");
    UpdateTitle();
  }
}

static uint32_t PollInput(void) {
  if (!(SDL_GetWindowFlags(s_window) & SDL_WINDOW_INPUT_FOCUS))
    return 0;
  const uint8_t *key = SDL_GetKeyboardState(NULL);
  uint32_t input = 0;
  if (key[SDL_SCANCODE_Z]) input |= 0x001;       /* B */
  if (key[SDL_SCANCODE_X]) input |= 0x002;       /* Y */
  if (key[SDL_SCANCODE_RSHIFT]) input |= 0x004;  /* Select */
  if (key[SDL_SCANCODE_RETURN]) input |= 0x008;  /* Start */
  if (key[SDL_SCANCODE_UP]) input |= 0x010;
  if (key[SDL_SCANCODE_DOWN]) input |= 0x020;
  if (key[SDL_SCANCODE_LEFT]) input |= 0x040;
  if (key[SDL_SCANCODE_RIGHT]) input |= 0x080;
  if (key[SDL_SCANCODE_S]) input |= 0x100;       /* A */
  if (key[SDL_SCANCODE_A]) input |= 0x200;       /* X */
  if (key[SDL_SCANCODE_Q]) input |= 0x400;       /* L */
  if (key[SDL_SCANCODE_W]) input |= 0x800;       /* R */

  if (s_controller) {
    if (SDL_GameControllerGetButton(s_controller,
                                    SDL_CONTROLLER_BUTTON_A)) input |= 0x001;
    if (SDL_GameControllerGetButton(s_controller,
                                    SDL_CONTROLLER_BUTTON_X)) input |= 0x002;
    if (SDL_GameControllerGetButton(s_controller,
                                    SDL_CONTROLLER_BUTTON_BACK)) input |= 0x004;
    if (SDL_GameControllerGetButton(s_controller,
                                    SDL_CONTROLLER_BUTTON_START)) input |= 0x008;
    if (SDL_GameControllerGetButton(s_controller,
                                    SDL_CONTROLLER_BUTTON_DPAD_UP)) input |= 0x010;
    if (SDL_GameControllerGetButton(s_controller,
                                    SDL_CONTROLLER_BUTTON_DPAD_DOWN)) input |= 0x020;
    if (SDL_GameControllerGetButton(s_controller,
                                    SDL_CONTROLLER_BUTTON_DPAD_LEFT)) input |= 0x040;
    if (SDL_GameControllerGetButton(s_controller,
                                    SDL_CONTROLLER_BUTTON_DPAD_RIGHT)) input |= 0x080;
    if (SDL_GameControllerGetButton(s_controller,
                                    SDL_CONTROLLER_BUTTON_B)) input |= 0x100;
    if (SDL_GameControllerGetButton(s_controller,
                                    SDL_CONTROLLER_BUTTON_Y)) input |= 0x200;
    if (SDL_GameControllerGetButton(s_controller,
                                    SDL_CONTROLLER_BUTTON_LEFTSHOULDER)) input |= 0x400;
    if (SDL_GameControllerGetButton(s_controller,
                                    SDL_CONTROLLER_BUTTON_RIGHTSHOULDER)) input |= 0x800;
  }
  return input;
}

static bool InitAudio(void) {
  SDL_AudioSpec desired;
  const char *preroll = getenv("DKC1_AUDIO_PREROLL");
  if (preroll && *preroll) {
    const int parsed = atoi(preroll);
    if (parsed >= 1 && parsed <= 4)
      s_audio_preroll_blocks = (unsigned)parsed;
  }
  SDL_zero(desired);
  desired.freq = kAudioRate;
  desired.format = AUDIO_S16SYS;
  desired.channels = kAudioChannels;
  desired.samples = kAudioScratchFrames;
  desired.callback = NULL;
  s_audio_device = SDL_OpenAudioDevice(NULL, 0, &desired, NULL, 0);
  if (!s_audio_device) {
    fprintf(stderr, "warning: audio unavailable: %s\n", SDL_GetError());
    return false;
  }
  RtlSetAudioOutputRate(kAudioRate);
  /* Do not start CoreAudio on an empty engine ring. The native producer needs
   * a few cartridge frames to reach its normal occupancy, after which the SDL
   * device receives a short host-side preroll. */
  SDL_PauseAudioDevice(s_audio_device, 1);
  AudioTraceStats stats;
  audio_trace_get_stats(&stats);
  s_audio_ring_frames = stats.occupancy_current;
  s_audio_internal_underflows = stats.output_underflows;
  s_audio_ring_start_threshold = stats.occupancy_current
      ? kAudioFramesPerBlock + 2 : kAudioRingStartFrames;
  s_audio_waiting_for_ring =
      stats.occupancy_current < s_audio_ring_start_threshold;
  s_audio_started = 0;
  return true;
}

static void ResetAudioTimeline(void) {
  if (!s_audio_device)
    return;
  /* SDL's queue belongs to the abandoned host timeline after a rewind or
   * pause. The runtime load already rebases the APU-port guest timeline; this
   * clears the other half and resumes only after a fresh device preroll. */
  SDL_ClearQueuedAudio(s_audio_device);
  SDL_PauseAudioDevice(s_audio_device, 1);
  s_audio_accumulator = 0.0;
  s_audio_started = 0;
  s_audio_last_queued_frames = 0;
  AudioTraceStats stats;
  audio_trace_get_stats(&stats);
  s_audio_ring_frames = stats.occupancy_current;
  s_audio_internal_underflows = stats.output_underflows;
  s_audio_ring_start_threshold = kAudioFramesPerBlock + 2;
  s_audio_waiting_for_ring =
      stats.occupancy_current < s_audio_ring_start_threshold;
}

static void PumpAudio(void) {
  if (!s_audio_device)
    return;
  if (s_audio_recovery_requested) {
    /* SDL may report an empty software queue while CoreAudio still owns
     * buffered samples, so zero queue occupancy alone is not starvation.
     * A long display-callback gap or fixed-clock deadline miss is the reliable
     * host-stall signal. */
    s_audio_recovery_requested = 0;
    s_audio_starvations++;
    ResetAudioTimeline();
  }
  AudioTraceStats stats;
  if (s_audio_waiting_for_ring || s_audio_collect_stats) {
    audio_trace_get_stats(&stats);
    s_audio_ring_frames = stats.occupancy_current;
    s_audio_internal_underflows = stats.output_underflows;
    if (s_audio_waiting_for_ring) {
      if (stats.occupancy_current < s_audio_ring_start_threshold)
        return;
      s_audio_waiting_for_ring = 0;
    }
  }

  const Uint32 bytes_per_frame =
      kAudioChannels * (Uint32)sizeof(int16_t);
  Uint32 queued_frames =
      SDL_GetQueuedAudioSize(s_audio_device) / bytes_per_frame;
  s_audio_last_queued_frames = queued_frames;
  /* Keep the queued device fed at the selected host presentation cadence.
   * The release clock is exactly 60 Hz; the opted-in display-link path updates
   * this rate only from stable 50-75 Hz callback intervals. */
  s_audio_accumulator += (double)kAudioRate / s_audio_pacing_fps;
  int frames = (int)s_audio_accumulator;
  s_audio_accumulator -= frames;
  if (frames <= 0)
    return;
  if (frames > kAudioScratchFrames)
    frames = kAudioScratchFrames;
  if (queued_frames >= kAudioMaximumQueuedFrames) {
    s_audio_drops++;
    return;
  }
  RtlRenderAudio(s_audio_scratch, frames, kAudioChannels);
  Dkc1Msu1Mix(s_msu1, s_audio_scratch, frames, kAudioChannels, kAudioRate);
  const Uint32 bytes = (Uint32)frames * kAudioChannels * sizeof(int16_t);
  if (SDL_QueueAudio(s_audio_device, s_audio_scratch, bytes) != 0) {
    s_audio_drops++;
    return;
  }
  queued_frames += (Uint32)frames;
  s_audio_last_queued_frames = queued_frames;
  if (s_audio_collect_stats) {
    audio_trace_get_stats(&stats);
    s_audio_ring_frames = stats.occupancy_current;
    s_audio_internal_underflows = stats.output_underflows;
  }
  if (!s_audio_started &&
      queued_frames >= s_audio_preroll_blocks * kAudioFramesPerBlock) {
    SDL_PauseAudioDevice(s_audio_device, 0);
    s_audio_started = 1;
  }
}

static void QuickSave(void) {
  if (RtlSaveSnapshot("quicksave.state"))
    snprintf(s_status, sizeof s_status, "saved quicksave.state");
  else
    snprintf(s_status, sizeof s_status, "quick save FAILED");
  s_reanchor_pacer = 1;
  UpdateTitle();
}

static void QuickLoad(void) {
  if (!RtlLoadSnapshot("quicksave.state")) {
    snprintf(s_status, sizeof s_status, "quick load FAILED");
  } else {
    ResetAudioTimeline();
    Dkc1Msu1Reset(s_msu1);
    ObserveMsu1MusicState();
    s_stomp_probe = (Dkc1StompProbe){0};
    StopControllerRumble();
    char error[256];
    if (!Dkc1FlightRecorderReanchorAfterStateLoad(
            s_host_frame, error, sizeof error))
      snprintf(s_status, sizeof s_status,
               "loaded; recorder reanchor failed: %.160s", error);
    else
      snprintf(s_status, sizeof s_status, "loaded quicksave.state");
    Dkc1DrawPpuFrame();
  }
  s_reanchor_pacer = 1;
  UpdateTitle();
}

static void ExportRepro(void) {
  char bundle[PATH_MAX];
  char error[256];
  if (Dkc1FlightRecorderExport(s_host_frame, bundle, sizeof bundle,
                               error, sizeof error))
    snprintf(s_status, sizeof s_status, "repro: %.180s", bundle);
  else
    snprintf(s_status, sizeof s_status, "repro failed: %.180s", error);
  s_reanchor_pacer = 1;
  UpdateTitle();
}

/* Switch only the host presentation width. The cartridge state is left
 * untouched, while the existing visible frame is center-cropped or centered
 * over black so a paused aspect change is immediately intelligible. */
static void SetAspectMode(Dkc1VideoAspect requested) {
  const Dkc1VideoAspect old_aspect = Dkc1VideoGetAspect();
  if (old_aspect == requested)
    return;

  const int old_width = s_width;
  Dkc1VideoSetAspect(requested);
  const int new_width = Dkc1VideoWidth();
  SDL_Texture *new_texture = SDL_CreateTexture(
      s_renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING,
      new_width, kDkc1VideoHeight);
  if (!new_texture) {
    Dkc1VideoSetAspect(old_aspect);
    snprintf(s_status, sizeof s_status, "aspect change failed: %.180s",
             SDL_GetError());
    UpdateTitle();
    return;
  }
  SDL_SetTextureBlendMode(new_texture, SDL_BLENDMODE_NONE);

  static uint8_t remapped[kDkc1VideoWidescreenWidth *
                          kDkc1VideoHeight * 4];
  const int copy_width = old_width < new_width ? old_width : new_width;
  const int source_x = old_width > new_width ? (old_width - new_width) / 2 : 0;
  const int dest_x = new_width > old_width ? (new_width - old_width) / 2 : 0;
  memset(remapped, 0, sizeof remapped);
  for (int y = 0; y < kDkc1VideoHeight; y++) {
    memcpy(remapped + ((size_t)y * new_width + dest_x) * 4,
           s_pixels + ((size_t)y * old_width + source_x) * 4,
           (size_t)copy_width * 4);
  }
  memcpy(s_pixels, remapped,
         (size_t)new_width * kDkc1VideoHeight * 4);

  SDL_DestroyTexture(s_texture);
  s_texture = new_texture;
  s_width = new_width;
  Dkc1BeginDrawing(s_pixels, (size_t)s_width * 4);
  ApplyPresentationGeometry();
  if (!s_fullscreen)
    ApplyWindowedSize();
  snprintf(s_status, sizeof s_status, "aspect changed to %s (%dx%d)",
           AspectName(requested),
           s_width, kDkc1VideoHeight);
  s_reanchor_pacer = 1;
  Present();
  UpdateTitle();
}

static void SetFullscreen(int fullscreen) {
  s_fullscreen = fullscreen != 0;
  if (SDL_SetWindowFullscreen(
          s_window, s_fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0) != 0) {
    s_fullscreen = !s_fullscreen;
    snprintf(s_status, sizeof s_status, "fullscreen change failed: %.170s",
             SDL_GetError());
  }
  ApplyPresentationGeometry();
  if (!s_fullscreen)
    ApplyWindowedSize();
  s_reanchor_pacer = 1;
  UpdateTitle();
}

static void SetFullscreenScaling(Dkc1MacFullscreenScaling scaling) {
  if (scaling < kDkc1MacFullscreenSmooth ||
      scaling >= kDkc1MacFullscreenScalingCount)
    scaling = kDkc1MacFullscreenSharpBilinear;
  s_fullscreen_scaling = scaling;
  Dkc1MacSetFullscreenScaling(s_fullscreen_scaling);
  ApplyPresentationGeometry();
  static const char *const names[] = {
    "smooth", "sharp bilinear", "pixel sharp"
  };
  snprintf(s_status, sizeof s_status, "fullscreen scaling: %s",
           names[s_fullscreen_scaling]);
  s_reanchor_pacer = 1;
  Present();
  UpdateTitle();
}

/* Level-wall presentation is host-only and takes effect on the next frame;
 * nothing in the cartridge or the shadow depends on it, so no reload. */
static void SetEdgePolicy(Dkc1EdgePolicy policy) {
  if (policy < kDkc1EdgeReflect || policy >= kDkc1EdgePolicyCount)
    policy = kDkc1EdgeGlide;
  Dkc1VideoSetEdgePolicy(policy);
  Dkc1MacSetWidescreenEdge(policy);
  snprintf(s_status, sizeof s_status, "level edge: %s",
           Dkc1EdgePolicyName(policy));
  UpdateTitle();
}

static void HandleKey(SDL_Keycode key, SDL_Keymod mod) {
  if ((mod & KMOD_GUI) && key == SDLK_q) {
    s_running = 0;
  } else if ((mod & KMOD_ALT) && key == SDLK_RETURN) {
    SetFullscreen(!s_fullscreen);
  } else if (key == SDLK_ESCAPE) {
    if (s_fullscreen) {
      SetFullscreen(0);
    } else {
      s_running = 0;
    }
  } else if (key == SDLK_F1) {
    Dkc1DebugSetProvenanceOverlay(!Dkc1DebugProvenanceOverlay());
  } else if (key == SDLK_F2) {
    Dkc1DebugSetLayerMask(0xff);
  } else if (key >= SDLK_F3 && key <= SDLK_F6) {
    static const uint8_t masks[] = {0x01, 0x02, 0x04, 0x10};
    Dkc1DebugSetLayerMask(masks[key - SDLK_F3]);
  } else if (key == SDLK_F7) {
    s_paused = !s_paused;
    if (s_paused) {
      StopControllerRumble();
      if (s_audio_device)
        SDL_PauseAudioDevice(s_audio_device, 1);
    } else {
      ResetAudioTimeline();
    }
    s_step_once = 0;
    s_reanchor_pacer = 1;
  } else if (key == SDLK_F8 && s_paused) {
    s_step_once = 1;
  } else if (key == SDLK_F9) {
    ExportRepro();
  } else if (key == SDLK_F11 || ((mod & KMOD_GUI) && key == SDLK_s)) {
    QuickSave();
  } else if (key == SDLK_F12 || ((mod & KMOD_GUI) && key == SDLK_l)) {
    QuickLoad();
  }
  UpdateTitle();
}

void Dkc1MacMenuCommand(int command) {
  switch (command) {
    case kDkc1MacMenuQuit:
      s_running = 0;
      break;
    case kDkc1MacMenuPause:
      s_paused = !s_paused;
      if (s_paused) {
        StopControllerRumble();
        if (s_audio_device)
          SDL_PauseAudioDevice(s_audio_device, 1);
      } else {
        ResetAudioTimeline();
      }
      s_step_once = 0;
      s_reanchor_pacer = 1;
      break;
    case kDkc1MacMenuStep:
      if (s_paused)
        s_step_once = 1;
      break;
    case kDkc1MacMenuQuickSave:
      QuickSave();
      return;
    case kDkc1MacMenuQuickLoad:
      QuickLoad();
      return;
    case kDkc1MacMenuExportRepro:
      ExportRepro();
      return;
    case kDkc1MacMenuToggleBabyKong:
      if (!Dkc1BabyKongReady()) {
        ChooseBabyKongRom();
      } else {
        Dkc1BabyKongSetEnabled(!Dkc1BabyKongEnabled());
        Dkc1MacSetBabyKongEnabled(Dkc1BabyKongEnabled());
        snprintf(s_status, sizeof s_status, "%s",
                 Dkc1BabyKongStatus());
      }
      break;
    case kDkc1MacMenuChooseBabyKongRom:
      ChooseBabyKongRom();
      break;
    case kDkc1MacMenuChooseMusicPack: {
      char *path = Dkc1MacChooseMsu1();
      if (path) {
        snprintf(s_status, sizeof s_status,
                 "music pack installed; restart DKC1Recomp to apply");
        free(path);
      }
      break;
    }
    case kDkc1MacMenuDisableMusicPack:
      Dkc1MacClearMsu1();
      snprintf(s_status, sizeof s_status,
               "replacement music disabled after restart");
      break;
    case kDkc1MacMenuFullscreen:
      SetFullscreen(!s_fullscreen);
      return;
    case kDkc1MacMenuFullscreenSmooth:
      SetFullscreenScaling(kDkc1MacFullscreenSmooth);
      return;
    case kDkc1MacMenuFullscreenSharpBilinear:
      SetFullscreenScaling(kDkc1MacFullscreenSharpBilinear);
      return;
    case kDkc1MacMenuFullscreenPixelSharp:
      SetFullscreenScaling(kDkc1MacFullscreenPixelSharp);
      return;
    case kDkc1MacMenuAspectNative:
      SetAspectMode(kDkc1VideoAspectNative);
      return;
    case kDkc1MacMenuAspect16x10:
      SetAspectMode(kDkc1VideoAspect16x10);
      return;
    case kDkc1MacMenuAspect16x9:
      SetAspectMode(kDkc1VideoAspect16x9);
      return;
    case kDkc1MacMenuEdgeReflect:
      SetEdgePolicy(kDkc1EdgeReflect);
      return;
    case kDkc1MacMenuEdgeBars:
      SetEdgePolicy(kDkc1EdgeBars);
      return;
    case kDkc1MacMenuEdgeShift:
      SetEdgePolicy(kDkc1EdgeShift);
      return;
    case kDkc1MacMenuEdgeGlide:
      SetEdgePolicy(kDkc1EdgeGlide);
      return;
    case kDkc1MacMenuLayerComposite:
      Dkc1DebugSetLayerMask(0xff);
      break;
    case kDkc1MacMenuLayerBg1:
      Dkc1DebugSetLayerMask(0x01);
      break;
    case kDkc1MacMenuLayerBg2:
      Dkc1DebugSetLayerMask(0x02);
      break;
    case kDkc1MacMenuLayerBg3:
      Dkc1DebugSetLayerMask(0x04);
      break;
    case kDkc1MacMenuLayerObj:
      Dkc1DebugSetLayerMask(0x10);
      break;
    case kDkc1MacMenuProvenance:
      Dkc1DebugSetProvenanceOverlay(!Dkc1DebugProvenanceOverlay());
      break;
    default:
      return;
  }
  UpdateTitle();
}

static void PollEvents(void) {
  SDL_Event event;
  while (SDL_PollEvent(&event)) {
    switch (event.type) {
      case SDL_QUIT:
        s_running = 0;
        break;
      case SDL_KEYDOWN:
        if (!event.key.repeat)
          HandleKey(event.key.keysym.sym, event.key.keysym.mod);
        break;
      case SDL_CONTROLLERDEVICEADDED:
        OpenFirstController();
        break;
      case SDL_CONTROLLERDEVICEREMOVED:
        ControllerRemoved(event.cdevice.which);
        break;
      case SDL_WINDOWEVENT:
        if (event.window.event == SDL_WINDOWEVENT_FOCUS_GAINED) {
          Dkc1MacMetalPresenterSetActive(1);
        } else if (event.window.event == SDL_WINDOWEVENT_RESTORED ||
                   event.window.event == SDL_WINDOWEVENT_SHOWN) {
          Dkc1MacMetalPresenterSetActive(1);
        } else if (event.window.event == SDL_WINDOWEVENT_FOCUS_LOST ||
                   event.window.event == SDL_WINDOWEVENT_MINIMIZED ||
                   event.window.event == SDL_WINDOWEVENT_HIDDEN) {
          Dkc1MacMetalPresenterSetActive(0);
        }
        if (event.window.event == SDL_WINDOWEVENT_FOCUS_GAINED ||
            event.window.event == SDL_WINDOWEVENT_FOCUS_LOST ||
            event.window.event == SDL_WINDOWEVENT_RESIZED ||
            event.window.event == SDL_WINDOWEVENT_SIZE_CHANGED ||
            event.window.event == SDL_WINDOWEVENT_RESTORED)
          s_reanchor_pacer = 1;
        break;
      default:
        break;
    }
  }
}

static void Cleanup(uint8_t *rom) {
  char error[256];
  if (!Dkc1WramDumpClose(&s_wram_dump, error, sizeof error))
    fprintf(stderr, "wram_dump: %s\n", error);
  Dkc1DebugDumpClose();
  Dkc1FlightRecorderClose();
  Dkc1InputPlaybackFree(&s_input_playback);
  Dkc1MacDisplayLinkStop();
  s_display_link_active = 0;
  Dkc1MacMetalPresenterStop();
  s_metal_presenter_active = 0;
  HapticWorkerStop();
  if (s_controller) {
    (void)SDL_GameControllerRumble(s_controller, 0, 0, 0);
    SDL_GameControllerClose(s_controller);
  }
  Dkc1Msu1Close(s_msu1);
  s_msu1 = NULL;
  Dkc1BabyKongUnload();
  if (s_audio_device)
    SDL_CloseAudioDevice(s_audio_device);
  if (s_texture)
    SDL_DestroyTexture(s_texture);
  if (s_renderer)
    SDL_DestroyRenderer(s_renderer);
  if (s_window)
    SDL_DestroyWindow(s_window);
  free(rom);
  SDL_Quit();
}

int main(int argc, char **argv) {
  SDL_SetMainReady();
#ifdef __APPLE__
  (void)pthread_set_qos_class_self_np(QOS_CLASS_USER_INTERACTIVE, 0);
#endif
  /* A native macOS fullscreen Space constrains SDL to the panel's inset safe
   * area (3949x2464 on the target 4112x2658 MacBook display). Set this before
   * the Cocoa video backend initializes so FULLSCREEN_DESKTOP uses the full
   * borderless drawable instead. */
  SDL_SetHint(SDL_HINT_VIDEO_MAC_FULLSCREEN_SPACES, "0");
  if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMECONTROLLER) != 0) {
    fprintf(stderr, "SDL initialization failed: %s\n", SDL_GetError());
    return 3;
  }

  char rom_path[PATH_MAX] = {0};
  const int rom_path_result = ResolveRomPath(argc, argv, rom_path);
  if (rom_path_result <= 0) {
    SDL_Quit();
    return rom_path_result < 0 ? 2 : 0;
  }

  size_t rom_size = 0;
  char rom_error[192];
  uint8_t *rom =
      Dkc1ReadVerifiedRom(rom_path, &rom_size, rom_error, sizeof rom_error);
  if (!rom) {
    char message[PATH_MAX + 256];
    snprintf(message, sizeof message, "%s\n\n%s", rom_error, rom_path);
    ShowError("Unsupported DKC1 ROM", message);
    SDL_Quit();
    return 2;
  }

  char *music_pack_path = ConfiguredMusicPackPath();
  if (music_pack_path) {
    s_msu1 = Dkc1Msu1Open(music_pack_path, rom_error, sizeof rom_error);
    if (!s_msu1 ||
        !Dkc1Msu1ApplySpcMusicMute(rom, rom_size,
                                   rom_error, sizeof rom_error)) {
      fprintf(stderr, "warning: MSU-1 music disabled: %s\n", rom_error);
      Dkc1Msu1Close(s_msu1);
      s_msu1 = NULL;
    } else {
      fprintf(stderr, "msu1: replacement music active from %s\n",
              music_pack_path);
    }
    free(music_pack_path);
  }

  PrepareUserDirectory();
  const char *aspect = getenv("DKC1_ASPECT");
  const char *widescreen = getenv("DKC1_WIDESCREEN");
  if (aspect && strcmp(aspect, "16:10") == 0)
    Dkc1VideoSetAspect(kDkc1VideoAspect16x10);
  else if (aspect && strcmp(aspect, "4:3") == 0)
    Dkc1VideoSetAspect(kDkc1VideoAspectNative);
  else
    Dkc1VideoSetWidescreen(!(widescreen && *widescreen == '0'));
  {
    /* Level-wall presentation: the View menu's saved choice (glide when
     * never set), overridden by DKC1_WIDESCREEN_EDGE for this run only. */
    Dkc1VideoSetEdgePolicy(Dkc1MacSavedWidescreenEdge());
    const char *edge_text = getenv("DKC1_WIDESCREEN_EDGE");
    Dkc1EdgePolicy edge_policy;
    if (edge_text && *edge_text &&
        Dkc1EdgePolicyFromName(edge_text, &edge_policy))
      Dkc1VideoSetEdgePolicy(edge_policy);
  }
  Dkc1VideoSetRom(rom, rom_size);
  RtlRegisterGame(Dkc1GameInfo());
  if (!SnesInit(rom, (int)rom_size)) {
    ShowError("DKC1Recomp", "The runtime rejected the verified ROM.");
    free(rom);
    SDL_Quit();
    return 4;
  }

  if (!getenv("DKC1_BABY_KONG_ROM")) {
    char *baby_rom = Dkc1MacSavedBabyKongRom();
    if (baby_rom) {
      char baby_error[192];
      if (!Dkc1BabyKongLoadRom(baby_rom, baby_error, sizeof baby_error))
        fprintf(stderr, "warning: Baby Kong disabled: %s\n", baby_error);
      free(baby_rom);
    }
  }
  if (Dkc1BabyKongReady()) {
    const char *baby_enabled = getenv("DKC1_BABY_KONG");
    Dkc1BabyKongSetEnabled(
        baby_enabled ? EnvironmentEnabled("DKC1_BABY_KONG")
                     : Dkc1MacSavedBabyKongEnabled() != 0);
  }

  const char *snapshot = getenv("DKC1_SAVESTATE_INPUT");
  if (snapshot && *snapshot && !RtlLoadSnapshot(snapshot)) {
    ShowError("DKC1Recomp", "Unable to load DKC1_SAVESTATE_INPUT.");
    free(rom);
    SDL_Quit();
    return 20;
  }
  const char *import = getenv("DKC1_SUPERZSNES_STATE");
  if (import && *import) {
    char error[256];
    if ((snapshot && *snapshot) ||
        !Dkc1ImportSuperZsnesState(import, error, sizeof error)) {
      ShowError("DKC1Recomp", error);
      free(rom);
      SDL_Quit();
      return 20;
    }
  }

  s_paused = EnvironmentEnabled("DKC1_START_PAUSED");
  s_fullscreen_scaling = Dkc1MacSavedFullscreenScaling();
  s_haptics_enabled = !getenv("DKC1_HAPTICS") ||
                      EnvironmentEnabled("DKC1_HAPTICS");
  if (!HapticWorkerStart()) {
    fprintf(stderr, "warning: haptic worker unavailable: %s\n",
            SDL_GetError());
    s_haptics_enabled = 0;
  }
  Dkc1Msu1Reset(s_msu1);
  ObserveMsu1MusicState();
  {
    const char *smoke = getenv("DKC1_SMOKE_TEST_FRAMES");
    if (smoke && *smoke) {
      char *end = NULL;
      long frames = strtol(smoke, &end, 10);
      if (end && !*end && frames > 0)
        s_smoke_test_frames = frames;
    }
  }
  s_width = Dkc1VideoWidth();
  Dkc1BeginDrawing(s_pixels, (size_t)s_width * 4);
  if (s_paused)
    Dkc1DrawPpuFrame();
  if (!InitVideo()) {
    ShowError("DKC1Recomp", SDL_GetError());
    Cleanup(rom);
    return 3;
  }
  Dkc1MacInstallMenu();
  InitAudio();
  OpenFirstController();

  char error[256];
  {
    const char *playback_path = getenv("SNESRECOMP_INPUT_PLAY");
    if (playback_path && *playback_path &&
        !Dkc1InputPlaybackLoad(playback_path, &s_input_playback,
                               error, sizeof error)) {
      ShowError("Input playback failed", error);
      Cleanup(rom);
      return 20;
    }
  }
  if (Dkc1WramDumpOpenFromEnvironment(&s_wram_dump,
                                      error, sizeof error) < 0) {
    ShowError("WRAM dump setup failed", error);
    Cleanup(rom);
    return 20;
  }
  Dkc1FlightRecorderSetBuildInfo(
      DKC1_BUILD_COMMIT " " DKC1_BUILD_CONFIG);
  if (Dkc1FlightRecorderInitialize(error, sizeof error) < 0) {
    ShowError("Flight recorder setup failed", error);
    Cleanup(rom);
    return 20;
  }

  const char *haptics_status =
      s_haptics_enabled ? "controller stomp haptics on" : "haptics off";
  if (s_msu1 && Dkc1Msu1CurrentTrack(s_msu1))
    snprintf(s_status, sizeof s_status, "MSU-1 track %u | %s",
             Dkc1Msu1CurrentTrack(s_msu1), haptics_status);
  else if (s_msu1)
    snprintf(s_status, sizeof s_status, "MSU-1 waiting for music cue | %s",
             haptics_status);
  else
    snprintf(s_status, sizeof s_status, "Z/X/S/A controls | %s",
             haptics_status);
  if (EnvironmentEnabled("DKC1_START_FULLSCREEN"))
    SetFullscreen(1);
  UpdateTitle();
  InitDisplayLink();
  ApplyPresentationGeometry();
  Present();

  Dkc1FramePacer pacer;
  Dkc1DisplayPacer display_pacer;
  Dkc1PacingLog pacing_log;
  FramePacerInit(&pacer);
  DisplayPacerInit(&display_pacer);
  PacingLogInit(&pacing_log);

  while (s_running) {
    if (s_paused && !s_step_once) {
      PollEvents();
      if (!s_running)
        break;
      if (s_paused && !s_step_once) {
        s_reanchor_pacer = 1;
        Present();
        SDL_Delay(16);
        continue;
      }
    }

    const int single_step = s_paused && s_step_once;
    const double cadence_wait_start = FramePacerNow();
    pacing_log.target = 0.0;
    if (single_step && s_reanchor_pacer) {
      FramePacerReanchor(&pacer, FramePacerNow());
      s_reanchor_pacer = 0;
    }
    int display_frame_sync = 0;
    if (!single_step) {
      if (s_reanchor_pacer) {
        FramePacerReanchor(&pacer, FramePacerNow());
        s_reanchor_pacer = 0;
      }
      if (s_display_link_active) {
        if (!DisplayPacerWaitForTarget(&pacer, &display_pacer)) {
          PollEvents();
          s_reanchor_pacer = 1;
          continue;
        }
        pacing_log.target = pacer.next_deadline;
        display_frame_sync = 1;
      } else {
        FramePacerWaitForWorkWindow(&pacer);
        pacing_log.target = pacer.next_deadline;
      }
    }
    pacing_log.wait_ms =
        (FramePacerNow() - cadence_wait_start) * 1000.0 / pacer.frequency;

    const double work_start = FramePacerNow();
    double phase_start = work_start;
    Dkc1FrameWorkProfile work_profile = {0};
    /* Pump after the cadence wait so keyboard/controller state is sampled
     * near the display link's target presentation timestamp. */
    PollEvents();
    double phase_end = FramePacerNow();
    work_profile.events = phase_end - phase_start;
    if (!s_running)
      break;
    if (s_paused && !s_step_once) {
      s_reanchor_pacer = 1;
      continue;
    }
    if (!single_step && s_reanchor_pacer) {
      if (display_frame_sync) {
        pacer.previous_present = 0.0;
        pacer.title_window_start = 0.0;
        pacer.title_window_intervals = 0;
      } else {
        FramePacerReanchor(&pacer, FramePacerNow());
      }
      s_reanchor_pacer = 0;
    }
    phase_start = phase_end;
    uint32_t input = s_input_playback.count
        ? Dkc1InputPlaybackFrame(&s_input_playback, (size_t)s_host_frame)
        : PollInput();
    Dkc1DebugRecordInput(input);
    phase_end = FramePacerNow();
    work_profile.input = phase_end - phase_start;
    phase_start = phase_end;
    Dkc1StompProbeCapture(&s_stomp_probe, g_ram);
    RtlRunFrame(input);
    if (Dkc1StompProbeAccepted(&s_stomp_probe, g_ram))
      PulseStompHaptic();
    ObserveMsu1MusicState();
    phase_end = FramePacerNow();
    work_profile.emulation = phase_end - phase_start;
    if (g_fail || !Dkc1LastLleResult()) {
      char message[160];
      if (g_fail) {
        snprintf(message, sizeof message,
                 "Runtime failure (off-rails execution).");
      } else {
        snprintf(message, sizeof message, "Execution stopped at $%06x.",
                 (unsigned)Dkc1ResumePc());
      }
      ShowError("DKC1Recomp stopped", message);
      break;
    }
    phase_start = phase_end;
    Dkc1DrawPpuFrame();
    phase_end = FramePacerNow();
    work_profile.ppu = phase_end - phase_start;
    phase_start = phase_end;
    s_host_frame++;
    Dkc1BlankScanFrame(s_host_frame, s_pixels, s_width,
                       kDkc1VideoHeight, Dkc1VideoTerrainReady());
    Dkc1InvariantMonitorFrame(s_host_frame);
    if (!Dkc1WramDumpFrame(&s_wram_dump, s_host_frame,
                           snes_frame_counter, g_ram,
                           error, sizeof error)) {
      snprintf(s_status, sizeof s_status, "WRAM dump failed: %.180s", error);
      s_paused = 1;
    }
    Dkc1DebugDumpFrame((int)s_host_frame);
    Dkc1FlightRecorderRecord(s_host_frame, input);
    phase_end = FramePacerNow();
    work_profile.diagnostics = phase_end - phase_start;
    phase_start = phase_end;
    PumpAudio();
    phase_end = FramePacerNow();
    work_profile.audio = phase_end - phase_start;
    phase_start = phase_end;
    if (EnvironmentEnabled("DKC1_LIVE_TITLE") &&
        (s_host_frame % 60) == 0)
      UpdateWindowTitle();
    phase_end = FramePacerNow();
    work_profile.title = phase_end - phase_start;
    phase_start = phase_end;
    /* Upload and encode while the target still has several milliseconds of
     * lead. Only the lightweight drawable submission remains after the final
     * display-link wait. */
    PreparePresentation();
    phase_end = FramePacerNow();
    work_profile.ppu += phase_end - phase_start;
    const double work_end = FramePacerNow();
    FramePacerRecordWork(&pacer, work_end - work_start);
    FramePacerRecordWorkProfile(&pacer, &work_profile,
                                work_end - work_start);
    pacing_log.setup_ms =
        (work_profile.events + work_profile.input) * 1000.0 / pacer.frequency;
    pacing_log.emulation_ms =
        work_profile.emulation * 1000.0 / pacer.frequency;
    pacing_log.render_ms = work_profile.ppu * 1000.0 / pacer.frequency;
    pacing_log.diagnostics_ms =
        work_profile.diagnostics * 1000.0 / pacer.frequency;
    pacing_log.audio_ms = work_profile.audio * 1000.0 / pacer.frequency;
    PacingLogInjectTestStall(&pacing_log, s_host_frame);

    /* CADisplayLink wakes one interval before a concrete targetTimestamp.
     * Texture upload and command encoding are already complete. Submit the
     * prepared drawable at a four-millisecond lead so compositor pickup
     * variance cannot move ordinary frames between adjacent refresh slots. */
    if (!single_step) {
      if (display_frame_sync &&
          FramePacerNow() > pacer.next_deadline -
              pacer.frequency * kMacSubmitLeadSeconds) {
        if (!DisplayPacerWaitForTarget(&pacer, &display_pacer)) {
          PollEvents();
          s_reanchor_pacer = 1;
          continue;
        }
        pacing_log.target = pacer.next_deadline;
      }
      const double final_wait_start = FramePacerNow();
      FramePacerWaitUntil(
          pacer.next_deadline - pacer.frequency * kMacSubmitLeadSeconds,
          pacer.frequency);
      pacing_log.wait_ms +=
          (FramePacerNow() - final_wait_start) * 1000.0 / pacer.frequency;
    }
    const double present_start = FramePacerNow();
    SubmitPresentation();
    const double presented_at = FramePacerNow();
    FramePacerRecordPresentWait(&pacer, presented_at - present_start);
    FramePacerRecordPresent(&pacer, presented_at);
    PacingLogPresented(&pacing_log, &pacer, &display_pacer,
                       work_start, work_end, present_start, presented_at);
    if (single_step) {
      s_reanchor_pacer = 1;
    } else if (display_frame_sync) {
      s_reanchor_pacer = s_paused ? 1 : 0;
    } else {
      FramePacerAdvance(&pacer, presented_at,
                        s_reanchor_pacer || s_paused);
      s_reanchor_pacer = s_paused ? 1 : 0;
    }
    if (s_smoke_test_frames > 0 && s_host_frame >= s_smoke_test_frames) {
      snprintf(s_status, sizeof s_status,
               "smoke test complete at frame %ld", s_host_frame);
      UpdateTitle();
      s_running = 0;
    }
    s_step_once = 0;
  }

  FramePacerPrintStats(&pacer);
  DisplayPacerPrintStats(&display_pacer);
  PacingLogClose(&pacing_log);
  Cleanup(rom);
  return 0;
}
