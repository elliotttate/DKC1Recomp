#ifndef DKC1_COOP_FOG_H
#define DKC1_COOP_FOG_H

#include "dkc1_coop_scroll.h"

/* The cartridge advects 56 four-line records between two buffers. Each
 * camera needs that history: translating P1's current table also translates
 * the camera deltas accumulated by its earlier records. */
typedef struct Dkc1CoopFogState {
  uint32_t version;
  uint16_t entrance, tick, x, y, previous_x, previous_y;
  uint8_t valid, reserved[3];
  uint8_t tables[0x240];
} Dkc1CoopFogState;

static int Dkc1CoopFogStateValid(const Dkc1CoopFogState *state) {
  if (state->version > 1 || state->valid > 1) return 0;
  if (!state->valid) return 1;
  if (state->version != 1 || state->entrance >= 0xe6u) return 0;
  for (unsigned buffer = 0; buffer < 2; buffer++)
    for (unsigned row = 0; row < 56; row++)
      if (state->tables[buffer * 0x120u + row * 5u] != 4) return 0;
  return 1;
}

static uint16_t Dkc1CoopFogRead(const uint8_t *ram, unsigned a) {
  return (uint16_t)(ram[a] | ((uint16_t)ram[a + 1] << 8));
}
static void Dkc1CoopFogWrite(uint8_t *ram, unsigned a, uint16_t v) {
  ram[a] = (uint8_t)v; ram[a + 1] = (uint8_t)(v >> 8);
}
static uint16_t Dkc1CoopFogX(uint16_t x) { return (uint16_t)(x + (x >> 2)); }

static int Dkc1CoopFogPrepare(Dkc1CoopFogState *state, Dkc1CoopScrollBus *bus,
    const uint8_t *rom, const uint8_t *native, uint16_t entrance,
    uint16_t native_x, uint16_t native_y, uint16_t x, uint16_t y) {
  uint16_t tick = Dkc1CoopFogRead(native, 0x28);
  if (!state->valid || state->entrance != entrance ||
      (uint16_t)(tick - state->tick) > 1u) {
    memset(state, 0, sizeof *state);
    state->version = 1; state->valid = 1;
    state->entrance = entrance; state->tick = tick;
    state->x = x; state->y = y;
    state->previous_x = Dkc1CoopFogX(x); state->previous_y = y;
    memcpy(state->tables, native + 0x10200, sizeof state->tables);
    uint16_t dx = (uint16_t)(Dkc1CoopFogX(x) - Dkc1CoopFogX(native_x));
    uint16_t dy = (uint16_t)(y - native_y);
    for (unsigned buffer = 0; buffer < 2; buffer++) {
      for (unsigned row = 0; row < 56; row++) {
        unsigned a = buffer * 0x120u + row * 5u;
        Dkc1CoopFogWrite(state->tables, a + 1,
            (uint16_t)(Dkc1CoopFogRead(state->tables, a + 1) + dx));
        Dkc1CoopFogWrite(state->tables, a + 3,
            (uint16_t)(Dkc1CoopFogRead(state->tables, a + 3) + dy));
      }
    }
    return 1;
  }
  if (state->tick == tick) return state->x == x && state->y == y;
  /* B547..B748 is the scroll-only tail of native B4CA. Its palette/matrix
   * preparation remains owned by the real update. Run its ROM bytes on
   * the isolated bus with this camera's previous coordinates and buffers. */
  if (!Dkc1CoopFogRead(native, 0x1df1) &&
      (Dkc1CoopFogRead(native, 0x1df5) || Dkc1CoopFogRead(native, 0x1df7))) {
    memset(bus, 0, sizeof *bus); bus->rom = rom;
    memcpy(bus->ram, native, sizeof bus->ram);
    memcpy(bus->ram + 0x10200, state->tables, sizeof state->tables);
    Dkc1CoopFogWrite(bus->ram, 0x88b, x);
    Dkc1CoopFogWrite(bus->ram, 0x895, y);
    Dkc1CoopFogWrite(bus->ram, 0xa79, (uint16_t)(y - state->previous_y));
    Dkc1CoopFogWrite(bus->ram, 0xa7b, state->previous_x);
    if (!Dkc1CoopScrollRunAt(bus, 0x80b547u)) return 0;
    memcpy(state->tables, bus->ram + 0x10200, sizeof state->tables);
    state->previous_x = Dkc1CoopFogRead(bus->ram, 0xa7b);
    state->previous_y = y;
  }
  state->tick = tick; state->x = x; state->y = y;
  return 1;
}

#endif
