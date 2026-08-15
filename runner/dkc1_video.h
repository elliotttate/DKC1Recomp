#ifndef DKC1_VIDEO_H
#define DKC1_VIDEO_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum {
  kDkc1VideoNativeWidth = 256,
  kDkc1VideoHeight = 224,
  /* SNES pixels present at a 7:6 pixel aspect. 342 source columns at 224
   * lines give 1.78125, within one pixel of exact 16:9 (same policy as
   * DKC2Recomp). Widescreen stays disabled until the DKC1 terrain
   * reconstruction is audited; the buffer is sized for it up front. */
  kDkc1VideoWidescreenExtra = 43,
  kDkc1VideoWidescreenWidth =
      kDkc1VideoNativeWidth + 2 * kDkc1VideoWidescreenExtra,
  kDkc1VideoBytesPerPixel = 4,
};

/* These symbols are the shared snesrecomp widescreen runtime contract. */
extern bool g_ws_active;
extern int g_ws_extra;

void Dkc1VideoSetWidescreen(bool enabled);
bool Dkc1VideoIsWidescreen(void);
void Dkc1VideoSetTerrainReady(bool ready);
bool Dkc1VideoTerrainReady(void);
int Dkc1VideoWidth(void);
int Dkc1VideoExtra(void);
size_t Dkc1VideoPixelCount(void);

#endif
