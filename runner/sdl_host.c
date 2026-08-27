/* Native macOS SDL2 frontend for DKC1Recomp.
 *
 * The recompiled cartridge/runtime stays identical to the Win32 and headless
 * hosts. This file owns only host presentation, input, queued audio, timing,
 * and user-facing save/repro shortcuts.
 */
#include "dkc1_blank_scan.h"
#include "dkc1_debug_dump.h"
#include "dkc1_flight_recorder.h"
#include "dkc1_game.h"
#include "dkc1_invariant_monitor.h"
#include "dkc1_video.h"
#include "macos_file_picker.h"
#include "verified_rom.h"
#include "wram_dump.h"

#include "common_cpu_infra.h"
#include "common_rtl.h"
#include "snes/snes.h"

#include <SDL.h>

#include <float.h>
#include <limits.h>
#include <mach/mach_time.h>
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
#define DKC1_BUILD_CONFIG "macos-dev"
#endif

enum {
  kWindowScale = 3,
  kAudioRate = 32040,
  kAudioChannels = 2,
  kAudioScratchFrames = 1024,
};

static const double kNativeFramesPerSecond = 60.098811862;

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
  uint64_t presented_frames;
  uint64_t interval_count;
  uint64_t title_window_intervals;
  uint64_t long_intervals;
  uint64_t reanchors;
} Dkc1FramePacer;

static uint8_t s_pixels[kDkc1VideoWidescreenWidth * kDkc1VideoHeight * 4];
static SDL_Window *s_window;
static SDL_Renderer *s_renderer;
static SDL_Texture *s_texture;
static SDL_AudioDeviceID s_audio_device;
static SDL_GameController *s_controller;
static int16_t s_audio_scratch[kAudioScratchFrames * kAudioChannels];
static double s_audio_accumulator;
static int s_running = 1;
static int s_paused;
static int s_step_once;
static int s_fullscreen;
static int s_width;
static long s_host_frame;
static long s_smoke_test_frames;
static int s_reanchor_pacer;
static double s_present_fps;
static char s_status[256] = "ready";
static Dkc1WramDump s_wram_dump;

static int EnvironmentEnabled(const char *name) {
  const char *value = getenv(name);
  return value && *value && *value != '0';
}

static double FramePacerNow(void) {
  return (double)mach_absolute_time();
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

/* Sleep on an absolute Mach deadline, retaining only the final 250 us for a
 * low-power CPU spin. Relative millisecond sleeps accumulate phase error and
 * were the source of alternating short/long presentation intervals. */
static void FramePacerWaitUntil(double deadline, double frequency) {
  const double spin_ticks = frequency / 4000.0;
  double now = FramePacerNow();
  if (deadline - now > spin_ticks)
    (void)mach_wait_until((uint64_t)(deadline - spin_ticks));
  while (FramePacerNow() < deadline)
    FramePacerCpuRelax();
}

static void FramePacerInit(Dkc1FramePacer *pacer) {
  mach_timebase_info_data_t timebase = {0, 0};
  mach_timebase_info(&timebase);
  if (!timebase.numer || !timebase.denom) {
    timebase.numer = 1;
    timebase.denom = 1;
  }
  memset(pacer, 0, sizeof *pacer);
  pacer->frequency = 1000000000.0 * (double)timebase.denom /
                     (double)timebase.numer;
  pacer->ticks_per_frame = pacer->frequency / kNativeFramesPerSecond;
  pacer->next_deadline = FramePacerNow() + pacer->ticks_per_frame;
  pacer->estimated_work_ticks = pacer->frequency / 500.0;
  pacer->previous_present = FramePacerNow();
  pacer->title_window_start = pacer->previous_present;
  pacer->interval_min = DBL_MAX;
}

static void FramePacerReanchor(Dkc1FramePacer *pacer, double now) {
  pacer->next_deadline = now + pacer->ticks_per_frame;
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
  double reserve = pacer->estimated_work_ticks + pacer->frequency / 4000.0;
  const double maximum = pacer->ticks_per_frame * 0.75;
  if (reserve > maximum)
    reserve = maximum;
  FramePacerWaitUntil(pacer->next_deadline - reserve, pacer->frequency);
}

static void FramePacerRecordWork(Dkc1FramePacer *pacer, double work_ticks) {
  const double minimum = pacer->frequency / 2000.0;
  const double maximum = pacer->ticks_per_frame * 0.75;
  pacer->work_sum += work_ticks;
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
              "[fps-stall] frame=%ld phase=present_interval duration_ms=%.3f\n",
              s_host_frame, interval * 1000.0 / pacer->frequency);
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

static void FramePacerAdvance(Dkc1FramePacer *pacer, double presented_at,
                              int force_reanchor) {
  const double lateness = presented_at - pacer->next_deadline;
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
          "average_work_ms=%.3f max_work_ms=%.3f\n",
          (unsigned long long)pacer->presented_frames, active_seconds,
          active_seconds > 0.0
              ? (double)pacer->interval_count / active_seconds : 0.0,
          kNativeFramesPerSecond,
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
          pacer->work_max * 1000.0 / pacer->frequency);
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

static void UpdateWindowTitle(void) {
  if (!s_window)
    return;
  char title[512];
  if (s_present_fps > 0.0) {
    snprintf(title, sizeof title,
             "DKC1Recomp %s | frame %ld | %.1f FPS | %s | %s | %s | %s",
             DKC1_BUILD_COMMIT, s_host_frame, s_present_fps,
             s_paused ? "PAUSED" : "running",
             Dkc1VideoIsWidescreen() ? "16:9" : "4:3",
             LayerName(Dkc1DebugLayerMask()), s_status);
  } else {
    snprintf(title, sizeof title,
             "DKC1Recomp %s | frame %ld | %s | %s | %s | %s",
             DKC1_BUILD_COMMIT, s_host_frame,
             s_paused ? "PAUSED" : "running",
             Dkc1VideoIsWidescreen() ? "16:9" : "4:3",
             LayerName(Dkc1DebugLayerMask()), s_status);
  }
  SDL_SetWindowTitle(s_window, title);
}

static void UpdateTitle(void) {
  UpdateWindowTitle();
  Dkc1MacUpdateMenuState(s_paused, s_fullscreen,
                         Dkc1VideoIsWidescreen(),
                         Dkc1DebugLayerMask(),
                         Dkc1DebugProvenanceOverlay());
}

static int ResolveRomPath(int argc, char **argv, char output[PATH_MAX]) {
  const char *candidate = argc > 1 ? argv[1] : getenv("DKC1_ROM");
  char *picked = NULL;
  if (!candidate || !*candidate) {
    picked = Dkc1MacChooseRom();
    candidate = picked;
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

static bool InitVideo(void) {
  const int window_width = s_width * kWindowScale;
  const int window_height = kDkc1VideoHeight * kWindowScale;
  s_window = SDL_CreateWindow(
      "DKC1Recomp", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
      window_width, window_height,
      SDL_WINDOW_ALLOW_HIGHDPI | SDL_WINDOW_RESIZABLE);
  if (!s_window)
    return false;

  SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");
  s_renderer = SDL_CreateRenderer(s_window, -1, SDL_RENDERER_ACCELERATED);
  if (!s_renderer)
    s_renderer = SDL_CreateRenderer(s_window, -1, SDL_RENDERER_SOFTWARE);
  if (!s_renderer)
    return false;

  s_texture = SDL_CreateTexture(s_renderer, SDL_PIXELFORMAT_ARGB8888,
                                SDL_TEXTUREACCESS_STREAMING, s_width,
                                kDkc1VideoHeight);
  if (!s_texture)
    return false;
  SDL_SetTextureBlendMode(s_texture, SDL_BLENDMODE_NONE);
  SDL_RenderSetLogicalSize(s_renderer, s_width, kDkc1VideoHeight);
  SDL_RenderSetIntegerScale(s_renderer, SDL_TRUE);
  SDL_SetRenderDrawColor(s_renderer, 0, 0, 0, 255);
  return true;
}

static void Present(void) {
  SDL_UpdateTexture(s_texture, NULL, s_pixels, s_width * 4);
  SDL_RenderClear(s_renderer);
  SDL_RenderCopy(s_renderer, s_texture, NULL, NULL);
  SDL_RenderPresent(s_renderer);
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

static void ControllerRemoved(SDL_JoystickID instance) {
  if (!s_controller)
    return;
  SDL_Joystick *joystick = SDL_GameControllerGetJoystick(s_controller);
  if (SDL_JoystickInstanceID(joystick) == instance) {
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
  SDL_PauseAudioDevice(s_audio_device, 0);
  return true;
}

static void PumpAudio(void) {
  if (!s_audio_device)
    return;
  s_audio_accumulator += (double)kAudioRate / kNativeFramesPerSecond;
  int frames = (int)s_audio_accumulator;
  s_audio_accumulator -= frames;
  if (frames <= 0)
    return;
  if (frames > kAudioScratchFrames)
    frames = kAudioScratchFrames;
  RtlRenderAudio(s_audio_scratch, frames, kAudioChannels);
  const Uint32 bytes = (Uint32)frames * kAudioChannels * sizeof(int16_t);
  const Uint32 queued = SDL_GetQueuedAudioSize(s_audio_device);
  if (queued < (Uint32)(kAudioRate * kAudioChannels * sizeof(int16_t) / 4))
    SDL_QueueAudio(s_audio_device, s_audio_scratch, bytes);
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
static void SetAspectMode(int widescreen) {
  const int requested = widescreen != 0;
  if (Dkc1VideoIsWidescreen() == requested)
    return;

  const int old_width = s_width;
  Dkc1VideoSetWidescreen(requested);
  const int new_width = Dkc1VideoWidth();
  SDL_Texture *new_texture = SDL_CreateTexture(
      s_renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING,
      new_width, kDkc1VideoHeight);
  if (!new_texture) {
    Dkc1VideoSetWidescreen(!requested);
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
  SDL_RenderSetLogicalSize(s_renderer, s_width, kDkc1VideoHeight);
  SDL_RenderSetIntegerScale(s_renderer, SDL_TRUE);
  if (!s_fullscreen)
    SDL_SetWindowSize(s_window, s_width * kWindowScale,
                      kDkc1VideoHeight * kWindowScale);
  snprintf(s_status, sizeof s_status, "aspect changed to %s (%dx%d)",
           requested ? "widescreen 16:9" : "native 4:3",
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
  s_reanchor_pacer = 1;
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
    case kDkc1MacMenuFullscreen:
      SetFullscreen(!s_fullscreen);
      return;
    case kDkc1MacMenuAspectNative:
      SetAspectMode(0);
      return;
    case kDkc1MacMenuAspectWidescreen:
      SetAspectMode(1);
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
  if (s_controller)
    SDL_GameControllerClose(s_controller);
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
  if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMECONTROLLER) != 0) {
    fprintf(stderr, "SDL initialization failed: %s\n", SDL_GetError());
    return 3;
  }

  char rom_path[PATH_MAX] = {0};
  if (!ResolveRomPath(argc, argv, rom_path)) {
    SDL_Quit();
    return 0;
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

  PrepareUserDirectory();
  const char *widescreen = getenv("DKC1_WIDESCREEN");
  Dkc1VideoSetWidescreen(!(widescreen && *widescreen == '0'));
  Dkc1VideoSetRom(rom, rom_size);
  RtlRegisterGame(Dkc1GameInfo());
  if (!SnesInit(rom, (int)rom_size)) {
    ShowError("DKC1Recomp", "The runtime rejected the verified ROM.");
    free(rom);
    SDL_Quit();
    return 4;
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

  snprintf(s_status, sizeof s_status,
           "Z/X/S/A controls | F7 pause | F11/F12 state | Alt-Return full screen");
  UpdateTitle();
  Present();

  Dkc1FramePacer pacer;
  FramePacerInit(&pacer);

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
    if (single_step && s_reanchor_pacer) {
      FramePacerReanchor(&pacer, FramePacerNow());
      s_reanchor_pacer = 0;
    }
    if (!single_step) {
      if (s_reanchor_pacer) {
        FramePacerReanchor(&pacer, FramePacerNow());
        s_reanchor_pacer = 0;
      }
      FramePacerWaitForWorkWindow(&pacer);
    }

    const double work_start = FramePacerNow();
    /* Pump after the coarse wait so keyboard/controller state is sampled near
     * the presentation deadline. AppKit menu/title work is deliberately part
     * of the measured workload rather than unbudgeted work between frames. */
    PollEvents();
    if (!s_running)
      break;
    if (s_paused && !s_step_once) {
      s_reanchor_pacer = 1;
      continue;
    }
    if (!single_step && s_reanchor_pacer) {
      FramePacerReanchor(&pacer, FramePacerNow());
      s_reanchor_pacer = 0;
    }
    uint32_t input = PollInput();
    Dkc1DebugRecordInput(input);
    RtlRunFrame(input);
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
    Dkc1DrawPpuFrame();
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
    PumpAudio();
    if ((s_host_frame % 60) == 0)
      UpdateWindowTitle();
    const double work_end = FramePacerNow();
    FramePacerRecordWork(&pacer, work_end - work_start);

    if (!single_step)
      FramePacerWaitUntil(pacer.next_deadline, pacer.frequency);
    Present();
    const double presented_at = FramePacerNow();
    FramePacerRecordPresent(&pacer, presented_at);
    if (single_step) {
      s_reanchor_pacer = 1;
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
  Cleanup(rom);
  return 0;
}
