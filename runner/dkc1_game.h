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
