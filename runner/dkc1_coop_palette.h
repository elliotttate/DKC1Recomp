#ifndef DKC1_COOP_PALETTE_H
#define DKC1_COOP_PALETTE_H
#include <stdint.h>
#include "dkc1_wram_gen.h"

/* Shared by native Kong CGRAM and private actor palettes. History belongs to
 * each caller's versioned semantic state, including speculative rollback. */
typedef struct Dkc1CoopWeatherState {
  uint16_t scene, peak, clearing, reserved;
} Dkc1CoopWeatherState;

static inline uint16_t Dkc1CoopBlend555(uint16_t from, uint16_t to,
                                     unsigned numer, unsigned denom) {
  uint16_t out = 0;
  if (!denom || numer >= denom) return to;
  for (unsigned shift = 0; shift <= 10; shift += 5) {
    unsigned a = (from >> shift) & 31u, b = (to >> shift) & 31u;
    unsigned value = b >= a ? a + (b - a) * numer / denom
                            : a - (a - b) * numer / denom;
    out = (uint16_t)(out | (value << shift));
  }
  return out;
}

static inline int Dkc1CoopWeatherObserve(const uint8_t *ram,
                                        Dkc1CoopWeatherState *state) {
  const uint16_t scene = Dkc1WramU16(ram, 0x3e);
  const unsigned wet = Dkc1WramU16(ram, 0x1e15) & 0x10;
  const unsigned goal = Dkc1WramU16(ram, 0x1dfd);
  const unsigned now = Dkc1WramU16(ram, 0x1dff);
  state->reserved = 0;
  if (state->scene != scene) {
    state->scene = scene; state->peak = 0; state->clearing = 0;
  }
  if (wet) {
    state->peak = 0; /* Legacy wire field; never a weather countdown. */
    state->clearing = 1;
  }
  /* The clear goal remains zero for the first frames after the rain bit
   * drops. A nonzero unfinished goal also reconstructs legacy cold loads. */
  int clear = !wet && ((state->clearing && (!goal || now < goal)) ||
                       (goal && now < goal));
  if (!wet && !clear) { state->clearing = 0; state->peak = 0; }
  return clear;
}

static inline int Dkc1CoopWeatherPalette(const uint8_t *ram, uint16_t source,
    const uint16_t *dry, Dkc1CoopWeatherState *state, uint16_t colors[15]) {
  int clear = Dkc1CoopWeatherObserve(ram, state);
  const unsigned wet = Dkc1WramU16(ram, 0x1e15) & 0x10;
  const unsigned goal = Dkc1WramU16(ram, 0x1dfd);
  const unsigned now = Dkc1WramU16(ram, 0x1dff);
  if (!wet && !dry) return 0;
  for (unsigned i = 0; i < 15; ++i) {
    uint16_t at = (uint16_t)(source + 0xa5adu + i * 2u);
    uint16_t weather = (uint16_t)(ram[0x10000u + at] |
        (uint16_t)ram[0x10000u + (uint16_t)(at + 1)] << 8);
    /* BD:F2A0 selects this bank-$7F row whenever bit $10 is set.
     * The cartridge has already applied lighting and palette transitions.
     * $7F:0204 is a color component (80:AA67), not a countdown; blending
     * this row a second time replaces level lighting with guessed colors. */
    if (wet) colors[i] = weather;
    else if (clear)
      colors[i] = goal && now ? Dkc1CoopBlend555(weather,dry[i],now,goal) : weather;
    else colors[i] = dry[i];
  }
  if (!wet && !clear) { state->clearing = 0; state->peak = 0; }
  return 1;
}
#endif
