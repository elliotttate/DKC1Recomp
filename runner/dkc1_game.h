#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "common_cpu_infra.h"
#include "dkc1_framegen.h"

const RtlGameInfo *Dkc1GameInfo(void);
void Dkc1BeginDrawing(uint8_t *pixels, size_t pitch);
void Dkc1DrawPpuFrame(void);
/* Host-only in-between frame for 120 Hz presentation. Renders the scene of
 * the last drawn frame with every background line and sprite moved halfway
 * back toward the frame before it, into `pixels` (same width and pitch
 * contract as Dkc1BeginDrawing). Returns false when no usable pair exists;
 * the caller then repeats the previous real frame. Cartridge state, the real
 * framebuffer, and every PPU register are left byte-identical. Only runs
 * when Dkc1FrameGenSetEnabled(true) was called by the host. */
bool Dkc1DrawInterpolatedFrame(uint8_t *pixels, size_t pitch,
                               Dkc1FrameGenStats *stats);
/* Queue the current raw frame and return delayed F plus its following F+0.5.
 * Adds four display frames of look-ahead; does not advance emulation. */
void Dkc1DrawSmoothedFrames(const uint8_t *real, uint8_t *display,
                           uint8_t *mid, bool have_mid,
                           Dkc1FrameGenStats *stats);
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
