#include "dkc1_game.h"
#include "dkc1_video.h"

#include "common_cpu_infra.h"
#include "common_rtl.h"
#include "cpu_state.h"
#include "snes/dma.h"
#include "snes/interp_bridge.h"
#include "snes/ppu.h"
#include "snes/saveload.h"
#include "snes/snes.h"
#include "snes/ws_shadow.h"

#include <stdbool.h>
#include <string.h>

enum {
  /* DKC1 USA v1.0 vectors: reset $80:8000, native NMI $80:A968 (the runtime's
   * pc24 convention folds the system banks to bank 00). */
  kDkc1ResetPc = 0x008000,
  kDkc1NmiPc = 0x00A968,
};

static bool s_cpu_initialized;
static uint32_t s_resume_pc = kDkc1ResetPc;
static int s_last_lle_result = 1;
static uint64_t s_next_frame_master;

typedef struct Dkc1HostSnapshot {
  CpuState cpu;
  uint32_t resume_pc;
  uint64_t next_frame_master;
  uint64_t main_cpu_cycles_estimate;
  uint64_t apu_pace_cycles_estimate;
  uint64_t apu_last_sync_cycles;
  uint64_t apu_last_sync_master;
  int last_lle_result;
  int frame_counter;
  uint8_t cpu_initialized;
  uint8_t last_hdmaen;
  uint8_t memsel;
} Dkc1HostSnapshot;

enum {
  /* NTSC master clocks per non-short host frame. */
  kDkc1NtscFrameMasterClocks = 1364 * 262,
};

static void Dkc1RunOneFrame(void) {
  bool first_frame = !s_cpu_initialized;
  if (s_next_frame_master == 0) {
    s_next_frame_master =
        g_cpu.master_cycles + kDkc1NtscFrameMasterClocks;
  }
  while (s_next_frame_master <= g_cpu.master_cycles)
    s_next_frame_master += kDkc1NtscFrameMasterClocks;
  interp_bridge_set_master_deadline(s_next_frame_master);

  if (first_frame) {
    cpu_state_init(&g_cpu, g_ram);
    s_cpu_initialized = true;
  }
  if (!first_frame && g_snes->nmiEnabled) {
    /* Rare's engine parks the main loop at WAI; the NMI performs the frame's
     * VBlank work. Push the interrupt frame at the parked PC and run handler
     * plus continuation to the next quiescent wait (works for both an RTI
     * handler and a DKC2-style non-returning frame dispatcher). */
    g_snes->inNmi = true;
    cpu_push_interrupt_frame_at(&g_cpu, s_resume_pc);
    s_last_lle_result =
        interp_bridge_run_until_quiescent(&g_cpu, kDkc1NmiPc);
  } else {
    s_last_lle_result =
        interp_bridge_run_until_quiescent(&g_cpu, s_resume_pc);
  }

  interp_bridge_set_master_deadline(0);
  s_resume_pc = interp_bridge_lle_resume_pc();
  if (g_cpu.master_cycles < s_next_frame_master) {
    g_cpu.master_cycles = s_next_frame_master;
    snes_sync_master_clock(g_snes, g_cpu.master_cycles);
  }
  s_next_frame_master += kDkc1NtscFrameMasterClocks;
}

static void Dkc1SaveExtra(SaveLoadInfo *sli) {
  Dkc1HostSnapshot snapshot;
  memset(&snapshot, 0, sizeof snapshot);
  snapshot.cpu = g_cpu;
  snapshot.cpu.ram = NULL;
  snapshot.resume_pc = s_resume_pc;
  snapshot.next_frame_master = s_next_frame_master;
  snapshot.main_cpu_cycles_estimate = g_main_cpu_cycles_estimate;
  snapshot.apu_pace_cycles_estimate = g_apu_pace_cycles_estimate;
  snapshot.apu_last_sync_cycles = g_apu_last_sync_cycles;
  snapshot.apu_last_sync_master = g_apu_last_sync_master;
  snapshot.last_lle_result = s_last_lle_result;
  snapshot.frame_counter = snes_frame_counter;
  snapshot.cpu_initialized = s_cpu_initialized ? 1u : 0u;
  snapshot.last_hdmaen = g_snesrecomp_last_hdmaen;
  snapshot.memsel = g_memsel;
  sli->func(sli, &snapshot, sizeof snapshot);
}

static void Dkc1LoadExtra(SaveLoadInfo *sli, uint32_t version) {
  (void)version;
  Dkc1HostSnapshot snapshot;
  sli->func(sli, &snapshot, sizeof snapshot);
  g_cpu = snapshot.cpu;
  g_cpu.ram = g_ram;
  s_resume_pc = snapshot.resume_pc;
  s_next_frame_master = snapshot.next_frame_master;
  g_main_cpu_cycles_estimate = snapshot.main_cpu_cycles_estimate;
  g_apu_pace_cycles_estimate = snapshot.apu_pace_cycles_estimate;
  g_apu_last_sync_cycles = snapshot.apu_last_sync_cycles;
  g_apu_last_sync_master = snapshot.apu_last_sync_master;
  s_last_lle_result = snapshot.last_lle_result;
  snes_frame_counter = snapshot.frame_counter;
  s_cpu_initialized = snapshot.cpu_initialized != 0;
  g_snesrecomp_last_hdmaen = snapshot.last_hdmaen;
  g_memsel = snapshot.memsel;
}

static void Dkc1ResetWidescreenShadow(void);

static void Dkc1OnStateLoaded(uint32_t version) {
  (void)version;
  g_cpu.ram = g_ram;
  g_apu_last_sync_master = g_cpu.master_cycles;
  g_snes->beamMasterLast = g_cpu.master_cycles;
  interp_bridge_set_master_deadline(0);
  Dkc1ResetWidescreenShadow();
}

static const RtlGameInfo kDkc1GameInfo = {
  .title = "dkc1",
  .initialize = NULL,
  .run_frame = &Dkc1RunOneFrame,
  .draw_ppu_frame = &Dkc1DrawPpuFrame,
  .save_name_prefix = "dkc1s",
  .state_save_extra = &Dkc1SaveExtra,
  .state_load_extra = &Dkc1LoadExtra,
  .on_state_loaded = &Dkc1OnStateLoaded,
};

const RtlGameInfo *Dkc1GameInfo(void) {
  return &kDkc1GameInfo;
}

void Dkc1BeginDrawing(uint8_t *pixels, size_t pitch) {
  PpuBeginDrawing(g_ppu, pixels, pitch, kPpuRenderFlags_NewRenderer);
}

/* ---- presentation-camera widescreen ------------------------------------
 * The stock logical camera keeps driving collision, exits, movement clamps,
 * and tile streaming. Generated visibility adapters may activate objects
 * that are genuinely visible in the host margins. The host renders those
 * margin columns in world space (WsShadow) and prefills terrain by decoding
 * the level map straight from ROM. */

static uint16_t Dkc1ReadWram16(uint16_t address) {
  return (uint16_t)g_ram[address] |
         ((uint16_t)g_ram[(uint16_t)(address + 1u)] << 8);
}

static bool s_ws_shadow_active;
static bool s_ws_source_valid;
static uint64_t s_ws_source_signature;
static bool s_ws_origin_valid[2];
static uint32_t s_ws_world_x[2];
static uint32_t s_ws_world_y[2];
static Dkc1LevelLayout s_ws_layout;
static int s_ws_layout_grace;  /* bounded transient calibration misses */

static uint64_t Dkc1LevelSourceSignature(void) {
  const uint64_t bank = g_ram[0x00d5];
  const uint64_t map = Dkc1ReadWram16(0x00d3);
  const uint64_t metatiles = Dkc1ReadWram16(0x1b11);
  const uint64_t vram = Dkc1ReadWram16(0x1b13);
  return bank | (map << 8) | (metatiles << 24) | (vram << 40);
}

static void Dkc1ResetWidescreenShadow(void) {
  if (s_ws_shadow_active)
    WsShadowReset();
  s_ws_shadow_active = false;
  memset(s_ws_origin_valid, 0, sizeof s_ws_origin_valid);
  s_ws_source_valid = false;
  s_ws_layout = kDkc1LayoutUnknown;
  s_ws_layout_grace = 0;
  Dkc1VideoSetPresentationBias(0);
  Dkc1VideoSetTerrainReady(false);
}

/* Clamp only the host presentation camera near level ends. A symmetric wide
 * viewport centered on camera X=lower asks for negative world columns, which
 * is why the left side stayed black until the player first scrolled. Moving
 * the presentation center inward exposes real level art immediately while
 * leaving collision, exits, camera bounds, and simulation untouched. */
static int Dkc1WidescreenPresentationBias(void) {
  const uint32_t camera = Dkc1ReadWram16(0x088b);
  const uint32_t lower = Dkc1ReadWram16(0x1b23);
  const uint32_t upper = Dkc1ReadWram16(0x1b25);
  const uint32_t extra = (uint32_t)Dkc1VideoExtra();
  if (upper < lower || upper - lower < extra * 2u)
    return 0;
  uint32_t target = camera;
  if (target < lower + extra)
    target = lower + extra;
  if (target > upper - extra)
    target = upper - extra;
  return (int32_t)target - (int32_t)camera;
}

/* Word address of a tile in a 64x32 SNES tilemap (world-keyed rolling map:
 * the column streamer at $81:883F keys VRAM columns by cameraX>>3 mod 64). */
static uint16_t Dkc1RollingMapWord(uint16_t map_base, uint32_t tile_x,
                                   uint32_t tile_y) {
  uint16_t word = (uint16_t)(map_base + ((tile_y & 0x1fu) << 5) +
                             (tile_x & 0x1fu));
  if (tile_x & 0x20u)
    word = (uint16_t)(word + 0x400u);
  return word;
}

/* Score a candidate layout by decoding the native viewport from ROM and
 * comparing with the live rolling tilemap. Dynamic tiles (animation, item
 * pickups) legitimately mismatch, so the gate is a ratio, not equality. */
static int Dkc1CalibrateLayout(Dkc1LevelLayout layout, uint16_t ppu_map_base,
                               uint8_t bank, uint16_t map_base,
                               uint16_t metatile_base, uint32_t world_x,
                               uint32_t world_y, int *decodable_out) {
  int matches = 0, decodable = 0;
  for (int row = 0; row < 28; row += 2) {
    for (int col = 0; col < 32; col += 2) {
      const uint32_t wtx = (world_x >> 3) + (uint32_t)col;
      const uint32_t wty = (world_y >> 3) + (uint32_t)row;
      uint16_t decoded;
      if (!Dkc1VideoDecodeLevelTile(layout, bank, map_base, metatile_base,
                                    wtx, wty, &decoded))
        continue;
      decodable++;
      const uint16_t live =
          g_ppu->vram[Dkc1RollingMapWord(ppu_map_base, wtx, wty) & 0x7fffu];
      if (live == decoded)
        matches++;
    }
  }
  if (decodable_out)
    *decodable_out = decodable;
  return matches;
}

static bool Dkc1PrepareWidescreenShadow(uint8_t layer_mask,
                                        int presentation_bias) {
  const uint32_t camera_x = Dkc1ReadWram16(0x088b);
  const uint32_t camera_y = Dkc1ReadWram16(0x0895);
  const uint64_t source_signature = Dkc1LevelSourceSignature();
  const uint16_t stream_vram = Dkc1ReadWram16(0x1b13);
  const uint8_t map_bank = g_ram[0x00d5];
  const uint16_t map_base = Dkc1ReadWram16(0x00d3);
  const uint16_t metatile_base = Dkc1ReadWram16(0x1b11);
  const int terrain_layer =
      Dkc1VideoTerrainLayer(layer_mask, g_ppu->bgXsc, stream_vram);

  if (!s_ws_shadow_active) {
    WsShadowReset();
    memset(s_ws_origin_valid, 0, sizeof s_ws_origin_valid);
    s_ws_shadow_active = true;
  }
  if (!s_ws_source_valid || source_signature != s_ws_source_signature) {
    WsShadowReset();
    memset(s_ws_origin_valid, 0, sizeof s_ws_origin_valid);
    s_ws_source_signature = source_signature;
    s_ws_source_valid = true;
    s_ws_layout = kDkc1LayoutUnknown;
    s_ws_layout_grace = 0;
  }

  const int keep_tiles = Dkc1VideoExtra() / 8 + 2;
  for (int layer = 0; layer < 2; layer++) {
    const uint8_t bit = (uint8_t)(1u << layer);
    if (!(layer_mask & bit)) {
      s_ws_origin_valid[layer] = false;
      continue;
    }
    if (layer == terrain_layer) {
      s_ws_world_x[layer] = Dkc1VideoUnwrapPpuScroll(
          (uint16_t)(g_ppu->hScroll[layer] + presentation_bias), camera_x);
      s_ws_world_y[layer] = Dkc1VideoUnwrapPpuScroll(
          (uint16_t)g_ppu->vScroll[layer], camera_y);
    } else {
      const uint32_t anchor_x = s_ws_origin_valid[layer]
                                    ? s_ws_world_x[layer] : camera_x;
      const uint32_t anchor_y = s_ws_origin_valid[layer]
                                    ? s_ws_world_y[layer] : camera_y;
      s_ws_world_x[layer] = Dkc1VideoUnwrapPpuScroll(
          (uint16_t)(g_ppu->hScroll[layer] + presentation_bias), anchor_x);
      s_ws_world_y[layer] = Dkc1VideoUnwrapPpuScroll(
          (uint16_t)g_ppu->vScroll[layer], anchor_y);
    }
    s_ws_origin_valid[layer] = true;

    WsShadowSetWorld(layer, s_ws_world_x[layer], s_ws_world_y[layer]);
    WsShadowSetScroll(layer,
                      (uint16_t)(g_ppu->hScroll[layer] + presentation_bias),
                      g_ppu->vScroll[layer]);
    WsShadowSetWestKeep(layer, keep_tiles);
    WsShadowSetEastKeep(layer, keep_tiles);
    /* Keep the default absolute-world Y key. RetainHistory intentionally
     * switches the shared shadow to viewport-relative Y keys; DKC1's ROM
     * decoder fills absolute world tile rows, so enabling it made every
     * margin lookup miss and exposed the transparent fallback as a hard
     * vertical cutoff. DKC2's exact-prefill path likewise leaves this off. */
    WsShadowSetRespectGameWrites(layer, layer == terrain_layer ? 1 : 0);
    uint16_t blank_entry = 0;
    if (!PPU_bigTiles(g_ppu, layer))
      Dkc1VideoFindTransparent4bppTile(
          g_ppu->vram, 0x8000u,
          (uint16_t)PPU_bgTileAdr(g_ppu, layer), &blank_entry);
    WsShadowSetBlankTile(layer, blank_entry);
    /* Parallax backdrops are horizontally periodic; fold their margins to
     * the congruent native column instead of exposing unwritten map. */
    if (layer == 1 && layer != terrain_layer)
      WsShadowSetPeriodicFold(layer);
  }

  WsShadowFrame(g_ppu);

  if (terrain_layer < 0 || PPU_bigTiles(g_ppu, terrain_layer))
    return false;

  /* Runtime layout calibration: decode the native window from ROM and
   * require it to agree with the live rolling tilemap before trusting the
   * decoder for margin prefill. Re-checked continuously so level
   * transitions and unforeseen layouts fail safe (blank margins). */
  const uint16_t ppu_map_base =
      (uint16_t)PPU_bgTilemapAdr(g_ppu, terrain_layer);
  const uint32_t wx = s_ws_world_x[terrain_layer];
  const uint32_t wy = s_ws_world_y[terrain_layer];
  Dkc1LevelLayout best = kDkc1LayoutUnknown;
  int best_matches = 0, best_decodable = 0;
  for (int candidate = kDkc1LayoutHorizontal;
       candidate <= kDkc1LayoutVertical; candidate++) {
    int decodable = 0;
    int matches = Dkc1CalibrateLayout(
        (Dkc1LevelLayout)candidate, ppu_map_base, map_bank, map_base,
        metatile_base, wx, wy, &decodable);
    if (matches > best_matches) {
      best_matches = matches;
      best_decodable = decodable;
      best = (Dkc1LevelLayout)candidate;
    }
  }
  const bool calibrated =
      best_decodable >= 64 && best_matches * 10 >= best_decodable * 7;
  if (calibrated) {
    s_ws_layout = best;
    /* Tolerate at most two isolated dynamic-tile mismatches. Confidence must
     * not accumulate with play time: the former saturating 1000-frame hold
     * could expose stale level art for seconds after a scene transition. */
    s_ws_layout_grace = 2;
  } else if (s_ws_layout_grace > 0) {
    s_ws_layout_grace--;
    if (s_ws_layout_grace == 0)
      s_ws_layout = kDkc1LayoutUnknown;
  } else {
    s_ws_layout = kDkc1LayoutUnknown;
  }
  if (s_ws_layout == kDkc1LayoutUnknown)
    return false;

  /* Prefill the margin columns (plus one guard tile each side) from ROM. */
  uint16_t blank_entry = 0;
  Dkc1VideoFindTransparent4bppTile(
      g_ppu->vram, 0x8000u,
      (uint16_t)PPU_bgTileAdr(g_ppu, terrain_layer), &blank_entry);
  /* Round the partial 43-pixel side margin up, then keep one complete guard
   * tile for fine scroll.  The old floor division seeded only six columns;
   * a nonzero scroll phase could sample the unseeded seventh column as the
   * thin black strip at the far-left edge. */
  const int margin_tiles = (Dkc1VideoExtra() + 7) / 8 + 1;
  const int visible_rows = (kDkc1VideoHeight >> 3) + 2;
  for (int side = 0; side < 2; side++) {
    for (int i = 0; i < margin_tiles; i++) {
      const int64_t signed_wtx =
          side == 0 ? (int64_t)(wx >> 3) - 1 - i
                    : (int64_t)(wx >> 3) + 32 + i;
      if (signed_wtx < 0)
        continue;
      const uint32_t wtx = (uint32_t)signed_wtx;
      for (int row = -1; row < visible_rows; row++) {
        const int64_t signed_wty = (int64_t)(wy >> 3) + row;
        if (signed_wty < 0)
          continue;
        const uint32_t wty = (uint32_t)signed_wty;
        uint16_t entry;
        if (!Dkc1VideoDecodeLevelTile(s_ws_layout, map_bank, map_base,
                                      metatile_base, wtx, wty, &entry))
          entry = blank_entry;
        WsShadowForceTile(terrain_layer, wtx, wty, entry);
      }
    }
  }
  return true;
}

void Dkc1DrawPpuFrame(void) {
  SimpleHdma channels[8];
  bool active[8] = {false};

  /* Widescreen is host-only presentation policy, reapplied every frame. */
  uint8_t wide_layer_mask =
      Dkc1VideoIsWidescreen()
          ? Dkc1VideoPpuWideLayerMask(g_ppu->bgmode, g_ppu->bgXsc,
                                      g_ppu->screenEnabled[0],
                                      g_ppu->screenEnabled[1])
          : 0;
  /* A Mode-1/64-column register shape is necessary but not sufficient:
   * logos and fixed screens can temporarily retain the same PPU shape. Build
   * and calibrate the world-keyed shadow first, then widen only a proven
   * level layout. This prevents stale gameplay/logo data in the margins. */
  const int presentation_bias =
      wide_layer_mask != 0 ? Dkc1WidescreenPresentationBias() : 0;
  const bool extend_world =
      wide_layer_mask != 0 &&
      Dkc1PrepareWidescreenShadow(wide_layer_mask, presentation_bias);
  if (extend_world) {
    Dkc1VideoSetPresentationBias(presentation_bias);
    PpuSetExtraSpace(g_ppu, (uint8_t)Dkc1VideoExtra());
    PpuSetWidescreenPresentationXBias(g_ppu, presentation_bias);
    PpuSetWidescreenLayerMask(g_ppu, wide_layer_mask);
    /* Repeat the native edge scanline for enabled, bounded background
     * planes. Jungle Hijinxs uses a 32-column BG3 sky behind independently
     * widened 64-column BG1/BG2; clamping BG3 exposed black from the host
     * margin up to the first fine-scroll tile boundary. This repeat is only
     * armed after the terrain decoder validates a gameplay scene. */
    uint8_t enabled = (uint8_t)((g_ppu->screenEnabled[0] |
                                 g_ppu->screenEnabled[1]) & 0x07u);
    const int terrain_layer = Dkc1VideoTerrainLayer(
        wide_layer_mask, g_ppu->bgXsc, Dkc1ReadWram16(0x1b13));
    const uint8_t terrain_bit =
        terrain_layer >= 0 ? (uint8_t)(1u << terrain_layer) : 0;
    /* Only the stream-selected terrain plane has an exact ROM/world decoder.
     * DKC1's other 64-column plane is parallax staging data, not a second
     * copy of the level map. Rendering it through the world shadow produced
     * 100% margin misses and transparent cutoffs. Match DKC2's proven policy:
     * repeat every enabled non-terrain BG from its authentic native scanline. */
    PpuSetWidescreenLayerRepeat(
        g_ppu, (uint8_t)(enabled & (uint8_t)~terrain_bit));
    Dkc1VideoSetTerrainReady(true);
  } else if (Dkc1VideoIsWidescreen()) {
    Dkc1ResetWidescreenShadow();
    /* Pillarbox fixed screens (logos, map, title): clear the host row and
     * center the authentic 256 columns. */
    size_t row_bytes = (size_t)Dkc1VideoWidth() * kDkc1VideoBytesPerPixel;
    for (int y = 0; y < kDkc1VideoHeight; y++)
      memset(g_ppu->renderBuffer + (size_t)y * g_ppu->renderPitch,
             0, row_bytes);
    PpuSetExtraSpaceCentered(g_ppu, (uint8_t)Dkc1VideoExtra());
    PpuSetWidescreenPresentationXBias(g_ppu, 0);
  } else {
    Dkc1ResetWidescreenShadow();
    PpuSetExtraSpace(g_ppu, 0);
    PpuSetWidescreenPresentationXBias(g_ppu, 0);
  }

  dma_startDma(g_dma, g_snesrecomp_last_hdmaen, true);
  for (int channel = 0; channel < 8; channel++) {
    active[channel] = g_dma->channel[channel].hdmaActive;
    if (active[channel])
      SimpleHdma_Init(&channels[channel], &g_dma->channel[channel]);
  }

  for (int line = 0; line <= 224; line++) {
    if (extend_world && presentation_bias != 0) {
      for (int layer = 0; layer < 4; layer++)
        g_ppu->hScroll[layer] =
            (uint16_t)(g_ppu->hScroll[layer] + presentation_bias);
    }
    ppu_runLine(g_ppu, line);
    if (extend_world && presentation_bias != 0) {
      for (int layer = 0; layer < 4; layer++)
        g_ppu->hScroll[layer] =
            (uint16_t)(g_ppu->hScroll[layer] - presentation_bias);
    }
    for (int channel = 0; channel < 8; channel++) {
      if (active[channel]) SimpleHdma_DoLine(&channels[channel]);
    }
  }

  /* Model the VBlank boundary after the visible lines so the PPU reloads its
   * internal OAM port from OAMADD before the next frame's OAM DMA. */
  (void)ppu_checkOverscan(g_ppu);
  ppu_handleVblank(g_ppu);
}

uint32_t Dkc1ResumePc(void) {
  return s_resume_pc;
}

int Dkc1LastLleResult(void) {
  return s_last_lle_result;
}

/* Required neutral hooks declared by generated funcs.h. */
void RunOneFrameOfGame_Internal(void) {
  Dkc1RunOneFrame();
}

void ResetSpritesFunc(int first) {
  (void)first;
}
