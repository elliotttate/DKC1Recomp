#ifndef DKC1_BABY_KONG_MOVEMENT_H
#define DKC1_BABY_KONG_MOVEMENT_H

#include <stdbool.h>
#include <stdint.h>

/* Compact, source-independent Kiddy-style tuning applied after DKC1's stock
 * player update. Button bits use the values written to DKC1's joypad WRAM:
 * SNES B=$8000 and Y=$4000. */
void Dkc1BabyKongTuneVelocity(uint16_t held, uint16_t pressed,
                              bool grounded, int16_t *x_velocity,
                              int16_t *y_velocity);

#endif
