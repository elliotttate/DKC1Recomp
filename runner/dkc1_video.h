#ifndef DKC1_VIDEO_H
#define DKC1_VIDEO_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum {
  kDkc1VideoNativeWidth = 256,
  kDkc1VideoHeight = 224,
  /* SNES pixels present at a 7:6 pixel aspect. 342 source columns at 224
   * lines give 1.78125, within one pixel of exact 16:9 (same policy as
   * DKC2Recomp). Widescreen stays disabled until the DKC1 terrain
   * reconstruction is audited; the buffer is sized for it up front. */
  kDkc1VideoWidescreenExtra = 43,
  kDkc1VideoWidescreenWidth =
      kDkc1VideoNativeWidth + 2 * kDkc1VideoWidescreenExtra,
  kDkc1VideoBytesPerPixel = 4,
};

/* These symbols are the shared snesrecomp widescreen runtime contract. */
extern bool g_ws_active;
extern int g_ws_extra;

void Dkc1VideoSetWidescreen(bool enabled);
bool Dkc1VideoIsWidescreen(void);
void Dkc1VideoSetTerrainReady(bool ready);
bool Dkc1VideoTerrainReady(void);
int Dkc1VideoWidth(void);
int Dkc1VideoExtra(void);
size_t Dkc1VideoPixelCount(void);

/* Presentation-camera widescreen support. The recompiled game code stays
 * byte-for-byte stock (logical camera, spawn scanner, actor pool, clamps);
 * everything below is host-side rendering policy only. */

/* Keep a reference to the verified ROM for level-map margin decoding. */
void Dkc1VideoSetRom(const uint8_t *rom, size_t size);

/* BG1/BG2 layers eligible for widening: Mode 1 with a 64-column tilemap. */
uint8_t Dkc1VideoPpuWideLayerMask(uint8_t bg_mode,
                                  const uint8_t bg_xsc[4],
                                  uint8_t main_layers,
                                  uint8_t sub_layers);

/* Which wide layer receives DKC1's rolling column stream: the layer whose
 * tilemap base matches the streamer VRAM base at $7E1B13. -1 when none. */
int Dkc1VideoTerrainLayer(uint8_t wide_layer_mask,
                          const uint8_t bg_xsc[4],
                          uint16_t stream_vram_word_address);

/* Expand a repeating 10-bit SNES scroll phase nearest a world-space anchor. */
uint32_t Dkc1VideoUnwrapPpuScroll(uint16_t ppu_scroll, uint32_t anchor);

/* Locate a fully transparent 4bpp character in live VRAM. */
bool Dkc1VideoFindTransparent4bppTile(const uint16_t *vram,
                                      size_t word_count,
                                      uint16_t character_base,
                                      uint16_t *tile_entry);

/* DKC1 level-map layouts (from Level_BuildTilemapColumn_TypeA/B at
 * $81:8705 / $81:8DFA): horizontal levels store one $20-byte column of 16
 * metatile rows per 32px of X; vertical levels store $80-byte rows of 64
 * metatiles. Metatiles are 32x32px: 16 tilemap words at cell*32, X-flip in
 * cell bit 14, Y-flip in bit 15 (both XORed into the emitted entries). */
typedef enum Dkc1LevelLayout {
  kDkc1LayoutUnknown = 0,
  kDkc1LayoutHorizontal,
  kDkc1LayoutVertical,
} Dkc1LevelLayout;

/* Decode one 8x8 tilemap entry for a world tile straight from the ROM level
 * map (map data and metatile definitions share one bank, per
 * Level_SetTilemapPointers at $81:8C66). Returns false when unresolvable. */
bool Dkc1VideoDecodeLevelTile(Dkc1LevelLayout layout,
                              uint8_t map_bank,
                              uint16_t map_base,
                              uint16_t metatile_base,
                              uint32_t world_tile_x,
                              uint32_t world_tile_y,
                              uint16_t *tile_entry);

#endif
