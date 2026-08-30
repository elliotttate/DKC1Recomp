#ifndef DKC1_MACOS_METAL_PRESENTER_H
#define DKC1_MACOS_METAL_PRESENTER_H

#include "macos_file_picker.h"

#include <stddef.h>
#include <stdint.h>

typedef struct Dkc1MacPresentationFrameInfo {
  int64_t host_frame;
  uint16_t camera_x;
  uint16_t camera_y;
  uint16_t bg_hscroll[4];
  uint16_t bg_vscroll[4];
} Dkc1MacPresentationFrameInfo;

/* Attaches a CAMetalLayer-backed overlay to the SDL-created NSWindow. The
 * presenter owns only host pixels and immutable per-frame metadata; it never
 * reads or mutates cartridge state from its display-link thread. */
int Dkc1MacMetalPresenterStart(void *native_window, double preferred_hz,
                               Dkc1MacFullscreenScaling scaling,
                               int fullscreen);
void Dkc1MacMetalPresenterQueueFrame(
    const uint32_t *pixels, int width, int height, int presentation_width,
    const Dkc1MacPresentationFrameInfo *info);
void Dkc1MacMetalPresenterSetGeometry(int presentation_width, int fullscreen);
void Dkc1MacMetalPresenterSetScaling(Dkc1MacFullscreenScaling scaling);
void Dkc1MacMetalPresenterSetActive(int active);
void Dkc1MacMetalPresenterStop(void);

#endif
