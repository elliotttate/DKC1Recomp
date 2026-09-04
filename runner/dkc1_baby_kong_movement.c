#include "dkc1_baby_kong_movement.h"

static int16_t ClampVelocity(int32_t value, int32_t limit) {
  if (value > limit) value = limit;
  if (value < -limit) value = -limit;
  return (int16_t)value;
}

void Dkc1BabyKongTuneVelocity(uint16_t held, uint16_t pressed,
                              bool grounded, int16_t *x_velocity,
                              int16_t *y_velocity) {
  if (!x_velocity || !y_velocity)
    return;

  if (grounded) {
    /* Kiddy carries more momentum through a held roll than DKC1 Donkey. */
    if ((held & 0x4000u) != 0 && *x_velocity != 0) {
      *x_velocity = ClampVelocity((int32_t)*x_velocity * 9 / 8, 0x0500);
    }
    return;
  }

  /* A newly pressed jump has a shorter initial rise, then the heavier body
   * accelerates downward slightly faster. DKC1 remains the collision oracle. */
  int32_t y = *y_velocity;
  if ((pressed & 0x8000u) != 0 && y < 0)
    y = y * 7 / 8;
  y += 0x18;
  *y_velocity = ClampVelocity(y, 0x0600);
}
