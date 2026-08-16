#ifndef DKC1_WS_TRACE_H
#define DKC1_WS_TRACE_H

#include "snes/ws_shadow.h"
#include "dkc1_video.h"

#include <stdbool.h>
#include <stdint.h>

typedef struct Dkc1WsTraceFrame {
  int frame;
  uint8_t wide_layer_mask;
  uint8_t render_layer_mask;
  uint8_t repeat_layer_mask;
  uint8_t prepare_bgmode;
  uint8_t prepare_inidisp;
  uint8_t prepare_main_layers;
  uint8_t prepare_sub_layers;
  uint8_t prepare_bgsc[4];
  uint16_t prepare_hscroll[4];
  uint16_t prepare_vscroll[4];
  int terrain_layer;
  int presentation_bias;
  int selected_layout;
  int layout_grace;
  int margin_tiles;
  uint64_t identity_hash;
  uint32_t identity_change_mask;
  int calibration_matches[2];
  int calibration_decodable[2];
  bool world_valid[2];
  uint32_t world_x[2];
  uint32_t world_y[2];
  bool shadow_origin_valid[2];
  uint32_t shadow_origin_x[2];
  uint32_t shadow_origin_y[2];
  uint32_t shadow_local_x[2];
  uint32_t shadow_local_y[2];
  bool reset;
  bool cold_start;
  bool source_reset;
  bool identity_reset;
  bool bounds_ready;
  bool calibration_accepted;
  bool grace_accepted;
  bool stream_revalidated;
  bool shadow_commit;
  bool shadow_frame;
  bool prefill;
  bool edge_extension;
  bool cartridge_stream_ready;
  Dkc1VideoStreamCoverageStats stream_coverage;
  bool centered_fallback;
  bool debug_forced_fallback;
  WsShadowMarginStat shadow_before[2];
  WsShadowMarginStat shadow_after[2];
} Dkc1WsTraceFrame;

/* Default-off. DKC1_WS_TRACE=1 writes dkc1_ws_trace.jsonl; any other nonzero
 * value is treated as a path. Initialization failure disables the trace. */
bool Dkc1WsTraceEnabled(void);
void Dkc1WsTraceEmit(const Dkc1WsTraceFrame *frame);

#endif
