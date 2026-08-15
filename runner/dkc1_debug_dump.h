#ifndef DKC1_DEBUG_DUMP_H
#define DKC1_DEBUG_DUMP_H

#include <stdbool.h>
#include <stdint.h>

/* Default-off headless evidence taps, all env-gated and inert when unset:
 *
 *   DKC1_WRAM_HASH_LOG=path   per-frame "frame fnv64(wram)" text lines
 *                             (full-WRAM ordered fingerprints; the cheap
 *                             pass-1 primitive for first-divergence search;
 *                             ranged raw dumps live in wram_dump.c)
 *   DKC1_OAM_LOG=prefix       per-frame records (frame, WRAM shadow $0200-
 *                             $041F, PPU OAM+high) -> prefix.bin + .jsonl
 *   DKC1_LIFECYCLE_TRACE=path transition-only actor/scanner/section JSONL
 *   DKC1_SESSION_DIR=dir      checkpoint evidence directory (default
 *                             "session"): NAME.wram.bin + checkpoints.jsonl
 *   DKC1_INPUT_RECORD=path    resolved per-frame joypad masks, one hex mask
 *                             per line — a valid input-playback file, so a
 *                             predicate-driven script route can be replayed
 *                             as an exact-frame schedule in both widescreen
 *                             modes (differential runs must not let waits
 *                             re-time themselves after a divergence)
 *
 * Call once per completed frame, after the game frame and PPU draw.
 */
void Dkc1DebugDumpFrame(int frame);

/* Record the frame's resolved input mask (before RtlRunFrame). */
void Dkc1DebugRecordInput(uint32_t mask);

/* Record a named checkpoint (script `checkpoint NAME`): full WRAM dump plus
 * a JSONL row with WRAM/VRAM/OAM-shadow/PPU-OAM sha256 hashes. */
bool Dkc1DebugCheckpoint(const char *name, int frame);

void Dkc1DebugDumpClose(void);

#endif
