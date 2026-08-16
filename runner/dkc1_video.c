#include "dkc1_video.h"

#include <stdlib.h>
#include <string.h>

#include "cpu_state.h"

bool g_ws_active;
int g_ws_extra;
static bool s_terrain_ready;
static int s_presentation_bias;

typedef struct Dkc1PlacedActorPhase {
  uint16_t id;
  uint16_t source;
  bool stock_started;
} Dkc1PlacedActorPhase;

/* Normal actor indexes are the even values $02..$32.  This is host-only
 * lifecycle state: no cartridge WRAM word is repurposed. */
static Dkc1PlacedActorPhase s_placed_actor_phases[0x1a];
static bool s_placed_actor_phases_seeded;

typedef struct Dkc1PlacedActorContext {
  uint16_t mode;
  uint16_t level;
  uint16_t entrance;
} Dkc1PlacedActorContext;

static Dkc1PlacedActorContext s_placed_actor_context;
static bool s_placed_actor_context_valid;

enum { kDkc1WramSize = 0x20000 };

static uint8_t s_prefetch_wram[kDkc1WramSize];
static bool s_prefetch_dispatch_active;

static bool Dkc1PrefetchPhaseGuardEnabled(void) {
  const char *value = getenv("DKC1_PREFETCH_PHASE_GUARD");
  return value && value[0] == '1' && value[1] == '\0';
}

static void Dkc1VideoClearPlacedActorPhases(void) {
  memset(s_placed_actor_phases, 0, sizeof s_placed_actor_phases);
  s_placed_actor_phases_seeded = false;
  s_prefetch_dispatch_active = false;
}

void Dkc1VideoResetPlacedActorPhases(void) {
  Dkc1VideoClearPlacedActorPhases();
  memset(&s_placed_actor_context, 0, sizeof s_placed_actor_context);
  s_placed_actor_context_valid = false;
}

void Dkc1VideoSetWidescreen(bool enabled) {
  if (g_ws_active != enabled) {
    s_terrain_ready = false;
    s_presentation_bias = 0;
    Dkc1VideoResetPlacedActorPhases();
  }
  g_ws_active = enabled;
  g_ws_extra = enabled ? kDkc1VideoWidescreenExtra : 0;
}

bool Dkc1VideoIsWidescreen(void) {
  return g_ws_active;
}

void Dkc1VideoSetTerrainReady(bool ready) {
  /* Presentation calibration can reject one frame without changing the
   * cartridge's object context.  Keep placed-actor lifecycle history across
   * that soft fallback; otherwise an already-prefetched actor is reseeded as
   * stock-started and advances before the native scanner reaches it. */
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

uint16_t Dkc1VideoMergeOamSizeAndXHigh(uint16_t existing_word,
                                      uint16_t size_mask,
                                      uint16_t screen_x) {
  if (!Dkc1VideoTerrainReady())
    return (uint16_t)(existing_word | size_mask);

  /* DATA_80A545 supplies one of the odd bits in 0xAAAA (the large-size
   * member of an OAM pair). PromoteOamSizeMask may already have mirrored it
   * into the neighboring even X-high bit, so first recover size bits only.
   * Clearing before setting is essential: a prior same-frame OAM user can
   * leave X-high=1 even though this rope's true 9-bit X is below 256. */
  const uint16_t size_bits = (uint16_t)(size_mask & 0xaaaau);
  const uint16_t x_high_bits = (uint16_t)(size_bits >> 1);
  uint16_t merged = (uint16_t)((existing_word & ~x_high_bits) | size_bits);
  if (screen_x & 0x0100u)
    merged = (uint16_t)(merged | x_high_bits);
  return merged;
}

static bool Dkc1UnsignedInside(uint16_t value, uint16_t left,
                               uint16_t right) {
  return left <= right ? value >= left && value <= right
                       : value >= left || value <= right;
}

static uint16_t Dkc1ReadWram16(const uint8_t *wram, uint16_t address) {
  return (uint16_t)(wram[address] |
                    ((uint16_t)wram[(uint16_t)(address + 1u)] << 8));
}

static void Dkc1VideoObservePlacedActorContext(const uint8_t *wram) {
  if (!wram)
    return;

  Dkc1PlacedActorContext current;
  current.mode = Dkc1ReadWram16(wram, 0x0032u);
  current.level = Dkc1ReadWram16(wram, 0x0030u);
  current.entrance = Dkc1ReadWram16(wram, 0x003eu);
  if (s_placed_actor_context_valid &&
      current.mode == s_placed_actor_context.mode &&
      current.level == s_placed_actor_context.level &&
      current.entrance == s_placed_actor_context.entrance)
    return;

  /* A real gameplay-context change starts a new allocation history.  This is
   * deliberately independent of PPU/BG calibration identity: BGMODE, BGSC,
   * screen-mask, or soft tile-agreement changes must not reseed actors. */
  Dkc1VideoClearPlacedActorPhases();
  s_placed_actor_context = current;
  s_placed_actor_context_valid = true;
}

void Dkc1VideoObserveActorPool(const uint8_t *wram) {
  Dkc1VideoObservePlacedActorContext(wram);
  if (!wram || !s_placed_actor_phases_seeded)
    return;

  /* Observe the completed previous frame before this frame's scanner can
   * allocate anything. Dispatch-only tracking cannot see a free slot because
   * empty actors are not dispatched; without this boundary pass, reuse of the
   * same slot for the same ID/source pair inherits stock_started=true and
   * advances the new actor during the widened-only prefetch interval. */
  for (uint16_t actor_index = 0x0002u; actor_index <= 0x0032u;
       actor_index = (uint16_t)(actor_index + 2u)) {
    const uint16_t id = (uint16_t)(wram[0x0d45u + actor_index] |
        ((uint16_t)wram[0x0d46u + actor_index] << 8));
    if (id != 0)
      continue;
    Dkc1PlacedActorPhase *phase =
        &s_placed_actor_phases[(actor_index - 2u) >> 1];
    phase->id = 0;
    phase->source = 0;
    phase->stock_started = false;
  }
}

bool Dkc1VideoShouldRunPlacedActor(struct CpuState *cpu) {
  if (!cpu || !Dkc1VideoIsWidescreen())
    return true;

  Dkc1VideoObservePlacedActorContext(cpu->ram);

  /* A loaded snapshot is left-censored: actors already present may have run
   * for hundreds of frames before host-only phase tracking existed.  Seed
   * the whole normal pool on the first dispatch after a reset and treat
   * those identities as started.  Only identities allocated afterwards are
   * eligible for the widened-prefetch delay. */
  if (!s_placed_actor_phases_seeded) {
    for (uint16_t seeded_index = 0x0002u; seeded_index <= 0x0032u;
         seeded_index = (uint16_t)(seeded_index + 2u)) {
      const unsigned seeded_ordinal =
          (unsigned)((seeded_index - 2u) >> 1);
      Dkc1PlacedActorPhase *seeded =
          &s_placed_actor_phases[seeded_ordinal];
      seeded->id = cpu_read16(
          cpu, 0x7e, (uint16_t)(0x0d45u + seeded_index));
      seeded->source = cpu_read16(
          cpu, 0x7e, (uint16_t)(0x15fdu + seeded_index));
      seeded->stock_started = seeded->id != 0;
    }
    s_placed_actor_phases_seeded = true;
  }

  const uint16_t actor_index = cpu_read16(
      cpu, 0x00, (uint16_t)(cpu->D + 0x0082u));
  if (actor_index < 0x0002u || actor_index > 0x0032u ||
      (actor_index & 1u) != 0)
    return true;

  const unsigned ordinal = (unsigned)((actor_index - 2u) >> 1);
  Dkc1PlacedActorPhase *phase = &s_placed_actor_phases[ordinal];
  const uint16_t id = cpu_read16(
      cpu, 0x7e, (uint16_t)(0x0d45u + actor_index));
  const uint16_t source = cpu_read16(
      cpu, 0x7e, (uint16_t)(0x15fdu + actor_index));
  if (phase->id != id || phase->source != source) {
    phase->id = id;
    phase->source = source;
    /* A new identity allocated while widening is inactive came from the
     * authentic stock scanner and is immediately safe to run.  A new
     * identity allocated while widening is active may be margin-prefetched
     * and must pass the reconstructed stock interval below. */
    phase->stock_started = !Dkc1VideoTerrainReady();
  }

  /* Kongs, generated effects, grouped children, and non-authored actor slots
   * do not use the ordinary eight-byte source-record window. */
  if (id == 0 || source >= 0x0100u) {
    phase->stock_started = true;
    return true;
  }

  const uint16_t entrance = cpu_read16(cpu, 0x7e, 0x003eu);
  if (entrance >= 0x00e6u) {
    phase->stock_started = true;
    return true;
  }
  const uint16_t table = cpu_read16(
      cpu, 0xbd, (uint16_t)(0x8000u + entrance * 2u));
  const uint16_t record = (uint16_t)(table + source * 8u);
  const uint16_t record_type = cpu_read16(cpu, 0xbd, record);
  if (record_type != 0x0001u) {
    phase->stock_started = true;
    return true;
  }

  const uint16_t current_left = cpu_read16(
      cpu, 0x00, (uint16_t)(cpu->D + 0x00efu));
  const uint16_t current_right = cpu_read16(
      cpu, 0x00, (uint16_t)(cpu->D + 0x00f1u));
  uint16_t stock_left = current_left;
  uint16_t stock_right = current_right;
  if (Dkc1VideoTerrainReady()) {
    const int extra = g_ws_extra;
    const int bias = s_presentation_bias;
    if (extra <= 0 || bias < -extra || bias > extra) {
      phase->stock_started = true;
      return true;
    }
    stock_left = (uint16_t)(current_left + extra - bias);
    stock_right = (uint16_t)(current_right - extra - bias);
  }
  const uint16_t source_x = cpu_read16(
      cpu, 0xbd, (uint16_t)(record + 2u));

  if (!phase->stock_started &&
      Dkc1UnsignedInside(source_x, stock_left, stock_right))
    phase->stock_started = true;
  return phase->stock_started;
}

bool Dkc1VideoBeginPlacedActorDispatch(struct CpuState *cpu) {
  /* A prefetched actor is allowed to execute its authentic routine so the
   * later cartridge presentation pass can observe its authored pose. The
   * matching end hook restores the complete 128 KiB WRAM image, preventing
   * movement, collision, spawns, sound-command staging, scratch values, and
   * object bookkeeping from escaping before stock eligibility. This is a
   * transaction around one dispatch, not an alternate actor implementation. */
  s_prefetch_dispatch_active = false;
  if (!Dkc1PrefetchPhaseGuardEnabled() || !cpu ||
      Dkc1VideoShouldRunPlacedActor(cpu))
    return false;

  memcpy(s_prefetch_wram, cpu->ram, kDkc1WramSize);
  s_prefetch_dispatch_active = true;
  return true;
}

void Dkc1VideoEndPlacedActorDispatch(struct CpuState *cpu) {
  if (!cpu || !s_prefetch_dispatch_active)
    return;
  memcpy(cpu->ram, s_prefetch_wram, kDkc1WramSize);
  s_prefetch_dispatch_active = false;
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
