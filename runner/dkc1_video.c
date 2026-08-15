#include "dkc1_video.h"

bool g_ws_active;
int g_ws_extra;
static bool s_terrain_ready;

void Dkc1VideoSetWidescreen(bool enabled) {
  if (g_ws_active != enabled)
    s_terrain_ready = false;
  g_ws_active = enabled;
  g_ws_extra = enabled ? kDkc1VideoWidescreenExtra : 0;
}

bool Dkc1VideoIsWidescreen(void) {
  return g_ws_active;
}

void Dkc1VideoSetTerrainReady(bool ready) {
  s_terrain_ready = g_ws_active && ready;
}

bool Dkc1VideoTerrainReady(void) {
  return g_ws_active && s_terrain_ready;
}

int Dkc1VideoWidth(void) {
  return kDkc1VideoNativeWidth + 2 * g_ws_extra;
}

int Dkc1VideoExtra(void) {
  return g_ws_extra;
}

size_t Dkc1VideoPixelCount(void) {
  return (size_t)Dkc1VideoWidth() * kDkc1VideoHeight;
}

static const uint8_t *s_rom;
static size_t s_rom_size;

void Dkc1VideoSetRom(const uint8_t *rom, size_t size) {
  s_rom = rom;
  s_rom_size = size;
}

/* HiROM bus address -> ROM offset (banks C0-FF full, 80-BF upper halves). */
static bool RomOffset(uint8_t bank, uint16_t address, size_t *offset) {
  size_t out;
  if (bank >= 0xC0) {
    out = ((size_t)(bank - 0xC0) << 16) | address;
  } else if (bank >= 0x80 && address >= 0x8000) {
    out = ((size_t)(bank - 0x80) << 16) | address;
  } else if (bank < 0x40 && address >= 0x8000) {
    out = ((size_t)bank << 16) | address;
  } else {
    return false;
  }
  if (!s_rom || out + 1 >= s_rom_size)
    return false;
  *offset = out;
  return true;
}

static bool RomWord(uint8_t bank, uint16_t address, uint16_t *value) {
  size_t offset;
  if (!RomOffset(bank, address, &offset))
    return false;
  *value = (uint16_t)(s_rom[offset] | ((uint16_t)s_rom[offset + 1] << 8));
  return true;
}

uint8_t Dkc1VideoPpuWideLayerMask(uint8_t bg_mode,
                                  const uint8_t bg_xsc[4],
                                  uint8_t main_layers,
                                  uint8_t sub_layers) {
  if ((bg_mode & 7u) != 1u) return 0;
  uint8_t enabled = (uint8_t)((main_layers | sub_layers) & 0x0f);
  uint8_t mask = 0;
  for (unsigned layer = 0; layer < 2; layer++) {
    uint8_t bit = (uint8_t)(1u << layer);
    if ((enabled & bit) && (bg_xsc[layer] & 1u))
      mask = (uint8_t)(mask | bit);
  }
  return mask;
}

int Dkc1VideoTerrainLayer(uint8_t wide_layer_mask,
                          const uint8_t bg_xsc[4],
                          uint16_t stream_vram_word_address) {
  if (!bg_xsc)
    return -1;
  const uint16_t stream_base =
      (uint16_t)(stream_vram_word_address & 0xfc00u);
  for (int layer = 0; layer < 2; layer++) {
    const uint8_t bit = (uint8_t)(1u << layer);
    const uint16_t map_base =
        (uint16_t)((uint16_t)(bg_xsc[layer] & 0xfcu) << 8);
    if ((wide_layer_mask & bit) && map_base == stream_base)
      return layer;
  }
  return -1;
}

uint32_t Dkc1VideoUnwrapPpuScroll(uint16_t ppu_scroll, uint32_t anchor) {
  const uint32_t period = 0x400u;
  const uint32_t half_period = period / 2u;
  uint32_t candidate = (anchor & ~(period - 1u)) |
                       ((uint32_t)ppu_scroll & (period - 1u));
  if (candidate + half_period < anchor)
    candidate += period;
  else if (candidate > anchor + half_period && candidate >= period)
    candidate -= period;
  return candidate;
}

bool Dkc1VideoFindTransparent4bppTile(const uint16_t *vram,
                                      size_t word_count,
                                      uint16_t character_base,
                                      uint16_t *tile_entry) {
  if (!vram || word_count < 0x8000u || !tile_entry)
    return false;
  for (uint16_t tile = 0; tile < 0x0400u; tile++) {
    const uint16_t address =
        (uint16_t)(character_base + (uint16_t)(tile * 16u));
    bool transparent = true;
    for (unsigned word = 0; word < 16u; word++) {
      if (vram[(address + word) & 0x7fffu] != 0) {
        transparent = false;
        break;
      }
    }
    if (transparent) {
      *tile_entry = tile;
      return true;
    }
  }
  return false;
}

bool Dkc1VideoDecodeLevelTile(Dkc1LevelLayout layout,
                              uint8_t map_bank,
                              uint16_t map_base,
                              uint16_t metatile_base,
                              uint32_t world_tile_x,
                              uint32_t world_tile_y,
                              uint16_t *tile_entry) {
  if (!tile_entry || layout == kDkc1LayoutUnknown)
    return false;
  const uint32_t metatile_x = world_tile_x >> 2;
  const uint32_t metatile_y = world_tile_y >> 2;
  uint16_t map_offset;
  if (layout == kDkc1LayoutHorizontal) {
    /* $81:8705: offset = (X & $FFE0) + ((Y & $1E0) >> 4). */
    map_offset = (uint16_t)(map_base + (metatile_x << 5) +
                            ((metatile_y & 0x0fu) << 1));
  } else {
    /* $81:8DFA: offset = ((X & $FFE0) >> 4) + row stride $80. */
    map_offset = (uint16_t)(map_base + ((metatile_x & 0x3fu) << 1) +
                            (metatile_y << 7));
  }
  uint16_t cell;
  if (!RomWord(map_bank, map_offset, &cell))
    return false;
  const uint16_t flips = (uint16_t)(cell & 0xc000u);
  unsigned sub_x = (unsigned)world_tile_x & 3u;
  unsigned sub_y = (unsigned)world_tile_y & 3u;
  if (flips & 0x4000u)
    sub_x = 3u - sub_x;
  if (flips & 0x8000u)
    sub_y = 3u - sub_y;
  /* 16-bit ASL x5 drops cell bits >= 11 exactly like the cartridge. */
  const uint16_t definition_offset =
      (uint16_t)((uint16_t)(cell << 5) + metatile_base +
                 (uint16_t)(sub_x * 2u + sub_y * 8u));
  uint16_t source;
  if (!RomWord(map_bank, definition_offset, &source))
    return false;
  *tile_entry = (uint16_t)(source ^ flips);
  return true;
}
