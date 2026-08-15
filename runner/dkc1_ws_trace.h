#ifndef DKC1_WS_TRACE_H
#define DKC1_WS_TRACE_H

#include "snes/ws_shadow.h"

#include <stdbool.h>
#include <stdint.h>

typedef struct Dkc1WsTraceFrame {
  int frame;
  uint8_t wide_layer_mask;
  uint8_t repeat_layer_mask;
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
  bool reset;
  bool cold_start;
  bool source_reset;
  bool identity_reset;
  bool bounds_ready;
  bool calibration_accepted;
  bool grace_accepted;
  bool shadow_commit;
  bool shadow_frame;
  bool prefill;
  bool edge_extension;
  bool centered_fallback;
  WsShadowMarginStat shadow_before[2];
  WsShadowMarginStat shadow_after[2];
} Dkc1WsTraceFrame;

/* Default-off. DKC1_WS_TRACE=1 writes dkc1_ws_trace.jsonl; any other nonzero
 * value is treated as a path. Initialization failure disables the trace. */
bool Dkc1WsTraceEnabled(void);
void Dkc1WsTraceEmit(const Dkc1WsTraceFrame *frame);

#endif
