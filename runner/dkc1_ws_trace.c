#include "dkc1_ws_trace.h"

#include "common_rtl.h"
#include "dkc1_video.h"
#include "snes/ppu.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static FILE *s_trace_file;
static bool s_trace_initialized;
static bool s_trace_enabled;

static uint16_t ReadWram16(uint16_t address) {
  return (uint16_t)g_ram[address] |
         ((uint16_t)g_ram[(uint16_t)(address + 1u)] << 8);
}

bool Dkc1WsTraceEnabled(void) {
  if (s_trace_initialized)
    return s_trace_enabled;
  s_trace_initialized = true;

  const char *setting = getenv("DKC1_WS_TRACE");
  if (!setting || !setting[0] || strcmp(setting, "0") == 0)
    return false;
  const char *path = strcmp(setting, "1") == 0
                         ? "dkc1_ws_trace.jsonl" : setting;
  s_trace_file = fopen(path, "wb");
  if (!s_trace_file) {
    fprintf(stderr, "DKC1_WS_TRACE: could not open %s\n", path);
    return false;
  }
  s_trace_enabled = true;
  fprintf(stderr, "DKC1_WS_TRACE: writing %s\n", path);
  return true;
}

static uint64_t Fnv1a(const void *data, size_t size, uint64_t hash) {
  const uint8_t *bytes = (const uint8_t *)data;
  for (size_t i = 0; i < size; i++) {
    hash ^= bytes[i];
    hash *= UINT64_C(1099511628211);
  }
  return hash;
}

static uint64_t HashFrameRegion(int x, int width) {
  if (!g_ppu || !g_ppu->renderBuffer || width <= 0)
    return 0;
  uint64_t hash = UINT64_C(1469598103934665603);
  for (int y = 0; y < kDkc1VideoHeight; y++) {
    const uint8_t *row = g_ppu->renderBuffer +
        (size_t)y * g_ppu->renderPitch +
        (size_t)x * kDkc1VideoBytesPerPixel;
    hash = Fnv1a(row, (size_t)width * kDkc1VideoBytesPerPixel, hash);
  }
  return hash;
}

static uint64_t HashShadowMargin(const Dkc1WsTraceFrame *frame,
                                 int layer, int side) {
  if (!frame->edge_extension || layer < 0 || layer >= 2 ||
      !frame->world_valid[layer])
    return 0;
  /* margin_tiles is only recorded on frames whose prefill ran. Widened
   * frames without prefill (calibration grace) are exactly the frames the
   * margin-nondeterminism investigation needs hashed, so fall back to the
   * geometric margin extent instead of skipping them. */
  int margin_tiles = frame->margin_tiles;
  if (margin_tiles <= 0)
    margin_tiles = (Dkc1VideoExtra() + 7) / 8 + 1;
  if (margin_tiles <= 0)
    return 0;
  uint64_t hash = UINT64_C(1469598103934665603);
  const uint32_t tx0 = frame->world_x[layer] >> 3;
  const uint32_t ty0 = frame->world_y[layer] >> 3;
  for (int row = -1; row < 30; row++) {
    const int64_t ty = (int64_t)ty0 + row;
    for (int col = 0; col < margin_tiles; col++) {
      const int64_t tx = side == 0
          ? (int64_t)tx0 - margin_tiles + col
          : (int64_t)tx0 + 32 + col;
      uint8_t state = 0;
      uint16_t entry = 0;
      if (tx >= 0 && ty >= 0)
        state = (uint8_t)WsShadowDebugCell(
            layer, (uint32_t)tx, (uint32_t)ty, &entry);
      hash = Fnv1a(&state, sizeof state, hash);
      hash = Fnv1a(&entry, sizeof entry, hash);
    }
  }
  return hash;
}

static uint64_t Delta(uint64_t before, uint64_t after) {
  return after >= before ? after - before : after;
}

static void PrintShadow(FILE *out, const WsShadowMarginStat *before,
                        const WsShadowMarginStat *after) {
  fprintf(out,
      "{\"west_hit\":%llu,\"west_miss\":%llu,"
      "\"east_hit\":%llu,\"east_miss\":%llu,"
      "\"prefill_seed\":%llu,\"prefill_refresh\":%llu,"
      "\"west_fold\":%llu,\"east_fold\":%llu,"
      "\"west_blank\":%llu,\"east_blank\":%llu,"
      "\"west_continuation\":%llu,\"east_continuation\":%llu,"
      "\"west_raw\":%llu,\"east_raw\":%llu}",
      (unsigned long long)Delta(before->westHit, after->westHit),
      (unsigned long long)Delta(before->westMiss, after->westMiss),
      (unsigned long long)Delta(before->eastHit, after->eastHit),
      (unsigned long long)Delta(before->eastMiss, after->eastMiss),
      (unsigned long long)Delta(before->prefillSeed, after->prefillSeed),
      (unsigned long long)Delta(before->prefillRefresh, after->prefillRefresh),
      (unsigned long long)Delta(before->westFold, after->westFold),
      (unsigned long long)Delta(before->eastFold, after->eastFold),
      (unsigned long long)Delta(before->westBlank, after->westBlank),
      (unsigned long long)Delta(before->eastBlank, after->eastBlank),
      (unsigned long long)Delta(before->westRawContinuation,
                                after->westRawContinuation),
      (unsigned long long)Delta(before->eastRawContinuation,
                                after->eastRawContinuation),
      (unsigned long long)Delta(before->westRawFallback,
                                after->westRawFallback),
      (unsigned long long)Delta(before->eastRawFallback,
                                after->eastRawFallback));
}

void Dkc1WsTraceEmit(const Dkc1WsTraceFrame *frame) {
  if (!frame || !Dkc1WsTraceEnabled() || !s_trace_file)
    return;

  const int extra = Dkc1VideoExtra();
  const uint64_t left_hash = HashFrameRegion(0, extra);
  const uint64_t center_hash = HashFrameRegion(extra, 256);
  const uint64_t right_hash = HashFrameRegion(extra + 256, extra);
  const uint64_t bg1_left = HashShadowMargin(frame, 0, 0);
  const uint64_t bg1_right = HashShadowMargin(frame, 0, 1);
  const uint64_t bg2_left = HashShadowMargin(frame, 1, 0);
  const uint64_t bg2_right = HashShadowMargin(frame, 1, 1);
  const uint64_t vram_hash = Fnv1a(
      g_ppu->vram, sizeof g_ppu->vram, UINT64_C(1469598103934665603));
  const uint64_t cgram_hash = Fnv1a(
      g_ppu->cgram, sizeof g_ppu->cgram,
      UINT64_C(1469598103934665603));
  uint64_t ppu_oam_hash = Fnv1a(
      g_ppu->oam, sizeof g_ppu->oam, UINT64_C(1469598103934665603));
  ppu_oam_hash = Fnv1a(g_ppu->highOam, sizeof g_ppu->highOam,
                       ppu_oam_hash);
  const uint64_t wram_oam_hash = Fnv1a(
      g_ram + 0x0200, 0x0220, UINT64_C(1469598103934665603));

  fprintf(s_trace_file,
      "{\"schema\":\"dkc1.ws.frame.v1\",\"frame\":%d,"
      "\"scene\":{\"mode\":%u,\"level\":%u,\"entrance\":%u,"
      "\"fade\":%u},"
      "\"source\":{\"bank\":%u,\"map\":%u,\"metatiles\":%u,"
      "\"stream_vram\":%u},"
      "\"identity\":{\"hash\":\"%016llx\",\"change_mask\":%u},"
      "\"camera\":{\"x\":%u,\"y\":%u,\"lower\":%u,"
      "\"upper\":%u,\"presentation_bias\":%d},"
      "\"ppu\":{\"mode\":%u,\"bgmode\":%u,\"inidisp\":%u,"
      "\"main\":%u,\"sub\":%u,\"bgsc\":[%u,%u,%u,%u],"
      "\"h\":[%u,%u,%u,%u],\"v\":[%u,%u,%u,%u],"
      "\"wide_mask\":%u,\"render_mask\":%u,\"repeat_mask\":%u,"
      "\"terrain_layer\":%d},"
      "\"calibration\":{\"horizontal\":[%d,%d],"
      "\"vertical\":[%d,%d],\"selected\":%d,\"grace\":%d},"
      "\"decision\":{\"reset\":%u,\"cold_start\":%u,"
      "\"source_reset\":%u,\"identity_reset\":%u,"
      "\"bounds_ready\":%u,"
      "\"calibration_accepted\":%u,\"grace_accepted\":%u,"
      "\"shadow_commit\":%u,\"shadow_frame\":%u,\"prefill\":%u,"
      "\"edge_extension\":%u,\"centered_fallback\":%u},"
      "\"world\":[{\"valid\":%u,\"x\":%u,\"y\":%u},"
      "{\"valid\":%u,\"x\":%u,\"y\":%u}],"
      "\"margin_tiles\":%d,\"shadow_delta\":[",
      frame->frame, (unsigned)ReadWram16(0x0032),
      (unsigned)ReadWram16(0x0030), (unsigned)ReadWram16(0x003e),
      (unsigned)ReadWram16(0x1df1),
      (unsigned)g_ram[0x00d5], (unsigned)ReadWram16(0x00d3),
      (unsigned)ReadWram16(0x1b11), (unsigned)ReadWram16(0x1b13),
      (unsigned long long)frame->identity_hash,
      (unsigned)frame->identity_change_mask,
      (unsigned)ReadWram16(0x088b), (unsigned)ReadWram16(0x0895),
      (unsigned)ReadWram16(0x1b23), (unsigned)ReadWram16(0x1b25),
      frame->presentation_bias, (unsigned)(g_ppu->bgmode & 7u),
      (unsigned)g_ppu->bgmode, (unsigned)g_ppu->inidisp,
      (unsigned)g_ppu->screenEnabled[0],
      (unsigned)g_ppu->screenEnabled[1],
      (unsigned)g_ppu->bgXsc[0], (unsigned)g_ppu->bgXsc[1],
      (unsigned)g_ppu->bgXsc[2], (unsigned)g_ppu->bgXsc[3],
      (unsigned)g_ppu->hScroll[0], (unsigned)g_ppu->hScroll[1],
      (unsigned)g_ppu->hScroll[2], (unsigned)g_ppu->hScroll[3],
      (unsigned)g_ppu->vScroll[0], (unsigned)g_ppu->vScroll[1],
      (unsigned)g_ppu->vScroll[2], (unsigned)g_ppu->vScroll[3],
      (unsigned)frame->wide_layer_mask,
      (unsigned)frame->render_layer_mask,
      (unsigned)frame->repeat_layer_mask, frame->terrain_layer,
      frame->calibration_matches[0], frame->calibration_decodable[0],
      frame->calibration_matches[1], frame->calibration_decodable[1],
      frame->selected_layout, frame->layout_grace,
      frame->reset ? 1u : 0u, frame->cold_start ? 1u : 0u,
      frame->source_reset ? 1u : 0u, frame->identity_reset ? 1u : 0u,
      frame->bounds_ready ? 1u : 0u,
      frame->calibration_accepted ? 1u : 0u,
      frame->grace_accepted ? 1u : 0u,
      frame->shadow_commit ? 1u : 0u,
      frame->shadow_frame ? 1u : 0u,
      frame->prefill ? 1u : 0u, frame->edge_extension ? 1u : 0u,
      frame->centered_fallback ? 1u : 0u,
      frame->world_valid[0] ? 1u : 0u, frame->world_x[0],
      frame->world_y[0], frame->world_valid[1] ? 1u : 0u,
      frame->world_x[1], frame->world_y[1], frame->margin_tiles);
  PrintShadow(s_trace_file, &frame->shadow_before[0],
              &frame->shadow_after[0]);
  fputc(',', s_trace_file);
  PrintShadow(s_trace_file, &frame->shadow_before[1],
              &frame->shadow_after[1]);
  fprintf(s_trace_file,
      "],\"hash\":{\"left\":\"%016llx\","
      "\"center\":\"%016llx\",\"right\":\"%016llx\","
      "\"bg1_left\":\"%016llx\",\"bg1_right\":\"%016llx\","
      "\"bg2_left\":\"%016llx\",\"bg2_right\":\"%016llx\","
      "\"vram\":\"%016llx\",\"cgram\":\"%016llx\","
      "\"ppu_oam\":\"%016llx\","
      "\"wram_oam\":\"%016llx\"}}\n",
      (unsigned long long)left_hash, (unsigned long long)center_hash,
      (unsigned long long)right_hash, (unsigned long long)bg1_left,
      (unsigned long long)bg1_right, (unsigned long long)bg2_left,
      (unsigned long long)bg2_right, (unsigned long long)vram_hash,
      (unsigned long long)cgram_hash,
      (unsigned long long)ppu_oam_hash, (unsigned long long)wram_oam_hash);
  fflush(s_trace_file);
}
