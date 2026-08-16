#include "dkc1_game.h"
#include "dkc1_video.h"
#include "input_playback.h"
#include "wram_dump.h"
#include "dkc1_script.h"
#include "dkc1_debug_dump.h"
#include "verified_rom.h"

#include "common_cpu_infra.h"
#include "common_rtl.h"
#include "cpu_state.h"
#include "sha256.h"
#include "snes/ppu.h"
#include "snes/ws_shadow.h"
#include "snes/apu.h"
#include "snes/interp_bridge.h"
#include "snes/snes.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void PrintHash(FILE *stream, const uint8_t hash[32]) {
  for (int i = 0; i < 32; i++) fprintf(stream, "%02x", hash[i]);
}

static uint16_t ReadWram16(size_t address) {
  return (uint16_t)(g_ram[address] | ((uint16_t)g_ram[address + 1] << 8));
}

static int WriteFramePpm(const char *path, const uint8_t *pixels,
                         size_t width, size_t height, size_t pitch) {
  FILE *stream = fopen(path, "wb");
  if (!stream) return 0;
  int ok = fprintf(stream, "P6\n%zu %zu\n255\n", width, height) > 0;
  for (size_t y = 0; ok && y < height; y++) {
    const uint8_t *row = pixels + y * pitch;
    for (size_t x = 0; ok && x < width; x++) {
      const uint8_t rgb[3] = { row[x * 4 + 2], row[x * 4 + 1],
                               row[x * 4] };
      ok = fwrite(rgb, 1, sizeof rgb, stream) == sizeof rgb;
    }
  }
  if (fclose(stream) != 0) ok = 0;
  return ok;
}

static int ParseFrameNumber(const char *text, long fallback, long *value) {
  if (!text || !*text) {
    *value = fallback;
    return 1;
  }
  char *end = NULL;
  long parsed = strtol(text, &end, 10);
  if (!end || *end != '\0' || parsed < 0)
    return 0;
  *value = parsed;
  return 1;
}

static unsigned long long s_trace_pc_hits;

static void TracePc(CpuState *cpu, uint32_t pc24) {
  s_trace_pc_hits++;
  if (s_trace_pc_hits <= 16 ||
      (s_trace_pc_hits & (s_trace_pc_hits - 1)) == 0) {
    fprintf(stderr,
            "dkc1_trace_pc hit=%llu frame=%d pc=$%06x a=$%04x x=$%04x "
            "y=$%04x s=$%04x db=$%02x p=$%02x mode=$%04x entrance=$%04x "
            "fade=$%04x camera=[$%04x,$%04x] ptr7a=$%06x\n",
            s_trace_pc_hits, snes_frame_counter, (unsigned)pc24, cpu->A,
            cpu->X, cpu->Y, cpu->S, cpu->DB, cpu->P,
            (unsigned)ReadWram16(0x0032), (unsigned)ReadWram16(0x003e),
            (unsigned)ReadWram16(0x1df1),
            (unsigned)ReadWram16(0x088b), (unsigned)ReadWram16(0x0895),
            (unsigned)(g_ram[0x7a] | ((uint32_t)g_ram[0x7b] << 8) |
                       ((uint32_t)g_ram[0x7c] << 16)));
    fflush(stderr);
  }
}

int main(int argc, char **argv) {
  if (argc < 2 || argc > 3) {
    fprintf(stderr, "usage: dkc1_snesrecomp_headless <rom.sfc> [frames]\n");
    return 2;
  }
  long frame_limit = argc == 3 ? strtol(argv[2], NULL, 10) : 600;
  if (frame_limit < 1 || frame_limit > 1000000) {
    fprintf(stderr, "frames must be between 1 and 1000000\n");
    return 2;
  }

  size_t rom_size = 0;
  char rom_error[160];
  uint8_t *rom =
      Dkc1ReadVerifiedRom(argv[1], &rom_size, rom_error, sizeof rom_error);
  if (!rom) {
    fprintf(stderr, "%s: %s\n", rom_error, argv[1]);
    return 2;
  }

  const char *widescreen_text = getenv("DKC1_WIDESCREEN");
  Dkc1VideoSetWidescreen(
      widescreen_text && *widescreen_text && *widescreen_text != '0');
  Dkc1VideoSetRom(rom, rom_size);
  RtlRegisterGame(Dkc1GameInfo());
  if (!SnesInit(rom, (int)rom_size)) {
    fprintf(stderr, "snesrecomp rejected the verified ROM\n");
    free(rom);
    return 4;
  }

  const char *sram_input = getenv("DKC1_SRAM_INPUT");
  if (sram_input && *sram_input) {
    FILE *f = fopen(sram_input, "rb");
    bool loaded = f && g_sram && g_sram_size > 0 &&
                  fread(g_sram, 1, (size_t)g_sram_size, f) ==
                      (size_t)g_sram_size &&
                  fgetc(f) == EOF;
    if (f) fclose(f);
    if (!loaded) {
      fprintf(stderr, "unable to load exact SRAM image: %s\n", sram_input);
      free(rom);
      return 13;
    }
  }

  const char *savestate_input = getenv("DKC1_SAVESTATE_INPUT");
  const char *superzsnes_input = getenv("DKC1_SUPERZSNES_STATE");
  const char *savestate_output = getenv("DKC1_SAVESTATE_OUTPUT");
  long savestate_save_at = 0;
  if (!ParseFrameNumber(getenv("DKC1_SAVESTATE_SAVE_AT"), 0,
                        &savestate_save_at) ||
      savestate_save_at > frame_limit ||
      (savestate_save_at &&
       (!savestate_output || !*savestate_output)) ||
      (savestate_input && *savestate_input && superzsnes_input &&
       *superzsnes_input) ||
      (savestate_input && *savestate_input && savestate_output &&
       *savestate_output && strcmp(savestate_input, savestate_output) == 0)) {
    fprintf(stderr,
            "invalid snapshot configuration: SAVE_AT must be within this "
            "run and input/output paths must differ\n");
    free(rom);
    return 20;
  }
  if (savestate_input && *savestate_input) {
    if (!RtlLoadSnapshot(savestate_input)) {
      fprintf(stderr, "unable to load native snapshot: %s\n",
              savestate_input);
      free(rom);
      return 20;
    }
    fprintf(stderr, "savestate: loaded native snapshot %s frame=%d\n",
            savestate_input, snes_frame_counter);
  }
  if (superzsnes_input && *superzsnes_input) {
    char import_error[256];
    if (!Dkc1ImportSuperZsnesState(superzsnes_input, import_error,
                                   sizeof import_error)) {
      fprintf(stderr, "unable to import SuperZSNES state: %s\n", import_error);
      free(rom);
      return 20;
    }
    fprintf(stderr,
            "savestate: imported SuperZSNES bundle %s frame=%d "
            "audio_history=reconstructed\n",
            superzsnes_input, snes_frame_counter);
  }

  const char *trace_pc_text = getenv("DKC1_TRACE_PC");
  if (trace_pc_text && *trace_pc_text) {
    char *end = NULL;
    unsigned long trace_pc = strtoul(trace_pc_text, &end, 16);
    if (!end || *end != '\0' || trace_pc > 0xfffffful) {
      fprintf(stderr, "DKC1_TRACE_PC must be a 24-bit hexadecimal address\n");
      free(rom);
      return 2;
    }
    interp_bridge_set_pre_opcode_hook((uint32_t)trace_pc, TracePc);
    fprintf(stderr, "dkc1_trace_pc armed=$%06lx\n", trace_pc);
  }

  enum {
    kBufferWidth = kDkc1VideoWidescreenWidth,
    kHeight = kDkc1VideoHeight,
    kBytesPerPixel = kDkc1VideoBytesPerPixel
  };
  static uint8_t pixels[kBufferWidth * kHeight * kBytesPerPixel];
  const size_t frame_width = (size_t)Dkc1VideoWidth();
  const size_t frame_bytes = frame_width * kHeight * kBytesPerPixel;
  Dkc1BeginDrawing(pixels, frame_width * kBytesPerPixel);

  const char *frame_sequence_prefix = getenv("DKC1_FRAME_PPM_PREFIX");
  long frame_sequence_start = 0;
  long frame_sequence_end = frame_limit - 1;
  long frame_sequence_step = 1;
  if (frame_sequence_prefix && *frame_sequence_prefix &&
      (!ParseFrameNumber(getenv("DKC1_FRAME_PPM_START"), 0,
                         &frame_sequence_start) ||
       !ParseFrameNumber(getenv("DKC1_FRAME_PPM_END"), frame_limit - 1,
                         &frame_sequence_end) ||
       !ParseFrameNumber(getenv("DKC1_FRAME_PPM_STEP"), 1,
                         &frame_sequence_step) ||
       frame_sequence_step < 1 ||
       frame_sequence_start > frame_sequence_end ||
       frame_sequence_end >= frame_limit)) {
    fprintf(stderr,
            "invalid DKC1_FRAME_PPM_START/END/STEP sequence range\n");
    free(rom);
    return 18;
  }

  enum { kMaximumAudioFramesPerVideoFrame = 534 };
  int16_t audio[kMaximumAudioFramesPerVideoFrame * 2];
  const double audio_frames_per_video_frame = 32040.0 / 60.098811862;
  double audio_frame_accumulator = 0.0;
  unsigned long long audio_rendered_frames = 0;
  uint64_t audio_fnv1a = UINT64_C(14695981039346656037);
  FILE *audio_pcm = NULL;
  const char *audio_pcm_path = getenv("DKC1_AUDIO_PCM");
  if (audio_pcm_path && *audio_pcm_path) {
    audio_pcm = fopen(audio_pcm_path, "wb");
    if (!audio_pcm) {
      fprintf(stderr, "unable to open private audio output: %s\n",
              audio_pcm_path);
      free(rom);
      return 10;
    }
  }
  unsigned long video_active_frames = 0;
  unsigned long blank_frames = 0;
  unsigned long consecutive_blank_frames = 0;
  unsigned long max_consecutive_blank_frames = 0;
  unsigned long obj_range_over_frames = 0;
  unsigned long obj_time_over_frames = 0;
  unsigned long audio_active_frames = 0;
  unsigned long audio_silent_frames = 0;
  unsigned long long audio_nonzero_samples = 0;
  unsigned audio_peak = 0;

  /* DKC1 gameplay-state telemetry (addresses from the disassembly analysis:
   * $0032 game mode, $003E entrance/level ID, $1DF1 fade stage,
   * $088B/$0895 camera, $0028 frame counter, $057B banana total). */
  int state_initialized = 0;
  uint16_t previous_game_mode = 0;
  uint16_t previous_entrance = 0;
  uint16_t previous_fade = 0;
  unsigned state_events = 0;
  const char *state_trace_text = getenv("DKC1_STATE_TRACE");
  const int emit_state_trace =
      state_trace_text && *state_trace_text && *state_trace_text != '0';

  Dkc1InputPlayback input_playback = {0};
  {
    const char *p = getenv("SNESRECOMP_INPUT_PLAY");
    if (p && p[0]) {
      char error[192];
      if (!Dkc1InputPlaybackLoad(p, &input_playback, error, sizeof error)) {
        fprintf(stderr, "input_play: %s: %s\n", p, error);
        if (audio_pcm) fclose(audio_pcm);
        free(rom);
        return 17;
      }
      fprintf(stderr, "input_play: loaded %zu frames from %s\n",
              input_playback.count, p);
    }
  }

  {
    const char *script_path = getenv("DKC1_SCRIPT");
    if (script_path && *script_path) {
      char error[256];
      if (input_playback.count) {
        fprintf(stderr,
                "script: DKC1_SCRIPT and SNESRECOMP_INPUT_PLAY are mutually "
                "exclusive\n");
        if (audio_pcm) fclose(audio_pcm);
        Dkc1InputPlaybackFree(&input_playback);
        free(rom);
        return 20;
      }
      if (!Dkc1ScriptLoad(script_path, error, sizeof error)) {
        fprintf(stderr, "script: %s: %s\n", script_path, error);
        if (audio_pcm) fclose(audio_pcm);
        Dkc1InputPlaybackFree(&input_playback);
        free(rom);
        return 20;
      }
      fprintf(stderr, "script: loaded %s\n", script_path);
    }
  }

  Dkc1WramDump wram_dump;
  {
    char error[256];
    int opened = Dkc1WramDumpOpenFromEnvironment(
        &wram_dump, error, sizeof error);
    if (opened < 0) {
      fprintf(stderr, "wram_dump: %s\n", error);
      if (audio_pcm) fclose(audio_pcm);
      Dkc1InputPlaybackFree(&input_playback);
      free(rom);
      return 19;
    }
    if (opened)
      fprintf(stderr,
              "wram_dump: frames=%ld-%ld bytes_per_frame=%zu raw=%s\n",
              wram_dump.first_frame, wram_dump.last_frame,
              wram_dump.payload_size, wram_dump.raw_path);
  }

  long frames_executed = 0;
  int savestate_saved = 0;
  for (long frame = 0; frame < frame_limit; frame++) {
    Dkc1ScriptOps script_ops = {0};
    uint32_t _in;
    if (Dkc1ScriptActive()) {
      bool script_failed = false;
      _in = Dkc1ScriptNextInput(g_ram, &script_ops, &script_failed);
      if (script_failed) {
        fprintf(stderr, "script: %s\n", Dkc1ScriptError());
        if (audio_pcm) fclose(audio_pcm);
        (void)Dkc1WramDumpClose(&wram_dump, NULL, 0);
        Dkc1DebugDumpClose();
        Dkc1InputPlaybackFree(&input_playback);
        free(rom);
        return 21;
      }
      if (script_ops.state_load && !RtlLoadSnapshot(script_ops.state_load)) {
        fprintf(stderr, "script: unable to load snapshot %s\n",
                script_ops.state_load);
        if (audio_pcm) fclose(audio_pcm);
        (void)Dkc1WramDumpClose(&wram_dump, NULL, 0);
        Dkc1DebugDumpClose();
        Dkc1InputPlaybackFree(&input_playback);
        free(rom);
        return 22;
      }
      if (script_ops.checkpoint &&
          !Dkc1DebugCheckpoint(script_ops.checkpoint, (int)frame)) {
        fprintf(stderr, "script: unable to record checkpoint %s\n",
                script_ops.checkpoint);
        if (audio_pcm) fclose(audio_pcm);
        (void)Dkc1WramDumpClose(&wram_dump, NULL, 0);
        Dkc1DebugDumpClose();
        Dkc1ScriptFree();
        Dkc1InputPlaybackFree(&input_playback);
        free(rom);
        return 22;
      }
      if (script_ops.state_save && !RtlSaveSnapshot(script_ops.state_save)) {
        fprintf(stderr, "script: unable to save snapshot %s\n",
                script_ops.state_save);
        if (audio_pcm) fclose(audio_pcm);
        (void)Dkc1WramDumpClose(&wram_dump, NULL, 0);
        Dkc1DebugDumpClose();
        Dkc1ScriptFree();
        Dkc1InputPlaybackFree(&input_playback);
        free(rom);
        return 22;
      }
      if (!script_ops.run_frame) {
        if (Dkc1ScriptFinished()) break;
        frame--;  /* a state-load boundary does not consume a route frame */
        continue;
      }
    } else {
      _in = Dkc1InputPlaybackFrame(&input_playback, (size_t)frame);
      script_ops.run_frame = true;
    }
    Dkc1DebugRecordInput(_in);
    RtlRunFrame(_in);
    if (g_fail) {
      fprintf(stderr,
              "snesrecomp reported an off-rails runtime failure at host "
              "frame %ld resume=$%06x\n",
              frame, (unsigned)Dkc1ResumePc());
      if (audio_pcm) fclose(audio_pcm);
      (void)Dkc1WramDumpClose(&wram_dump, NULL, 0);
      Dkc1InputPlaybackFree(&input_playback);
      free(rom);
      return 6;
    }
    if (!Dkc1LastLleResult()) {
      fprintf(stderr,
              "LLE stopped at host frame %ld resume=$%06x x=$%04x "
              "apu_in=%02x%02x%02x%02x apu_out=%02x%02x%02x%02x "
              "spc_pc=$%04x ipl=%d\n",
              frame, (unsigned)Dkc1ResumePc(), g_cpu.X,
              g_snes->apu->inPorts[3], g_snes->apu->inPorts[2],
              g_snes->apu->inPorts[1], g_snes->apu->inPorts[0],
              g_snes->apu->outPorts[3], g_snes->apu->outPorts[2],
              g_snes->apu->outPorts[1], g_snes->apu->outPorts[0],
              g_snes->apu->spc->pc, g_snes->apu->romReadable ? 1 : 0);
      if (audio_pcm) fclose(audio_pcm);
      (void)Dkc1WramDumpClose(&wram_dump, NULL, 0);
      Dkc1InputPlaybackFree(&input_playback);
      free(rom);
      return 5;
    }
    Dkc1DrawPpuFrame();
    if (g_ppu->rangeOver) obj_range_over_frames++;
    if (g_ppu->timeOver) obj_time_over_frames++;
    {
      char error[192];
      if (!Dkc1WramDumpFrame(&wram_dump, frame + 1,
                             snes_frame_counter, g_ram,
                             error, sizeof error)) {
        fprintf(stderr, "wram_dump: %s\n", error);
        if (audio_pcm) fclose(audio_pcm);
        (void)Dkc1WramDumpClose(&wram_dump, NULL, 0);
        Dkc1InputPlaybackFree(&input_playback);
        free(rom);
        return 19;
      }
    }
    Dkc1DebugDumpFrame((int)(frame + 1));
    if (frame_sequence_prefix && *frame_sequence_prefix &&
        frame >= frame_sequence_start && frame <= frame_sequence_end &&
        (frame - frame_sequence_start) % frame_sequence_step == 0) {
      char path[1024];
      int length = snprintf(path, sizeof path, "%s_%06ld.ppm",
                            frame_sequence_prefix, frame);
      if (length < 0 || (size_t)length >= sizeof path ||
          !WriteFramePpm(path, pixels, frame_width, kHeight,
                         frame_width * kBytesPerPixel)) {
        fprintf(stderr, "unable to write private frame sequence at %ld\n",
                frame);
        if (audio_pcm) fclose(audio_pcm);
        (void)Dkc1WramDumpClose(&wram_dump, NULL, 0);
        Dkc1InputPlaybackFree(&input_playback);
        free(rom);
        return 18;
      }
    }

    uint16_t game_mode = ReadWram16(0x0032);
    uint16_t entrance = ReadWram16(0x003e);
    uint16_t fade = ReadWram16(0x1df1);
    int state_changed = !state_initialized ||
                        game_mode != previous_game_mode ||
                        entrance != previous_entrance ||
                        fade != previous_fade;
    if (state_changed) {
      state_events++;
      if (emit_state_trace) {
        fprintf(stderr,
                "state_event frame=%ld mode=$%04x entrance=$%04x "
                "fade=$%04x camera=[$%04x,$%04x] frame_ctr=$%04x "
                "bananas=$%04x\n",
                frame + 1, game_mode, entrance, fade,
                ReadWram16(0x088b), ReadWram16(0x0895),
                ReadWram16(0x0028), ReadWram16(0x057b));
      }
    }
    previous_game_mode = game_mode;
    previous_entrance = entrance;
    previous_fade = fade;
    state_initialized = 1;

    int frame_active = 0;
    for (size_t i = 0; i < frame_bytes; i++) {
      if (pixels[i] != 0) {
        frame_active = 1;
        break;
      }
    }
    if (frame_active) {
      video_active_frames++;
      consecutive_blank_frames = 0;
    } else {
      blank_frames++;
      consecutive_blank_frames++;
      if (consecutive_blank_frames > max_consecutive_blank_frames)
        max_consecutive_blank_frames = consecutive_blank_frames;
    }

    audio_frame_accumulator += audio_frames_per_video_frame;
    int audio_frames_this_frame = (int)audio_frame_accumulator;
    audio_frame_accumulator -= audio_frames_this_frame;
    if (audio_frames_this_frame < 0 ||
        audio_frames_this_frame > kMaximumAudioFramesPerVideoFrame) {
      fprintf(stderr, "invalid audio frame request: %d\n",
              audio_frames_this_frame);
      if (audio_pcm) fclose(audio_pcm);
      (void)Dkc1WramDumpClose(&wram_dump, NULL, 0);
      Dkc1InputPlaybackFree(&input_playback);
      free(rom);
      return 11;
    }
    size_t audio_samples_this_frame =
        (size_t)audio_frames_this_frame * 2u;
    memset(audio, 0, audio_samples_this_frame * sizeof audio[0]);
    RtlRenderAudio(audio, audio_frames_this_frame, 2);
    audio_rendered_frames += (unsigned)audio_frames_this_frame;
    int audio_active = 0;
    for (size_t i = 0; i < audio_samples_this_frame; i++) {
      int sample = audio[i];
      unsigned magnitude = (unsigned)(sample < 0 ? -sample : sample);
      if (magnitude != 0) {
        audio_active = 1;
        audio_nonzero_samples++;
        if (magnitude > audio_peak) audio_peak = magnitude;
      }
      audio_fnv1a ^= (uint8_t)(sample & 0xff);
      audio_fnv1a *= UINT64_C(1099511628211);
      audio_fnv1a ^= (uint8_t)(((uint16_t)sample >> 8) & 0xff);
      audio_fnv1a *= UINT64_C(1099511628211);
    }
    if (audio_pcm &&
        fwrite(audio, sizeof audio[0], audio_samples_this_frame, audio_pcm) !=
            audio_samples_this_frame) {
      fprintf(stderr, "unable to write private audio output: %s\n",
              audio_pcm_path);
      fclose(audio_pcm);
      (void)Dkc1WramDumpClose(&wram_dump, NULL, 0);
      Dkc1InputPlaybackFree(&input_playback);
      free(rom);
      return 12;
    }
    if (audio_active)
      audio_active_frames++;
    else
      audio_silent_frames++;

    if (savestate_output && *savestate_output &&
        savestate_save_at == frame + 1) {
      if (!RtlSaveSnapshot(savestate_output)) {
        fprintf(stderr,
                "unable to save native snapshot at relative frame %ld: %s\n",
                frame + 1, savestate_output);
        if (audio_pcm) fclose(audio_pcm);
        (void)Dkc1WramDumpClose(&wram_dump, NULL, 0);
        Dkc1InputPlaybackFree(&input_playback);
        free(rom);
        return 20;
      }
      savestate_saved = 1;
    }
    frames_executed = frame + 1;
  }

  if (savestate_output && *savestate_output && savestate_save_at != 0 &&
      !savestate_saved) {
    fprintf(stderr,
            "snapshot frame %ld was not reached (route completed at %ld)\n",
            savestate_save_at, frames_executed);
    if (audio_pcm) fclose(audio_pcm);
    (void)Dkc1WramDumpClose(&wram_dump, NULL, 0);
    Dkc1InputPlaybackFree(&input_playback);
    free(rom);
    return 20;
  }
  if (savestate_output && *savestate_output && savestate_save_at == 0 &&
      !RtlSaveSnapshot(savestate_output)) {
    fprintf(stderr, "unable to save native snapshot at end of run: %s\n",
            savestate_output);
    if (audio_pcm) fclose(audio_pcm);
    (void)Dkc1WramDumpClose(&wram_dump, NULL, 0);
    Dkc1InputPlaybackFree(&input_playback);
    free(rom);
    return 20;
  }
  if (savestate_output && *savestate_output)
    fprintf(stderr, "savestate: saved native snapshot %s relative_frame=%ld\n",
            savestate_output,
            savestate_save_at ? savestate_save_at : frames_executed);

  if (audio_pcm && fclose(audio_pcm) != 0) {
    fprintf(stderr, "unable to close private audio output: %s\n",
            audio_pcm_path);
    (void)Dkc1WramDumpClose(&wram_dump, NULL, 0);
    free(rom);
    Dkc1InputPlaybackFree(&input_playback);
    return 13;
  }
  audio_pcm = NULL;
  {
    char error[192];
    if (!Dkc1WramDumpClose(&wram_dump, error, sizeof error)) {
      fprintf(stderr, "wram_dump: %s\n", error);
      Dkc1InputPlaybackFree(&input_playback);
      free(rom);
      return 19;
    }
  }

  uint8_t frame_hash[32];
  uint8_t wram_hash[32];
  uint8_t vram_hash[32];
  uint8_t cgram_hash[32];
  uint8_t oam_hash[32];
  uint8_t oam_source_hash[32];
  uint8_t oam_bytes[544];
  unsigned vram_words = 0;
  unsigned cgram_words = 0;
  for (size_t i = 0; i < sizeof g_ppu->vram / sizeof g_ppu->vram[0]; i++)
    if (g_ppu->vram[i] != 0) vram_words++;
  for (size_t i = 0; i < sizeof g_ppu->cgram / sizeof g_ppu->cgram[0]; i++)
    if (g_ppu->cgram[i] != 0) cgram_words++;
  sha256_compute(pixels, frame_bytes, frame_hash);
  sha256_compute(g_ram, 0x20000, wram_hash);
  sha256_compute((const uint8_t *)g_ppu->vram, sizeof g_ppu->vram, vram_hash);
  sha256_compute((const uint8_t *)g_ppu->cgram, sizeof g_ppu->cgram,
                 cgram_hash);
  memcpy(oam_bytes, g_ppu->oam, sizeof g_ppu->oam);
  memcpy(oam_bytes + sizeof g_ppu->oam, g_ppu->highOam,
         sizeof g_ppu->highOam);
  sha256_compute(oam_bytes, sizeof oam_bytes, oam_hash);
  /* DKC1 builds its OAM image at WRAM $0200-$041F (DKC1_Global_OAMBuffer +
   * upper table), transferred during VBlank. */
  sha256_compute(g_ram + 0x200, sizeof oam_bytes, oam_source_hash);
  WsShadowMarginStat shadow[2];
  WsShadowGetMarginStats(0, &shadow[0]);
  WsShadowGetMarginStats(1, &shadow[1]);
  printf("video_state inidisp=$%02x bgmode=$%02x main=$%02x sub=$%02x "
         "bgsc=[$%02x,$%02x,$%02x,$%02x] "
         "scroll=[%u,%u;%u,%u;%u,%u;%u,%u] terrain_ready=%d "
         "nmi=%d frame_counter=%d vram_words=%u cgram_words=%u "
         "obj_range_over=%d obj_time_over=%d "
         "mode=$%04x entrance=$%04x fade=$%04x camera=[$%04x,$%04x] "
         "bounds=[$%04x,$%04x] presentation_bias=%d "
         "frame_ctr=$%04x bananas=$%04x\n",
         g_ppu->inidisp, g_ppu->bgmode, g_ppu->screenEnabled[0],
         g_ppu->screenEnabled[1], g_ppu->bgXsc[0], g_ppu->bgXsc[1],
         g_ppu->bgXsc[2], g_ppu->bgXsc[3],
         g_ppu->hScroll[0], g_ppu->vScroll[0],
         g_ppu->hScroll[1], g_ppu->vScroll[1],
         g_ppu->hScroll[2], g_ppu->vScroll[2],
         g_ppu->hScroll[3], g_ppu->vScroll[3],
         Dkc1VideoTerrainReady() ? 1 : 0,
         g_snes->nmiEnabled ? 1 : 0,
         snes_frame_counter, vram_words, cgram_words,
         g_ppu->rangeOver ? 1 : 0, g_ppu->timeOver ? 1 : 0,
         ReadWram16(0x0032), ReadWram16(0x003e), ReadWram16(0x1df1),
         ReadWram16(0x088b), ReadWram16(0x0895),
         ReadWram16(0x1b23), ReadWram16(0x1b25),
         Dkc1VideoPresentationBias(), ReadWram16(0x0028),
         ReadWram16(0x057b));
  printf("shadow_stats "
         "bg1=[west_hit=%llu west_miss=%llu east_hit=%llu east_miss=%llu "
         "west_blank=%llu east_blank=%llu west_cont=%llu east_cont=%llu "
         "west_raw=%llu east_raw=%llu] "
         "bg2=[west_hit=%llu west_miss=%llu east_hit=%llu east_miss=%llu "
         "west_blank=%llu east_blank=%llu west_cont=%llu east_cont=%llu "
         "west_raw=%llu east_raw=%llu]\n",
         (unsigned long long)shadow[0].westHit,
         (unsigned long long)shadow[0].westMiss,
         (unsigned long long)shadow[0].eastHit,
         (unsigned long long)shadow[0].eastMiss,
         (unsigned long long)shadow[0].westBlank,
         (unsigned long long)shadow[0].eastBlank,
         (unsigned long long)shadow[0].westRawContinuation,
         (unsigned long long)shadow[0].eastRawContinuation,
         (unsigned long long)shadow[0].westRawFallback,
         (unsigned long long)shadow[0].eastRawFallback,
         (unsigned long long)shadow[1].westHit,
         (unsigned long long)shadow[1].westMiss,
         (unsigned long long)shadow[1].eastHit,
         (unsigned long long)shadow[1].eastMiss,
         (unsigned long long)shadow[1].westBlank,
         (unsigned long long)shadow[1].eastBlank,
         (unsigned long long)shadow[1].westRawContinuation,
         (unsigned long long)shadow[1].eastRawContinuation,
         (unsigned long long)shadow[1].westRawFallback,
         (unsigned long long)shadow[1].eastRawFallback);
  printf("frame_sha256=");
  PrintHash(stdout, frame_hash);
  printf("\nwram_sha256=");
  PrintHash(stdout, wram_hash);
  printf("\nvram_sha256=");
  PrintHash(stdout, vram_hash);
  printf("\ncgram_sha256=");
  PrintHash(stdout, cgram_hash);
  printf("\noam_sha256=");
  PrintHash(stdout, oam_hash);
  printf("\noam_source_sha256=");
  PrintHash(stdout, oam_source_hash);
  printf("\nrun_stats video_active_frames=%lu blank_frames=%lu "
         "max_consecutive_blank_frames=%lu obj_range_over_frames=%lu "
         "obj_time_over_frames=%lu audio_active_frames=%lu "
         "audio_silent_frames=%lu audio_frames=%llu "
         "audio_nonzero_samples=%llu audio_peak=%u audio_fnv1a=%016llx "
         "state_events=%u",
         video_active_frames, blank_frames, max_consecutive_blank_frames,
         obj_range_over_frames, obj_time_over_frames,
         audio_active_frames, audio_silent_frames, audio_rendered_frames,
         audio_nonzero_samples, audio_peak,
         (unsigned long long)audio_fnv1a, state_events);
  const char *frame_output = getenv("DKC1_FRAME_PPM");
  if (frame_output && *frame_output) {
    if (!WriteFramePpm(frame_output, pixels, frame_width, kHeight,
                       frame_width * kBytesPerPixel)) {
      fprintf(stderr, "\nunable to write private frame output: %s\n",
              frame_output);
      Dkc1InputPlaybackFree(&input_playback);
      free(rom);
      return 7;
    }
    printf("\nframe_output=%s", frame_output);
  }
  const char *wram_output = getenv("DKC1_WRAM_OUTPUT");
  if (wram_output && *wram_output) {
    FILE *stream = fopen(wram_output, "wb");
    int wram_ok = stream && fwrite(g_ram, 1, 0x20000, stream) == 0x20000;
    if (stream && fclose(stream) != 0) wram_ok = 0;
    if (!wram_ok) {
      fprintf(stderr, "\nunable to write private WRAM output: %s\n",
              wram_output);
      Dkc1InputPlaybackFree(&input_playback);
      free(rom);
      return 9;
    }
    printf("\nwram_output=%s", wram_output);
  }
  const char *vram_output = getenv("DKC1_VRAM_OUTPUT");
  if (vram_output && *vram_output) {
    FILE *stream = fopen(vram_output, "wb");
    size_t vbytes = sizeof g_ppu->vram;
    int vram_ok = stream && fwrite(g_ppu->vram, 1, vbytes, stream) == vbytes;
    if (stream && fclose(stream) != 0) vram_ok = 0;
    if (!vram_ok) {
      fprintf(stderr, "\nunable to write private VRAM output: %s\n",
              vram_output);
      Dkc1InputPlaybackFree(&input_playback);
      free(rom);
      return 9;
    }
    printf("\nvram_output=%s", vram_output);
  }
  Dkc1DebugDumpClose();
  Dkc1ScriptFree();
  printf("\nresult=completed frames=%ld\n", frames_executed);
  free(rom);
  Dkc1InputPlaybackFree(&input_playback);
  return 0;
}
