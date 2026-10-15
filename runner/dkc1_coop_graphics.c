#include "dkc1_coop_graphics.h"

#include "common_rtl.h"
#include "cpu_state.h"
#include "dkc1_coop_entities.h"
#include "dkc1_coop_mod.h"
#include "dkc1_wram_gen.h"

#include <string.h>

enum { kFlags = 0x0c69, kSpriteId = 0x0d45, kGraphicsOwners = 0x1a6f,
       kPaletteSources = 0x1ad3, kEntityCount = 128 };

typedef struct EntityGraphics {
  uint16_t blocks;
  uint16_t palette_source;
  uint8_t palette_owned;
  uint8_t palette_selection; /* $80 | OBJ row at the last explicit request. */
  uint8_t reserved[2];
} EntityGraphics;

typedef struct GraphicsState {
  uint32_t version;
  EntityGraphics entities[kEntityCount];
  Dkc1CoopWeatherState weather;
} GraphicsState;

static GraphicsState s_state = {4}, s_saved;
static int s_transaction;
static const uint8_t *s_rom;
static size_t s_rom_size;
static uint16_t Read16(unsigned address) {
  return Dkc1WramU16(g_ram, address);
}

static int ActiveHandle(uint16_t handle) {
  return Dkc1CoopEntitiesActive() && Dkc1CoopEntityIsHandle(handle);
}

static EntityGraphics *Graphics(uint16_t handle) {
  return ActiveHandle(handle) ? &s_state.entities[handle >> 1] : NULL;
}

static void SetNZ(struct CpuState *cpu, uint16_t value) {
  cpu->_flag_N = (value & 0x8000u) != 0;
  cpu->_flag_Z = value == 0;
  cpu->P = (uint8_t)((cpu->P & ~0x82u) |
                    (cpu->_flag_N ? 0x80u : 0u) |
                    (cpu->_flag_Z ? 0x02u : 0u));
}

static void SetCarry(struct CpuState *cpu, int value) {
  cpu->_flag_C = value != 0;
  cpu->P = (uint8_t)((cpu->P & ~1u) | (value != 0));
}

static uint16_t Flags(uint16_t handle) {
  return Dkc1CoopEntityRead16(g_ram, kFlags, handle);
}

static uint32_t StackReturnPc(struct CpuState *cpu, int long_return) {
  const uint16_t address = (uint16_t)(cpu_read16(
      cpu, 0, (uint16_t)(cpu->S + 1u)) + 1u);
  const uint8_t bank = long_return ?
      cpu_read8(cpu, 0, (uint16_t)(cpu->S + 3u)) : cpu->PB;
  return ((uint32_t)bank << 16) | address;
}

void Dkc1CoopGraphicsSetRom(const uint8_t *rom, size_t size) {
  s_rom = rom;
  s_rom_size = size;
}

void Dkc1CoopGraphicsReset(void) {
  memset(&s_state, 0, sizeof s_state);
  s_state.version = 4;
}

void Dkc1CoopGraphicsNoteFrame(void) {
  if (g_ram && Dkc1CoopEntitiesActive())
    Dkc1CoopWeatherObserve(g_ram, &s_state.weather);
}

void Dkc1CoopGraphicsResetEntity(uint16_t handle) {
  if (handle < 0x100u && !(handle & 1u))
    memset(&s_state.entities[handle >> 1], 0, sizeof(EntityGraphics));
}

void Dkc1CoopGraphicsSeedNative(void) {
  if (!Dkc1CoopEntitiesActive()) return;
  for (uint16_t handle = 2; handle <= 0x32; handle += 2) {
    if (!Read16(kSpriteId + handle)) continue;
    EntityGraphics *entry = &s_state.entities[handle >> 1];
    const unsigned tile_slot = (Flags(handle) & 0x01e0u) >> 4;
    entry->blocks = 0;
    for (unsigned i = tile_slot; i <= 0x1cu &&
         Read16(kGraphicsOwners + i) == handle; i += 2)
      entry->blocks++;
    entry->palette_source = Read16(kPaletteSources +
                                  ((Flags(handle) >> 8) & 0x0eu));
    entry->palette_owned = entry->palette_source != 0;
    entry->palette_selection = entry->palette_owned ?
        (uint8_t)(0x80u | ((Flags(handle) >> 9) & 7u)) : 0;
  }
}

unsigned Dkc1CoopGraphicsBlocks(uint16_t handle) {
  EntityGraphics *entry = Graphics(handle);
  return entry ? entry->blocks : 0;
}

int Dkc1CoopGraphicsGetPalette(uint16_t handle, uint16_t colors[16]) {
  EntityGraphics *entry = Graphics(handle);
  if (!entry || !colors || !entry->palette_owned) return 0;
  unsigned bits = Flags(handle) & 0x0e00u;
  /* An explicit replacement can retain a physical row while its source
   * table belongs to another actor. Trust ownership only while the actor
   * still selects the row recorded by that request. A later fixed-row
   * property change continues to use the native CGRAM fallback below. */
  if (entry->palette_selection == (uint8_t)(0x80u | (bits >> 9)))
    return Dkc1CoopGraphicsReadPalette(entry->palette_source, colors);
  /* A native initializer may replace an inherited virtual palette with a
   * fixed OBJ row (Necky's tire does this after spawning). Its live flags
   * select that row, not the parent's stored source. Mid-scene native
   * allocations remain valid when the physical source table agrees. */
  uint16_t native_source = Read16(kPaletteSources + (bits >> 8));
  if (bits != kDkc1CoopGraphicsPaletteBits && native_source &&
      native_source != entry->palette_source)
    return 0;
  return Dkc1CoopGraphicsReadPalette(entry->palette_source, colors);
}

int Dkc1CoopGraphicsReadPalette(uint16_t source, uint16_t colors[16]) {
  if (!colors) return 0;
  const int rom_row = s_rom && (size_t)0x3c0000u + source + 30u <= s_rom_size &&
      source >= 0x8000u && source <= 0xffe2u;
  uint16_t dry[15];
  if (rom_row) {
    for (unsigned i = 0; i < 15; ++i) {
      size_t at = 0x3c0000u + source + i * 2u;
      dry[i] = (uint16_t)(s_rom[at] | (uint16_t)s_rom[at + 1] << 8);
    }
  }
  colors[0] = 0;
  return Dkc1CoopWeatherPalette(g_ram, source, rom_row ? dry : NULL,
                                &s_state.weather, colors + 1);
}

void Dkc1CoopGraphicsCopyPalette(uint16_t destination, uint16_t source) {
  EntityGraphics *dst = Graphics(destination), *src = Graphics(source);
  if (!dst || !src) return;
  /* Copy ownership by value. Releasing/reusing the parent never changes its
   * already-spawned child's palette, and no alias refcount is required. */
  dst->palette_source = src->palette_source;
  dst->palette_owned = src->palette_owned;
  /* Inheriting colors is not an explicit row selection by this child. */
  dst->palette_selection = 0;
}

void Dkc1CoopGraphicsUseFixedPalette(uint16_t handle) {
  EntityGraphics *entry = Graphics(handle);
  if (entry) {
    entry->palette_source = 0;
    entry->palette_owned = 0;
    entry->palette_selection = 0;
  }
}

int Dkc1CoopGraphicsAllocate(struct CpuState *cpu, int contiguous) {
  if (!cpu || cpu->m_flag || cpu->x_flag) return 0;
  EntityGraphics *entry = Graphics(cpu->X);
  if (!entry) return 0;
  const uint16_t count = contiguous ?
      cpu_read16(cpu, 0, (uint16_t)(cpu->D + 0x4cu)) : 1u;
  if (!count) return 0;
  entry->blocks = count;
  const uint16_t flags = (uint16_t)((Flags(cpu->X) & ~0x01ffu) |
                                   kDkc1CoopGraphicsTileBase);
  Dkc1CoopEntityWrite16(g_ram, kFlags, cpu->X, flags);
  cpu->Y = contiguous ? (uint16_t)(8u + (count - 1u) * 2u) : 8u;
  cpu->A = contiguous ? cpu->X : flags;
  if (contiguous) {
    cpu_write16(cpu, 0, (uint16_t)(cpu->D + 0x4cu), 0);
    cpu_write16(cpu, 0, (uint16_t)(cpu->D + 0x4eu), 0);
  }
  SetNZ(cpu, contiguous ? 0u : flags);
  SetCarry(cpu, 0);
  return 1;
}

int Dkc1CoopGraphicsRelease(struct CpuState *cpu, uint16_t handle) {
  EntityGraphics *entry = Graphics(handle);
  if (!entry || !cpu) return 0;
  entry->blocks = 0;
  cpu->Y = (Flags(handle) & 0x01e0u) >> 4;
  cpu->A = 0;
  SetNZ(cpu, 0);
  SetCarry(cpu, 0);
  return 1;
}

int Dkc1CoopGraphicsCapacity(struct CpuState *cpu) {
  if (!cpu || !Dkc1CoopEntitiesActive()) return 0;
  /* The remaining entity handles, rather than eleven physical tile slots,
   * bound allocation. A request cannot exhaust host-owned tile metadata. */
  cpu->A = cpu->X = 0x7fffu;
  cpu->Y = 6;
  SetNZ(cpu, cpu->A);
  SetCarry(cpu, 0);
  return 1;
}

static void SetPalette(struct CpuState *cpu, uint16_t handle,
                       uint16_t source, uint16_t bits) {
  EntityGraphics *entry = &s_state.entities[handle >> 1];
  entry->palette_source = source;
  entry->palette_owned = 1;
  entry->palette_selection = (uint8_t)(0x80u | (bits >> 9));
  cpu_write16(cpu, 0, (uint16_t)(cpu->D + 0x8cu), source);
  cpu_write16(cpu, 0, (uint16_t)(cpu->D + 0x76u), bits >> 8);
  cpu->X = bits >> 8;
  cpu->A = bits;
  SetNZ(cpu, 0); /* XBA uses the low result byte for N/Z. */
  SetCarry(cpu, 0);
}

int Dkc1CoopGraphicsCancelKongPaletteUpload(struct CpuState *cpu) {
  if (!cpu || cpu->m_flag || cpu->x_flag || !Dkc1CoopEntitiesActive()) return 0;
  /* $8084D7/$8084F0 cancel the palette upload from the preceding Kong
   * spawn. The virtual allocator never queued it. Decrementing $1AD1 here
   * would expose empty DMA records (bank/address/CGRAM index all zero). */
  cpu->A = Read16(0x1ad1u) & 0x0fu;
  SetNZ(cpu, cpu->A);
  return 1;
}

int Dkc1CoopGraphicsAllocatePalette(struct CpuState *cpu,
                                  uint32_t return_pc) {
  if (!cpu || cpu->m_flag || cpu->x_flag || !Dkc1CoopEntitiesActive()) return 0;
  if (return_pc == 0xffffffffu) return_pc = StackReturnPc(cpu, 1);
  uint16_t handle;
  switch (return_pc) {
    case 0xb58141u:
    case 0x81d55du:
    case 0xb58465u: /* Native scene-dependent actor palette property. */
      handle = cpu_read16(cpu, 0, (uint16_t)(cpu->D + 0x86u));
      break;
    case 0xb6d21fu:
      handle = cpu->Y;
      break;
    default: return 0; /* Global/menu allocations keep their cartridge slots. */
  }
  if (!ActiveHandle(handle)) return 0;
  SetPalette(cpu, handle, cpu->A, kDkc1CoopGraphicsPaletteBits);
  return 1;
}

int Dkc1CoopGraphicsReplacePalette(struct CpuState *cpu, int change_flags) {
  if (!cpu || cpu->m_flag || cpu->x_flag || !ActiveHandle(cpu->X)) return 0;
  const uint16_t handle = cpu->X;
  const uint16_t bits = change_flags ? kDkc1CoopGraphicsPaletteBits :
                                      (Flags(handle) & 0x0e00u);
  /* Mounting exchanges the displayed Kong/animal roles. Palette replacement
   * addresses the displayed role, while allocation metadata stays with the
   * original owner used by the private renderer. */
  uint16_t owner = Dkc1CoopActorPaletteOwner(g_ram, handle);
  if (!change_flags && owner != handle && (handle == 2 || handle == 4)) {
    const uint32_t caller = StackReturnPc(cpu, 1);
    /* $B880D2 redirects only its leader through $B88128. Its two follower
     * calls still address the logical Kong even when co-op mounted it. */
    if (caller == 0xb88109u || caller == 0xb88127u) owner = handle;
  }
  SetPalette(cpu, owner, cpu->A, bits);
  if (change_flags) {
    cpu->A = (uint16_t)((Flags(handle) & ~0x0e00u) | bits);
    Dkc1CoopEntityWrite16(g_ram, kFlags, handle, cpu->A);
    cpu->X = cpu->Y = handle;
    SetNZ(cpu, cpu->A);
  }
  return 1;
}

int Dkc1CoopGraphicsReleasePalette(struct CpuState *cpu,
                                 uint32_t return_pc) {
  if (!cpu || cpu->m_flag || cpu->x_flag || !Dkc1CoopEntitiesActive()) return 0;
  if (return_pc == 0xffffffffu) {
    /* An interpreter-owned frame has no paired host caller. Its real stack
     * still identifies the same narrow actor call sites. */
    return_pc = StackReturnPc(cpu, 0);
    if (return_pc != 0xbdf46du && return_pc != 0xbdf483u &&
        return_pc != 0xbdf499u)
      return_pc = StackReturnPc(cpu, 1);
  }
  uint16_t handle;
  switch (return_pc) {
    case 0xbdf46du:
    case 0xbdf483u:
    case 0xbdf499u:
      handle = cpu_read16(cpu, 0, (uint16_t)(cpu->D + 0x82u));
      break;
    case 0x81d8b6u:
    case 0xb6bc8fu:
    case 0xb6c51du:
      handle = cpu->X;
      break;
    default: return 0;
  }
  EntityGraphics *entry = Graphics(handle);
  if (!entry || !entry->palette_owned) return 0;
  entry->palette_owned = 0;
  entry->palette_source = 0;
  entry->palette_selection = 0;
  cpu->A = cpu->X = (cpu->A >> 8) & 0x0eu;
  SetNZ(cpu, 0);
  return 1;
}

int Dkc1CoopGraphicsBeginTransaction(void) {
  if (s_transaction) return 0;
  s_saved = s_state;
  s_transaction = 1;
  return 1;
}

void Dkc1CoopGraphicsEndTransaction(int commit) {
  if (!s_transaction) return;
  if (!commit) s_state = s_saved;
  s_transaction = 0;
}

size_t Dkc1CoopGraphicsStateSize(void) { return sizeof s_state; }

int Dkc1CoopGraphicsSaveState(void *destination, size_t size) {
  if (!destination || size != sizeof s_state) return 0;
  memcpy(destination, &s_state, size);
  return 1;
}

static uint16_t ScenePaletteSelection(void) {
  unsigned entrance = Read16(0x3e);
  uint16_t source = entrance == 0x31 || entrance == 0x3c || entrance == 0x82 ||
                    entrance == 0x83 || entrance == 0xcd ? 0x8cecu : 0x8cb0u;
  return (uint16_t)(source + ((Read16(0x579) & 0x100u) ? 30u : 0u));
}

int Dkc1CoopGraphicsLoadState(const void *source, size_t size) {
  const size_t legacy_size = offsetof(GraphicsState, weather);
  if (!source || (size != sizeof s_state && size != legacy_size)) return 0;
  GraphicsState candidate = {0};
  memcpy(&candidate, source, size);
  if ((candidate.version != 1 || size != legacy_size) &&
      ((candidate.version < 2 || candidate.version > 4) || size != sizeof s_state)) return 0;
  if (candidate.weather.clearing > 1 || candidate.weather.reserved > 1 ||
      (candidate.version < 3 && candidate.weather.reserved)) return 0;
  /* The withdrawn bonus-lighting experiment used v3/reserved=1.
   * Keep its snapshots readable, discarding only that presentation marker. */
  candidate.weather.reserved = 0;
  for (unsigned i = 0; i < kEntityCount; ++i) {
    EntityGraphics *entry = &candidate.entities[i];
    if (entry->palette_owned > 1 || entry->reserved[0] || entry->reserved[1] ||
        (!entry->palette_owned && entry->palette_selection) ||
        (entry->palette_selection &&
         (candidate.version < 4 || (entry->palette_selection & 0xf8u) != 0x80u))) return 0;
    if (candidate.version < 4 && entry->palette_owned && ActiveHandle((uint16_t)(i * 2u))) {
      /* Older snapshots did not record explicit physical-row replacement.
       * B58424..B58465 uniquely selects these four scene palette sources.
       * Reconstruct that semantic selection only when its current native
       * selector agrees; do not repair CGRAM or guess other legacy owners. */
      if (entry->palette_source == ScenePaletteSelection())
        entry->palette_selection = (uint8_t)(0x80u | ((Flags((uint16_t)(i * 2u)) >> 9) & 7u));
    }
  }
  candidate.version = 4;
  s_state = candidate;
  return 1;
}
