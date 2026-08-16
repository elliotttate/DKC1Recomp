#ifndef DKC1_VIDEO_H
#define DKC1_VIDEO_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum {
  kDkc1VideoNativeWidth = 256,
  kDkc1VideoHeight = 224,
  /* SNES pixels present at a 7:6 pixel aspect. 342 source columns at 224
   * lines give 1.78125, within one pixel of exact 16:9 (same policy as
   * DKC2Recomp). Widescreen stays disabled until the DKC1 terrain
   * reconstruction is audited; the buffer is sized for it up front. */
  kDkc1VideoWidescreenExtra = 43,
  kDkc1VideoWidescreenWidth =
      kDkc1VideoNativeWidth + 2 * kDkc1VideoWidescreenExtra,
  kDkc1VideoBytesPerPixel = 4,
};

/* These symbols are the shared snesrecomp widescreen runtime contract. */
extern bool g_ws_active;
extern int g_ws_extra;

void Dkc1VideoSetWidescreen(bool enabled);
bool Dkc1VideoIsWidescreen(void);
void Dkc1VideoSetTerrainReady(bool ready);
bool Dkc1VideoTerrainReady(void);
int Dkc1VideoWidth(void);
int Dkc1VideoExtra(void);
size_t Dkc1VideoPixelCount(void);

/*
 * Generated-code presentation adapters.  These deliberately key from the
 * terrain-ready latch instead of the widescreen setting alone: fixed screens
 * and unrecognized level layouts retain the cartridge's exact cull windows.
 * The logical camera and collision/exit bounds are never changed.
 */
uint16_t Dkc1VideoExpandCullLeft(uint16_t native_margin);
uint16_t Dkc1VideoExpandCullSpan(uint16_t native_span);
/* Placed-object scanner constants stay native when the host-owned margin
 * proxy experiment is enabled; other sprite/OAM culls remain widened. */
uint16_t Dkc1VideoObjectScannerCullLeft(uint16_t native_margin);
uint16_t Dkc1VideoObjectScannerCullSpan(uint16_t native_span);
uint16_t Dkc1VideoPromoteOamXHigh(uint16_t screen_x);
void Dkc1VideoSetPresentationBias(int bias);
int Dkc1VideoPresentationBias(void);

/* Private vertical-rope renderer adapters.  BiasCullX is used only for the
 * existing visibility comparisons; the original coordinate remains in the
 * game's $76 scratch word and is still written to OAM.  The generated
 * adapter retains that original X before $76 is repacked as Y:X-low. The
 * final merge owns both the size and adjacent X-high bits so a reused OAM
 * slot cannot leak an old X-high bit into a rope segment. */
uint16_t Dkc1VideoBiasCullX(uint16_t screen_x);
uint16_t Dkc1VideoInitialBackstep(struct CpuState *cpu,
                                  uint16_t native_backstep);
uint16_t Dkc1VideoInitialColumnCount(struct CpuState *cpu,
                                     uint16_t native_count);
uint16_t Dkc1VideoSelectStreamX(struct CpuState *cpu,
                                uint16_t stock_stream_x);
bool Dkc1VideoCartridgeTerrainReady(const uint8_t *wram);
void Dkc1VideoInvalidateStreamCoverage(void);

typedef struct Dkc1VideoStreamCoverageStats {
  uint16_t mode;
  uint16_t level;
  uint16_t entrance;
  uint16_t last_layer_x;
  uint16_t last_selected_x;
  uint8_t unique_columns;
  uint8_t required_columns;
  uint32_t initial_count_calls;
  uint32_t initial_count_rejected;
  uint32_t selector_calls;
  uint32_t observed_columns;
  bool context_valid;
  bool ready;
} Dkc1VideoStreamCoverageStats;

void Dkc1VideoGetStreamCoverageStats(Dkc1VideoStreamCoverageStats *stats);
uint16_t Dkc1VideoPromoteOamSizeMask(uint16_t size_mask,
                                    uint16_t screen_x);
uint16_t Dkc1VideoMergeOamSizeAndXHigh(uint16_t existing_word,
                                      uint16_t size_mask,
                                      uint16_t screen_x);

struct CpuState;
/* The stock vertical row builder refreshes only 36 tile entries before its
 * full 64-entry ring DMA.  That is enough for the native viewport, but wide
 * vertical motion can expose stale entries beyond it.  Generated wrappers
 * call Begin at the authentic row-body entry and Advance immediately after
 * CODE_818A18 has copied the staged row.  Advance requests one tail-call for
 * the second pass, then restores the cartridge's Layer1 X exactly. */
void Dkc1VideoBeginWideRowBuild(struct CpuState *cpu, bool alternate);
uint8_t Dkc1VideoAdvanceWideRowBuild(struct CpuState *cpu);
/* Exact CODE_BBA849 call-site hooks. They borrow only currently free actor
 * slots and restore every normal-actor word after OAM generation. */
void Dkc1MarginProxyBeginRender(struct CpuState *cpu);
void Dkc1MarginProxyEndRender(struct CpuState *cpu);
/* Widened placed-object windows can allocate ordinary actors before the
 * cartridge's native scanner would have admitted their authored source
 * record.  Delay only that actor's first behavior dispatch until the source
 * reaches the reconstructed stock window.  The initialized actor remains in
 * the pool so the host can present it in the added margin. */
bool Dkc1VideoShouldRunPlacedActor(struct CpuState *cpu);
bool Dkc1VideoBeginPlacedActorDispatch(struct CpuState *cpu);
void Dkc1VideoEndPlacedActorDispatch(struct CpuState *cpu);
/* Observe free normal-pool slots at the boundary before allocation begins.
 * A slot can later be reused for the same ID/source pair, which is still a
 * new lifecycle generation and must not inherit the previous generation's
 * stock-started decision. */
void Dkc1VideoObserveActorPool(const uint8_t *wram);
void Dkc1VideoResetPlacedActorPhases(void);

/* Versioned host-only lifecycle snapshot used by DKC1 save-state v8. This
 * preserves placed-actor phase and stream-coverage decisions that widened
 * gameplay depends on, without serializing the temporary 128 KiB rollback
 * buffer (saves occur only at frame boundaries). */
size_t Dkc1VideoSnapshotSize(void);
bool Dkc1VideoSnapshotSave(void *data, size_t size);
bool Dkc1VideoSnapshotLoad(const void *data, size_t size);

/* Type-$05 groups mark their parent active even when the fixed actor pool
 * prevented one or more children from allocating. Wider prefetch makes that
 * stock one-shot failure reachable. When an active group is still inside the
 * widened window, prepare the existing cartridge child loop to retry only
 * its zero-bookmark children. */
bool Dkc1VideoPrepareType5ChildRetry(struct CpuState *cpu);

/* Presentation-camera widescreen support. Logical camera coordinates,
 * collision, movement clamps, exits, boss bounds, and tile streaming stay
 * stock. Generated presentation adapters widen visibility and placed-object
 * activation only after the host has proven a supported gameplay layout. */

/* Keep a reference to the verified ROM for level-map margin decoding. */
void Dkc1VideoSetRom(const uint8_t *rom, size_t size);

/* BG1/BG2 layers eligible for widening: Mode 1 with a 64-column tilemap. */
uint8_t Dkc1VideoPpuWideLayerMask(uint8_t bg_mode,
                                  const uint8_t bg_xsc[4],
                                  uint8_t main_layers,
                                  uint8_t sub_layers);

/* Which wide layer receives DKC1's rolling column stream: the layer whose
 * tilemap base matches the streamer VRAM base at $7E1B13. -1 when none. */
int Dkc1VideoTerrainLayer(uint8_t wide_layer_mask,
                          const uint8_t bg_xsc[4],
                          uint16_t stream_vram_word_address);

/* Expand a repeating 10-bit SNES scroll phase nearest a world-space anchor. */
uint32_t Dkc1VideoUnwrapPpuScroll(uint16_t ppu_scroll, uint32_t anchor);

/* Locate a fully transparent 4bpp character in live VRAM. */
bool Dkc1VideoFindTransparent4bppTile(const uint16_t *vram,
                                      size_t word_count,
                                      uint16_t character_base,
                                      uint16_t *tile_entry);

/* DKC1 level-map layouts (from Level_BuildTilemapColumn_TypeA/B at
 * $81:8705 / $81:8DFA): horizontal levels store one $20-byte column of 16
 * metatile rows per 32px of X; vertical levels store $80-byte rows of 64
 * metatiles. Metatiles are 32x32px: 16 tilemap words at cell*32, X-flip in
 * cell bit 14, Y-flip in bit 15 (both XORed into the emitted entries). */
typedef enum Dkc1LevelLayout {
  kDkc1LayoutUnknown = 0,
  kDkc1LayoutHorizontal,
  kDkc1LayoutVertical,
} Dkc1LevelLayout;

/* Decode one 8x8 tilemap entry for a world tile straight from the ROM level
 * map. Level_SetTilemapPointers at $81:8C66 publishes the map bank in $D5
 * and the independently selected metatile-definition bank in $D6. Several
 * layouts use different banks (underwater is map $E9 / definitions $D0), so
 * keep both explicit. Returns false when either source is unresolvable. */
bool Dkc1VideoDecodeLevelTile(Dkc1LevelLayout layout,
                              uint8_t map_bank,
                              uint8_t metatile_bank,
                              uint16_t map_base,
                              uint16_t metatile_base,
                              uint32_t world_tile_x,
                              uint32_t world_tile_y,
                              uint16_t *tile_entry);

#endif
