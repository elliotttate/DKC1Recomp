#include "dkc1_blank_scan.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { kWidth = 342, kHeight = 224, kExtra = 43 };

int main(void) {
  static uint8_t pixels[kWidth * kHeight * 4];
  memset(pixels, 0, sizeof pixels);

  /* Both margins are flat black, but the two adjacent native columns carry
   * enough structure to prove that the whole picture is not a fade. */
  for (int y = 0; y < kHeight; y++) {
    const uint32_t color = (y & 1) ? UINT32_C(0xffffffff)
                                   : UINT32_C(0xff204080);
    memcpy(pixels + ((size_t)y * kWidth + kExtra) * 4, &color, 4);
    memcpy(pixels + ((size_t)y * kWidth + (kWidth - kExtra - 1)) * 4,
           &color, 4);
  }

  _putenv_s("DKC1_BLANK_SCAN", "");
  _putenv_s("DKC1_AUTO_EXPORT", "1");

  Dkc1BlankScanFrame(1, pixels, kWidth, kHeight, false);
  if (Dkc1BlankScanEventCount() != 0) {
    fprintf(stderr, "centered pillarbox was classified as gameplay cull\n");
    return 1;
  }

  Dkc1BlankScanFrame(2, pixels, kWidth, kHeight, true);
  if (Dkc1BlankScanEventCount() != 1) {
    fprintf(stderr, "full extended-gameplay cull was not detected\n");
    return 2;
  }

  /* A 16-line failure is intentionally too small to dominate a whole
   * 224-line column. The band detector must still preserve it. */
  for (int y = 0; y < kHeight; y++) {
    for (int x = 0; x < kWidth; x++) {
      const uint32_t color = UINT32_C(0xff000000) |
          (uint32_t)((x * 5 + y * 13) & 0x00ffffff);
      memcpy(pixels + ((size_t)y * kWidth + x) * 4, &color, 4);
    }
  }
  for (int y = 96; y < 112; y++) {
    memset(pixels + (size_t)y * kWidth * 4, 0, (size_t)kExtra * 4);
    memset(pixels + ((size_t)y * kWidth + kWidth - kExtra) * 4,
           0, (size_t)kExtra * 4);
  }
  Dkc1BlankScanFrame(3, pixels, kWidth, kHeight, true);
  if (Dkc1BlankScanEventCount() != 2) {
    fprintf(stderr, "partial-height gameplay cull was not detected\n");
    return 3;
  }

  /* A flat opening wholly inside the margin is scenery, not a 256px cut.
   * Preserve structured pixels immediately beside both legacy boundaries,
   * then reproduce the 27px Expresso-return gap that exposed the old false
   * positive. */
  for (int y = 0; y < kHeight; y++) {
    for (int x = 0; x < kWidth; x++) {
      const uint32_t color = UINT32_C(0xff000000) |
          (uint32_t)((x * 17 + y * 29) & 0x00ffffff);
      memcpy(pixels + ((size_t)y * kWidth + x) * 4, &color, 4);
    }
  }
  for (int y = 112; y < 128; y++) {
    memset(pixels + ((size_t)y * kWidth + 303) * 4, 0, 27u * 4u);
  }
  Dkc1BlankScanFrame(4, pixels, kWidth, kHeight, true);
  if (Dkc1BlankScanEventCount() != 2) {
    fprintf(stderr, "authored interior margin gap was classified as cull\n");
    return 4;
  }

  puts("BLANK_SCAN_DETECTOR_OK");
  return 0;
}
