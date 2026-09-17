#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "snes/ppu.h"
#include "snes/snes.h"
#include "snes/ws_shadow.h"

Snes *g_snes;
int snes_frame_counter;
#define CHECK(c) do { if (!(c)) { \
  fprintf(stderr, "PPU check failed at line %d: %s\n", __LINE__, #c); \
  return 1; } } while (0)

int main(void) {
  Ppu *ppu = ppu_init();
  CHECK(ppu != NULL);
  static uint8_t pixels[342 * 224 * 4];
  PpuZbufType native[342], shadow[342];
  for (int fine = 0; fine < 8; fine++) {
    for (int side = 0; side < 3; side++) {
      ppu_reset(ppu);
      WsShadowReset();
      WsShadowSetPresentationPolicy(0);
      ppu->inidisp = 15;
      ppu->bgmode = 1;
      ppu->screenEnabled[0] = 1;
      ppu->bgXsc[0] = 1;
      ppu->bgTileAdr = 1;
      ppu->hScroll[0] = (uint16_t)(64 + fine);
      const uint16_t native_tile = (uint16_t)(1 | (fine << 10) |
          ((fine & 1) ? 0x4000 : 0) | ((fine & 2) ? 0x8000 : 0));
      const uint16_t shadow_tile = (uint16_t)(2 | ((7 - fine) << 10) |
          ((fine & 1) ? 0 : 0x4000) | ((fine & 2) ? 0 : 0x8000) | 0x2000);
      for (int i = 0; i < 2048; i++) ppu->vram[i] = native_tile;
      for (int y = 0; y < 8; y++) {
        ppu->vram[0x1010 + y] = (uint16_t)(0x35 + y);
        ppu->vram[0x1020 + y] = (uint16_t)((0x23 + y) << 8);
      }
      PpuBeginDrawing(ppu, pixels, 342 * 4, kPpuRenderFlags_NewRenderer);
      PpuSetExtraSpace(ppu, 43);
      PpuSetWidescreenLayerMask(ppu, 1);
      ppu_runLine(ppu, 1);
      for (int x = -43; x < 299; x++)
        native[x + 43] = ppu->bgBuffers[0].data[x + kPpuExtraLeftRight];
      for (int i = 0; i < 2048; i++) ppu->vram[i] = shadow_tile;
      ppu_runLine(ppu, 1);
      for (int x = -43; x < 299; x++)
        shadow[x + 43] = ppu->bgBuffers[0].data[x + kPpuExtraLeftRight];
      for (int i = 0; i < 2048; i++) ppu->vram[i] = native_tile;

      const int left = side == 1 ? 9 : 0;
      const int right = side == 2 ? 247 : 256;
      WsShadowSetWorld(0, 64 + fine, 0);
      WsShadowSetScroll(0, 64 + fine, 0);
      WsShadowSetNativeViewportInset(0, left, 256 - right);
      WsShadowFrame(ppu);
      /* Deliberately different valid shadow entries at every position. The
       * renderer must protect the entire native interval, even straddlers. */
      for (unsigned x = 0; x < 64; x++)
        WsShadowForceTile(0, x, 0, shadow_tile);
      WsShadowSetPresentationPolicy(kWsShadowPixelBoundaries);
      WsShadowDebugSetProvenanceEnabled(true);
      WsShadowDebugBeginFrame();
      ppu_runLine(ppu, 1);
      for (int x = -43; x < 299; x++) {
        const PpuZbufType pixel =
            ppu->bgBuffers[0].data[x + kPpuExtraLeftRight];
        if (x >= left && x < right) {
          CHECK(pixel == native[x + 43]);
          CHECK(WsShadowDebugProvenanceAt(0, x, 1) ==
                kWsShadowProvenanceNone);
        } else {
          CHECK(pixel == shadow[x + 43]);
          CHECK(WsShadowDebugProvenanceAt(0, x, 1) !=
                kWsShadowProvenanceNone);
        }
      }
    }
  }

  WsShadowReset();
  WsShadowSetWorld(0, 40001, 0);
  WsShadowSetCaptureWorld(0, 39991, 0); /* host bias +10 */
  WsShadowSetScroll(0, 1010, 0);
  WsShadowSetPresentationPolicy(kWsShadowLiveScroll);
  CHECK(WsShadowPresentWorldX(0, 256, 1020) == 40257);
  CHECK(WsShadowPresentWorldX(0, 256, 0) == 40261); /* +4, wraps at 1024 */
  CHECK(WsShadowPresentWorldX(0, -10, 1016) == 39987);
  WsShadowSetWorld(0, 40001, 0);
  WsShadowSetCaptureWorld(0, 40011, 0); /* host bias -10 */
  WsShadowSetScroll(0, 10, 0);
  CHECK(WsShadowPresentWorldX(0, 256, 1020) == 40253); /* -4 */
  WsShadowSetPresentationPolicy(0);
  CHECK(WsShadowPresentWorldX(0, 256, 1020) == 40257);

  /* A cold underwater cache at fine Y=7 encounters VOFS+1 on its last
   * scanline. The partially native left tile then needs the 30th live row. */
  ppu_reset(ppu);
  WsShadowReset();
  ppu->bgmode = 1; ppu->bgXsc[0] = 1;
  ppu->hScroll[0] = 143; ppu->vScroll[0] = 767;
  WsShadowSetWorld(0, 1167, 255);
  WsShadowSetCaptureWorld(0, 1167, 255);
  WsShadowSetScroll(0, 143, 767);
  WsShadowSetBlankTile(0, 0);
  WsShadowSetPresentationPolicy(kWsShadowPixelBoundaries | kWsShadowLiveScroll);
  WsShadowFrame(ppu);
  CHECK(WsShadowTileDebug(0, -1, 223, 1, 992, 144, 0, 0x2222) == 0);
  WsShadowCaptureTile(0, 145, 60, 0x2222);
  CHECK(WsShadowTileDebug(0, -1, 223, 1, 992, 144, 0, 0x2222) == 0x2222);

  /* Opt-ins are host settings, not serialized cache fields. */
  const size_t size = WsShadowSnapshotSize();
  void *a = malloc(size), *b = malloc(size);
  CHECK(a && b && WsShadowSnapshotSave(a, size));
  WsShadowSetPresentationPolicy(kWsShadowPixelBoundaries | kWsShadowLiveScroll);
  CHECK(WsShadowSnapshotSize() == size && WsShadowSnapshotSave(b, size));
  CHECK(memcmp(a, b, size) == 0);
  CHECK(WsShadowSnapshotLoad(a, size));
  free(a); free(b);
  ppu_free(ppu);
  puts("pixel boundaries, scanline scroll, bias, and state compatibility: PASS");
  return 0;
}
