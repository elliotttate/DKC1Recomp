#include "dkc1_video.h"

#include "cpu_state.h"

bool g_ws_active;
int g_ws_extra;
static bool s_terrain_ready;
static int s_presentation_bias;

void Dkc1VideoSetWidescreen(bool enabled) {
  if (g_ws_active != enabled) {
    s_terrain_ready = false;
    s_presentation_bias = 0;
  }
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

uint16_t Dkc1VideoExpandCullLeft(uint16_t native_margin) {
  int margin = native_margin;
  if (Dkc1VideoTerrainReady())
    margin += g_ws_extra - s_presentation_bias;
  return (uint16_t)(margin < 0 ? 0 : margin);
}

uint16_t Dkc1VideoExpandCullSpan(uint16_t native_span) {
  return (uint16_t)(native_span +
                    (Dkc1VideoTerrainReady() ? 2 * g_ws_extra : 0));
}

uint16_t Dkc1VideoPromoteOamXHigh(uint16_t screen_x) {
  /* Several DKC1 direct OAM writers derive X-high from the sign bit because
   * stock play only presents negative off-left coordinates.  Positive
   * coordinates in the host's right margin need bit 8 mirrored into bit 15
   * before that original XBA/shift packing sequence runs. */
  if (Dkc1VideoTerrainReady() && (screen_x & 0x0100u))
    return (uint16_t)(screen_x | 0x8000u);
  return screen_x;
}

uint16_t Dkc1VideoBiasCullX(uint16_t screen_x) {
  /* Convert [-extra, 255+extra] to [0, 255+2*extra] for private renderers
   * whose stock code performs a sign test followed by a positive-span test.
   * This value is for comparisons only; the original screen X remains in
   * the game's scratch/OAM path. */
  return (uint16_t)(screen_x +
                    (Dkc1VideoTerrainReady()
                         ? g_ws_extra - s_presentation_bias : 0));
}

void Dkc1VideoSetPresentationBias(int bias) {
  if (bias < -g_ws_extra) bias = -g_ws_extra;
  if (bias > g_ws_extra) bias = g_ws_extra;
  s_presentation_bias = g_ws_active ? bias : 0;
}

int Dkc1VideoPresentationBias(void) {
  return g_ws_active ? s_presentation_bias : 0;
}

uint16_t Dkc1VideoPromoteOamSizeMask(uint16_t size_mask,
                                    uint16_t screen_x) {
  if (Dkc1VideoTerrainReady() && (screen_x & 0x0100u))
    return (uint16_t)(size_mask | (size_mask >> 1));
  return size_mask;
}

bool Dkc1VideoPrepareType5ChildRetry(struct CpuState *cpu) {
  if (!cpu || !Dkc1VideoTerrainReady())
    return false;

  const uint16_t parent_index =
      cpu_read16(cpu, 0x00, (uint16_t)(cpu->D + 0x00a4));
  const uint16_t bookmark = cpu_read16(
      cpu, cpu->DB, (uint16_t)(0x192bu + parent_index));
  if ((bookmark & 0x00ffu) == 0)
    return false;

  const uint16_t parent_record = cpu->Y;
  /* The parent record's +6 word is an absolute source-table cursor to the
   * record immediately before its children, not a parent-relative offset. */
  const uint16_t first_child = (uint16_t)(
      cpu_read16(cpu, cpu->DB, (uint16_t)(parent_record + 0x0006u)) +
      0x0008u);
  const uint16_t layer_x = cpu_read16(cpu, cpu->DB, 0x088bu);
  const uint16_t right =
      (uint16_t)(layer_x + Dkc1VideoExpandCullLeft(0x0120u));
  if (right < cpu_read16(
                  cpu, cpu->DB, (uint16_t)(first_child + 0x0002u)))
    return false;

  uint16_t last_child = first_child;
  for (unsigned child = 0; child < 0x100u; child++) {
    if (cpu_read16(
            cpu, cpu->DB, (uint16_t)(last_child + 0x0008u)) == 0)
      break;
    last_child = (uint16_t)(last_child + 0x0008u);
    if (child == 0xffu)
      return false;  /* malformed source chain: fail closed */
  }

  const uint16_t left =
      (uint16_t)(layer_x - Dkc1VideoExpandCullLeft(0x0020u));
  const uint16_t last_x = cpu_read16(
      cpu, cpu->DB, (uint16_t)(last_child + 0x0002u));
  if ((left & 0x8000u) == 0 || left < 0xfc00u) {
    if (left >= last_x)
      return false;
  } else if (left < last_x) {
    return false;
  }

  cpu_write16(cpu, 0x00, (uint16_t)(cpu->D + 0x0076u), parent_record);
  cpu_write16(cpu, 0x00, (uint16_t)(cpu->D + 0x0078u), first_child);

  /* Match the child loop's stock caller contract: parent map index on the
   * emulated stack, $A4 advanced to the first child bookmark, and Y pointing
   * at the first eight-byte source record. */
  /* Match a native 16-bit PHA/PHY exactly.  In native mode the 65816 first
   * decrements S, writes the little-endian word at that address, then
   * decrements S once more.  Writing at the old S and subtracting two makes
   * the later PLA consume one stale byte and shifts the caller's RTS frame;
   * that eventually returned through arbitrary ROM data and blacked the
   * recompile output. */
  cpu->S = (uint16_t)(cpu->S - 1u);
  cpu_write16(cpu, 0x00, cpu->S, parent_index);
  cpu->S = (uint16_t)(cpu->S - 1u);
  const uint16_t child_index = (uint16_t)(
      parent_index + ((uint16_t)(first_child - parent_record) >> 3));
  cpu_write16(cpu, 0x00, (uint16_t)(cpu->D + 0x00a4u), child_index);
  cpu->Y = first_child;
  return true;
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
