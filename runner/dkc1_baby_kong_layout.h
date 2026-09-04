#ifndef DKC1_BABY_KONG_LAYOUT_H
#define DKC1_BABY_KONG_LAYOUT_H

#include <stdbool.h>
#include <stdint.h>

/* DKC3 sprite graphics are uploaded into a 16-tile-wide virtual VRAM sheet.
 * The source bytes can be split into two DMA groups, so a virtual tile is not
 * necessarily at the same linear offset in the ROM payload. */
static inline bool Dkc1BabyKongResolveTile(uint8_t dma1_count,
                                           uint8_t dma2_start,
                                           uint8_t dma2_count,
                                           unsigned virtual_tile,
                                           unsigned *source_tile) {
  if (virtual_tile < dma1_count) {
    *source_tile = virtual_tile;
    return true;
  }
  if (virtual_tile >= dma2_start &&
      virtual_tile - dma2_start < dma2_count) {
    *source_tile = (unsigned)dma1_count + virtual_tile - dma2_start;
    return true;
  }
  return false;
}

static inline unsigned Dkc1BabyKongLargeTile(unsigned piece,
                                              unsigned x,
                                              unsigned y) {
  const unsigned base = (piece / 8u) * 32u + (piece % 8u) * 2u;
  return base + y * 16u + x;
}

static inline bool Dkc1BabyKongOamXMatches(int dx) {
  return dx >= -72 && dx <= 72;
}

static inline int Dkc1BabyKongAnchorFromOpaqueBottom(int native_bottom,
                                                     int local_bottom) {
  return native_bottom - local_bottom;
}

#endif
