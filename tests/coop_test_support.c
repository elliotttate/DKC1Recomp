#include "coop_test_support.h"
#include "dkc1_coop_entities.h"
#include "dkc1_coop_world.h"
#include <assert.h>
#include <stdbool.h>
#include <string.h>
const uint8_t *g_rom;
static uint16_t extended[0x2000][64];

static int second_available;
static uint16_t second_x, second_y;
static unsigned camera_calls;

void Dkc1CoopWorldReset(void) {}

int Dkc1CoopEntityIsHandle(uint16_t handle) {
  return ((handle >= 2 && handle <= 0x32) ||
          (handle >= 0x80 && handle <= 0xfe)) && !(handle & 1);
}

uint16_t Dkc1CoopEntityNextHandle(uint16_t handle) {
  return handle == 0x32 ? 0x80 : (uint16_t)(handle + 2);
}

uint16_t Dkc1CoopEntityLastHandle(void) { return 0xfe; }

uint16_t Dkc1CoopEntityRead16(const uint8_t *wram, uint16_t base,
                            uint16_t handle) {
  if (handle >= 0x80) {
    assert(base < 0x2000 && handle <= 0xfe);
    return extended[base][(handle-0x80)/2];
  }
  uint16_t address = (uint16_t)(base + handle);
  return (uint16_t)(wram[address] |
                    ((uint16_t)wram[(uint16_t)(address + 1)] << 8));
}

void Dkc1CoopEntityWrite16(uint8_t *wram, uint16_t base, uint16_t handle,
                          uint16_t value) {
  if (handle >= 0x80) {
    assert(base < 0x2000 && handle <= 0xfe);
    extended[base][(handle-0x80)/2] = value;
    return;
  }
  uint16_t address = (uint16_t)(base + handle);
  wram[address] = (uint8_t)value;
  wram[(uint16_t)(address + 1)] = (uint8_t)(value >> 8);
}

void CoopTestResetSupport(void) {
  memset(extended,0,sizeof extended);
  second_available = 0;
  second_x = second_y = 0;
  camera_calls = 0;
}

void Dkc1CoopGraphicsResetEntity(uint16_t handle) { (void)handle; }

void CoopTestSetSecondCamera(int available, uint16_t x, uint16_t y) {
  second_available = available;
  second_x = x;
  second_y = y;
}

unsigned CoopTestCameraCalls(void) { return camera_calls; }

int Dkc1CoopWorldGetCamera(const uint8_t *wram, int view,
                          uint16_t *x, uint16_t *y) {
  if (!wram || !x || !y || view < 0 || view > 1) return 0;
  *x = Dkc1CoopEntityRead16(wram, 0x088b, 0);
  *y = Dkc1CoopEntityRead16(wram, 0x0895, 0);
  if (!view) return 1;
  ++camera_calls;
  if (!second_available) return 0;
  *x = second_x;
  *y = second_y;
  return 1;
}

bool RtlSpeculativeExecutionActive(void) { return false; }

void Dkc1CoopWorldNoteKongCamera(const uint8_t *ram,uint16_t slot) { (void)ram;(void)slot; }
