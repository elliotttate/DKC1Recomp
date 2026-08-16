#ifndef DKC1_DEBUG_DUMP_H
#define DKC1_DEBUG_DUMP_H

#include <stdbool.h>
#include <stddef.h>
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
 *   DKC1_LIFECYCLE_SAMPLE_EVERY_FRAME=1 adds exact active-actor samples;
 *                             required before the prefetch phase auditor may
 *                             call a stock-allocation-frame match harmless
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

/* Decision-level companion to DKC1_LIFECYCLE_TRACE. These calls are inert
 * unless that trace is enabled. They record why a placed actor was first
 * held, released at the reconstructed stock window, or accepted as a native
 * allocation, and when the gameplay context—not PPU presentation state—was
 * reset. */
void Dkc1DebugTracePlacedActorContext(uint16_t mode, uint16_t level,
                                      uint16_t entrance);
void Dkc1DebugTracePlacedActorPhase(const char *event,
                                    uint16_t actor_index, uint16_t id,
                                    uint16_t source, uint16_t source_x,
                                    uint16_t current_left,
                                    uint16_t current_right,
                                    uint16_t stock_left,
                                    uint16_t stock_right,
                                    bool terrain_ready);

/* Record the complete write set made by one prefetched actor dispatch before
 * the transaction is rolled back.  The lifecycle JSON separates writes to
 * the actor's own indexed fields from other actor slots, OAM, source-record
 * bookkeeping, and global WRAM.  This is the fail-closed evidence used to
 * decide whether a sprite class can become a presentation-only margin proxy. */
void Dkc1DebugTracePrefetchTransaction(uint16_t actor_index, uint16_t id,
                                       uint16_t source,
                                       const uint8_t *before,
                                       const uint8_t *after, size_t size);
void Dkc1DebugDumpClose(void);

#endif
