#include "dkc1_margin_proxy.h"

#include "common_rtl.h"
#include "cpu_state.h"
#include "dkc1_video.h"
#include "funcs.h"
#include "snes/dma.h"
#include "snes/ppu.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
  kDkc1ProxyWramSize = 0x20000,
  kDkc1ProxyFirstActor = 0x02,
  kDkc1ProxyLastActor = 0x32,
  kDkc1ProxyActorCount = 0x19,
  kDkc1ProxyScratchActor = 0x32,
  kDkc1ProxyBookkeeping = 0x192b,
  kDkc1ProxySnapshotMagic = 0x3158504du, /* "MPX1" */
  kDkc1ProxySnapshotVersion = 1,
};

/* Exact normal-actor word tables from RAM_Map_DKC1.asm.  No gaps are treated
 * as actor-owned state. */
static const uint16_t kDkc1ProxyActorTables[] = {
  0x0a7du, 0x0ab1u, 0x0ae5u, 0x0b19u, 0x0b8du, 0x0bc1u,
  0x0c35u, 0x0c69u, 0x0cddu, 0x0d11u, 0x0d45u, 0x0db9u,
  0x0dedu, 0x0e21u, 0x0e55u, 0x0e89u, 0x0ebdu, 0x0ef1u,
  0x0f25u, 0x0f59u, 0x0f8du, 0x0fc1u, 0x0ff5u, 0x1029u,
  0x109du, 0x10d1u, 0x1105u, 0x1139u, 0x116du, 0x11a1u,
  0x11d5u, 0x1209u, 0x123du, 0x1271u, 0x12a5u, 0x12d9u,
  0x130du, 0x1341u, 0x1375u, 0x13e9u, 0x145du, 0x1491u,
  0x14c5u, 0x14f9u, 0x152du, 0x1561u, 0x1595u, 0x15c9u,
  0x15fdu, 0x1631u, 0x1665u,
};

enum {
  kDkc1ProxyActorWords =
      sizeof kDkc1ProxyActorTables / sizeof kDkc1ProxyActorTables[0],
  kDkc1ProxyWordCurrentOamZ = 0,
  /* $0AB1 is indexed like the actor tables but contains the global sorted
   * actor-index list.  It is not state owned by the actor whose numeric
   * index happens to equal this array offset. */
  kDkc1ProxyWordDrawOrder = 1,
  kDkc1ProxyWordDisplayedPose = 2,
  kDkc1ProxyWordX = 3,
  kDkc1ProxyWordOamZ = 4,
  kDkc1ProxyWordY = 5,
  kDkc1ProxyWordTable0C35 = 6,
  kDkc1ProxyWordYxppccct = 7,
  kDkc1ProxyWordTable0Cdd = 8,
  kDkc1ProxyWordCurrentPose = 9,
  kDkc1ProxyWordSpriteId = 10,
  kDkc1ProxyWordState = 23,
  kDkc1ProxyWordAnimation = 26,
};

typedef struct Dkc1MarginProxyCandidate {
  uint16_t mode;
  uint16_t level;
  uint16_t entrance;
  uint16_t source;
  uint16_t type;
  uint16_t x;
  uint16_t y;
  uint16_t initializer;
  uint16_t id;
} Dkc1MarginProxyCandidate;

#include "dkc1_margin_proxy_manifest.inc"

enum {
  kDkc1ProxyCandidateCount =
      sizeof kDkc1MarginProxyCandidates /
      sizeof kDkc1MarginProxyCandidates[0],
};

typedef struct Dkc1MarginProxyState {
  uint16_t words[kDkc1ProxyActorWords];
  uint32_t updatedFrame;
  uint8_t initialized;
  uint8_t rejected;
  uint8_t dead;
  uint8_t reserved;
} Dkc1MarginProxyState;

typedef struct Dkc1MarginProxySnapshot {
  uint32_t magic;
  uint32_t version;
  uint32_t size;
  uint32_t candidateCount;
  uint16_t mode;
  uint16_t level;
  uint16_t entrance;
  uint16_t contextValid;
  Dkc1MarginProxyState proxies[kDkc1ProxyCandidateCount];
} Dkc1MarginProxySnapshot;

typedef struct Dkc1MarginProxyRenderSlot {
  uint16_t actorIndex;
  uint16_t proxyIndex;
  uint16_t words[kDkc1ProxyActorWords];
} Dkc1MarginProxyRenderSlot;

static Dkc1MarginProxyState s_proxies[kDkc1ProxyCandidateCount];
static Dkc1MarginProxyRenderSlot s_render_slots[kDkc1ProxyActorCount];
static uint8_t s_transaction_wram[kDkc1ProxyWramSize];
static Ppu s_transaction_ppu;
static Dma s_transaction_dma;
static unsigned s_render_slot_count;
static uint16_t s_context_mode;
static uint16_t s_context_level;
static uint16_t s_context_entrance;
static bool s_context_valid;
static bool s_render_active;
static int s_enabled_cache = -1;
static int s_render_enabled_cache = -1;
static FILE *s_log;
static uint32_t s_unexpected_wram_offset = UINT32_MAX;

static uint16_t Read16(const uint8_t *memory, uint16_t address) {
  return (uint16_t)(memory[address] |
                    ((uint16_t)memory[(uint16_t)(address + 1u)] << 8));
}

static void Write16(uint8_t *memory, uint16_t address, uint16_t value) {
  memory[address] = (uint8_t)value;
  memory[(uint16_t)(address + 1u)] = (uint8_t)(value >> 8);
}

static void OpenLog(void) {
  if (s_log)
    return;
  const char *path = getenv("DKC1_MARGIN_PROXY_LOG");
  if (path && *path)
    s_log = fopen(path, "ab");
}

static void LogEvent(const char *event, unsigned proxy_index,
                     uint16_t actor_index, const char *reason) {
  const uint32_t unexpected_wram_offset = s_unexpected_wram_offset;
  s_unexpected_wram_offset = UINT32_MAX;
  OpenLog();
  if (!s_log)
    return;
  const Dkc1MarginProxyCandidate *candidate =
      &kDkc1MarginProxyCandidates[proxy_index];
  const Dkc1MarginProxyState *proxy = &s_proxies[proxy_index];
  const uint16_t layer_x = Read16(g_ram, 0x088bu);
  const uint16_t proxy_x = proxy->words[kDkc1ProxyWordX];
  const uint16_t proxy_y = proxy->words[kDkc1ProxyWordY];
  const uint16_t proxy_screen_x = (uint16_t)(proxy_x - layer_x);
  fprintf(s_log,
          "{\"schema\":\"dkc1.margin-proxy.v1\",\"frame\":%d,"
          "\"event\":\"%s\",\"mode\":%u,\"level\":%u,"
          "\"entrance\":%u,\"source\":%u,\"id\":%u,"
          "\"actor_index\":%u,\"reason\":\"%s\","
          "\"unexpected_wram_offset\":%u,"
          "\"candidate_x\":%u,\"candidate_y\":%u,"
          "\"layer_x\":%u,\"proxy_x\":%u,\"proxy_y\":%u,"
          "\"proxy_screen_x\":%d,\"proxy_current_oam_z\":%u,"
          "\"proxy_draw_order\":%u,\"proxy_displayed_pose\":%u,"
          "\"proxy_oam_z\":%u,\"proxy_table_0c35\":%u,"
          "\"proxy_yxppccct\":%u,\"proxy_table_0cdd\":%u,"
          "\"proxy_current_pose\":%u,\"proxy_sprite_id\":%u,"
          "\"proxy_state\":%u,\"proxy_animation\":%u,"
          "\"global_oam_index\":%u}\n",
          snes_frame_counter, event, candidate->mode, candidate->level,
          candidate->entrance, candidate->source, candidate->id, actor_index,
          reason ? reason : "", unexpected_wram_offset,
          candidate->x, candidate->y, layer_x, proxy_x, proxy_y,
          (int)(int16_t)proxy_screen_x,
          proxy->words[kDkc1ProxyWordCurrentOamZ],
          proxy->words[kDkc1ProxyWordDrawOrder],
          proxy->words[kDkc1ProxyWordDisplayedPose],
          proxy->words[kDkc1ProxyWordOamZ],
          proxy->words[kDkc1ProxyWordTable0C35],
          proxy->words[kDkc1ProxyWordYxppccct],
          proxy->words[kDkc1ProxyWordTable0Cdd],
          proxy->words[kDkc1ProxyWordCurrentPose],
          proxy->words[kDkc1ProxyWordSpriteId],
          proxy->words[kDkc1ProxyWordState],
          proxy->words[kDkc1ProxyWordAnimation],
          Read16(g_ram, 0x008eu));
  fflush(s_log);
}

bool Dkc1MarginProxyEnabled(void) {
  if (s_enabled_cache < 0) {
    const char *value = getenv("DKC1_MARGIN_PROXIES");
    s_enabled_cache = value && value[0] == '1' && value[1] == '\0';
  }
  return s_enabled_cache != 0 && Dkc1VideoIsWidescreen();
}

/* Diagnostic A/B lever: keep the native cartridge scanner selected while
 * suppressing only proxy simulation/injection.  Production defaults on; the
 * exact "0" value is used by regression tests to prove the transaction itself
 * cannot perturb guest state. */
static bool Dkc1MarginProxyRenderEnabled(void) {
  if (s_render_enabled_cache < 0) {
    const char *value = getenv("DKC1_MARGIN_PROXY_RENDER");
    s_render_enabled_cache =
        !(value && value[0] == '0' && value[1] == '\0');
  }
  return s_render_enabled_cache != 0;
}

void Dkc1MarginProxyReset(void) {
  memset(s_proxies, 0, sizeof s_proxies);
  memset(s_render_slots, 0, sizeof s_render_slots);
  s_render_slot_count = 0;
  s_render_active = false;
  s_context_mode = 0;
  s_context_level = 0;
  s_context_entrance = 0;
  s_context_valid = false;
}

static void SyncContext(const uint8_t *wram) {
  const uint16_t mode = Read16(wram, 0x0032u);
  const uint16_t level = Read16(wram, 0x0030u);
  const uint16_t entrance = Read16(wram, 0x003eu);
  if (s_context_valid && mode == s_context_mode && level == s_context_level &&
      entrance == s_context_entrance)
    return;
  Dkc1MarginProxyReset();
  s_context_mode = mode;
  s_context_level = level;
  s_context_entrance = entrance;
  s_context_valid = true;
}

static uint16_t ActorWord(const uint8_t *wram, uint16_t actor_index,
                          unsigned word) {
  return Read16(wram, (uint16_t)(kDkc1ProxyActorTables[word] + actor_index));
}

static void SetActorWord(uint8_t *wram, uint16_t actor_index, unsigned word,
                         uint16_t value) {
  Write16(wram, (uint16_t)(kDkc1ProxyActorTables[word] + actor_index), value);
}

static void CaptureActor(const uint8_t *wram, uint16_t actor_index,
                         uint16_t words[kDkc1ProxyActorWords]) {
  for (unsigned word = 0; word < kDkc1ProxyActorWords; word++)
    words[word] = ActorWord(wram, actor_index, word);
}

static void RestoreActor(uint8_t *wram, uint16_t actor_index,
                         const uint16_t words[kDkc1ProxyActorWords]) {
  for (unsigned word = 0; word < kDkc1ProxyActorWords; word++)
    SetActorWord(wram, actor_index, word, words[word]);
}

static bool ActorByteOwned(size_t offset, uint16_t actor_index) {
  for (unsigned word = 0; word < kDkc1ProxyActorWords; word++) {
    const size_t address = kDkc1ProxyActorTables[word] + actor_index;
    if (offset == address || offset == address + 1u)
      return true;
  }
  return false;
}

static bool TransactionWritesAreIsolated(const uint8_t *after,
                                         uint16_t actor_index) {
  s_unexpected_wram_offset = UINT32_MAX;
  for (size_t offset = 0; offset < kDkc1ProxyWramSize; offset++) {
    if (after[offset] == s_transaction_wram[offset])
      continue;
    /* Spawn scripts may enqueue sprite tiles and palettes while filling the
     * actor's pose.  These two buffers are presentation queues, and the full
     * transaction restores them before returning; accepting their transient
     * writes does not publish graphics state or advance gameplay. */
    if (offset < 0x0200u || (offset >= 0x08abu && offset <= 0x08acu) ||
        (offset >= 0x170fu && offset <= 0x1af2u) ||
        ActorByteOwned(offset, actor_index))
      continue;
    s_unexpected_wram_offset = (uint32_t)offset;
    return false;
  }
  return true;
}

typedef struct Dkc1MarginProxyExternalSnapshot {
  uint64_t mainCpuCycles;
  uint64_t apuPaceCycles;
  uint64_t apuLastSyncCycles;
  uint64_t apuLastSyncMaster;
  uint8_t lastHdmaen;
  bool havePpu;
  bool haveDma;
} Dkc1MarginProxyExternalSnapshot;

static void BeginTransaction(Dkc1MarginProxyExternalSnapshot *external) {
  memcpy(s_transaction_wram, g_ram, sizeof s_transaction_wram);
  memset(external, 0, sizeof *external);
  external->mainCpuCycles = g_main_cpu_cycles_estimate;
  external->apuPaceCycles = g_apu_pace_cycles_estimate;
  external->apuLastSyncCycles = g_apu_last_sync_cycles;
  external->apuLastSyncMaster = g_apu_last_sync_master;
  external->lastHdmaen = g_snesrecomp_last_hdmaen;
  if (g_ppu) {
    s_transaction_ppu = *g_ppu;
    external->havePpu = true;
  }
  if (g_dma) {
    s_transaction_dma = *g_dma;
    external->haveDma = true;
  }
}

static const char *ExternalStateDifference(
    const Dkc1MarginProxyExternalSnapshot *external) {
  switch (RtlSpeculativeExecutionViolation()) {
    case RTL_SPECULATIVE_VIOLATION_NESTED:
      return "speculative_nested";
    case RTL_SPECULATIVE_VIOLATION_MMIO_READ:
      return "speculative_mmio_read";
    case RTL_SPECULATIVE_VIOLATION_MMIO_WRITE:
      return "speculative_mmio_write";
    default:
      break;
  }
  if (external->havePpu &&
      memcmp(g_ppu, &s_transaction_ppu, sizeof s_transaction_ppu) != 0)
    return "ppu";
  if (external->haveDma &&
      memcmp(g_dma, &s_transaction_dma, sizeof s_transaction_dma) != 0)
    return "dma";
  if (g_main_cpu_cycles_estimate != external->mainCpuCycles)
    return "main_cpu_cycles";
  if (g_apu_pace_cycles_estimate != external->apuPaceCycles)
    return "apu_pace_cycles";
  if (g_apu_last_sync_cycles != external->apuLastSyncCycles)
    return "apu_sync_cycles";
  if (g_apu_last_sync_master != external->apuLastSyncMaster)
    return "apu_sync_master";
  if (g_snesrecomp_last_hdmaen != external->lastHdmaen)
    return "hdmaen";
  return NULL;
}

static void EndTransaction(
    const Dkc1MarginProxyExternalSnapshot *external) {
  memcpy(g_ram, s_transaction_wram, sizeof s_transaction_wram);
  if (external->havePpu)
    *g_ppu = s_transaction_ppu;
  if (external->haveDma)
    *g_dma = s_transaction_dma;
  g_main_cpu_cycles_estimate = external->mainCpuCycles;
  g_apu_pace_cycles_estimate = external->apuPaceCycles;
  g_apu_last_sync_cycles = external->apuLastSyncCycles;
  g_apu_last_sync_master = external->apuLastSyncMaster;
  g_snesrecomp_last_hdmaen = external->lastHdmaen;
}

static void PrepareScratchCpu(CpuState *scratch, const CpuState *source,
                              uint8_t bank) {
  *scratch = *source;
  scratch->D = 0;
  scratch->DB = 0x7e;
  scratch->PB = bank;
  scratch->m_flag = 0;
  scratch->x_flag = 0;
  scratch->P = (uint8_t)(scratch->P & ~0x30u);
  scratch->host_return_valid = 0;
}

static bool ValidateCandidateRecord(const Dkc1MarginProxyCandidate *candidate) {
  if (!g_rom || candidate->entrance >= 0x00e6u || candidate->type != 1u)
    return false;
  const uint8_t *pointer = RomPtr(0xbd8000u + candidate->entrance * 2u);
  if (!pointer)
    return false;
  const uint16_t table = (uint16_t)(pointer[0] | ((uint16_t)pointer[1] << 8));
  const uint16_t record = (uint16_t)(table + candidate->source * 8u);
  const uint8_t *bytes = RomPtr(0xbd0000u | record);
  if (!bytes)
    return false;
  const uint16_t type = (uint16_t)(bytes[0] | ((uint16_t)bytes[1] << 8));
  const uint16_t x = (uint16_t)(bytes[2] | ((uint16_t)bytes[3] << 8));
  const uint16_t y = (uint16_t)(bytes[4] | ((uint16_t)bytes[5] << 8));
  const uint16_t initializer =
      (uint16_t)(bytes[6] | ((uint16_t)bytes[7] << 8));
  return type == candidate->type && x == candidate->x && y == candidate->y &&
         initializer == candidate->initializer;
}

static bool InitializeProxy(CpuState *cpu, unsigned proxy_index) {
  Dkc1MarginProxyState *proxy = &s_proxies[proxy_index];
  const Dkc1MarginProxyCandidate *candidate =
      &kDkc1MarginProxyCandidates[proxy_index];
  if (proxy->initialized)
    return !proxy->rejected && !proxy->dead;
  proxy->initialized = 1;
  if (!ValidateCandidateRecord(candidate)) {
    proxy->rejected = 1;
    LogEvent("reject", proxy_index, 0, "manifest_record_mismatch");
    return false;
  }

  Dkc1MarginProxyExternalSnapshot external;
  BeginTransaction(&external);
  RtlSpeculativeExecutionBegin();
  for (unsigned word = 0; word < kDkc1ProxyActorWords; word++)
    SetActorWord(g_ram, kDkc1ProxyScratchActor, word, 0);
  Write16(g_ram, 0x0086u, kDkc1ProxyScratchActor);
  Write16(g_ram, (uint16_t)(0x15fdu + kDkc1ProxyScratchActor),
          candidate->source);
  Write16(g_ram, (uint16_t)(0x0b19u + kDkc1ProxyScratchActor), candidate->x);
  Write16(g_ram, (uint16_t)(0x0bc1u + kDkc1ProxyScratchActor), candidate->y);
  Write16(g_ram, (uint16_t)(0x0c35u + kDkc1ProxyScratchActor), 0x00ecu);

  CpuState scratch;
  PrepareScratchCpu(&scratch, cpu, 0xb5u);
  scratch.X = kDkc1ProxyScratchActor;
  scratch.Y = candidate->initializer;
  cpu_push_jsl_return_frame(&scratch);
  const RecompReturn result = CODE_B58052_M0X0(&scratch);
  const bool wram_isolated = TransactionWritesAreIsolated(
      g_ram, kDkc1ProxyScratchActor);
  const char *external_difference = ExternalStateDifference(&external);
  const bool external_isolated = external_difference == NULL;
  const bool isolated = wram_isolated && external_isolated;
  const uint16_t id = Read16(
      g_ram, (uint16_t)(0x0d45u + kDkc1ProxyScratchActor));
  if (result == RECOMP_RETURN_NORMAL && isolated && id == candidate->id)
    CaptureActor(g_ram, kDkc1ProxyScratchActor, proxy->words);
  RtlSpeculativeExecutionEnd();
  EndTransaction(&external);

  if (result != RECOMP_RETURN_NORMAL || !isolated || id != candidate->id) {
    proxy->rejected = 1;
    LogEvent("reject", proxy_index, 0,
             result != RECOMP_RETURN_NORMAL ? "initializer_nonlocal_return" :
             !wram_isolated ? "initializer_wram_side_effect" :
             !external_isolated ? external_difference :
                                  "initializer_id_mismatch");
    return false;
  }
  proxy->updatedFrame = UINT32_MAX;
  LogEvent("initialize", proxy_index, kDkc1ProxyScratchActor, "");
  return true;
}

static bool UpdateProxy(CpuState *cpu, unsigned proxy_index) {
  Dkc1MarginProxyState *proxy = &s_proxies[proxy_index];
  if (!proxy->initialized || proxy->rejected || proxy->dead)
    return false;
  if (proxy->updatedFrame == (uint32_t)snes_frame_counter)
    return true;

  Dkc1MarginProxyExternalSnapshot external;
  BeginTransaction(&external);
  RtlSpeculativeExecutionBegin();
  RestoreActor(g_ram, kDkc1ProxyScratchActor, proxy->words);
  Write16(g_ram, 0x0082u, kDkc1ProxyScratchActor);

  CpuState scratch;
  PrepareScratchCpu(&scratch, cpu, 0xbfu);
  scratch.A = Read16(
      g_ram, (uint16_t)(0x0d45u + kDkc1ProxyScratchActor));
  cpu_push_jsl_return_frame(&scratch);
  const RecompReturn result = CODE_BF8087_M0X0(&scratch);
  const bool wram_isolated = TransactionWritesAreIsolated(
      g_ram, kDkc1ProxyScratchActor);
  const char *external_difference = ExternalStateDifference(&external);
  const bool external_isolated = external_difference == NULL;
  const bool isolated = wram_isolated && external_isolated;
  const uint16_t id = Read16(
      g_ram, (uint16_t)(0x0d45u + kDkc1ProxyScratchActor));
  if (result == RECOMP_RETURN_NORMAL && isolated && id != 0)
    CaptureActor(g_ram, kDkc1ProxyScratchActor, proxy->words);
  RtlSpeculativeExecutionEnd();
  EndTransaction(&external);

  proxy->updatedFrame = (uint32_t)snes_frame_counter;
  if (result != RECOMP_RETURN_NORMAL || !isolated) {
    proxy->rejected = 1;
    LogEvent("reject", proxy_index, 0,
             result != RECOMP_RETURN_NORMAL ? "update_nonlocal_return" :
             !wram_isolated ? "update_wram_side_effect" :
                              external_difference);
    return false;
  }
  if (id == 0) {
    proxy->dead = 1;
    LogEvent("retire", proxy_index, 0, "actor_deleted_itself");
    return false;
  }
  return true;
}

static uint16_t ScannerLeft(uint16_t layer_x, uint16_t margin) {
  uint16_t left = (uint16_t)(layer_x - margin);
  if (left >= 0xfc00u)
    left = 0;
  return left;
}

static bool InWindow(uint16_t x, uint16_t left, uint16_t span) {
  const uint16_t right = (uint16_t)(left + span);
  return left < x && x <= right;
}

static bool CandidateNeedsProxy(const Dkc1MarginProxyCandidate *candidate) {
  if (candidate->mode != s_context_mode || candidate->level != s_context_level ||
      candidate->entrance != s_context_entrance || candidate->source >= 0x100u)
    return false;
  if (g_ram[kDkc1ProxyBookkeeping + candidate->source] != 0)
    return false;
  const uint16_t layer_x = Read16(g_ram, 0x088bu);
  const uint16_t stock_left = ScannerLeft(layer_x, 0x0020u);
  const int wide_margin = 0x20 + Dkc1VideoExtra() -
                          Dkc1VideoPresentationBias();
  if (wide_margin < 0 || wide_margin > 0xffff)
    return false;
  const uint16_t wide_left = ScannerLeft(layer_x, (uint16_t)wide_margin);
  const uint16_t wide_span =
      (uint16_t)(0x0140u + 2u * (unsigned)Dkc1VideoExtra());
  return InWindow(candidate->x, wide_left, wide_span) &&
         !InWindow(candidate->x, stock_left, 0x0140u);
}

void Dkc1MarginProxyBeginRender(struct CpuState *cpu) {
  s_render_slot_count = 0;
  s_render_active = false;
  if (!cpu || !Dkc1MarginProxyEnabled() ||
      !Dkc1MarginProxyRenderEnabled() || !Dkc1VideoTerrainReady())
    return;
  SyncContext(cpu->ram);

  uint16_t free_slots[kDkc1ProxyActorCount];
  unsigned free_count = 0;
  for (uint16_t actor_index = kDkc1ProxyFirstActor;
       actor_index <= kDkc1ProxyLastActor;
       actor_index = (uint16_t)(actor_index + 2u)) {
    if (Read16(g_ram, (uint16_t)(0x0d45u + actor_index)) == 0)
      free_slots[free_count++] = actor_index;
  }

  for (unsigned proxy_index = 0;
       proxy_index < kDkc1ProxyCandidateCount &&
       s_render_slot_count < free_count;
       proxy_index++) {
    const Dkc1MarginProxyCandidate *candidate =
        &kDkc1MarginProxyCandidates[proxy_index];
    if (!CandidateNeedsProxy(candidate) ||
        !InitializeProxy(cpu, proxy_index) ||
        !UpdateProxy(cpu, proxy_index))
      continue;

    Dkc1MarginProxyRenderSlot *slot =
        &s_render_slots[s_render_slot_count];
    slot->actorIndex = free_slots[s_render_slot_count];
    slot->proxyIndex = (uint16_t)proxy_index;
    CaptureActor(g_ram, slot->actorIndex, slot->words);
    RestoreActor(g_ram, slot->actorIndex, s_proxies[proxy_index].words);
    /* Preserve the draw-order list exactly.  The renderer discovers the
     * borrowed actor because the authentic list already contains every
     * normal-pool index; replacing this word with the scratch actor's zero
     * silently removed the borrowed slot from that list. */
    SetActorWord(g_ram, slot->actorIndex, kDkc1ProxyWordDrawOrder,
                 slot->words[kDkc1ProxyWordDrawOrder]);
    LogEvent("inject", proxy_index, slot->actorIndex, "");
    s_render_slot_count++;
  }
  s_render_active = s_render_slot_count != 0;
}

void Dkc1MarginProxyEndRender(struct CpuState *cpu) {
  (void)cpu;
  if (!s_render_active)
    return;
  for (unsigned i = 0; i < s_render_slot_count; i++) {
    Dkc1MarginProxyRenderSlot *slot = &s_render_slots[i];
    Dkc1MarginProxyState *proxy = &s_proxies[slot->proxyIndex];
    /* Preserve renderer-owned displayed-pose bookkeeping in host state. */
    const uint16_t proxy_draw_order =
        proxy->words[kDkc1ProxyWordDrawOrder];
    CaptureActor(g_ram, slot->actorIndex, proxy->words);
    proxy->words[kDkc1ProxyWordDrawOrder] = proxy_draw_order;
    RestoreActor(g_ram, slot->actorIndex, slot->words);
    LogEvent("restore", slot->proxyIndex, slot->actorIndex, "");
  }
  s_render_slot_count = 0;
  s_render_active = false;
}

size_t Dkc1MarginProxySnapshotSize(void) {
  return sizeof(Dkc1MarginProxySnapshot);
}

bool Dkc1MarginProxySnapshotSave(void *data, size_t size) {
  if (!data || size < sizeof(Dkc1MarginProxySnapshot) || s_render_active)
    return false;
  Dkc1MarginProxySnapshot snapshot;
  memset(&snapshot, 0, sizeof snapshot);
  snapshot.magic = kDkc1ProxySnapshotMagic;
  snapshot.version = kDkc1ProxySnapshotVersion;
  snapshot.size = sizeof snapshot;
  snapshot.candidateCount = kDkc1ProxyCandidateCount;
  snapshot.mode = s_context_mode;
  snapshot.level = s_context_level;
  snapshot.entrance = s_context_entrance;
  snapshot.contextValid = s_context_valid ? 1u : 0u;
  memcpy(snapshot.proxies, s_proxies, sizeof s_proxies);
  memcpy(data, &snapshot, sizeof snapshot);
  return true;
}

bool Dkc1MarginProxySnapshotLoad(const void *data, size_t size) {
  if (!data || size != sizeof(Dkc1MarginProxySnapshot))
    return false;
  Dkc1MarginProxySnapshot snapshot;
  memcpy(&snapshot, data, sizeof snapshot);
  if (snapshot.magic != kDkc1ProxySnapshotMagic ||
      snapshot.version != kDkc1ProxySnapshotVersion ||
      snapshot.size != sizeof snapshot ||
      snapshot.candidateCount != kDkc1ProxyCandidateCount ||
      snapshot.contextValid > 1u)
    return false;
  Dkc1MarginProxyReset();
  memcpy(s_proxies, snapshot.proxies, sizeof s_proxies);
  s_context_mode = snapshot.mode;
  s_context_level = snapshot.level;
  s_context_entrance = snapshot.entrance;
  s_context_valid = snapshot.contextValid != 0;
  return true;
}
