#ifndef DKC1_FRAMEGEN_H
#define DKC1_FRAMEGEN_H
/*
 * Optional Windows presentation smoothing at 60/120 Hz. A four-frame buffer
 * supplies future animation artwork for generated poses between held cells.
 * The 120 Hz path also re-renders intermediate scroll/OAM positions.
 * Raw cartridge frames remain untouched; no extra emulation or guest writes.
 * Unsupported composition and ambiguous sprite ownership retain raw output.
 * The generated poses use bounded optical flow and fractional sampling and
 * may soften fine sprite detail; they are not original authored artwork.
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct Ppu Ppu;

enum {
  kDkc1FrameGenLines = 225,   /* pre-render line 0 plus 224 visible lines */
  kDkc1FrameGenLayers = 4,
  kDkc1FrameGenOamSlots = 128,
};

typedef struct Dkc1FrameGenStats {
  /* Sprite pieces by how their in-between position was chosen. */
  unsigned sprites_exact;      /* identical piece found at the predicted spot */
  unsigned sprites_actor;      /* moved with the nearest live actor's velocity */
  unsigned sprites_nearest;    /* unique identical piece found a few px away */
  unsigned sprites_unmatched;  /* left where frame N drew it */
  unsigned actors_tracked;     /* live actors with a usable velocity */
  unsigned poses_changed;      /* tracked actors whose displayed pose changed */
  unsigned sprites_tween;      /* frame N pieces with no exact match: their
                                  area receives a synthesized in-between */
  unsigned tween_rects;        /* in-between rectangles in the plan */
  unsigned tween_cells;        /* 4x4 cells examined by the synthesizer */
  unsigned tween_moved;        /* cells whose content was found displaced */
  unsigned tween_pixels;       /* output pixels taken from a displaced spot */
  unsigned pose_actors, pose_pixels, pose_mismatch; /* applied groups, changes, oracle errors */
  int pose_source_frame;
  int camera_dx, camera_dy;    /* frame N camera minus frame N-1 camera */
  int max_scroll_step;         /* largest per-line scroll delta this pair */
  const char *reject;          /* NULL when the last plan was usable */
} Dkc1FrameGenStats;

/* A screen-space rectangle in decoded OAM coordinates (before the host
 * presentation bias and framebuffer centering are applied). */
typedef struct Dkc1FrameGenRect {
  int16_t x, y, w, h;
} Dkc1FrameGenRect;

typedef struct Dkc1FrameGenPlan {
  bool valid;
  /* Added to the PPU scroll registers before each line renders. */
  int16_t h_shift[kDkc1FrameGenLayers][kDkc1FrameGenLines];
  int16_t v_shift[kDkc1FrameGenLayers][kDkc1FrameGenLines];
  /* Replacement OAM for the in-between render of frame N's sprites. */
  uint16_t oam[kDkc1FrameGenOamSlots * 2];
  uint8_t high_oam[32];
  uint8_t right_hint[16];
  /* Frame N-1's sprites moved forward to the same in-between positions, for
   * a second render with frame N-1's VRAM/CGRAM. Pieces that exist
   * identically in both frames land on exactly the same pixels in both
   * renders; only pieces that changed (a new animation cell, a sprite that
   * appeared or vanished) differ. Dissolving the two renders 50/50 inside
   * `tween` therefore cross-fades exactly those cells, with occlusion, color
   * math and palette handled by the PPU in each render. */
  uint16_t oam_prev[kDkc1FrameGenOamSlots * 2];
  uint8_t high_oam_prev[32];
  uint8_t right_hint_prev[16];
  Dkc1FrameGenRect tween[kDkc1FrameGenOamSlots * 2];
  unsigned tween_count;
  Dkc1FrameGenStats stats;
} Dkc1FrameGenPlan;

/* Master switch: hosts that present in-between frames turn this on. While it
 * is off every other call returns immediately, so headless validation and
 * evidence captures pay nothing and see no change. */
void Dkc1FrameGenSetEnabled(bool enabled);
bool Dkc1FrameGenEnabled(void);

/* Called by the real render loop with the unbiased scroll registers the PPU
 * is about to consume for `line` (0..224). */
void Dkc1FrameGenCaptureLine(const Ppu *ppu, int line);

/* Called once after the real render loop with the PPU and WRAM that frame
 * consumed. `frame_counter` is the guest frame number and
 * `presentation_width` the host framebuffer width in pixels. `terrain_layer`
 * is the source-backed rolling terrain plane, or -1 when unproven. */
void Dkc1FrameGenCaptureFrame(const Ppu *ppu, const uint8_t *wram,
                              int frame_counter, int presentation_width,
                              int terrain_layer);

/* Forget history after a state load, rewind, or aspect change so the next
 * in-between frame is never built from an unrelated timeline. */
void Dkc1FrameGenInvalidate(void);

/* Buffered pose interpolation. Four completed frames (66.7 ms at 60 Hz)
 * provide future pose anchors. The original framebuffer remains an oracle.
 * Capture kinds: 0=real OBJ/registers, 1=real backgrounds, 2=mid OBJ,
 * 3=mid backgrounds. Returns delayed real F and the following midpoint F+0.5.
 * Only the Windows opt-in presenter requests these. */
void Dkc1PoseGenBegin(void);
void Dkc1PoseGenCaptureLine(const Ppu *ppu, int line, int kind);
void Dkc1PoseGenPresent(const uint8_t *real, const uint8_t *mid, bool mid_valid,
                       uint8_t *out_real, uint8_t *out_mid,
                       Dkc1FrameGenStats *stats);

/* Build the in-between plan from the last two captured frames. Returns false
 * (with plan->stats.reject set) when the pair must not be interpolated: the
 * first frame after a reset, a scene cut, changed PPU layout, forced blank,
 * or a discontinuous frame counter. */
bool Dkc1FrameGenBuildPlan(Dkc1FrameGenPlan *plan);

/* Synthesize in-between animation cells in place. `out` holds the in-between
 * render of frame N's sprites and `cur` an untouched copy of it; `prev` is
 * the in-between render of frame N-1's sprites at the same positions. Inside
 * the plan's rectangles every 4x4 cell whose content differs between the two
 * renders is searched for the symmetric displacement h such that the frame
 * N-1 image at (p - h) matches the frame N image at (p + h): content that
 * moved by 2h between the cells is then drawn halfway, at p, copied from one
 * source only. Cells that cannot be explained by motion keep frame N's
 * pixels, so the result is never a double exposure. Coordinates are
 * framebuffer pixels; `bias` and `extra_left` map plan rectangles into it. */
void Dkc1FrameGenSynthesize(uint8_t *out, size_t out_pitch,
                            const uint8_t *cur, size_t cur_pitch,
                            const uint8_t *prev, size_t prev_pitch,
                            int width, int height,
                            const Dkc1FrameGenPlan *plan, int bias,
                            int extra_left, Dkc1FrameGenStats *stats);

/* Animation cadence evidence, independent of frame generation. When
 * DKC1_ANIM_CADENCE_LOG names a file, every guest frame appends one JSON line
 * with the camera and each live actor's slot, sprite id, displayed pose,
 * animation frame/id, and world position. tools/analyze_anim_cadence.py turns
 * it into per-sprite pose hold lengths and effective animation rates. Reads
 * WRAM only; a no-op unless the variable is set. */
void Dkc1AnimCadenceLogFrame(const uint8_t *wram, int frame_counter);

#endif
