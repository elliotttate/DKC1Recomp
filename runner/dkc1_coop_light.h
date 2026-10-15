#ifndef DKC1_COOP_LIGHT_H
#define DKC1_COOP_LIGHT_H

#include <stdint.h>

typedef struct Dkc1CoopLightPlan {
  uint16_t screens, table[3], scroll_y, fixed_color;
  uint8_t visible, headers, scroll_header, color_header;
} Dkc1CoopLightPlan;

/* Private PPU equivalent of $80B1BA/$80B3D7. Shape and color tables are
 * still produced by the cartridge's $80B788/$80B891, without a gameplay tick. */
static Dkc1CoopLightPlan Dkc1CoopPlanLight(uint16_t x, uint16_t y,
    uint16_t light_x, uint16_t light_y, uint16_t clock, uint16_t flags) {
  Dkc1CoopLightPlan p = {0};
  p.screens = 0x1300;
  if ((uint16_t)(light_x - x + 0x40u) >= 0x180u) return p;
  p.visible = 1;
  p.screens = flags & 0x200u ? 0x1304 : 0x0013;
  unsigned phase = clock & 1u;
  uint16_t distance = (uint16_t)(0x1f8u - light_y - y);
  p.table[0] = phase ? 0x500 : 0x200;
  p.table[1] = 0x2000; p.table[2] = 0x2402;
  if (distance & 0x8000u) {
    unsigned clipped = (uint16_t)(0u - distance);
    if (clipped > 0xc0u) clipped = 0xc0u;
    p.table[0] = (uint16_t)(p.table[0] + clipped * 3u);
    p.table[1] = (uint16_t)(p.table[1] + (clipped + phase * 2u) * 5u);
    p.table[2] = (uint16_t)(p.table[1] + 0x402u);
  } else {
    p.headers = 1;
    p.scroll_header = (uint8_t)(distance + 1u);
    p.color_header = (uint8_t)(distance + 1u + phase * 2u);
  }
  p.scroll_y = (uint16_t)(y + light_y + 0x14u);
  return p;
}

/* $80B38B keeps terrain on main and adds the beam on sub. Its fixed
 * backdrop color is independent of lamp visibility and camera position. */
static Dkc1CoopLightPlan Dkc1CoopPlanLoopyLight(uint16_t x, uint16_t y,
    uint16_t light_x, uint16_t light_y, uint16_t clock, uint16_t flags) {
  Dkc1CoopLightPlan p = Dkc1CoopPlanLight(x, y, light_x, light_y, clock, flags);
  p.screens = p.visible && (flags & 0x200u) ? 0x0413 : 0x0013;
  p.fixed_color = (uint16_t)((flags & 0x200u ? 3u : 9u) * 0x421u);
  return p;
}

/* $80B206 shares the shape projection but keeps BG3 on main in both switch
 * states and uses the alternate color tables for STOP. */
static Dkc1CoopLightPlan Dkc1CoopPlanStopGoLight(uint16_t x, uint16_t y,
    uint16_t light_x, uint16_t light_y, uint16_t clock, uint16_t flags) {
  Dkc1CoopLightPlan p = Dkc1CoopPlanLight(x, y, light_x, light_y, clock, flags);
  if (p.visible) {
    p.screens = 0x1304;
    if (flags & 0x200u) { p.table[1] += 0x808u; p.table[2] += 0x808u; }
  }
  return p;
}

#endif
