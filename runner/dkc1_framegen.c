/* Host-only motion interpolation between two completed cartridge frames.
 * See dkc1_framegen.h. Reads guest memory and PPU registers only. */
#include "dkc1_framegen.h"

#include "dkc1_video.h"
#include "dkc1_wram_gen.h"
#include "snes/ppu.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _MSC_VER
/* Host interpolation is a per-pixel hot path. The repository's /O1 builds
 * otherwise favor code size here, reducing the 120 Hz presentation margin. */
#pragma optimize("t", on)
#pragma intrinsic(abs, memcpy, memset, memcmp)
#endif

enum {
  /* A per-line scroll change past this is a cut or wipe, not motion. */
  kMaxScrollStep = 48,
  /* An actor that moved further than this in one frame was teleported,
   * respawned into a reused slot, or is a screen-wrapped coordinate. */
  kMaxActorStep = 64,
  /* A piece without an exact match follows the nearest live actor within
   * this Chebyshev distance of its centre. */
  kActorRadius = 64,
  /* Last resort: a unique identical piece this close in the previous frame. */
  kNearestRadius = 6,
  kActorSlotFirst = 0x02,
  kActorSlotLast = 0x32,
  kActorSlotStep = 0x02,
  kActorCount = (kActorSlotLast - kActorSlotFirst) / kActorSlotStep + 1,
  kBuckets = 512,
  kHistory = 3,
};

typedef struct Piece {
  int16_t x, y;      /* decoded screen position (9-bit X, signed Y) */
  uint16_t attr;     /* second OAM word: tile, palette, priority, flips */
  uint8_t size;      /* square hardware size in pixels */
  int16_t next;      /* bucket chain */
} Piece;

typedef struct Actor {
  bool alive;
  uint16_t id;
  uint16_t pose;     /* displayed pose: the sprite cell being drawn */
  uint16_t source;
  int32_t x, y;      /* world position */
} Actor;

typedef struct Frame {
  bool valid;
  int frame_counter;
  int width;
  uint16_t hscroll[kDkc1FrameGenLayers][kDkc1FrameGenLines];
  uint16_t vscroll[kDkc1FrameGenLayers][kDkc1FrameGenLines];
  /* Register signature: a change means a different scene layout. */
  uint8_t bgmode, obsel, inidisp, main_enabled, sub_enabled;
  uint8_t bgxsc[4];
  uint16_t bg_tile_adr;
  uint8_t extra_right;
  uint8_t hint_strict;
  uint8_t hint[16];
  uint16_t oam[kDkc1FrameGenOamSlots * 2];
  uint8_t high_oam[32];
  Piece pieces[kDkc1FrameGenOamSlots];
  int16_t buckets[kBuckets];
  Actor actors[kActorCount];
  int32_t camera_x, camera_y;
} Frame;

typedef struct Candidate {
  int dx, dy;
} Candidate;

static bool s_enabled;
static Frame s_history[kHistory];
static int s_head;      /* index of the newest complete frame */
static int s_count;     /* complete frames since the last invalidate */
static uint16_t s_line_hscroll[kDkc1FrameGenLayers][kDkc1FrameGenLines];
static uint16_t s_line_vscroll[kDkc1FrameGenLayers][kDkc1FrameGenLines];
static void PoseGenReset(void);
static void PoseGenActors(const Frame *f, const Ppu *ppu);

void Dkc1FrameGenSetEnabled(bool enabled) {
  if (s_enabled != enabled)
    Dkc1FrameGenInvalidate();
  s_enabled = enabled;
}

bool Dkc1FrameGenEnabled(void) {
  return s_enabled;
}

void Dkc1FrameGenInvalidate(void) {
  PoseGenReset();
  s_count = 0;
  for (int i = 0; i < kHistory; i++)
    s_history[i].valid = false;
}

void Dkc1FrameGenCaptureLine(const Ppu *ppu, int line) {
  if (!s_enabled || !ppu || line < 0 || line >= kDkc1FrameGenLines)
    return;
  for (int layer = 0; layer < kDkc1FrameGenLayers; layer++) {
    s_line_hscroll[layer][line] = ppu->hScroll[layer];
    s_line_vscroll[layer][line] = ppu->vScroll[layer];
  }
}

static unsigned PieceHash(uint16_t attr, uint8_t size, int x, int y) {
  unsigned h = (unsigned)attr * 0x9e3779b1u;
  h ^= (unsigned)size * 0x85ebca6bu;
  h ^= (unsigned)(x & 0xffff) * 0xc2b2ae35u;
  h ^= (unsigned)(y & 0xffff) * 0x27d4eb2fu;
  h ^= h >> 15;
  return h & (kBuckets - 1);
}

/* Mirror of the renderer's 9-bit X decode, including the ambiguous
 * right-margin band policy, so predicted positions live in the same space
 * the PPU will place the moved sprite in. */
static int DecodeX(const Frame *f, int slot) {
  const int index = slot * 2;
  int x = f->oam[index] & 0xff;
  x |= ((f->high_oam[index >> 3] >> (index & 7)) & 1) << 8;
  if (x >= 256 + f->extra_right) {
    x -= 512;
  } else if (x >= 256 && f->hint_strict) {
    if (!(f->hint[slot >> 3] & (1u << (slot & 7))))
      x -= 512;
  }
  return x;
}

static void DecodePieces(Frame *f) {
  static const uint8_t kSizes[8][2] = {
    {8, 16}, {8, 32}, {8, 64}, {16, 32},
    {16, 64}, {32, 64}, {16, 32}, {16, 32}
  };
  for (int b = 0; b < kBuckets; b++)
    f->buckets[b] = -1;
  const int size_mode = (f->obsel >> 5) & 7;
  for (int slot = 0; slot < kDkc1FrameGenOamSlots; slot++) {
    Piece *p = &f->pieces[slot];
    const int index = slot * 2;
    const int size_bit = (f->high_oam[index >> 3] >> ((index & 7) + 1)) & 1;
    int y = f->oam[index] >> 8;
    if (y >= 224)
      y -= 256;
    p->x = (int16_t)DecodeX(f, slot);
    p->y = (int16_t)y;
    p->attr = f->oam[index + 1];
    p->size = kSizes[size_mode][size_bit];
    const unsigned bucket = PieceHash(p->attr, p->size, p->x, p->y);
    p->next = f->buckets[bucket];
    f->buckets[bucket] = (int16_t)slot;
  }
}

/* Slot of an identical piece at exactly (x, y), or -1. Identical pieces
 * (parked entries, repeated bananas) pair one-to-one: an unclaimed twin is
 * preferred so the reverse pass does not treat the leftovers as vanished. */
static int FindPiece(const Frame *f, uint16_t attr, uint8_t size,
                     int x, int y, const bool *claimed) {
  int slot = f->buckets[PieceHash(attr, size, x, y)];
  int fallback = -1;
  while (slot >= 0) {
    const Piece *p = &f->pieces[slot];
    if (p->attr == attr && p->size == size && p->x == x && p->y == y) {
      if (!claimed || !claimed[slot])
        return slot;
      if (fallback < 0)
        fallback = slot;
    }
    slot = p->next;
  }
  return fallback;
}

void Dkc1FrameGenCaptureFrame(const Ppu *ppu, const uint8_t *wram,
                              int frame_counter, int presentation_width) {
  if (!s_enabled || !ppu || !wram)
    return;
  const int next = (s_head + 1) % kHistory;
  Frame *f = &s_history[next];
  memcpy(f->hscroll, s_line_hscroll, sizeof f->hscroll);
  memcpy(f->vscroll, s_line_vscroll, sizeof f->vscroll);
  f->frame_counter = frame_counter;
  f->width = presentation_width;
  f->bgmode = ppu->bgmode;
  f->obsel = ppu->obsel;
  f->inidisp = ppu->inidisp;
  f->main_enabled = ppu->screenEnabled[0];
  f->sub_enabled = ppu->screenEnabled[1];
  memcpy(f->bgxsc, ppu->bgXsc, sizeof f->bgxsc);
  f->bg_tile_adr = ppu->bgTileAdr;
  f->extra_right = ppu->extraRightCur;
  f->hint_strict = ppu->wsOamRightHintStrict;
  memcpy(f->hint, ppu->wsOamRightHint, sizeof f->hint);
  memcpy(f->oam, ppu->oam, sizeof f->oam);
  memcpy(f->high_oam, ppu->highOam, sizeof f->high_oam);
  DecodePieces(f);
  for (int i = 0; i < kActorCount; i++) {
    const uint32_t slot = (uint32_t)(kActorSlotFirst + i * kActorSlotStep);
    Actor *a = &f->actors[i];
    a->id = Dkc1Actor_SpriteIDLo(wram, slot);
    a->alive = a->id != 0;
    a->pose = Dkc1Actor_DisplayedPoseLo(wram, slot);
    a->source = Dkc1WramU16(wram, 0x15fd + slot);
    a->x = Dkc1Actor_SprXPos(wram, slot);
    a->y = Dkc1Actor_SprYPos(wram, slot);
  }
  f->camera_x = Dkc1WramU16(wram, DKC1_WRAM_CameraX);
  f->camera_y = Dkc1WramU16(wram, DKC1_WRAM_CameraY);
  f->valid = true;
  s_head = next;
  if (s_count < kHistory)
    s_count++;
  PoseGenActors(f, ppu);
}

#include "dkc1_posegen.inc"

static int Wrap10(int delta) {
  delta &= 0x3ff;
  if (delta >= 0x200)
    delta -= 0x400;
  return delta;
}

static int Halve(int delta) {
  /* Truncate toward zero: an odd one-pixel step stays put rather than
   * dithering between two rounding directions on alternate frames. */
  return delta / 2;
}

static bool Reject(Dkc1FrameGenPlan *plan, const char *reason) {
  plan->valid = false;
  plan->stats.reject = reason;
  return false;
}

static void EncodeInto(uint16_t *oam, uint8_t *high_oam, uint8_t *right_hint,
                       int extra_right, int slot, int x, int y) {
  const int index = slot * 2;
  if (x < -256) x = -256;
  if (x > 511) x = 511;
  const unsigned raw = (unsigned)x & 0x1ffu;
  oam[index] = (uint16_t)((raw & 0xffu) | ((unsigned)(y & 0xff) << 8));
  const uint8_t bit = (uint8_t)(1u << (index & 7));
  if (raw & 0x100u)
    high_oam[index >> 3] |= bit;
  else
    high_oam[index >> 3] &= (uint8_t)~bit;
  const uint8_t hint_bit = (uint8_t)(1u << (slot & 7));
  if (x >= 256 && x < 256 + extra_right)
    right_hint[slot >> 3] |= hint_bit;
  else
    right_hint[slot >> 3] &= (uint8_t)~hint_bit;
}

static void EncodePiece(Dkc1FrameGenPlan *plan, const Frame *cur, int slot,
                        int x, int y) {
  EncodeInto(plan->oam, plan->high_oam, plan->right_hint, cur->extra_right,
             slot, x, y);
}

static void AddTweenRect(Dkc1FrameGenPlan *plan, int x, int y, int size) {
  /* Pieces entirely outside the widest possible view (parked sprites) never
   * reach the screen; skip them so the dissolve mask stays small. */
  if (x + size <= -96 || x >= 256 + 96 || y + size <= 0 || y >= 224)
    return;
  if (plan->tween_count >= kDkc1FrameGenOamSlots * 2)
    return;
  Dkc1FrameGenRect *r = &plan->tween[plan->tween_count++];
  r->x = (int16_t)x;
  r->y = (int16_t)y;
  r->w = (int16_t)size;
  r->h = (int16_t)size;
}

bool Dkc1FrameGenBuildPlan(Dkc1FrameGenPlan *plan) {
  if (!plan)
    return false;
  memset(&plan->stats, 0, sizeof plan->stats);
  if (!s_enabled)
    return Reject(plan, "disabled");
  if (s_count < 2)
    return Reject(plan, "no history");
  const Frame *cur = &s_history[s_head];
  const Frame *prev = &s_history[(s_head + kHistory - 1) % kHistory];
  const Frame *prev2 =
      s_count >= 3 ? &s_history[(s_head + kHistory - 2) % kHistory] : NULL;
  if (!cur->valid || !prev->valid)
    return Reject(plan, "no history");
  if (cur->frame_counter != prev->frame_counter + 1)
    return Reject(plan, "discontinuous");
  if (cur->width != prev->width)
    return Reject(plan, "width");
  if ((cur->inidisp & 0x80) || (prev->inidisp & 0x80))
    return Reject(plan, "forced blank");
  if (cur->bgmode != prev->bgmode || cur->obsel != prev->obsel ||
      cur->bg_tile_adr != prev->bg_tile_adr ||
      cur->main_enabled != prev->main_enabled ||
      cur->sub_enabled != prev->sub_enabled ||
      memcmp(cur->bgxsc, prev->bgxsc, sizeof cur->bgxsc) != 0)
    return Reject(plan, "registers");

  /* Backgrounds: halve each line's scroll delta on every layer. */
  static int dh[kDkc1FrameGenLayers][kDkc1FrameGenLines];
  static int dv[kDkc1FrameGenLayers][kDkc1FrameGenLines];
  int max_step = 0;
  for (int layer = 0; layer < kDkc1FrameGenLayers; layer++) {
    for (int line = 0; line < kDkc1FrameGenLines; line++) {
      const int h = Wrap10((int)cur->hscroll[layer][line] -
                           (int)prev->hscroll[layer][line]);
      const int v = Wrap10((int)cur->vscroll[layer][line] -
                           (int)prev->vscroll[layer][line]);
      if (abs(h) > max_step) max_step = abs(h);
      if (abs(v) > max_step) max_step = abs(v);
      dh[layer][line] = h;
      dv[layer][line] = v;
      plan->h_shift[layer][line] = (int16_t)-Halve(h);
      plan->v_shift[layer][line] = (int16_t)-Halve(v);
    }
  }
  plan->stats.max_scroll_step = max_step;
  if (max_step > kMaxScrollStep)
    return Reject(plan, "scroll cut");

  const int cam_dx = (int16_t)(cur->camera_x - prev->camera_x);
  const int cam_dy = (int16_t)(cur->camera_y - prev->camera_y);
  plan->stats.camera_dx = cam_dx;
  plan->stats.camera_dy = cam_dy;
  if (abs(cam_dx) > kMaxScrollStep || abs(cam_dy) > kMaxScrollStep)
    return Reject(plan, "camera cut");

  /* Live actors with a screen-space velocity. Two velocities are kept per
   * actor because the OAM the PPU consumed may lag the actor table by one
   * frame; the exact-match test below decides which one is real. */
  typedef struct Track {
    int ox, oy;              /* screen origin at frame N */
    int ox_prev, oy_prev;    /* screen origin at frame N-1 */
    Candidate delta[2];
    int deltas;
  } Track;
  Track tracks[kActorCount];
  int track_count = 0;
  for (int i = 0; i < kActorCount; i++) {
    const Actor *a = &cur->actors[i];
    const Actor *b = &prev->actors[i];
    if (!a->alive || !b->alive || a->id != b->id)
      continue;
    const int dx = (int16_t)(a->x - b->x) - cam_dx;
    const int dy = (int16_t)(a->y - b->y) - cam_dy;
    if (abs(dx) > kMaxActorStep || abs(dy) > kMaxActorStep)
      continue;
    if (a->pose != b->pose)
      plan->stats.poses_changed++;
    Track *t = &tracks[track_count++];
    t->ox = (int16_t)(a->x - cur->camera_x);
    t->oy = (int16_t)(a->y - cur->camera_y);
    t->ox_prev = (int16_t)(b->x - prev->camera_x);
    t->oy_prev = (int16_t)(b->y - prev->camera_y);
    t->delta[0].dx = dx;
    t->delta[0].dy = dy;
    t->deltas = 1;
    if (prev2 && prev2->valid && prev2->actors[i].alive &&
        prev2->actors[i].id == b->id &&
        prev->frame_counter == prev2->frame_counter + 1) {
      const int pdx = (int16_t)(b->x - prev2->actors[i].x) -
                      (int16_t)(prev->camera_x - prev2->camera_x);
      const int pdy = (int16_t)(b->y - prev2->actors[i].y) -
                      (int16_t)(prev->camera_y - prev2->camera_y);
      if ((pdx != dx || pdy != dy) && abs(pdx) <= kMaxActorStep &&
          abs(pdy) <= kMaxActorStep) {
        t->delta[1].dx = pdx;
        t->delta[1].dy = pdy;
        t->deltas = 2;
      }
    }
  }
  plan->stats.actors_tracked = (unsigned)track_count;

  memcpy(plan->oam, cur->oam, sizeof plan->oam);
  memcpy(plan->high_oam, cur->high_oam, sizeof plan->high_oam);
  memcpy(plan->right_hint, cur->hint, sizeof plan->right_hint);
  memcpy(plan->oam_prev, prev->oam, sizeof plan->oam_prev);
  memcpy(plan->high_oam_prev, prev->high_oam, sizeof plan->high_oam_prev);
  memcpy(plan->right_hint_prev, prev->hint, sizeof plan->right_hint_prev);
  plan->tween_count = 0;
  /* Frame N-1 pieces claimed by an exact match, with the forward move that
   * lands them on the same in-between pixels as their frame N twin. */
  bool prev_matched[kDkc1FrameGenOamSlots];
  int prev_fx[kDkc1FrameGenOamSlots], prev_fy[kDkc1FrameGenOamSlots];
  memset(prev_matched, 0, sizeof prev_matched);

  for (int slot = 0; slot < kDkc1FrameGenOamSlots; slot++) {
    const Piece *p = &cur->pieces[slot];
    const int cx = p->x + p->size / 2;
    const int cy = p->y + p->size / 2;
    /* Nearest live actors first, so a piece verifies against the velocity
     * most likely to be its own before trying the others. */
    int order[kActorCount];
    int dist[kActorCount];
    for (int i = 0; i < track_count; i++) {
      const int ddx = abs(cx - tracks[i].ox);
      const int ddy = abs(cy - tracks[i].oy);
      const int d = ddx > ddy ? ddx : ddy;
      int j = i;
      while (j > 0 && dist[j - 1] > d) {
        order[j] = order[j - 1];
        dist[j] = dist[j - 1];
        j--;
      }
      order[j] = i;
      dist[j] = d;
    }
    const int line = p->y + 1 < 1 ? 1
                     : p->y + 1 > kDkc1FrameGenLines - 1
                         ? kDkc1FrameGenLines - 1 : p->y + 1;
    Candidate candidates[2 + kActorCount * 2 + 2];
    int candidate_count = 0;
    candidates[candidate_count].dx = 0;
    candidates[candidate_count].dy = 0;
    candidate_count++;
    for (int k = 0; k < track_count; k++) {
      const Track *t = &tracks[order[k]];
      for (int d = 0; d < t->deltas; d++)
        candidates[candidate_count++] = t->delta[d];
    }
    /* World-static pieces outside the actor pool (rope segments, effects)
     * move against either terrain plane. */
    for (int layer = 0; layer < 2; layer++) {
      candidates[candidate_count].dx = -dh[layer][line];
      candidates[candidate_count].dy = -dv[layer][line];
      candidate_count++;
    }

    int sx = 0, sy = 0;
    bool matched = false, exact = false;
    for (int c = 0; c < candidate_count && !matched; c++) {
      const int dx = candidates[c].dx, dy = candidates[c].dy;
      const int q = FindPiece(prev, p->attr, p->size, p->x - dx, p->y - dy,
                              prev_matched);
      if (q >= 0) {
        sx = Halve(dx);
        sy = Halve(dy);
        matched = true;
        exact = true;
        prev_matched[q] = true;
        prev_fx[q] = dx - sx;
        prev_fy[q] = dy - sy;
        plan->stats.sprites_exact++;
      }
    }
    if (!matched && track_count && dist[0] <= kActorRadius) {
      const Track *t = &tracks[order[0]];
      sx = Halve(t->delta[0].dx);
      sy = Halve(t->delta[0].dy);
      matched = true;
      plan->stats.sprites_actor++;
    }
    if (!matched) {
      int found = 0, fdx = 0, fdy = 0;
      for (int q = 0; q < kDkc1FrameGenOamSlots && found < 2; q++) {
        const Piece *o = &prev->pieces[q];
        if (o->attr != p->attr || o->size != p->size)
          continue;
        const int ddx = p->x - o->x, ddy = p->y - o->y;
        if (abs(ddx) <= kNearestRadius && abs(ddy) <= kNearestRadius) {
          found++;
          fdx = ddx;
          fdy = ddy;
        }
      }
      if (found == 1) {
        sx = Halve(fdx);
        sy = Halve(fdy);
        matched = true;
        plan->stats.sprites_nearest++;
      }
    }
    if (!matched)
      plan->stats.sprites_unmatched++;
    if (sx || sy)
      EncodePiece(plan, cur, slot, p->x - sx, p->y - sy);
    if (!exact) {
      /* A piece with no identical twin in frame N-1 is a changed cell (or a
       * sprite that just appeared): dissolve its in-between footprint. */
      plan->stats.sprites_tween++;
      AddTweenRect(plan, p->x - sx, p->y - sy, p->size);
    }
  }

  /* Frame N-1's pieces at the same in-between positions. Twins reuse the
   * exact forward move; the rest follow the nearest live actor's velocity
   * (a vanished sprite fades out where it was). */
  for (int q = 0; q < kDkc1FrameGenOamSlots; q++) {
    const Piece *o = &prev->pieces[q];
    int fx = 0, fy = 0;
    if (prev_matched[q]) {
      fx = prev_fx[q];
      fy = prev_fy[q];
    } else {
      const int cx = o->x + o->size / 2, cy = o->y + o->size / 2;
      int best = -1, best_d = kActorRadius + 1;
      for (int i = 0; i < track_count; i++) {
        const int ddx = abs(cx - tracks[i].ox_prev);
        const int ddy = abs(cy - tracks[i].oy_prev);
        const int d = ddx > ddy ? ddx : ddy;
        if (d < best_d) {
          best_d = d;
          best = i;
        }
      }
      if (best >= 0) {
        fx = tracks[best].delta[0].dx - Halve(tracks[best].delta[0].dx);
        fy = tracks[best].delta[0].dy - Halve(tracks[best].delta[0].dy);
      }
      AddTweenRect(plan, o->x + fx, o->y + fy, o->size);
    }
    if (fx || fy)
      EncodeInto(plan->oam_prev, plan->high_oam_prev, plan->right_hint_prev,
                 cur->extra_right, q, o->x + fx, o->y + fy);
  }
  plan->stats.tween_rects = plan->tween_count;
  plan->valid = true;
  plan->stats.reject = NULL;
  return true;
}

/* ---- In-between cell synthesis --------------------------------------- */

enum {
  kTweenCell = 4,                       /* output cell size */
  kTweenWindow = 8,                     /* matching window around a cell */
  kTweenSearch = 4,                     /* half-displacement per side, px */
  kTweenDilate = kTweenSearch + kTweenWindow,
  kTweenAcceptPerPixel = 18,            /* mean luma error a match may have */
  kTweenLambda = 40,                    /* window-SAD bias per |h| unit */
  kTweenPixelAccept = 9 * 18,           /* 3x3 local error to trust a copy */
  kTweenOutside = 96,                   /* luma error for off-frame samples */
  kTweenMaxWidth = kDkc1VideoWidescreenWidth,
  kTweenMaxHeight = kDkc1VideoHeight,
  kTweenGridW = (kTweenMaxWidth + kTweenCell - 1) / kTweenCell,
  kTweenGridH = (kTweenMaxHeight + kTweenCell - 1) / kTweenCell,
};

static uint8_t s_luma_cur[kTweenMaxWidth * kTweenMaxHeight];
static uint8_t s_luma_prev[kTweenMaxWidth * kTweenMaxHeight];
static int8_t s_cell_hx[kTweenGridH][kTweenGridW];
static int8_t s_cell_hy[kTweenGridH][kTweenGridW];
static uint8_t s_cell_ok[kTweenGridH][kTweenGridW];
static uint8_t s_cell_active[kTweenGridH][kTweenGridW];

static void LumaPlane(uint8_t *plane, const uint8_t *pixels, size_t pitch,
                      int width, int height) {
  for (int y = 0; y < height; y++) {
    const uint8_t *row = pixels + (size_t)y * pitch;
    uint8_t *dst = plane + (size_t)y * width;
    for (int x = 0; x < width; x++) {
      const uint8_t *p = row + (size_t)x * 4;  /* B G R X */
      dst[x] = (uint8_t)((p[2] * 77 + p[1] * 150 + p[0] * 29) >> 8);
    }
  }
}

static inline int LumaError(const uint8_t *a, const uint8_t *b, int width,
                            int height, int ax, int ay, int bx, int by) {
  if (ax < 0 || ay < 0 || ax >= width || ay >= height ||
      bx < 0 || by < 0 || bx >= width || by >= height)
    return kTweenOutside;
  return abs((int)a[ay * width + ax] - (int)b[by * width + bx]);
}

/* Window SAD for the symmetric displacement h around cell origin (x0, y0),
 * with early exit once the running sum passes `limit`. */
static int WindowError(int width, int height, int x0, int y0, int hx, int hy,
                       int limit) {
  int sum = 0;
  const int w0 = x0 - (kTweenWindow - kTweenCell) / 2;
  const int h0 = y0 - (kTweenWindow - kTweenCell) / 2;
  for (int y = h0; y < h0 + kTweenWindow; y++) {
    for (int x = w0; x < w0 + kTweenWindow; x++) {
      sum += LumaError(s_luma_prev, s_luma_cur, width, height,
                       x - hx, y - hy, x + hx, y + hy);
    }
    if (sum > limit)
      return sum;
  }
  return sum;
}

static int LocalError(int width, int height, int x, int y, int hx, int hy) {
  int sum = 0;
  for (int dy = -1; dy <= 1; dy++)
    for (int dx = -1; dx <= 1; dx++)
      sum += LumaError(s_luma_prev, s_luma_cur, width, height,
                       x + dx - hx, y + dy - hy, x + dx + hx, y + dy + hy);
  return sum;
}

void Dkc1FrameGenSynthesize(uint8_t *out, size_t out_pitch,
                            const uint8_t *cur, size_t cur_pitch,
                            const uint8_t *prev, size_t prev_pitch,
                            int width, int height,
                            const Dkc1FrameGenPlan *plan, int bias,
                            int extra_left, Dkc1FrameGenStats *stats) {
  if (!out || !cur || !prev || !plan || width <= 0 || height <= 0 ||
      width > kTweenMaxWidth || height > kTweenMaxHeight)
    return;
  const int grid_w = (width + kTweenCell - 1) / kTweenCell;
  const int grid_h = (height + kTweenCell - 1) / kTweenCell;
  memset(s_cell_active, 0, sizeof s_cell_active);
  bool any = false;
  for (unsigned i = 0; i < plan->tween_count; i++) {
    const Dkc1FrameGenRect *r = &plan->tween[i];
    int x0 = r->x - bias + extra_left - kTweenDilate;
    int y0 = r->y - kTweenDilate;
    int x1 = r->x - bias + extra_left + r->w + kTweenDilate;
    int y1 = r->y + r->h + kTweenDilate;
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > width) x1 = width;
    if (y1 > height) y1 = height;
    if (x0 >= x1 || y0 >= y1)
      continue;
    for (int cy = y0 / kTweenCell; cy < (y1 + kTweenCell - 1) / kTweenCell; cy++)
      for (int cx = x0 / kTweenCell; cx < (x1 + kTweenCell - 1) / kTweenCell; cx++)
        s_cell_active[cy][cx] = 1;
    any = true;
  }
  if (!any)
    return;
  LumaPlane(s_luma_cur, cur, cur_pitch, width, height);
  LumaPlane(s_luma_prev, prev, prev_pitch, width, height);

  /* Pass 1: one symmetric displacement per cell. Cells whose window is
   * identical in both renders are static and cost nothing. */
  unsigned cells = 0, moved = 0;
  for (int cy = 0; cy < grid_h; cy++) {
    for (int cx = 0; cx < grid_w; cx++) {
      s_cell_ok[cy][cx] = 0;
      s_cell_hx[cy][cx] = 0;
      s_cell_hy[cy][cx] = 0;
      if (!s_cell_active[cy][cx])
        continue;
      const int x0 = cx * kTweenCell, y0 = cy * kTweenCell;
      cells++;
      if (WindowError(width, height, x0, y0, 0, 0, 0) == 0)
        continue;  /* static: frame N pixels already in place */
      int best = WindowError(width, height, x0, y0, 0, 0, 1 << 30);
      int best_hx = 0, best_hy = 0;
      for (int radius = 1; radius <= kTweenSearch; radius++) {
        for (int hy = -radius; hy <= radius; hy++) {
          const int span = radius - abs(hy);
          for (int side = -1; side <= 1; side += 2) {
            const int hx = side * span;
            if (span == 0 && side == 1)
              continue;
            const int limit = best - kTweenLambda * radius;
            if (limit <= 0)
              continue;
            const int err = WindowError(width, height, x0, y0, hx, hy, limit);
            if (err + kTweenLambda * radius < best) {
              best = err + kTweenLambda * radius;
              best_hx = hx;
              best_hy = hy;
            }
          }
        }
      }
      if (best_hx || best_hy) {
        if (best <= kTweenAcceptPerPixel * kTweenWindow * kTweenWindow) {
          s_cell_ok[cy][cx] = 1;
          s_cell_hx[cy][cx] = (int8_t)best_hx;
          s_cell_hy[cy][cx] = (int8_t)best_hy;
          moved++;
        }
      }
    }
  }

  /* Pass 2: per pixel, choose among this cell's and its neighbours'
   * displacements by local agreement and copy from frame N's render at the
   * displaced spot. Unexplained pixels keep frame N. */
  unsigned pixels_moved = 0;
  for (int cy = 0; cy < grid_h; cy++) {
    for (int cx = 0; cx < grid_w; cx++) {
      if (!s_cell_active[cy][cx])
        continue;
      int cand_x[5], cand_y[5];
      int candidates = 0;
      const int ncx[5] = {cx, cx - 1, cx + 1, cx, cx};
      const int ncy[5] = {cy, cy, cy, cy - 1, cy + 1};
      for (int n = 0; n < 5; n++) {
        const int qx = ncx[n], qy = ncy[n];
        if (qx < 0 || qy < 0 || qx >= grid_w || qy >= grid_h ||
            !s_cell_ok[qy][qx])
          continue;
        cand_x[candidates] = s_cell_hx[qy][qx];
        cand_y[candidates] = s_cell_hy[qy][qx];
        candidates++;
      }
      if (!candidates)
        continue;
      for (int y = cy * kTweenCell; y < (cy + 1) * kTweenCell && y < height; y++) {
        for (int x = cx * kTweenCell; x < (cx + 1) * kTweenCell && x < width; x++) {
          int best = LocalError(width, height, x, y, 0, 0);
          int best_hx = 0, best_hy = 0;
          for (int c = 0; c < candidates; c++) {
            const int err = LocalError(width, height, x, y, cand_x[c], cand_y[c]);
            if (err < best) {
              best = err;
              best_hx = cand_x[c];
              best_hy = cand_y[c];
            }
          }
          if ((best_hx || best_hy) && best <= kTweenPixelAccept) {
            const int sx = x + best_hx, sy = y + best_hy;
            if (sx >= 0 && sy >= 0 && sx < width && sy < height) {
              const uint8_t *s = cur + (size_t)sy * cur_pitch + (size_t)sx * 4;
              uint8_t *d = out + (size_t)y * out_pitch + (size_t)x * 4;
              d[0] = s[0];
              d[1] = s[1];
              d[2] = s[2];
              pixels_moved++;
            }
          }
        }
      }
    }
  }
  if (stats) {
    stats->tween_cells = cells;
    stats->tween_moved = moved;
    stats->tween_pixels = pixels_moved;
  }
}

void Dkc1AnimCadenceLogFrame(const uint8_t *wram, int frame_counter) {
  static FILE *log;
  static int checked;
  if (!checked) {
    checked = 1;
    const char *path = getenv("DKC1_ANIM_CADENCE_LOG");
    if (path && *path) {
      log = fopen(path, "wb");
      if (log)
        fprintf(log, "{\"schema\":\"dkc1.anim_cadence.v1\","
                     "\"actor_slots\":[%d,%d,%d]}\n",
                kActorSlotFirst, kActorSlotLast, kActorSlotStep);
    }
  }
  if (!log || !wram)
    return;
  fprintf(log, "{\"frame\":%d,\"cam\":[%u,%u],\"actors\":[", frame_counter,
          Dkc1WramU16(wram, DKC1_WRAM_CameraX),
          Dkc1WramU16(wram, DKC1_WRAM_CameraY));
  int written = 0;
  for (int i = 0; i < kActorCount; i++) {
    const uint32_t slot = (uint32_t)(kActorSlotFirst + i * kActorSlotStep);
    const uint16_t id = Dkc1Actor_SpriteIDLo(wram, slot);
    if (!id)
      continue;
    fprintf(log, "%s[%u,%u,%u,%u,%u,%u,%u]", written ? "," : "", slot, id,
            Dkc1Actor_DisplayedPoseLo(wram, slot),
            Dkc1Actor_SprAnimFrame(wram, slot),
            Dkc1Actor_SprAnimID(wram, slot),
            Dkc1Actor_SprXPos(wram, slot), Dkc1Actor_SprYPos(wram, slot));
    written++;
  }
  fputs("]}\n", log);
  if ((frame_counter & 0xff) == 0)
    fflush(log);
}
