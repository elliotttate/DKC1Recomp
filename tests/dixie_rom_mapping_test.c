#include <assert.h>
#include <stdint.h>
#include <string.h>
#include "snes/cart.h"

/* Exercise the same HiROM resolver used by cart_getRomPtr and DMA reads. */
#define cart_getRomPtr cart_hirom_rom_ptr

static uint8_t image[0x600000];
int main(void) {
  Cart cart;
  memset(&cart, 0, sizeof cart);
  cart.type = CART_HIROM;
  cart.rom = image;
  cart.romSize = sizeof image;
  image[0x016000] = 0xff;
  image[0x416000] = 0x06;
  /* Even expanded files retain stock mapping unless explicitly selected. */
  assert(cart_getRomPtr(&cart, 0x41, 0x6000) == image + 0x016000);
  cart_set_hirom_linear_data_banks(&cart, true);
  assert(*cart_getRomPtr(&cart, 0x41, 0x6000) == 0x06);
  assert(cart_getRomPtr(&cart, 0x40, 0) == image + 0x400000);
  assert(cart_getRomPtr(&cart, 0x5f, 0xffff) == image + 0x5fffff);
  assert(cart_getRomPtr(&cart, 0x60, 0) == NULL);
  assert(cart_getRomPtr(&cart, 0x7d, 0xffff) == NULL);
  assert(cart_getRomPtr(&cart, 0x7e, 0) == NULL);
  assert(cart_getRomPtr(&cart, 0x7f, 0xffff) == NULL);
  /* Boot/code aliases and original full-bank DMA sources remain intact. */
  assert(cart_getRomPtr(&cart, 0x00, 0x8000) == image + 0x8000);
  assert(cart_getRomPtr(&cart, 0x80, 0x8000) == image + 0x8000);
  assert(cart_getRomPtr(&cart, 0xc0, 0) == image);
  assert(cart_getRomPtr(&cart, 0xc1, 0x6000) == image + 0x016000);
  assert(cart_getRomPtr(&cart, 0xf0, 0x1234) == image + 0x301234);
  assert(cart_getRomPtr(&cart, 0x00, 0x2000) == NULL);
  cart_set_hirom_linear_data_banks(&cart, false);
  assert(cart_getRomPtr(&cart, 0x41, 0x6000) == image + 0x016000);
  cart.romSize = 0x400000;
  cart_set_hirom_linear_data_banks(&cart, true);
  assert(!cart.hiromLinearDataBanks);
  cart.type = CART_LOROM;
  cart.romSize = sizeof image;
  cart_set_hirom_linear_data_banks(&cart, true);
  assert(!cart.hiromLinearDataBanks);
  assert(cart_getRomPtr(&cart, 0x41, 0x6000) == NULL);
  return 0;
}
