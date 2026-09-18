/* Windows presentation, pacing and frame generation for the SDL host.
 *
 * Direct3D 11 flip-model swap chain (frame-latency waitable object, one
 * frame of latency) running the project's Metal graphics passes translated
 * to HLSL, the compositor-locked pacer (waitable / DwmFlush / timer modes)
 * and the 60/120 Hz frame-generation path, all carried over from the native
 * Win32 host. OpenGL 3.3 (windows_graphics.c) remains the fallback when
 * Direct3D is unavailable or DKC1_PRESENTER=opengl is set.
 *
 * Only completed immutable host pixels are read; no guest state is touched. */
#ifndef DKC1_WINDOWS_PRESENT_H
#define DKC1_WINDOWS_PRESENT_H
#include <SDL.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "desktop_graphics.h"

/* Per-frame host measurements folded into the pacing log. */
typedef struct Dkc1WinFrameTiming {
  double events_ms, setup_ms, emulation_ms, render_ms, diagnostics_ms;
  double audio_ms, interp_ms;
  int audio_queued_frames;
  unsigned long audio_starvations, audio_drops, audio_ring_frames;
  unsigned long long audio_internal_underflows;
} Dkc1WinFrameTiming;

/* Presenter. */
bool Dkc1WinPresentInit(SDL_Window *window, char *error, size_t error_size);
void Dkc1WinPresentClose(void);
const char *Dkc1WinPresenterName(void);   /* "d3d11" or "opengl" */
bool Dkc1WinPresenterIsD3D(void);

/* Pacer. Start after the presenter so the mode can follow the swap chain. */
void Dkc1WinPacerStart(void);
bool Dkc1WinPacerWorkFirst(void);
void Dkc1WinPacerMarkWorkStart(void);
void Dkc1WinPacerWaitFrame(bool work_first);
void Dkc1WinPacerReset(void);
void Dkc1WinPacerSetMinimized(bool minimized);
double Dkc1WinPacerRefreshHz(void);
const char *Dkc1WinPacerModeName(void);
int Dkc1WinPacerDivisor(void);

/* Frame generation (host-only presentation smoothing). */
void Dkc1WinFrameGenSetEnabled(bool enabled);
bool Dkc1WinFrameGenEnabled(void);
bool Dkc1WinFrameGenSupported(void);    /* display refresh allows 120 Hz pairs */
bool Dkc1WinFrameGenExtraRefresh(void);
void Dkc1WinFrameGenInvalidate(void);
/* Run once per emulated frame after Dkc1DrawPpuFrame. */
void Dkc1WinFrameGenProcess(const uint8_t *raw, int width, int height,
                            bool allowed);
const uint8_t *Dkc1WinFrameGenDisplay(const uint8_t *raw);
const uint8_t *Dkc1WinFrameGenMid(void);   /* NULL without a midpoint */

/* Present `display` (and schedule `mid`, when non-NULL, for the following
 * refresh). `paced` frames wait for their slot and are logged; unpaced
 * presents (menus, pause) show immediately. */
void Dkc1WinPresentFrame(const uint8_t *display, const uint8_t *mid,
                         int width, int height, int presentation_width,
                         const Dkc1GraphicsSettings *settings, bool paced,
                         long host_frame, const Dkc1WinFrameTiming *timing);

/* Synthetic Direct3D test for --graphics-test; no ROM or game art. */
int Dkc1WinPresentTest(void);

#endif
