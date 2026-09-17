#ifndef DKC1_SHADOW_WINDOW_H
#define DKC1_SHADOW_WINDOW_H
#include <stdbool.h>
#include <stdint.h>

/* Match the cache's exclusive upper bounds, with unsigned world coordinates
 * and wide sums. Presentation and cartridge capture must both fit. */
static inline bool Dkc1ShadowWindowContains(
    uint32_t ox, uint32_t oy, uint32_t wx, uint32_t wy,
    uint32_t cx, uint32_t cy, unsigned extra,
    uint32_t capacity_x, uint32_t capacity_y) {
  if (wx < ox || wy < oy || cx < ox || cy < oy) return false;
  return (uint64_t)(wx - ox) + 256u + extra + 8u < capacity_x &&
         (uint64_t)(wy - oy) + 224u + 8u < capacity_y &&
         (uint64_t)(cx - ox) + 256u + 8u < capacity_x &&
         (uint64_t)(cy - oy) + 224u + 8u < capacity_y;
}
#endif
