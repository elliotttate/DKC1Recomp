/* Exercise phase eligibility and native None dispatch with actual metadata.
 * The legacy co-op-off transaction remains a separate rollback contract. */
#include "dkc1_video.c"
#include <assert.h>

uint8 g_ram[0x20000];
static uint8 source_rom[0x10000];
static int enabled, second_available, before_calls, after_calls;
static uint16_t second_x, second_y;

int Dkc1CoopEnabled(void) { return enabled; }
void Dkc1CoopActorAllocated(uint16_t h) {}
void Dkc1CoopActorCreated(CpuState *c,uint16_t h) {}
void Dkc1CoopCrateHit(const uint8_t *r,uint16_t h) {}
uint16_t Dkc1CoopCompanionBinding(void){return 0;}
void Dkc1CoopRestoreCompanionBinding(uint16_t b) {}

uint16_t Dkc1CoopActorPaletteOwner(const uint8_t *ram, uint16_t h) { return h; }
void Dkc1CoopBeforeActor(uint8_t *ram, uint32_t slot, uint16_t dp) { ++before_calls; }
void Dkc1CoopAfterActor(uint8_t *ram) { ++after_calls; }
int Dkc1CoopWorldGetCamera(const uint8_t *ram, int view, uint16_t *x, uint16_t *y) {
  assert(view == 1);
  if (!second_available) return 0;
  *x = second_x; *y = second_y;
  return 1;
}

bool Dkc1MarginProxyEnabled(void) { return false; }
void Dkc1MarginProxyReset(void) {}
size_t Dkc1MarginProxySnapshotSize(void) { return 0; }
bool Dkc1MarginProxySnapshotSave(void *p, size_t n) { return true; }
bool Dkc1MarginProxySnapshotLoad(const void *p, size_t n) { return true; }
void Dkc1DebugTracePlacedActorContext(uint16_t m, uint16_t l, uint16_t e) {}
void Dkc1DebugTracePlacedActorPhase(const char *event, uint16_t index, uint16_t id,
    uint16_t source, uint16_t x, uint16_t left, uint16_t right,
    uint16_t stock_left, uint16_t stock_right, bool ready) {}
void Dkc1DebugTracePrefetchTransaction(uint16_t index, uint16_t id, uint16_t source,
    const uint8_t *before, const uint8_t *after, size_t size) {}

uint8 cpu_read8(CpuState *cpu, uint8 bank, uint16 address) {
  return cpu->open_bus = bank == 0xbd ? source_rom[address] : cpu->ram[address];
}
uint16 cpu_read16(CpuState *cpu, uint8 bank, uint16 address) {
  uint16 v = cpu_read8(cpu, bank, address);
  return (uint16)(v | (uint16)cpu_read8(cpu, bank, (uint16)(address + 1)) << 8);
}
void cpu_write8(CpuState *cpu, uint8 bank, uint16 address, uint8 value) {
  assert(bank != 0xbd);
  cpu->ram[address] = value; cpu->open_bus = value;
}
void cpu_write16(CpuState *cpu, uint8 bank, uint16 address, uint16 value) {
  cpu_write8(cpu, bank, address, (uint8)value);
  cpu_write8(cpu, bank, (uint16)(address + 1), (uint8)(value >> 8));
}
static void Word(uint8_t *ram, uint16_t address, uint16_t value) {
  ram[address] = (uint8_t)value;
  ram[(uint16_t)(address + 1)] = (uint8_t)(value >> 8);
}
static void Setup(CpuState *cpu, int coop, unsigned mode) {
  enabled = coop; second_available = coop;
  second_x = 0x200; second_y = 0x100;
  before_calls = after_calls = 0;
  memset(g_ram, 0, sizeof g_ram);
  memset(source_rom, 0, sizeof source_rom);
  memset(cpu, 0, sizeof *cpu); cpu->ram = g_ram; cpu->X = 6; cpu->A = 5;
  Dkc1CoopEntitiesReset(); Dkc1CoopGraphicsReset();
  (void)Dkc1CoopEntitiesActive();
  Dkc1VideoSetWidescreen(true); Dkc1VideoResetPlacedActorPhases();
  Dkc1VideoSetTerrainReady(true);
  Word(g_ram, 0x32, (uint16_t)mode);
  Word(g_ram, 0x4a, 0x1000);
  Word(g_ram, 0x82, 6);
  /* Primary wide scanner [1B5,34B] reconstructs stock [1E0,320]. */
  Word(g_ram, 0xef, 0x1b5); Word(g_ram, 0xf1, 0x34b);
  Word(source_rom, 0x8000, 0x9000);
  Word(source_rom, 0x9010, 1);
  Word(source_rom, 0x9012, 0x340);
  Word(source_rom, 0x9014, 0xf00);
  Dkc1VideoObserveActorPool(g_ram); /* Empty frame boundary, then allocation. */
  Word(g_ram, 0xd4b, 5); Word(g_ram, 0x1603, 2);
}

int main(void) {
#ifdef _WIN32
  _putenv_s("DKC1_PREFETCH_PHASE_GUARD", "1");
  _putenv_s("DKC1_PREFETCH_TRANSACTION_DEBUG", "0");
#else
  setenv("DKC1_PREFETCH_PHASE_GUARD", "1", 1);
  setenv("DKC1_PREFETCH_TRANSACTION_DEBUG", "0", 1);
#endif
  CpuState cpu;
  Setup(&cpu, 0, 1);
  assert(!Dkc1VideoShouldRunPlacedActor(&cpu));
  Word(source_rom, 0x9012, 0x320);
  assert(Dkc1VideoShouldRunPlacedActor(&cpu));

  Setup(&cpu, 1, 1);
  assert(!Dkc1VideoShouldRunPlacedActor(&cpu)); /* Neither stock window owns it. */
  second_x = 0x360; /* Exactly on P2's strict left edge: still ineligible. */
  assert(!Dkc1VideoShouldRunPlacedActor(&cpu));
  second_x = 0x220; /* On P2's inclusive right edge. */
  assert(Dkc1VideoShouldRunPlacedActor(&cpu));
  second_available = 0;
  assert(Dkc1VideoShouldRunPlacedActor(&cpu)); /* A started actor stays started. */
  Word(g_ram, 0xd4b, 0); Dkc1VideoObserveActorPool(g_ram);
  Word(g_ram, 0xd4b, 5);
  assert(!Dkc1VideoShouldRunPlacedActor(&cpu)); /* Reused identity cannot inherit it. */

  Setup(&cpu, 1, 3);
  second_x = 0x220;
  Word(source_rom, 0x9014, 0x800);
  assert(!Dkc1VideoShouldRunPlacedActor(&cpu)); /* Far down the vertical room. */
  Word(source_rom, 0x9014, 0xf00);
  assert(Dkc1VideoShouldRunPlacedActor(&cpu));

  Setup(&cpu, 1, 1);
  assert(!Dkc1VideoShouldRunPlacedActor(&cpu));
  Dkc1VideoSetTerrainReady(false);
  Word(g_ram, 0xef, 0x1e0); Word(g_ram, 0xf1, 0x320);
  assert(!Dkc1VideoShouldRunPlacedActor(&cpu)); /* Soft fallback preserves hold. */
  second_x = 0x220;
  assert(Dkc1VideoShouldRunPlacedActor(&cpu));

  Setup(&cpu, 1, 1);
  Dkc1VideoResetPlacedActorPhases(); Dkc1VideoObserveActorPool(g_ram);
  assert(Dkc1VideoShouldRunPlacedActor(&cpu)); /* Imported preexisting actors trusted. */

  Setup(&cpu, 1, 1);
  const size_t entity_size = Dkc1CoopEntitiesStateSize();
  const size_t graphics_size = Dkc1CoopGraphicsStateSize();
  void *entities = malloc(entity_size), *graphics = malloc(graphics_size);
  void *actual = malloc(entity_size > graphics_size ? entity_size : graphics_size);
  uint8_t *wram = malloc(sizeof g_ram);
  assert(entities && graphics && actual && wram);
  assert(Dkc1CoopEntitiesSaveState(entities, entity_size));
  assert(Dkc1CoopGraphicsSaveState(graphics, graphics_size));
  memcpy(wram, g_ram, sizeof g_ram);
  assert(Dkc1VideoBeginPlacedActorDispatch(&cpu));
  assert(cpu.A == 0 && !before_calls && !s_prefetch_dispatch_active);
  /* A deadline/save before End must already see canonical serializable state. */
  assert(!memcmp(wram, g_ram, sizeof g_ram));
  assert(Dkc1CoopEntitiesSaveState(actual, entity_size));
  assert(!memcmp(entities, actual, entity_size));
  assert(Dkc1CoopGraphicsSaveState(actual, graphics_size));
  assert(!memcmp(graphics, actual, graphics_size));
  Dkc1VideoEndPlacedActorDispatch(&cpu);

  /* A nested metadata owner cannot make the pending actor run unprotected.
   * A remains the dispatch ID until BF8087, so zero routes to native None. */
  cpu.A = 5;
  assert(Dkc1CoopEntitiesBeginTransaction());
  assert(Dkc1VideoBeginPlacedActorDispatch(&cpu));
  assert(cpu.A == 0 && cpu_read16(&cpu, 0, 0xd4b) == 5);
  Dkc1VideoEndPlacedActorDispatch(&cpu);
  assert(!Dkc1CoopEntitiesBeginTransaction()); /* Outer ownership survived. */
  Dkc1CoopEntitiesEndTransaction(0);
  cpu.A = 5;
  assert(Dkc1CoopGraphicsBeginTransaction());
  assert(Dkc1VideoBeginPlacedActorDispatch(&cpu));
  assert(cpu.A == 0 && cpu_read16(&cpu, 0, 0xd4b) == 5);
  Dkc1VideoEndPlacedActorDispatch(&cpu);
  assert(Dkc1CoopEntitiesBeginTransaction()); /* No partial entity snapshot left. */
  Dkc1CoopEntitiesEndTransaction(0);
  assert(!Dkc1CoopGraphicsBeginTransaction());
  Dkc1CoopGraphicsEndTransaction(0);

  Setup(&cpu, 0, 1);
  memcpy(wram, g_ram, sizeof g_ram);
  assert(Dkc1VideoBeginPlacedActorDispatch(&cpu));
  assert(cpu.A == 5 && s_prefetch_dispatch_active);
  Word(g_ram, 0x159b, 0x40);
  Dkc1VideoEndPlacedActorDispatch(&cpu);
  assert(!after_calls && !memcmp(wram, g_ram, sizeof g_ram));

  free(entities); free(graphics); free(actual); free(wram);
  return 0;
}
