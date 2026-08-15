/* Minimal interactive Win32 host for the DKC1 recompilation.
 *
 * One file, no SDL: GDI StretchDIBits presentation, waveOut audio, and
 * keyboard input mapped to the shared runtime's snes9x-style joypad bits
 * (bit0=B, 1=Y, 2=Select, 3=Start, 4=Up, 5=Down, 6=Left, 7=Right,
 *  8=A, 9=X, 10=L, 11=R).
 *
 * Keys: arrows = D-pad, Z=B (jump), X=Y (run/grab), A=X, S=A,
 *       Q=L, W=R, Enter=Start, Right Shift=Select, Esc=quit.
 */
#include "dkc1_game.h"
#include "dkc1_debug_dump.h"
#include "dkc1_flight_recorder.h"
#include "dkc1_script.h"
#include "dkc1_video.h"
#include "input_playback.h"
#include "verified_rom.h"
#include "wram_dump.h"

#include "common_cpu_infra.h"
#include "common_rtl.h"
#include "snes/snes.h"

#include <windows.h>
#include <mmsystem.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
  kScale = 2,
  kPanelWidth = 380,
  kAudioBuffers = 8,
  kAudioFramesPerBuffer = 536,
};

static uint8_t s_pixels[kDkc1VideoWidescreenWidth * kDkc1VideoHeight * 4];
static BITMAPINFO s_bmi;
static HWND s_window;
static int s_running = 1;
static int s_width;
static int s_height;
static int s_panel_enabled = 1;
static int s_paused;
static int s_step_once;
static int s_script_loaded;
static int s_script_failed;
static int s_route_finished;
static int s_export_requested;
static long s_host_frame;
static uint32_t s_last_input;
static char s_host_status[512] = "manual play";
static Dkc1InputPlayback s_input_playback;
static Dkc1WramDump s_wram_dump;

static uint16_t ReadWram16(unsigned address) {
  return (uint16_t)(g_ram[address] | ((uint16_t)g_ram[address + 1] << 8));
}

static int EnvironmentEnabled(const char *name) {
  const char *value = getenv(name);
  return value && *value && *value != '0';
}

static const char *LayerModeName(uint8_t mask) {
  switch (mask) {
    case 0x01: return "BG1";
    case 0x02: return "BG2";
    case 0x04: return "BG3";
    case 0x08: return "BG4";
    case 0x10: return "OBJ";
    default: return "composite";
  }
}

static void UpdateDebugTitle(void) {
  if (!s_window) return;
  char title[320];
  snprintf(title, sizeof title,
           "DKC1Recomp | frame %ld | %s | %s | provenance %s",
           s_host_frame, s_paused ? "PAUSED" : "running",
           LayerModeName(Dkc1DebugLayerMask()),
           Dkc1DebugProvenanceOverlay() ? "ON" : "off");
  SetWindowTextA(s_window, title);
}

static void PresentFrame(HDC dc) {
  if (!dc || !s_window || !s_width || !s_height) return;
  if (s_bmi.bmiHeader.biSize) {
    StretchDIBits(dc, 0, 0, s_width * kScale, s_height * kScale,
                  0, 0, s_width, s_height, s_pixels, &s_bmi,
                  DIB_RGB_COLORS, SRCCOPY);
  }
  if (!s_panel_enabled) return;

  RECT panel = {s_width * kScale, 0,
                s_width * kScale + kPanelWidth, s_height * kScale};
  HBRUSH background = CreateSolidBrush(RGB(18, 21, 25));
  FillRect(dc, &panel, background);
  DeleteObject(background);
  SetBkMode(dc, TRANSPARENT);
  SetTextColor(dc, RGB(222, 230, 238));
  HFONT font = (HFONT)GetStockObject(ANSI_FIXED_FONT);
  HFONT old_font = (HFONT)SelectObject(dc, font);

  char script[256] = "manual keyboard input";
  if (s_script_loaded) Dkc1ScriptStatus(script, sizeof script);
  else if (s_input_playback.count)
    snprintf(script, sizeof script, "input playback: %zu frames",
             s_input_playback.count);
  char text[2048];
  snprintf(text, sizeof text,
           "VISIBLE WIDESCREEN DEBUGGER\r\n"
           "\r\n"
           "Host frame: %ld\r\n"
           "State: %s%s%s\r\n"
           "Input: $%03X\r\n"
           "Route: %s\r\n"
           "Status: %s\r\n"
           "\r\n"
           "Mode / level / entrance\r\n"
           "$%04X / $%04X / $%04X\r\n"
           "Layer scroll X/Y: $%04X / $%04X\r\n"
           "Camera bounds: $%04X .. $%04X\r\n"
           "Scanner: $%04X  range $%04X..$%04X\r\n"
           "Section: $%04X\r\n"
           "\r\n"
           "Evidence taps\r\n"
           "WS trace: %s\r\n"
           "OAM: %s   lifecycle: %s\r\n"
           "WRAM dump: %s   input record: %s\r\n"
           "Flight recorder: %s\r\n"
           "\r\n"
           "F1 provenance   F2 composite\r\n"
           "F3 BG1  F4 BG2  F5 BG3  F6 OBJ\r\n"
           "F7 pause/resume   F8 single-step\r\n"
           "F9 export rolling repro bundle\r\n"
           "Esc quit\r\n"
           "\r\n"
           "The side panel is host-only and is not\r\n"
           "included in framebuffer evidence.",
           s_host_frame,
           s_paused ? "PAUSED" : "running",
           s_route_finished ? " / ROUTE COMPLETE" : "",
           s_script_failed ? " / FAILED" : "", s_last_input,
           script, s_host_status,
           ReadWram16(0x0032), ReadWram16(0x0030), ReadWram16(0x003e),
           ReadWram16(0x088b), ReadWram16(0x0895),
           ReadWram16(0x1b23), ReadWram16(0x1b25),
           ReadWram16(0x1e03), ReadWram16(0x1e07), ReadWram16(0x1e09),
           ReadWram16(0x05c1),
           EnvironmentEnabled("DKC1_WS_TRACE") ? "ON" : "off",
           EnvironmentEnabled("DKC1_OAM_LOG") ? "ON" : "off",
           EnvironmentEnabled("DKC1_LIFECYCLE_TRACE") ? "ON" : "off",
           EnvironmentEnabled("DKC1_WRAM_DUMP") ? "ON" : "off",
           EnvironmentEnabled("DKC1_INPUT_RECORD") ? "ON" : "off",
           Dkc1FlightRecorderEnabled() ? "ARMED (60 seconds)" : "off");
  RECT text_rect = panel;
  text_rect.left += 12;
  text_rect.top += 12;
  text_rect.right -= 10;
  DrawTextA(dc, text, -1, &text_rect,
            DT_LEFT | DT_TOP | DT_NOPREFIX | DT_WORDBREAK);
  SelectObject(dc, old_font);
}

static HWAVEOUT s_waveout;
static WAVEHDR s_wave_headers[kAudioBuffers];
static int16_t s_wave_data[kAudioBuffers][kAudioFramesPerBuffer * 2];
static int s_wave_index;
static double s_audio_accumulator;

static LRESULT CALLBACK WindowProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
  switch (msg) {
    case WM_CLOSE:
    case WM_DESTROY:
      s_running = 0;
      PostQuitMessage(0);
      return 0;
    case WM_KEYDOWN:
      if (lp & (1u << 30)) return 0;  /* ignore key-repeat toggles */
      if (wp == VK_ESCAPE) {
        s_running = 0;
        PostQuitMessage(0);
      } else if (wp == VK_F1) {
        Dkc1DebugSetProvenanceOverlay(!Dkc1DebugProvenanceOverlay());
        UpdateDebugTitle();
      } else if (wp == VK_F2) {
        Dkc1DebugSetLayerMask(0xff);
        UpdateDebugTitle();
      } else if (wp >= VK_F3 && wp <= VK_F6) {
        static const uint8_t masks[] = {0x01, 0x02, 0x04, 0x10};
        Dkc1DebugSetLayerMask(masks[wp - VK_F3]);
        UpdateDebugTitle();
      } else if (wp == VK_F7) {
        s_paused = !s_paused;
        s_step_once = 0;
        snprintf(s_host_status, sizeof s_host_status,
                 "%s by user", s_paused ? "paused" : "resumed");
        UpdateDebugTitle();
      } else if (wp == VK_F8 && s_paused) {
        s_step_once = 1;
        snprintf(s_host_status, sizeof s_host_status,
                 "single frame requested");
      } else if (wp == VK_F9) {
        s_export_requested = 1;
        snprintf(s_host_status, sizeof s_host_status,
                 "repro bundle export requested");
      }
      InvalidateRect(hwnd, NULL, FALSE);
      return 0;
    case WM_PAINT: {
      PAINTSTRUCT ps;
      HDC dc = BeginPaint(hwnd, &ps);
      PresentFrame(dc);
      EndPaint(hwnd, &ps);
      return 0;
    }
  }
  return DefWindowProc(hwnd, msg, wp, lp);
}

static uint32_t PollInput(void) {
  if (GetForegroundWindow() != s_window)
    return 0;
  uint32_t inputs = 0;
  if (GetAsyncKeyState('Z') & 0x8000) inputs |= 0x001;      /* B */
  if (GetAsyncKeyState('X') & 0x8000) inputs |= 0x002;      /* Y */
  if (GetAsyncKeyState(VK_RSHIFT) & 0x8000) inputs |= 0x004;/* Select */
  if (GetAsyncKeyState(VK_RETURN) & 0x8000) inputs |= 0x008;/* Start */
  if (GetAsyncKeyState(VK_UP) & 0x8000) inputs |= 0x010;
  if (GetAsyncKeyState(VK_DOWN) & 0x8000) inputs |= 0x020;
  if (GetAsyncKeyState(VK_LEFT) & 0x8000) inputs |= 0x040;
  if (GetAsyncKeyState(VK_RIGHT) & 0x8000) inputs |= 0x080;
  if (GetAsyncKeyState('S') & 0x8000) inputs |= 0x100;      /* A */
  if (GetAsyncKeyState('A') & 0x8000) inputs |= 0x200;      /* X */
  if (GetAsyncKeyState('Q') & 0x8000) inputs |= 0x400;      /* L */
  if (GetAsyncKeyState('W') & 0x8000) inputs |= 0x800;      /* R */
  return inputs;
}

static void AudioInit(void) {
  WAVEFORMATEX format;
  memset(&format, 0, sizeof format);
  format.wFormatTag = WAVE_FORMAT_PCM;
  format.nChannels = 2;
  format.nSamplesPerSec = 32040;
  format.wBitsPerSample = 16;
  format.nBlockAlign = 4;
  format.nAvgBytesPerSec = 32040 * 4;
  if (waveOutOpen(&s_waveout, WAVE_MAPPER, &format, 0, 0, CALLBACK_NULL)
      != MMSYSERR_NOERROR) {
    s_waveout = NULL;
    return;
  }
  for (int i = 0; i < kAudioBuffers; i++) {
    s_wave_headers[i].lpData = (LPSTR)s_wave_data[i];
    s_wave_headers[i].dwBufferLength = sizeof s_wave_data[i];
    waveOutPrepareHeader(s_waveout, &s_wave_headers[i],
                         sizeof s_wave_headers[i]);
    s_wave_headers[i].dwFlags |= WHDR_DONE;
  }
}

static void AudioPump(void) {
  if (!s_waveout) return;
  s_audio_accumulator += 32040.0 / 60.098811862;
  int frames = (int)s_audio_accumulator;
  s_audio_accumulator -= frames;
  if (frames <= 0) return;
  if (frames > kAudioFramesPerBuffer) frames = kAudioFramesPerBuffer;
  WAVEHDR *header = &s_wave_headers[s_wave_index];
  if (!(header->dwFlags & WHDR_DONE))
    return;  /* device is behind; drop this frame's audio */
  int16_t *samples = (int16_t *)header->lpData;
  memset(samples, 0, (size_t)frames * 4);
  RtlRenderAudio(samples, frames, 2);
  header->dwBufferLength = (DWORD)frames * 4;
  waveOutWrite(s_waveout, header, sizeof *header);
  s_wave_index = (s_wave_index + 1) % kAudioBuffers;
}

int main(int argc, char **argv) {
  const char *rom_path = argc > 1 ? argv[1] : "dkc1.sfc";
  size_t rom_size = 0;
  char rom_error[160];
  uint8_t *rom =
      Dkc1ReadVerifiedRom(rom_path, &rom_size, rom_error, sizeof rom_error);
  if (!rom) {
    char message[320];
    snprintf(message, sizeof message,
             "usage: dkc1_desktop <rom.sfc>\n\n%s: %s", rom_error, rom_path);
    MessageBoxA(NULL, message, "DKC1Recomp", MB_ICONERROR);
    return 2;
  }

  {
    const char *widescreen_text = getenv("DKC1_WIDESCREEN");
    Dkc1VideoSetWidescreen(
        !(widescreen_text && *widescreen_text == '0'));  /* default on */
  }
  Dkc1VideoSetRom(rom, rom_size);
  RtlRegisterGame(Dkc1GameInfo());
  if (!SnesInit(rom, (int)rom_size)) {
    MessageBoxA(NULL, "snesrecomp rejected the verified ROM", "DKC1Recomp",
                MB_ICONERROR);
    return 4;
  }

  {
    const char *panel_text = getenv("DKC1_DESKTOP_DEBUG_PANEL");
    s_panel_enabled = !(panel_text && *panel_text == '0');
  }
  {
    const char *snapshot = getenv("DKC1_SAVESTATE_INPUT");
    if (snapshot && *snapshot && !RtlLoadSnapshot(snapshot)) {
      char message[1024];
      snprintf(message, sizeof message, "Unable to load native snapshot:\n%s",
               snapshot);
      MessageBoxA(NULL, message, "DKC1Recomp", MB_ICONERROR);
      free(rom);
      return 20;
    }
  }
  {
    const char *playback_path = getenv("SNESRECOMP_INPUT_PLAY");
    if (playback_path && *playback_path) {
      char error[256];
      if (!Dkc1InputPlaybackLoad(playback_path, &s_input_playback,
                                 error, sizeof error)) {
        char message[768];
        snprintf(message, sizeof message, "Input playback failed:\n%s\n%s",
                 playback_path, error);
        MessageBoxA(NULL, message, "DKC1Recomp", MB_ICONERROR);
        free(rom);
        return 20;
      }
      snprintf(s_host_status, sizeof s_host_status,
               "loaded input playback: %s", playback_path);
    }
  }
  {
    const char *script_path = getenv("DKC1_SCRIPT");
    if (script_path && *script_path) {
      char error[256];
      if (s_input_playback.count) {
        MessageBoxA(NULL,
                    "DKC1_SCRIPT and SNESRECOMP_INPUT_PLAY are mutually exclusive",
                    "DKC1Recomp", MB_ICONERROR);
        Dkc1InputPlaybackFree(&s_input_playback);
        free(rom);
        return 20;
      }
      if (!Dkc1ScriptLoad(script_path, error, sizeof error)) {
        char message[768];
        snprintf(message, sizeof message, "Route script failed:\n%s\n%s",
                 script_path, error);
        MessageBoxA(NULL, message, "DKC1Recomp", MB_ICONERROR);
        free(rom);
        return 20;
      }
      s_script_loaded = 1;
      snprintf(s_host_status, sizeof s_host_status,
               "loaded route: %s", script_path);
    }
  }
  {
    char error[256];
    int opened = Dkc1WramDumpOpenFromEnvironment(&s_wram_dump,
                                                  error, sizeof error);
    if (opened < 0) {
      char message[512];
      snprintf(message, sizeof message, "WRAM dump setup failed:\n%s", error);
      MessageBoxA(NULL, message, "DKC1Recomp", MB_ICONERROR);
      Dkc1ScriptFree();
      Dkc1InputPlaybackFree(&s_input_playback);
      free(rom);
      return 20;
    }
  }
  {
    char error[256];
    int armed = Dkc1FlightRecorderInitialize(error, sizeof error);
    if (armed < 0) {
      char message[512];
      snprintf(message, sizeof message,
               "Flight recorder setup failed:\n%s", error);
      MessageBoxA(NULL, message, "DKC1Recomp", MB_ICONERROR);
      (void)Dkc1WramDumpClose(&s_wram_dump, NULL, 0);
      Dkc1ScriptFree();
      Dkc1InputPlaybackFree(&s_input_playback);
      free(rom);
      return 20;
    }
  }

  s_width = Dkc1VideoWidth();
  s_height = kDkc1VideoHeight;
  Dkc1BeginDrawing(s_pixels, (size_t)s_width * 4);

  WNDCLASSA wc;
  memset(&wc, 0, sizeof wc);
  wc.lpfnWndProc = WindowProc;
  wc.hInstance = GetModuleHandle(NULL);
  wc.hCursor = LoadCursor(NULL, IDC_ARROW);
  wc.lpszClassName = "DKC1RecompWindow";
  RegisterClassA(&wc);

  RECT rect = { 0, 0,
                s_width * kScale + (s_panel_enabled ? kPanelWidth : 0),
                s_height * kScale };
  AdjustWindowRect(&rect, WS_OVERLAPPEDWINDOW & ~WS_THICKFRAME, FALSE);
  s_window = CreateWindowA(
      wc.lpszClassName,
      "DKC1Recomp — Z=B  X=Y  S=A  A=X  Q/W=L/R  Enter=Start  Esc=quit",
      (WS_OVERLAPPEDWINDOW & ~WS_THICKFRAME & ~WS_MAXIMIZEBOX) | WS_VISIBLE,
      CW_USEDEFAULT, CW_USEDEFAULT,
      rect.right - rect.left, rect.bottom - rect.top,
      NULL, NULL, wc.hInstance, NULL);
  UpdateDebugTitle();

  memset(&s_bmi, 0, sizeof s_bmi);
  s_bmi.bmiHeader.biSize = sizeof s_bmi.bmiHeader;
  s_bmi.bmiHeader.biWidth = s_width;
  s_bmi.bmiHeader.biHeight = -s_height;  /* top-down */
  s_bmi.bmiHeader.biPlanes = 1;
  s_bmi.bmiHeader.biBitCount = 32;
  s_bmi.bmiHeader.biCompression = BI_RGB;

  AudioInit();

  LARGE_INTEGER freq, next, now;
  QueryPerformanceFrequency(&freq);
  QueryPerformanceCounter(&next);
  const double ticks_per_frame = (double)freq.QuadPart / 60.098811862;
  double next_tick = (double)next.QuadPart;

  while (s_running) {
    MSG msg;
    while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
      TranslateMessage(&msg);
      DispatchMessage(&msg);
    }
    if (!s_running) break;

    if (s_export_requested) {
      char bundle[1024], error[256];
      s_export_requested = 0;
      if (Dkc1FlightRecorderExport(s_host_frame, bundle, sizeof bundle,
                                   error, sizeof error))
        snprintf(s_host_status, sizeof s_host_status,
                 "repro exported: %.470s", bundle);
      else
        snprintf(s_host_status, sizeof s_host_status,
                 "repro export failed: %.460s", error);
      UpdateDebugTitle();
    }

    if (s_paused && !s_step_once) {
      HDC dc = GetDC(s_window);
      PresentFrame(dc);
      ReleaseDC(s_window, dc);
      Sleep(16);
      continue;
    }

    Dkc1ScriptOps script_ops = {0};
    uint32_t input = 0;
    int run_frame = 1;
    if (s_script_loaded) {
      if (Dkc1ScriptFinished()) {
        s_route_finished = 1;
        s_paused = 1;
        s_step_once = 0;
        snprintf(s_host_status, sizeof s_host_status,
                 "route complete; paused for inspection");
        UpdateDebugTitle();
        continue;
      }
      bool failed = false;
      input = Dkc1ScriptNextInput(g_ram, &script_ops, &failed);
      if (failed) {
        s_script_failed = 1;
        s_paused = 1;
        s_step_once = 0;
        snprintf(s_host_status, sizeof s_host_status, "%s",
                 Dkc1ScriptError());
        UpdateDebugTitle();
        continue;
      }
      if (script_ops.state_load && !RtlLoadSnapshot(script_ops.state_load)) {
        s_script_failed = 1;
        s_paused = 1;
        snprintf(s_host_status, sizeof s_host_status,
                 "unable to load snapshot: %.430s", script_ops.state_load);
        UpdateDebugTitle();
        continue;
      }
      if (script_ops.checkpoint &&
          !Dkc1DebugCheckpoint(script_ops.checkpoint, (int)s_host_frame)) {
        s_script_failed = 1;
        s_paused = 1;
        snprintf(s_host_status, sizeof s_host_status,
                 "unable to record checkpoint: %.400s", script_ops.checkpoint);
        UpdateDebugTitle();
        continue;
      }
      if (script_ops.state_save && !RtlSaveSnapshot(script_ops.state_save)) {
        s_script_failed = 1;
        s_paused = 1;
        snprintf(s_host_status, sizeof s_host_status,
                 "unable to save snapshot: %.430s", script_ops.state_save);
        UpdateDebugTitle();
        continue;
      }
      run_frame = script_ops.run_frame ? 1 : 0;
    } else if (s_input_playback.count) {
      input = Dkc1InputPlaybackFrame(&s_input_playback,
                                      (size_t)s_host_frame);
    } else {
      input = PollInput();
    }

    if (!run_frame) {
      UpdateDebugTitle();
      continue;
    }

    s_last_input = input;
    Dkc1DebugRecordInput(input);
    RtlRunFrame(input);
    if (g_fail) {
      MessageBoxA(s_window, "runtime failure (off-rails execution)",
                  "DKC1Recomp", MB_ICONERROR);
      break;
    }
    if (!Dkc1LastLleResult()) {
      char message[128];
      snprintf(message, sizeof message,
               "execution stopped at $%06x", (unsigned)Dkc1ResumePc());
      MessageBoxA(s_window, message, "DKC1Recomp", MB_ICONERROR);
      break;
    }
    Dkc1DrawPpuFrame();
    s_host_frame++;
    {
      char error[256];
      if (!Dkc1WramDumpFrame(&s_wram_dump, s_host_frame,
                             snes_frame_counter, g_ram,
                             error, sizeof error)) {
        s_paused = 1;
        snprintf(s_host_status, sizeof s_host_status,
                 "WRAM dump failed: %.430s", error);
      }
    }
    Dkc1DebugDumpFrame((int)s_host_frame);
    Dkc1FlightRecorderRecord(s_host_frame, input);
    AudioPump();

    HDC dc = GetDC(s_window);
    PresentFrame(dc);
    ReleaseDC(s_window, dc);
    if ((s_host_frame % 15) == 0) UpdateDebugTitle();
    s_step_once = 0;

    next_tick += ticks_per_frame;
    for (;;) {
      QueryPerformanceCounter(&now);
      double remaining = next_tick - (double)now.QuadPart;
      if (remaining <= 0) break;
      double ms = remaining * 1000.0 / (double)freq.QuadPart;
      if (ms > 2.0)
        Sleep((DWORD)(ms - 1.0));
      else
        Sleep(0);
    }
    QueryPerformanceCounter(&now);
    if ((double)now.QuadPart - next_tick > ticks_per_frame * 8)
      next_tick = (double)now.QuadPart;  /* fell far behind; resync */
  }

  if (s_waveout) {
    waveOutReset(s_waveout);
    waveOutClose(s_waveout);
  }
  {
    char error[256];
    if (!Dkc1WramDumpClose(&s_wram_dump, error, sizeof error))
      fprintf(stderr, "wram_dump: %s\n", error);
  }
  Dkc1DebugDumpClose();
  Dkc1FlightRecorderClose();
  Dkc1ScriptFree();
  Dkc1InputPlaybackFree(&s_input_playback);
  free(rom);
  return 0;
}
