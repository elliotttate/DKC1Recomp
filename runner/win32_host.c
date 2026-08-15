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
#include "dkc1_video.h"
#include "verified_rom.h"

#include "common_cpu_infra.h"
#include "common_rtl.h"
#include "snes/snes.h"

#include <windows.h>
#include <mmsystem.h>
#include <stdio.h>

enum {
  kScale = 2,
  kAudioBuffers = 8,
  kAudioFramesPerBuffer = 536,
};

static uint8_t s_pixels[kDkc1VideoWidescreenWidth * kDkc1VideoHeight * 4];
static BITMAPINFO s_bmi;
static HWND s_window;
static int s_running = 1;

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
      if (wp == VK_ESCAPE) {
        s_running = 0;
        PostQuitMessage(0);
      }
      return 0;
    case WM_PAINT: {
      PAINTSTRUCT ps;
      BeginPaint(hwnd, &ps);
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

  const int width = Dkc1VideoWidth();
  const int height = kDkc1VideoHeight;
  Dkc1BeginDrawing(s_pixels, (size_t)width * 4);

  WNDCLASSA wc;
  memset(&wc, 0, sizeof wc);
  wc.lpfnWndProc = WindowProc;
  wc.hInstance = GetModuleHandle(NULL);
  wc.hCursor = LoadCursor(NULL, IDC_ARROW);
  wc.lpszClassName = "DKC1RecompWindow";
  RegisterClassA(&wc);

  RECT rect = { 0, 0, width * kScale, height * kScale };
  AdjustWindowRect(&rect, WS_OVERLAPPEDWINDOW & ~WS_THICKFRAME, FALSE);
  s_window = CreateWindowA(
      wc.lpszClassName,
      "DKC1Recomp — Z=B  X=Y  S=A  A=X  Q/W=L/R  Enter=Start  Esc=quit",
      (WS_OVERLAPPEDWINDOW & ~WS_THICKFRAME & ~WS_MAXIMIZEBOX) | WS_VISIBLE,
      CW_USEDEFAULT, CW_USEDEFAULT,
      rect.right - rect.left, rect.bottom - rect.top,
      NULL, NULL, wc.hInstance, NULL);

  memset(&s_bmi, 0, sizeof s_bmi);
  s_bmi.bmiHeader.biSize = sizeof s_bmi.bmiHeader;
  s_bmi.bmiHeader.biWidth = width;
  s_bmi.bmiHeader.biHeight = -height;  /* top-down */
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

    RtlRunFrame(PollInput());
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
    AudioPump();

    HDC dc = GetDC(s_window);
    StretchDIBits(dc, 0, 0, width * kScale, height * kScale,
                  0, 0, width, height, s_pixels, &s_bmi,
                  DIB_RGB_COLORS, SRCCOPY);
    ReleaseDC(s_window, dc);

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
  free(rom);
  return 0;
}
