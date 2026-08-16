#pragma once

#include <stddef.h>
#include <stdint.h>

#include "common_cpu_infra.h"

const RtlGameInfo *Dkc1GameInfo(void);
void Dkc1BeginDrawing(uint8_t *pixels, size_t pitch);
void Dkc1DrawPpuFrame(void);
uint32_t Dkc1ResumePc(void);
int Dkc1LastLleResult(void);

/* Host-only widescreen diagnostics. These alter only presentation and are
 * never serialized or observed by the recompiled game. */
void Dkc1DebugSetLayerMask(uint8_t mask);
uint8_t Dkc1DebugLayerMask(void);
void Dkc1DebugSetProvenanceOverlay(int enabled);
int Dkc1DebugProvenanceOverlay(void);

/* Import a portable bundle emitted by tools/SuperZSNESStateExporter. This is
 * deliberately game-specific: it maps DKC's frame-boundary v0.230 state into
 * the recomp execution host and fails closed on active DMA or unsupported
 * state shapes. The DSP is register-faithful but its private interpolation
 * history is reconstructed, so callers must not use the first audio buffer as
 * a bit-exact oracle. */
int Dkc1ImportSuperZsnesState(const char *bundle_directory,
                              char *error, size_t error_size);
