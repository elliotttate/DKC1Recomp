#include "dkc1_coop_entities.h"

#include "cpu_state.h"
#include "dkc1_coop_mod.h"
#include "dkc1_coop_graphics.h"

#include <string.h>

/* Byte-grounded normal-actor tables, also enumerated by the existing margin
 * proxy. $0AB1 is the global draw-order array, not actor-owned state; it still
 * needs extra indexed cells when handles are added. Gaps are never copied.
 * This ascending order is also ENT1's historical field-row identity. */
static const uint16_t kTables[] = {
#define COOP_ACTOR_FIELD(base) base,
#include "dkc1_coop_fields.def"
#undef COOP_ACTOR_FIELD
};
enum { kTableCount = sizeof kTables / sizeof kTables[0] };

enum {
  kEntityWireMagic = 0x31544e45u, /* ENT1 */
  kEntityWireVersion = 1,
  kEntityWireV1LegacyBytes = 13240,
  kEntityWireV1Bytes = 13272,
  kEntityWirePrefixBytes = 80,
  kEntityWireEffectViewBytes = 32,
  kEntityWireFieldsBytes = 13158, /* 51 tables, each 258 bytes. */
};

typedef struct EntityStorage {
  uint32_t magic;
  uint32_t version;
  uint32_t reserved;
  uint32_t active;
  uint8_t pool[kDkc1CoopEntityExtraCount];
  uint8_t effect_view[kEntityWireEffectViewBytes];
  /* Full byte offsets allow the cartridge's high-byte and unaligned accesses
   * ($0C6A,X, $1208,X, etc.) without changing their word semantics. */
  uint8_t fields[kTableCount][0x102];
} EntityStorage;

/* Both v1 shapes have two trailing alignment bytes. Legacy migration has
 * always discarded that old padding while current payloads retain it. */
typedef struct EntityWireV1Legacy {
  uint32_t magic, version, reserved, active;
  uint8_t pool[64];
  uint8_t fields[51][258];
} EntityWireV1Legacy;
typedef char EntityWireTableCount[kTableCount == 51 ? 1 : -1];
typedef char EntityWirePrefixLayout[offsetof(EntityStorage, effect_view) == kEntityWirePrefixBytes ? 1 : -1];
typedef char EntityWireFieldsLayout[offsetof(EntityStorage, fields) == 112 && sizeof(((EntityStorage *)0)->fields) == kEntityWireFieldsBytes ? 1 : -1];
typedef char EntityWireV1Layout[sizeof(EntityStorage) == kEntityWireV1Bytes ? 1 : -1];
typedef char EntityWireV1LegacyLayout[offsetof(EntityWireV1Legacy, fields) == kEntityWirePrefixBytes && sizeof(EntityWireV1Legacy) == kEntityWireV1LegacyBytes ? 1 : -1];

static EntityStorage s_entities;
static EntityStorage s_transaction;
static int s_transaction_active;
static uint16_t s_transaction_companion;

int Dkc1CoopEntitiesActive(void) {
  if (!s_entities.active && Dkc1CoopEnabled()) {
    s_entities.active = 1;
    Dkc1CoopGraphicsSeedNative();
  }
  return s_entities.active != 0;
}

static int Extended(uint16_t handle) {
  return handle >= kDkc1CoopEntityExtraFirst &&
         handle <= kDkc1CoopEntityExtraLast && !(handle & 1u);
}

/* One resolver serves dynamic interpreter recognition and all indexed cells.
 * Adjacent byte operands retain the field's wire row and signed displacement. */
static int FieldBinding(uint16_t base, int *displacement) {
  unsigned first = 0, end = kTableCount;
  while (first < end) {
    unsigned row = first + (end - first) / 2u;
    if (base < kTables[row] - 1u) end = row;
    else if (base > kTables[row] + 1u) first = row + 1u;
    else {
      if (displacement) *displacement = (int)base - (int)kTables[row];
      return (int)row;
    }
  }
  return -1;
}

int Dkc1CoopEntityIsField(uint16_t base) {
  return FieldBinding(base, NULL) >= 0;
}

static uint8_t *Cell(uint16_t base, uint16_t index) {
  if (index < 0x80u || index > 0xffu)
    return NULL;
  int displacement, row = FieldBinding(base, &displacement);
  if (row < 0) return NULL;
  int offset = (int)index + displacement;
  if (offset >= 0 && offset < 0x102)
    return &s_entities.fields[row][offset];
  return NULL;
}

static int RamBank(uint8_t bank) {
  return bank == 0x7e || (bank & 0x40u) == 0;
}

unsigned Dkc1CoopEffectView(uint16_t handle) {
  return handle >= 0x34 && handle < 0x74 && !(handle & 1u) ?
      s_entities.effect_view[(handle - 0x34) >> 1] : 0;
}

static void NoteEffectId(const uint8_t *ram, uint16_t base,
                         uint16_t handle, uint8_t id) {
  if (base==0x1595u && id && Dkc1CoopEntitiesActive())
    Dkc1CoopCrateHit(ram,handle);
  if (base == 0x0d45 && handle >= 0x34 && handle < 0x74 &&
      !(handle & 1u) && Dkc1CoopEntitiesActive())
    s_entities.effect_view[(handle - 0x34) >> 1] =
        Dkc1CoopEffectUsesOwnView(id) && ram[0x056f] == 2;
}

int Dkc1CoopEntityIsHandle(uint16_t handle) {
  return (!(handle & 1u) && handle >= 2u && handle <= 0x32u) ||
         (Dkc1CoopEntitiesActive() && Extended(handle));
}

uint16_t Dkc1CoopEntityNextHandle(uint16_t handle) {
  uint16_t next = (uint16_t)(handle + 2u);
  return Dkc1CoopEntityLoopIndex(next);
}

uint16_t Dkc1CoopEntityPreviousHandle(uint16_t handle) {
  if (Dkc1CoopEntitiesActive() && handle == 0x80u)
    return 0x32u;
  return (uint16_t)(handle - 2u);
}

uint16_t Dkc1CoopEntityLastHandle(void) {
  return Dkc1CoopEntitiesActive() ? 0xfeu : 0x32u;
}

uint16_t Dkc1CoopEntityLoopIndex(uint16_t index) {
  return Dkc1CoopEntitiesActive() && index == 0x34u ? 0x80u : index;
}

uint16_t Dkc1CoopEntityLiveLoopIndex(uint16_t index) {
  if (!Dkc1CoopEntitiesActive())
    return index;
  if (index == 0x34u)
    index = 0x80u;
  /* Only five ID-guarded dispatch/search loops use this helper. Native
   * slots keep their original timing; empty host capacity is not guest work.
   * Read live state each time so an earlier actor may spawn a later one. */
  while (Extended(index)) {
    const uint8_t *id = Cell(0x0d45u, index);
    if (id[0] || id[1])
      break;
    index = (uint16_t)(index + 2u);
  }
  return index;
}

uint16_t Dkc1CoopEntityBackwardIndex(uint16_t index) {
  return Dkc1CoopEntitiesActive() && index == 0x7eu ? 0x32u : index;
}

uint16_t Dkc1CoopEntityPoolLoopIndex(uint16_t index, unsigned native_pool_end) {
  if (!Dkc1CoopEntitiesActive())
    return index;
  if (index == native_pool_end)
    index = 0x80u;
  while (Extended(index) &&
         s_entities.pool[(index - 0x80u) / 2u] != native_pool_end)
    index = (uint16_t)(index + 2u);
  return index;
}

uint16_t Dkc1CoopEntityLoopEnd(uint16_t native_end) {
  return Dkc1CoopEntitiesActive() ? 0x100u : native_end;
}

uint16_t Dkc1CoopEntityPoolBackwardIndex(uint16_t index, unsigned native_pool_end) {
  if (!Dkc1CoopEntitiesActive()) return index;
  while (Extended(index) &&
         s_entities.pool[(index - 0x80u) / 2u] != native_pool_end)
    index = (uint16_t)(index - 2u);
  return index == 0x7eu ? (uint16_t)(native_pool_end - 2u) : index;
}

void Dkc1CoopEntityCollisionCursor(struct CpuState *cpu) {
  if (!Dkc1CoopEntitiesActive()) return;
  /* $BBA594/$BBA66A decrement $88 before testing candidates. Their Kong-only
   * callers start at 4/6 and stay native; $BBA58D/$BBA663 search pool $1E.
   * Rejoin that pool at $1C, never the effect pool or unrelated $1E..$32. */
  uint16_t address = (uint16_t)(cpu->D + 0x88u);
  uint16_t index = cpu_read16(cpu, 0, address);
  uint16_t next = Dkc1CoopEntityPoolBackwardIndex(index, 0x1e);
  if (index != next) cpu_write16(cpu, 0, address, next);
}

uint16_t Dkc1CoopEntityRead16(const uint8_t *wram, uint16_t base,
                           uint16_t handle) {
  const uint8_t *p = Dkc1CoopEntitiesActive() ? Cell(base, handle) : NULL;
  uint16_t address = (uint16_t)(base + handle);
  if (!p)
    return (uint16_t)(wram[address] |
                      ((uint16_t)wram[(uint16_t)(address + 1u)] << 8));
  return (uint16_t)(p[0] | ((uint16_t)p[1] << 8));
}

void Dkc1CoopEntityWrite16(uint8_t *wram, uint16_t base, uint16_t handle,
                         uint16_t value) {
  NoteEffectId(wram, base, handle, (uint8_t)value);
  uint8_t *p = Dkc1CoopEntitiesActive() ? Cell(base, handle) : NULL;
  uint16_t address = (uint16_t)(base + handle);
  if (!p) {
    wram[address] = (uint8_t)value;
    wram[(uint16_t)(address + 1u)] = (uint8_t)(value >> 8);
  } else {
    p[0] = (uint8_t)value;
    p[1] = (uint8_t)(value >> 8);
  }
}

uint8_t Dkc1CoopIndexedRead8(CpuState *cpu, uint8_t bank,
                          uint16_t base, uint16_t index) {
  uint8_t *p = Dkc1CoopEntitiesActive() && RamBank(bank) ? Cell(base, index) : NULL;
  uint32_t address = ((uint32_t)bank << 16) + base + index;
  if (!p)
    return cpu_read8(cpu, (uint8_t)(address >> 16), (uint16_t)address);
  cpu->open_bus = p[0];
  return p[0];
}

uint16_t Dkc1CoopIndexedRead16(CpuState *cpu, uint8_t bank,
                            uint16_t base, uint16_t index) {
  uint8_t *p = Dkc1CoopEntitiesActive() && RamBank(bank) ? Cell(base, index) : NULL;
  uint32_t address = ((uint32_t)bank << 16) + base + index;
  if (!p)
    return cpu_read16(cpu, (uint8_t)(address >> 16), (uint16_t)address);
  cpu->open_bus = p[1];
  return (uint16_t)(p[0] | ((uint16_t)p[1] << 8));
}

void Dkc1CoopIndexedWrite8(CpuState *cpu, uint8_t bank,
                         uint16_t base, uint16_t index, uint8_t value) {
  if (RamBank(bank)) NoteEffectId(cpu->ram, base, index, value);
  uint8_t *p = Dkc1CoopEntitiesActive() && RamBank(bank) ? Cell(base, index) : NULL;
  uint32_t address = ((uint32_t)bank << 16) + base + index;
  if (!p) {
    cpu_write8(cpu, (uint8_t)(address >> 16), (uint16_t)address, value);
    return;
  }
  p[0] = value;
  cpu->open_bus = value;
}

void Dkc1CoopIndexedWrite16(CpuState *cpu, uint8_t bank,
                          uint16_t base, uint16_t index, uint16_t value) {
  if (RamBank(bank)) NoteEffectId(cpu->ram, base, index, (uint8_t)value);
  if (Dkc1CoopEntitiesActive() && RamBank(bank) && base==0x0d45u &&
      value==0x0du && Dkc1CoopEntityIsHandle(index))
    Dkc1CoopActorCreated(cpu,index);
  uint8_t *p = Dkc1CoopEntitiesActive() && RamBank(bank) ? Cell(base, index) : NULL;
  uint32_t address = ((uint32_t)bank << 16) + base + index;
  if (!p) {
    cpu_write16(cpu, (uint8_t)(address >> 16), (uint16_t)address, value);
    return;
  }
  p[0] = (uint8_t)value;
  p[1] = (uint8_t)(value >> 8);
  cpu->open_bus = p[1];
}

int Dkc1CoopEntityAllocate(CpuState *cpu, unsigned native_pool_end) {
  uint16_t handle;
  if (!cpu || !Dkc1CoopEntitiesActive())
    return 0;
  for (handle = 0x80; handle <= 0xfe; handle += 2) {
    if (Dkc1CoopEntityRead16(cpu->ram, 0x0d45, handle) == 0) {
      s_entities.pool[(handle - 0x80u) / 2u] = (uint8_t)native_pool_end;
      cpu->X = handle;
      return 1;
    }
  }
  return 0;
}

void Dkc1CoopEntityAllocated(CpuState *cpu) {
  if (Dkc1CoopEntitiesActive() && cpu && Dkc1CoopEntityIsHandle(cpu->X))
    { Dkc1CoopGraphicsResetEntity(cpu->X); Dkc1CoopActorAllocated(cpu->X); }
}

void Dkc1CoopEntityScriptWrite16(CpuState *cpu, uint16_t value) {
  /* $B6E85F adds the scripted actor handle ($50) to a literal field before
   * TAX. Recover that proven base here, without changing direct-page reads. */
  uint16_t address = (uint16_t)(cpu->D + cpu->X);
  if (Dkc1CoopEntitiesActive()) {
    uint16_t dp = (uint16_t)(cpu->D + 0x50u);
    uint16_t handle = (uint16_t)(cpu->ram[dp] |
        ((uint16_t)cpu->ram[(uint16_t)(dp + 1u)] << 8));
    uint16_t base = (uint16_t)(address - handle);
    if (!Extended(handle) && base==0x0d45u && value==0x0du && Dkc1CoopEntityIsHandle(handle))
      Dkc1CoopActorCreated(cpu,handle);
    if (Extended(handle) && Cell(base, handle)) {
      Dkc1CoopIndexedWrite16(cpu, 0, base, handle, value);
      return;
    }
  }
  cpu_write16(cpu, 0, address, value);
}

void Dkc1CoopEntityClearRange(uint16_t base, uint16_t size) {
  unsigned i;
  if (!Dkc1CoopEntitiesActive())
    return;
  for (i = 0; i < kTableCount; ++i) {
    /* A native bulk clear which covers the complete actor table also clears
     * its host continuation. Partial/scratch ranges cannot reach this path. */
    if (base <= kTables[i] &&
        (uint32_t)base + size >= (uint32_t)kTables[i] + 0x34u)
      memset(s_entities.fields[i], 0, sizeof s_entities.fields[i]);
  }
}


void Dkc1CoopEntitiesReset(void) {
  uint16_t handle;
  memset(&s_entities, 0, sizeof s_entities);
  s_entities.magic = kEntityWireMagic;
  s_entities.version = kEntityWireVersion;
  s_transaction_active = 0;
  /* Old cartridge states have no continuation of the draw-order table. */
  for (handle = 0x80; handle <= 0xfe; handle += 2) {
    uint8_t *p = Cell(0x0ab1, handle);
    p[0] = (uint8_t)handle;
  }
}

void Dkc1CoopEntitiesPoolReset(void) {
  if (!Dkc1CoopEnabled()) {
    Dkc1CoopEntitiesReset();
    Dkc1CoopGraphicsReset();
  }
}

int Dkc1CoopEntitiesBeginTransaction(void) {
  if (s_transaction_active)
    return 0;
  s_transaction_companion=Dkc1CoopCompanionBinding();
  s_transaction = s_entities;
  s_transaction_active = 1;
  return 1;
}

void Dkc1CoopEntitiesEndTransaction(int commit) {
  if (!s_transaction_active)
    return;
  if (!commit) {
    s_entities = s_transaction;
    Dkc1CoopRestoreCompanionBinding(s_transaction_companion);
  }
  s_transaction_active = 0;
}

size_t Dkc1CoopEntitiesStateSize(void) { return kEntityWireV1Bytes; }

int Dkc1CoopEntitiesSaveState(void *destination, size_t size) {
  if (!destination || size != kEntityWireV1Bytes || s_transaction_active)
    return 0;
  s_entities.magic = kEntityWireMagic;
  s_entities.version = kEntityWireVersion;
  memcpy(destination, &s_entities, kEntityWireV1Bytes);
  return 1;
}

int Dkc1CoopEntitiesLoadState(const void *source, size_t size) {
  uint32_t header[2];
  if (!source || (size != kEntityWireV1Bytes && size != kEntityWireV1LegacyBytes) ||
      s_transaction_active)
    return 0;
  memcpy(header, source, sizeof header);
  if (header[0] != kEntityWireMagic || header[1] != kEntityWireVersion)
    return 0;
  if (size == kEntityWireV1LegacyBytes) {
    /* Early DWS8 v3 snapshots persisted these actors before flight ownership
     * was added. Preserve their fields and default only the missing owners. */
    memset(&s_entities, 0, sizeof s_entities);
    memcpy(&s_entities, source, kEntityWirePrefixBytes);
    memcpy(s_entities.fields, (const uint8_t *)source + kEntityWirePrefixBytes,
           kEntityWireFieldsBytes);
  } else {
    memcpy(&s_entities, source, kEntityWireV1Bytes);
  }
  return 1;
}
