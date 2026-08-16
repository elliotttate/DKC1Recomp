#include "dkc1_video.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cpu_state.h"
#include "dkc1_debug_dump.h"
#include "dkc1_margin_proxy.h"

bool g_ws_active;
int g_ws_extra;
static bool s_terrain_ready;
static int s_presentation_bias;

typedef struct Dkc1PlacedActorPhase {
  uint16_t id;
  uint16_t source;
  bool stock_started;
  bool suppression_reported;
  bool fallback_hold_reported;
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
static uint16_t Dkc1ReadWram16(const uint8_t *wram, uint16_t address);
static uint16_t s_prefetch_actor_index;
static bool s_prefetch_transaction_detail_reported[0x1a];

typedef struct Dkc1StreamCoverage {
  uint16_t mode;
  uint16_t level;
  uint16_t entrance;
  uint64_t columns;
  uint8_t unique_columns;
  uint8_t required_columns;
  uint16_t last_layer_x;
  uint16_t last_selected_x;
  uint32_t initial_count_calls;
  uint32_t initial_count_rejected;
  uint32_t selector_calls;
  uint32_t observed_columns;
  bool context_valid;
  bool ready;
} Dkc1StreamCoverage;

static Dkc1StreamCoverage s_stream_coverage;

typedef struct Dkc1WideRowBuild {
  uint16_t original_layer_x;
  uint8_t kind;   /* 1 = standard $81890E, 2 = alternate $818CEF. */
  uint8_t phase;  /* 1 = left pass, 2 = right pass. */
} Dkc1WideRowBuild;

static Dkc1WideRowBuild s_wide_row_build;

enum {
  kDkc1VideoSnapshotMagic = 0x31535644u, /* "DVS1" */
  kDkc1VideoSnapshotVersion = 2,
  kDkc1MarginProxySnapshotCapacity = 2048,
};

typedef struct Dkc1VideoHostSnapshotV1 {
  uint32_t magic;
  uint32_t version;
  uint32_t size;
  uint8_t terrainReady;
  uint8_t phasesSeeded;
  uint8_t contextValid;
  uint8_t reserved;
  int32_t presentationBias;
  Dkc1PlacedActorPhase phases[0x1a];
  Dkc1PlacedActorContext context;
  Dkc1StreamCoverage streamCoverage;
} Dkc1VideoHostSnapshotV1;

typedef struct Dkc1VideoHostSnapshot {
  Dkc1VideoHostSnapshotV1 legacy;
  uint32_t marginProxySize;
  uint8_t marginProxy[kDkc1MarginProxySnapshotCapacity];
} Dkc1VideoHostSnapshot;

size_t Dkc1VideoSnapshotSize(void) {
  return sizeof(Dkc1VideoHostSnapshot);
}

bool Dkc1VideoSnapshotSave(void *data, size_t size) {
  if (!data || size < sizeof(Dkc1VideoHostSnapshot))
    return false;
  Dkc1VideoHostSnapshot snapshot;
  memset(&snapshot, 0, sizeof snapshot);
  snapshot.legacy.magic = kDkc1VideoSnapshotMagic;
  snapshot.legacy.version = kDkc1VideoSnapshotVersion;
  snapshot.legacy.size = sizeof snapshot;
  snapshot.legacy.terrainReady = s_terrain_ready ? 1u : 0u;
  snapshot.legacy.phasesSeeded = s_placed_actor_phases_seeded ? 1u : 0u;
  snapshot.legacy.contextValid = s_placed_actor_context_valid ? 1u : 0u;
  snapshot.legacy.presentationBias = s_presentation_bias;
  memcpy(snapshot.legacy.phases, s_placed_actor_phases,
         sizeof snapshot.legacy.phases);
  snapshot.legacy.context = s_placed_actor_context;
  snapshot.legacy.streamCoverage = s_stream_coverage;
  snapshot.marginProxySize = (uint32_t)Dkc1MarginProxySnapshotSize();
  if (snapshot.marginProxySize > sizeof snapshot.marginProxy ||
      !Dkc1MarginProxySnapshotSave(snapshot.marginProxy,
                                   snapshot.marginProxySize))
    return false;
  memcpy(data, &snapshot, sizeof snapshot);
  return true;
}

bool Dkc1VideoSnapshotLoad(const void *data, size_t size) {
  if (!data)
    return false;
  Dkc1VideoHostSnapshotV1 legacy;
  if (size == sizeof legacy) {
    memcpy(&legacy, data, sizeof legacy);
    if (legacy.magic != kDkc1VideoSnapshotMagic || legacy.version != 1u ||
        legacy.size != sizeof legacy)
      return false;
    Dkc1MarginProxyReset();
  } else if (size == sizeof(Dkc1VideoHostSnapshot)) {
    Dkc1VideoHostSnapshot snapshot;
    memcpy(&snapshot, data, sizeof snapshot);
    legacy = snapshot.legacy;
    if (legacy.magic != kDkc1VideoSnapshotMagic ||
        legacy.version != kDkc1VideoSnapshotVersion ||
        legacy.size != sizeof snapshot ||
        snapshot.marginProxySize != Dkc1MarginProxySnapshotSize() ||
        snapshot.marginProxySize > sizeof snapshot.marginProxy)
      return false;
    /* Validate every legacy-owned field before allowing the proxy loader to
     * mutate its host-side state.  A corrupt snapshot must be rejected as a
     * transaction, not leave valid proxy data paired with invalid video
     * history. */
    if (legacy.streamCoverage.unique_columns > 64u ||
        legacy.streamCoverage.required_columns > 64u)
      return false;
    if (!Dkc1MarginProxySnapshotLoad(snapshot.marginProxy,
                                     snapshot.marginProxySize))
      return false;
  } else {
    return false;
  }
  if (legacy.streamCoverage.unique_columns > 64u ||
      legacy.streamCoverage.required_columns > 64u)
    return false;
  memcpy(s_placed_actor_phases, legacy.phases,
         sizeof s_placed_actor_phases);
  s_placed_actor_phases_seeded = legacy.phasesSeeded != 0;
  s_placed_actor_context = legacy.context;
  s_placed_actor_context_valid = legacy.contextValid != 0;
  s_stream_coverage = legacy.streamCoverage;
  s_terrain_ready = g_ws_active && legacy.terrainReady != 0;
  s_presentation_bias = legacy.presentationBias;
  if (s_presentation_bias < -g_ws_extra) s_presentation_bias = -g_ws_extra;
  if (s_presentation_bias > g_ws_extra) s_presentation_bias = g_ws_extra;
  s_prefetch_dispatch_active = false;
  s_prefetch_actor_index = 0;
  memset(s_prefetch_transaction_detail_reported, 0,
         sizeof s_prefetch_transaction_detail_reported);
  return true;
}

static bool Dkc1StreamDebugEnabled(void) {
  const char *value = getenv("DKC1_STREAM_DEBUG");
  return value && value[0] == '1' && value[1] == '\0';
}

static bool Dkc1PrefetchPhaseGuardEnabled(void) {
  const char *value = getenv("DKC1_PREFETCH_PHASE_GUARD");
  return value && value[0] == '1' && value[1] == '\0';
}

static bool Dkc1PrefetchTransactionDebugEnabled(void) {
  const char *value = getenv("DKC1_PREFETCH_TRANSACTION_DEBUG");
  return value && value[0] == '1' && value[1] == '\0';
}

static void Dkc1VideoClearPrefetchTransactionDebug(void) {
  s_prefetch_actor_index = 0;
  memset(s_prefetch_transaction_detail_reported, 0,
         sizeof s_prefetch_transaction_detail_reported);
}

static void Dkc1VideoClearPlacedActorPhases(void) {
  memset(s_placed_actor_phases, 0, sizeof s_placed_actor_phases);
  s_placed_actor_phases_seeded = false;
  s_prefetch_dispatch_active = false;
  Dkc1VideoClearPrefetchTransactionDebug();
}

void Dkc1VideoResetPlacedActorPhases(void) {
  Dkc1VideoClearPlacedActorPhases();
  Dkc1MarginProxyReset();
  memset(&s_placed_actor_context, 0, sizeof s_placed_actor_context);
  s_placed_actor_context_valid = false;
  memset(&s_stream_coverage, 0, sizeof s_stream_coverage);
  memset(&s_wide_row_build, 0, sizeof s_wide_row_build);
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

uint16_t Dkc1VideoObjectScannerCullLeft(uint16_t native_margin) {
  return Dkc1MarginProxyEnabled()
             ? native_margin : Dkc1VideoExpandCullLeft(native_margin);
}

uint16_t Dkc1VideoObjectScannerCullSpan(uint16_t native_span) {
  return Dkc1MarginProxyEnabled()
             ? native_span : Dkc1VideoExpandCullSpan(native_span);
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

enum {
  /* The visible 16:9 extension is 43 pixels per side, but the renderer
   * samples seven complete margin tiles so arbitrary sub-tile scroll phases
   * always have a populated neighbor.  Six tiles (48 pixels) leave one
   * physical ring column outside the cartridge initializer/row sweep; it can
   * later cross the native center as an 8-pixel vertical seam.  Match the
   * renderer and the proven ROM patch with seven tiles (56 pixels), then let
   * the host crop the unused guard pixels. */
  kDkc1StreamMargin =
      ((kDkc1VideoWidescreenExtra + 7) & ~7) + 8,
  /* Each stock pass stages 36 entries.  Offset the second pass by 18 tiles
   * so the two windows overlap by half and cover 54 coherent columns. */
  kDkc1WideRowPassSeparation = 18 * 8,
};

static bool Dkc1VideoCartridgeWideningSceneEligible(
    const struct CpuState *cpu) {
  if (!cpu || !Dkc1VideoIsWidescreen())
    return false;

  /* Fail closed.  The shared stock initializer is not itself proof that its
   * map decoder can safely synthesize extra columns: fresh-entry A/B tests
   * proved native-ring corruption in both a fixed bonus cave and ordinary
   * Ropey Rampage.  Host-side ROM prefill plus the stock cartridge stream
   * produce clean 16:9 output for Jungle and Ropey.  Keep the old rewrite
   * available only as an explicit research switch until a per-layout
   * capability oracle replaces this assumption.  A post-entry save state may
   * already contain damaged VRAM and is never valid evidence for this switch. */
  const char *experimental =
      getenv("DKC1_ENABLE_EXPERIMENTAL_CARTRIDGE_WIDENING");
  if (!experimental || !*experimental || *experimental == '0')
    return false;

  const uint16_t mode = Dkc1ReadWram16(cpu->ram, 0x0032u);
  const uint16_t level = Dkc1ReadWram16(cpu->ram, 0x0030u);
  const uint16_t entrance = Dkc1ReadWram16(cpu->ram, 0x003eu);

  /* Jungle Hijinxs Bonus 1 uses a fixed cave tilemap whose stock initializer
   * is not a rolling-map capability boundary.  Widening its backstep/count
   * writes unrelated cave columns into the native ring; later identity reset
   * correctly rejects the alleged coverage, but cannot undo those VRAM
   * writes.  Keep cartridge execution stock and let the fail-closed host
   * presentation supply/blank the side margins for this exact scene. */
  if (mode == 0x0001u && level == 0x0009u && entrance == 0x0006u)
    return false;

  return true;
}

static bool Dkc1VideoStreamWideningEligible(const struct CpuState *cpu) {
  if (!Dkc1VideoCartridgeWideningSceneEligible(cpu))
    return false;
  const uint16_t lower = Dkc1ReadWram16(cpu->ram, 0x1b23u);
  const uint16_t upper = Dkc1ReadWram16(cpu->ram, 0x1b25u);
  return upper >= lower &&
         (uint16_t)(upper - lower) >=
             (uint16_t)(2 * kDkc1VideoWidescreenExtra);
}

void Dkc1VideoBeginWideRowBuild(struct CpuState *cpu, bool alternate) {
  if (!cpu || s_wide_row_build.phase != 0 ||
      !Dkc1VideoStreamWideningEligible(cpu))
    return;

  const uint16_t layer_x = Dkc1ReadWram16(cpu->ram, 0x088bu);
  s_wide_row_build.original_layer_x = layer_x;
  s_wide_row_build.kind = alternate ? 2u : 1u;
  s_wide_row_build.phase = 1u;
  cpu_write16(cpu, 0x7e, 0x088bu,
              (uint16_t)(layer_x - kDkc1StreamMargin));
}

uint8_t Dkc1VideoAdvanceWideRowBuild(struct CpuState *cpu) {
  if (!cpu || s_wide_row_build.phase == 0)
    return 0;

  const uint8_t kind = s_wide_row_build.kind;
  if (s_wide_row_build.phase == 1u) {
    s_wide_row_build.phase = 2u;
    cpu_write16(cpu, 0x7e, 0x088bu,
                (uint16_t)(s_wide_row_build.original_layer_x +
                           kDkc1WideRowPassSeparation -
                           kDkc1StreamMargin));
    return kind;
  }

  cpu_write16(cpu, 0x7e, 0x088bu,
              s_wide_row_build.original_layer_x);
  memset(&s_wide_row_build, 0, sizeof s_wide_row_build);
  return 0;
}

static void Dkc1VideoSyncStreamContext(const uint8_t *wram) {
  if (!wram)
    return;
  const uint16_t mode = Dkc1ReadWram16(wram, 0x0032u);
  const uint16_t level = Dkc1ReadWram16(wram, 0x0030u);
  const uint16_t entrance = Dkc1ReadWram16(wram, 0x003eu);
  if (s_stream_coverage.context_valid &&
      s_stream_coverage.mode == mode &&
      s_stream_coverage.level == level &&
      s_stream_coverage.entrance == entrance)
    return;
  /* Some underwater entrances build the new level's complete cartridge
   * tile ring before publishing its final level number.  The entrance ID is
   * already final and the world-map path cannot start coverage because its
   * camera span is 0. Preserve an in-flight, observed fill across that one
   * level/mode publication boundary; save-state loads and real entrance
   * changes still reset through ResetPlacedActorPhases or the branch below. */
  if (s_stream_coverage.context_valid &&
      s_stream_coverage.entrance == entrance &&
      s_stream_coverage.required_columns != 0 &&
      !s_stream_coverage.ready) {
    /* Preserve only an initializer that is still in flight. A completed
     * fill belongs to the mode/level identity under which it was observed.
     * Bonus exits can finish a provisional fill while the old level number
     * is still published; carrying that ready proof into the returned level
     * makes the first visible frame trust stale edge columns instead of
     * performing the clean ROM-prefill bootstrap. */
    s_stream_coverage.mode = mode;
    s_stream_coverage.level = level;
    return;
  }
  memset(&s_stream_coverage, 0, sizeof s_stream_coverage);
  s_stream_coverage.mode = mode;
  s_stream_coverage.level = level;
  s_stream_coverage.entrance = entrance;
  s_stream_coverage.context_valid = true;
}

static void Dkc1VideoBeginStreamCoverage(struct CpuState *cpu,
                                         uint8_t required_columns) {
  if (!cpu)
    return;
  const uint32_t count_calls = s_stream_coverage.initial_count_calls;
  const uint32_t count_rejected = s_stream_coverage.initial_count_rejected;
  const uint32_t selector_calls = s_stream_coverage.selector_calls;
  memset(&s_stream_coverage, 0, sizeof s_stream_coverage);
  s_stream_coverage.initial_count_calls = count_calls;
  s_stream_coverage.initial_count_rejected = count_rejected;
  s_stream_coverage.selector_calls = selector_calls;
  s_stream_coverage.required_columns = required_columns;
  if (Dkc1StreamDebugEnabled())
    fprintf(stderr, "stream: begin required=%u mode=%04x level=%04x entrance=%04x\n",
            required_columns, Dkc1ReadWram16(cpu->ram, 0x0032u),
            Dkc1ReadWram16(cpu->ram, 0x0030u),
            Dkc1ReadWram16(cpu->ram, 0x003eu));
  /* Do not bind mode/level/entrance inside the initializer transaction.
   * Underwater transitions can mutate those words between count setup,
   * individual column passes, and the completed frame.  A complete fill is
   * attached to the stable frame-boundary identity below. */
}

static void Dkc1VideoObserveStreamColumn(struct CpuState *cpu,
                                         uint16_t world_x) {
  if (!cpu || s_stream_coverage.required_columns == 0)
    return;
  const uint64_t bit = UINT64_C(1) << ((world_x >> 3) & 0x3fu);
  if (!(s_stream_coverage.columns & bit)) {
    s_stream_coverage.columns |= bit;
    s_stream_coverage.unique_columns++;
  }
  if (s_stream_coverage.unique_columns >=
      s_stream_coverage.required_columns) {
    s_stream_coverage.ready = true;
    if (Dkc1StreamDebugEnabled() &&
        s_stream_coverage.unique_columns ==
            s_stream_coverage.required_columns)
      fprintf(stderr, "stream: complete required=%u selected=%04x\n",
              s_stream_coverage.required_columns, world_x);
  }
}

bool Dkc1VideoCartridgeTerrainReady(const uint8_t *wram) {
  if (!Dkc1VideoIsWidescreen() || !wram)
    return false;
  if (!s_stream_coverage.ready)
    return false;
  if (!s_stream_coverage.context_valid) {
    s_stream_coverage.mode = Dkc1ReadWram16(wram, 0x0032u);
    s_stream_coverage.level = Dkc1ReadWram16(wram, 0x0030u);
    s_stream_coverage.entrance = Dkc1ReadWram16(wram, 0x003eu);
    s_stream_coverage.context_valid = true;
    if (Dkc1StreamDebugEnabled())
      fprintf(stderr,
              "stream: bind-complete mode=%04x level=%04x entrance=%04x\n",
              s_stream_coverage.mode, s_stream_coverage.level,
              s_stream_coverage.entrance);
    return true;
  }
  if (Dkc1StreamDebugEnabled())
    fprintf(stderr,
            "stream: query mode=%04x/%04x level=%04x/%04x entrance=%04x/%04x ready=%u\n",
            s_stream_coverage.mode, Dkc1ReadWram16(wram, 0x0032u),
            s_stream_coverage.level, Dkc1ReadWram16(wram, 0x0030u),
            s_stream_coverage.entrance, Dkc1ReadWram16(wram, 0x003eu),
            s_stream_coverage.ready ? 1u : 0u);
  Dkc1VideoSyncStreamContext(wram);
  return s_stream_coverage.ready;
}

void Dkc1VideoInvalidateStreamCoverage(void) {
  /* Keep lifetime counters for diagnostics, but discard the alleged fill.
   * A later checksum-locked initializer can establish a new proof from an
   * empty transaction. */
  s_stream_coverage.columns = 0;
  s_stream_coverage.unique_columns = 0;
  s_stream_coverage.required_columns = 0;
  s_stream_coverage.last_layer_x = 0;
  s_stream_coverage.last_selected_x = 0;
  s_stream_coverage.observed_columns = 0;
  s_stream_coverage.context_valid = false;
  s_stream_coverage.ready = false;
}

void Dkc1VideoGetStreamCoverageStats(Dkc1VideoStreamCoverageStats *stats) {
  if (!stats)
    return;
  memset(stats, 0, sizeof *stats);
  stats->mode = s_stream_coverage.mode;
  stats->level = s_stream_coverage.level;
  stats->entrance = s_stream_coverage.entrance;
  stats->last_layer_x = s_stream_coverage.last_layer_x;
  stats->last_selected_x = s_stream_coverage.last_selected_x;
  stats->unique_columns = s_stream_coverage.unique_columns;
  stats->required_columns = s_stream_coverage.required_columns;
  stats->initial_count_calls = s_stream_coverage.initial_count_calls;
  stats->initial_count_rejected = s_stream_coverage.initial_count_rejected;
  stats->selector_calls = s_stream_coverage.selector_calls;
  stats->observed_columns = s_stream_coverage.observed_columns;
  stats->context_valid = s_stream_coverage.context_valid;
  stats->ready = s_stream_coverage.ready;
}

static int Dkc1VideoAlignedStreamBias(const struct CpuState *cpu,
                                      uint16_t camera) {
  if (!Dkc1VideoStreamWideningEligible(cpu))
    return 0;
  const uint16_t lower = Dkc1ReadWram16(cpu->ram, 0x1b23u);
  const uint16_t upper = Dkc1ReadWram16(cpu->ram, 0x1b25u);
  int32_t target = camera;
  if (target < (int32_t)lower + kDkc1VideoWidescreenExtra)
    target = (int32_t)lower + kDkc1VideoWidescreenExtra;
  if (target > (int32_t)upper - kDkc1VideoWidescreenExtra)
    target = (int32_t)upper - kDkc1VideoWidescreenExtra;
  const int bias = (int)(target - camera);
  if (bias > 0)
    return (bias + 7) & ~7;
  if (bias < 0)
    return -(((-bias) + 7) & ~7);
  return 0;
}

uint16_t Dkc1VideoInitialBackstep(struct CpuState *cpu,
                                  uint16_t native_backstep) {
  const bool scene_eligible =
      Dkc1VideoCartridgeWideningSceneEligible(cpu);
  if (Dkc1StreamDebugEnabled())
    fprintf(stderr,
            "stream: backstep native=%04x wide=%u scene_eligible=%u\n",
            native_backstep, Dkc1VideoIsWidescreen() ? 1u : 0u,
            scene_eligible ? 1u : 0u);
  /* These helpers are injected into DKC's two shared initializer bodies, but
   * reaching one of those bodies does not prove that a particular layout can
   * accept wider cartridge writes.  Underwater entrances may also reach them
   * before final camera bounds are published.  The explicit policy above is
   * therefore the capability boundary; presentation stays centered until
   * SelectStreamX proves every column. */
  if (!scene_eligible)
    return native_backstep;
  if (native_backstep == 0x0100u)
    return 0x0170u; /* 46 columns * 8 pixels. */
  if (native_backstep == 0x0108u)
    return 0x0178u; /* Preserve the alternate path's extra column. */
  return native_backstep;
}

uint16_t Dkc1VideoInitialColumnCount(struct CpuState *cpu,
                                     uint16_t native_count) {
  const bool scene_eligible =
      Dkc1VideoCartridgeWideningSceneEligible(cpu);
  if (Dkc1StreamDebugEnabled())
    fprintf(stderr,
            "stream: count native=%04x wide=%u scene_eligible=%u\n",
            native_count, Dkc1VideoIsWidescreen() ? 1u : 0u,
            scene_eligible ? 1u : 0u);
  s_stream_coverage.initial_count_calls++;
  if (!scene_eligible) {
    s_stream_coverage.initial_count_rejected++;
    return native_count;
  }
  if (native_count == 0x0020u) {
    Dkc1VideoBeginStreamCoverage(cpu, 0x2eu);
    return 0x002eu;
  }
  if (native_count == 0x0021u) {
    Dkc1VideoBeginStreamCoverage(cpu, 0x2fu);
    return 0x002fu;
  }
  return native_count;
}

uint16_t Dkc1VideoSelectStreamX(struct CpuState *cpu,
                                uint16_t stock_stream_x) {
  s_stream_coverage.selector_calls++;
  const bool initialization_active =
      cpu && Dkc1VideoIsWidescreen() &&
      s_stream_coverage.required_columns != 0 &&
      !s_stream_coverage.ready;
  if (!Dkc1VideoStreamWideningEligible(cpu) && !initialization_active)
    return stock_stream_x;

  const uint16_t layer_x = Dkc1ReadWram16(cpu->ram, 0x088bu);
  const uint16_t step = Dkc1ReadWram16(cpu->ram, 0x0a75u);
  const uint16_t init_kind = Dkc1ReadWram16(cpu->ram, 0x1a5bu);
  const uint16_t target_x = Dkc1ReadWram16(cpu->ram, 0x1a5eu);
  const uint16_t upper = Dkc1ReadWram16(cpu->ram, 0x1b25u);
  s_stream_coverage.last_layer_x = layer_x;

  /* Standard initial fill. With the widened 368-pixel backstep, +312 starts
   * 56 pixels left of the final camera while preserving its final value. */
  if (init_kind == 1u && step == 8u) {
    const int bias = Dkc1VideoAlignedStreamBias(cpu, target_x);
    const uint16_t selected =
        (uint16_t)(stock_stream_x + kDkc1StreamMargin + bias);
    s_stream_coverage.last_selected_x = selected;
    s_stream_coverage.observed_columns++;
    Dkc1VideoObserveStreamColumn(cpu, selected);
    return selected;
  }

  /* The alternate fill uses one additional column. Its temporary Layer1 X
   * sits outside the current upper bound; ordinary high-world camera values
   * remain inside the bound and must not be mistaken for initialization. */
  if (init_kind == 0u && step == 8u &&
      (initialization_active ||
       (layer_x > upper && layer_x != target_x))) {
    const int bias = Dkc1VideoAlignedStreamBias(cpu, target_x);
    const uint16_t selected =
        (uint16_t)(stock_stream_x + kDkc1StreamMargin + 8 + bias);
    s_stream_coverage.last_selected_x = selected;
    s_stream_coverage.observed_columns++;
    Dkc1VideoObserveStreamColumn(cpu, selected);
    return selected;
  }

  const int bias = Dkc1VideoAlignedStreamBias(cpu, layer_x);
  if ((int16_t)step < 0)
    return (uint16_t)(stock_stream_x - kDkc1StreamMargin + bias);
  return (uint16_t)(stock_stream_x + kDkc1StreamMargin + bias);
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
  Dkc1DebugTracePlacedActorContext(current.mode, current.level,
                                   current.entrance);
}

void Dkc1VideoObserveActorPool(const uint8_t *wram) {
  Dkc1VideoObservePlacedActorContext(wram);
  if (!wram)
    return;

  /* This observer runs at the frame boundary before the cartridge scanner.
   * Seed only identities that already exist at that boundary.  That makes a
   * loaded save state left-censored (its actors are trusted), while actors
   * allocated by the widened scanner later in this same frame remain new and
   * must pass the reconstructed stock window.
   *
   * Do not defer this seed until the first actor dispatch: on a fresh level
   * the widened scanner may allocate a margin-only actor between this
   * boundary and that dispatch.  The old deferred seed incorrectly marked
   * that new actor stock-started and let its AI run many frames early. */
  if (!s_placed_actor_phases_seeded) {
    for (uint16_t actor_index = 0x0002u; actor_index <= 0x0032u;
         actor_index = (uint16_t)(actor_index + 2u)) {
      Dkc1PlacedActorPhase *phase =
          &s_placed_actor_phases[(actor_index - 2u) >> 1];
      phase->id = Dkc1ReadWram16(
          wram, (uint16_t)(0x0d45u + actor_index));
      phase->source = Dkc1ReadWram16(
          wram, (uint16_t)(0x15fdu + actor_index));
      phase->stock_started = phase->id != 0;
    }
    s_placed_actor_phases_seeded = true;
    return;
  }

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
    phase->suppression_reported = false;
    phase->fallback_hold_reported = false;
  }
}

bool Dkc1VideoShouldRunPlacedActor(struct CpuState *cpu) {
  if (!cpu || !Dkc1VideoIsWidescreen())
    return true;

  Dkc1VideoObservePlacedActorContext(cpu->ram);

  /* Dkc1VideoObserveActorPool normally seeds the pool before the scanner.
   * If an integration ever dispatches without that required frame-boundary
   * observation, prefer an empty seeded baseline.  Suppressing a newly seen
   * wide-prefetched actor is safer than silently advancing its gameplay AI;
   * loaded states use the normal pre-scanner path above and remain trusted. */
  if (!s_placed_actor_phases_seeded) {
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
  const bool new_identity = phase->id != id || phase->source != source;
  if (new_identity) {
    phase->id = id;
    phase->source = source;
    /* A new identity allocated while widening is inactive came from the
     * authentic stock scanner and is immediately safe to run.  A new
     * identity allocated while widening is active may be margin-prefetched
     * and must pass the reconstructed stock interval below. */
    phase->stock_started = !Dkc1VideoTerrainReady();
    phase->suppression_reported = false;
    phase->fallback_hold_reported = false;
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

  const bool was_started = phase->stock_started;
  if (!phase->stock_started &&
      Dkc1UnsignedInside(source_x, stock_left, stock_right))
    phase->stock_started = true;
  if (new_identity) {
    Dkc1DebugTracePlacedActorPhase(
        phase->stock_started ? "stock_identity" : "prefetch_candidate",
        actor_index, id, source, source_x, current_left, current_right,
        stock_left, stock_right, Dkc1VideoTerrainReady());
  }
  if (!was_started && phase->stock_started) {
    Dkc1DebugTracePlacedActorPhase(
        "prefetch_released", actor_index, id, source, source_x,
        current_left, current_right, stock_left, stock_right,
        Dkc1VideoTerrainReady());
  } else if (!phase->stock_started && !Dkc1VideoTerrainReady() &&
             !phase->fallback_hold_reported) {
    Dkc1DebugTracePlacedActorPhase(
        "soft_fallback_held", actor_index, id, source, source_x,
        current_left, current_right, stock_left, stock_right, false);
    phase->fallback_hold_reported = true;
  } else if (!phase->stock_started && !phase->suppression_reported) {
    Dkc1DebugTracePlacedActorPhase(
        "prefetch_suppressed", actor_index, id, source, source_x,
        current_left, current_right, stock_left, stock_right,
        Dkc1VideoTerrainReady());
    phase->suppression_reported = true;
  }
  return phase->stock_started;
}

bool Dkc1VideoBeginPlacedActorDispatch(struct CpuState *cpu) {
  /* A prefetched actor is allowed to execute its authentic routine inside a
   * transaction. The matching end hook restores the complete 128 KiB WRAM
   * image, preventing movement, collision, spawns, sound-command staging,
   * scratch values, and object bookkeeping from escaping before stock
   * eligibility. This keeps the cartridge dispatcher/control flow intact
  * without inventing an alternate actor implementation. */
  s_prefetch_dispatch_active = false;
  if (!Dkc1PrefetchPhaseGuardEnabled() || !cpu ||
      Dkc1VideoShouldRunPlacedActor(cpu))
    return false;

  memcpy(s_prefetch_wram, cpu->ram, kDkc1WramSize);
  s_prefetch_dispatch_active = true;
  s_prefetch_actor_index = cpu_read16(
      cpu, 0x00, (uint16_t)(cpu->D + 0x0082u));
  return true;
}

void Dkc1VideoEndPlacedActorDispatch(struct CpuState *cpu) {
  if (!cpu || !s_prefetch_dispatch_active)
    return;

  if (Dkc1PrefetchTransactionDebugEnabled()) {
    const uint16_t index = s_prefetch_actor_index;
    unsigned changed = 0;
    for (unsigned i = 0; i < kDkc1WramSize; i++)
      changed += s_prefetch_wram[i] != cpu->ram[i];
    fprintf(stderr,
            "DKC1 prefetch transaction: actor=$%04X id=$%04X source=$%04X "
            "changed=%u pose=$%04X->$%04X current=$%04X->$%04X "
            "state=$%04X->$%04X anim=$%04X->$%04X gfx=$%04X->$%04X "
            "oam=$%04X->$%04X\n",
            index,
            Dkc1ReadWram16(s_prefetch_wram, (uint16_t)(0x0d45u + index)),
            Dkc1ReadWram16(s_prefetch_wram, (uint16_t)(0x15fdu + index)),
            changed,
            Dkc1ReadWram16(s_prefetch_wram, (uint16_t)(0x0ae5u + index)),
            Dkc1ReadWram16(cpu->ram, (uint16_t)(0x0ae5u + index)),
            Dkc1ReadWram16(s_prefetch_wram, (uint16_t)(0x0d11u + index)),
            Dkc1ReadWram16(cpu->ram, (uint16_t)(0x0d11u + index)),
            Dkc1ReadWram16(s_prefetch_wram, (uint16_t)(0x1029u + index)),
            Dkc1ReadWram16(cpu->ram, (uint16_t)(0x1029u + index)),
            Dkc1ReadWram16(s_prefetch_wram, (uint16_t)(0x10d1u + index)),
            Dkc1ReadWram16(cpu->ram, (uint16_t)(0x10d1u + index)),
            Dkc1ReadWram16(s_prefetch_wram, (uint16_t)(0x0c69u + index)),
            Dkc1ReadWram16(cpu->ram, (uint16_t)(0x0c69u + index)),
            Dkc1ReadWram16(s_prefetch_wram, 0x008eu),
            Dkc1ReadWram16(cpu->ram, 0x008eu));
    if (index >= 2u && index <= 0x32u && (index & 1u) == 0) {
      const unsigned ordinal = (index - 2u) >> 1;
      if (!s_prefetch_transaction_detail_reported[ordinal]) {
        Dkc1DebugTracePrefetchTransaction(
            index,
            Dkc1ReadWram16(s_prefetch_wram,
                           (uint16_t)(0x0d45u + index)),
            Dkc1ReadWram16(s_prefetch_wram,
                           (uint16_t)(0x15fdu + index)),
            s_prefetch_wram, cpu->ram, kDkc1WramSize);
        fprintf(stderr, "DKC1 prefetch changed offsets:");
        unsigned emitted = 0;
        for (unsigned i = 0; i < kDkc1WramSize && emitted < 128u; i++) {
          if (s_prefetch_wram[i] == cpu->ram[i])
            continue;
          fprintf(stderr, " %05X:%02X>%02X", i, s_prefetch_wram[i],
                  cpu->ram[i]);
          emitted++;
        }
        fprintf(stderr, "%s\n", changed > emitted ? " ..." : "");
        s_prefetch_transaction_detail_reported[ordinal] = true;
      }
    }
  }

  memcpy(cpu->ram, s_prefetch_wram, kDkc1WramSize);
  s_prefetch_dispatch_active = false;
}

bool Dkc1VideoPrepareType5ChildRetry(struct CpuState *cpu) {
  /* Native scanner timing in margin-proxy mode never creates an early type-5
   * parent, so the widened-prefetch recovery path must remain dormant. */
  if (Dkc1MarginProxyEnabled() || !cpu || !Dkc1VideoTerrainReady())
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
