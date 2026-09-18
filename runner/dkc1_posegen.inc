/* Included by dkc1_framegen.c: host-only, bounded look-ahead pose renderer.
 * All inputs are copies of completed frames. No emulation or guest writes. */
#include <math.h>
enum {
  PG_DELAY = 4,
  PG_RING = 16,
  PG_SIDE = 96,
  PG_HALF = 48,
  PG_W = kDkc1VideoWidescreenWidth,
  PG_H = 224,
  PG_PIX = PG_W * PG_H,
  PG_GRID = PG_SIDE / 4
};
typedef struct PgActor {
  uint16_t id, source;
  int x, y, rank;
  uint32_t hash;
  uint16_t tex[PG_SIDE * PG_SIDE];
  int x0, y0, x1, y1;
} PgActor;
typedef struct PgLine {
  uint16_t palette[256], fixed, logic;
  uint32_t windows;
  uint8_t brightness, math, select, main, sub, wl, wr, wl2, wr2;
  int left, right;
  bool supported;
} PgLine;
typedef struct PgSurface {
  uint16_t bg[2][PG_PIX], obj[PG_PIX];
  uint16_t layer[3][PG_PIX], scroll[2][3][PG_H];
  bool layers_valid[PG_H];
  PgLine line[PG_H];
  uint32_t raw[PG_PIX];
  bool captured;
} PgSurface;
typedef struct PgFrame {
  int sequence, width, bias, extra, guest_frame, camera_x, camera_y;
  int terrain_layer;
  uint32_t layout;
  bool valid, mid_valid, queued;
  PgActor actors[kActorCount];
  PgSurface real, mid;
} PgFrame;
typedef struct PgFlow {
  uint32_t a, b;
  int8_t dx[PG_GRID * PG_GRID], dy[PG_GRID * PG_GRID];
} PgFlow;
static PgFrame pg_frames[PG_RING];
static PgFlow pg_flow[kActorCount];
static int pg_head, pg_count, pg_sequence;
static bool pg_started;
static FILE *pg_log;
static bool pg_log_checked;
static unsigned pg_last_actors, pg_last_pixels, pg_last_mismatch;
static void PoseGenReset(void) {
  pg_count = 0;
  pg_sequence = 0;
  pg_started = false;
  memset(pg_flow, 0, sizeof pg_flow);
}
void Dkc1PoseGenBegin(void) {
  if (!s_enabled) return;
  /* Debug isolation/inspection can capture a render without presenting it.
   * Never let such a ring slot expose stale raw pixels when smoothing resumes. */
  if (pg_started && !pg_frames[pg_head].queued) {
    pg_count = 0;
    memset(pg_flow, 0, sizeof pg_flow);
  }
  if (!pg_log_checked) {
    pg_log_checked = true;
    const char *path = getenv("DKC1_POSE_LOG");
    if (path && *path) pg_log = fopen(path, "wb");
  }
  pg_head = (pg_head + 1) % PG_RING;
  PgFrame *f = &pg_frames[pg_head];
  f->valid = false;
  f->mid_valid = false;
  f->queued = false;
  f->real.captured = f->mid.captured = false;
  f->sequence = ++pg_sequence;
  pg_started = true;
}
void Dkc1PoseGenCaptureLine(const Ppu *p, int line, int kind) {
  if (!s_enabled || !pg_started || line < 1 || line > PG_H) return;
  PgFrame *f = &pg_frames[pg_head];
  PgSurface *s = kind >= 2 ? &f->mid : &f->real;
  int width = 256 + 2 * p->extraLeftRight, y = line - 1;
  if (width > PG_W) return;
  size_t offset = (size_t)y * width;
  int left = kPpuExtraLeftRight - p->extraLeftRight;
  if (kind & 1) {
    for (int sub = 0; sub < 2; sub++)
      memcpy(s->bg[sub] + offset, p->bgBuffers[sub].data + left, width * 2);
    bool sub_math = PPU_preventMathMode(p) != 3 && PPU_addSubscreen(p) &&
                    PPU_mathEnabled(p);
    s->layers_valid[y] = PPU_mode(p) == 1 &&
        !(p->screenWindowed[0] & 7) && !(p->screenWindowed[1] & 7) &&
        !(PPU_mosaicSize(p) > 1 && (p->mosaic & 7)) &&
        (!sub_math || p->screenEnabled[0] == p->screenEnabled[1]);
    for (int layer = 0; layer < 3; layer++) {
      memcpy(s->layer[layer] + offset,
             p->overlayBuffers[layer].data + left, width * 2);
      s->scroll[0][layer][y] = p->hScroll[layer] & 1023;
      s->scroll[1][layer][y] = p->vScroll[layer] & 1023;
    }
    s->captured = true;
  } else {
    memcpy(s->obj + offset, p->objBuffer.data + left, width * 2);
    PgLine *l = &s->line[y];
    memcpy(l->palette, p->cgram, sizeof l->palette);
    l->fixed = p->fixedColor;
    l->logic = p->wbgobjlog;
    l->windows = p->windowsel;
    l->brightness = p->inidisp & 15;
    l->math = p->cgadsub;
    l->select = p->cgwsel;
    l->main = p->screenEnabled[0];
    l->sub = p->screenEnabled[1];
    l->wl = p->window1left;
    l->wr = p->window1right;
    l->wl2 = p->window2left;
    l->wr2 = p->window2right;
    l->left = p->extraLeftRight - p->extraLeftCur;
    l->right = p->extraLeftRight + 256 + p->extraRightCur;
    /* Unsupported OBJ windows, interlace and forced blank retain raw output.
     * Background windows are already resolved by the original renderer. */
    l->supported = PPU_mode(p) == 1 && !(p->inidisp & 128) &&
                   !(p->setini & 0xb) && !(p->screenWindowed[0] & 16) &&
                   !(p->screenWindowed[1] & 16);
  }
}
/* Track connected OAM images, not WRAM slot guesses: the sprite producer's
 * coordinate domain and DMA lag vary. Palette/priority/facing, piece adjacency,
 * one-to-one continuity and actual decoded artwork define these tracks. */
static void PoseGenActors(const Frame *frame, const Ppu *p) {
  if (!pg_started) return;
  PgFrame *f = &pg_frames[pg_head],
          *previous = &pg_frames[(pg_head + PG_RING - 1) % PG_RING];
  uint32_t layout =
      p->bgmode | (p->obsel << 8) | ((uint32_t)p->bgTileAdr << 16);
  for (int i = 0; i < 4; i++) layout = (layout ^ p->bgXsc[i]) * 16777619u;
  if (pg_count &&
      (previous->guest_frame + 1 != frame->frame_counter ||
       previous->layout != layout || previous->width != frame->width ||
       previous->terrain_layer != frame->terrain_layer ||
       abs((int16_t)(frame->camera_x - previous->camera_x)) > 48 ||
       abs((int16_t)(frame->camera_y - previous->camera_y)) > 48 ||
       (p->inidisp & 128))) {
    pg_count = 0;
    memset(pg_flow, 0, sizeof pg_flow);
  }
  f->layout = layout;
  f->guest_frame = frame->frame_counter;
  f->camera_x = frame->camera_x;
  f->camera_y = frame->camera_y;
  f->width = frame->width;
  f->terrain_layer = frame->terrain_layer;
  f->extra = p->extraLeftRight;
  f->bias = p->wsPresentationXBias;
  int first = PPU_objPriority(p) ? (p->oamaddl & 0xfe) / 2 : 0;
  int parent[128];
  bool visible[128];
  for (int n = 0; n < 128; n++) {
    const Piece *q = &frame->pieces[n];
    parent[n] = n;
    int x = q->x - f->bias + f->extra, y = q->y;
    visible[n] = x + q->size > 0 && x < f->width && y + q->size > 0 && y < PG_H;
  }
  for (int a = 0; a < 128; a++)
    if (visible[a])
      for (int b = a + 1; b < 128; b++)
        if (visible[b]) {
          const Piece *u = &frame->pieces[a], *v = &frame->pieces[b];
          if ((u->attr & 0xfe00) != (v->attr & 0xfe00)) continue;
          if (u->x > v->x + v->size + 1 || v->x > u->x + u->size + 1 ||
              u->y > v->y + v->size + 1 || v->y > u->y + u->size + 1)
            continue;
          int ra = a, rb = b;
          while (parent[ra] != ra) ra = parent[ra];
          while (parent[rb] != rb) rb = parent[rb];
          if (ra != rb) parent[rb] = ra;
        }
  for (int n = 0; n < 128; n++) {
    int r = n;
    while (parent[r] != r) r = parent[r];
    parent[n] = r;
  }
  memset(f->actors, 0, sizeof f->actors);
  bool used[kActorCount] = {false};
  static unsigned serial = 1;
  for (int group = 0; group < 128; group++)
    if (visible[group] && parent[group] == group) {
      int x0 = 10000, y0 = 10000, x1 = -10000, y1 = -10000, rank0 = 128;
      for (int n = 0; n < 128; n++)
        if (visible[n] && parent[n] == group) {
          const Piece *q = &frame->pieces[n];
          int x = q->x - f->bias + f->extra, y = q->y;
          if (x < x0) x0 = x;
          if (y < y0) y0 = y;
          if (x + q->size > x1) x1 = x + q->size;
          if (y + q->size > y1) y1 = y + q->size;
          int rank = (n - first + 128) & 127;
          if (rank < rank0) rank0 = rank;
        }
      if (x1 - x0 > PG_SIDE - 12 || y1 - y0 > PG_SIDE - 12) continue;
      int cx = (x0 + x1) / 2, cy = (y0 + y1) / 2;
      unsigned identity = (frame->pieces[group].attr & 0xfe00) + 1;
      int index = -1, best = 49;
      if (pg_count && previous->valid && previous->width == f->width) {
        for (int i = 0; i < kActorCount; i++)
          if (!used[i]) {
            const PgActor *a = &previous->actors[i];
            if (a->id != identity) continue;
            int dx = abs(a->x - cx), dy = abs(a->y - cy), d = dx > dy ? dx : dy;
            if (d < best) {
              best = d;
              index = i;
            }
          }
      }
      bool matched = index >= 0;
      if (index < 0)
        for (int i = 0; i < kActorCount; i++)
          if (!used[i] && (!pg_count || !previous->actors[i].id)) {
            index = i;
            break;
          }
      if (index < 0)
        for (int i = 0; i < kActorCount; i++)
          if (!used[i]) {
            index = i;
            break;
          }
      if (index < 0) continue;
      used[index] = true;
      PgActor *a = &f->actors[index];
      a->id = (uint16_t)identity;
      a->source = matched ? previous->actors[index].source : (uint16_t)serial++;
      a->x = cx;
      a->y = cy;
      a->rank = rank0;
      a->x0 = a->y0 = PG_SIDE;
      for (int rank = 127; rank >= 0; rank--) {
        int slot = (first + rank) & 127;
        if (!visible[slot] || parent[slot] != group) continue;
        const Piece *q = &frame->pieces[slot];
        unsigned attr = q->attr;
        int sx = q->x - f->bias + f->extra, sy = q->y;
        int base = (attr & 256) ? PPU_objTileAdr2(p) : PPU_objTileAdr1(p);
        unsigned z =
            (128 + ((attr >> 9) & 7) * 16) |
            (SPRITE_PRIO_TO_PRIO((attr >> 12) & 3, (attr & 0x800) == 0) << 8);
        for (int y = 0; y < q->size; y++)
          for (int x = 0; x < q->size; x++) {
            int ox = sx + x - cx + PG_HALF, oy = sy + y - cy + PG_HALF;
            if (ox < 0 || oy < 0 || ox >= PG_SIDE || oy >= PG_SIDE) continue;
            int tx = (attr & 0x4000) ? q->size - 1 - x : x,
                ty = (attr & 0x8000) ? q->size - 1 - y : y;
            int tile = (((attr & 255) / 16 + ty / 8) * 16) |
                       (((attr & 15) + tx / 8) & 15),
                addr = (base + tile * 16 + (ty & 7)) & 0x7fff;
            uint32_t bits = ((uint32_t)p->vram[addr] |
                             ((uint32_t)p->vram[(addr + 8) & 0x7fff] << 16)) >>
                            (7 - (tx & 7));
            unsigned ix = (bits & 1) | ((bits >> 7) & 2) | ((bits >> 14) & 4) |
                          ((bits >> 21) & 8);
            if (!ix) continue;
            a->tex[oy * PG_SIDE + ox] = (uint16_t)(z + ix);
            if (ox < a->x0) a->x0 = ox;
            if (oy < a->y0) a->y0 = oy;
            if (ox + 1 > a->x1) a->x1 = ox + 1;
            if (oy + 1 > a->y1) a->y1 = oy + 1;
          }
      }
      uint32_t h = 2166136261u;
      for (int n = 0; n < PG_SIDE * PG_SIDE; n++)
        h = (h ^ a->tex[n]) * 16777619u;
      a->hash = h;
      if (a->x0 >= a->x1) a->id = 0;
      if (pg_log && a->id)
        fprintf(pg_log,
                "{\"capture\":%d,\"actor\":%d,\"id\":%u,\"source\":%u,\"hash\":"
                "%u,\"x\":%d,\"y\":%d,\"bounds\":[%d,%d,%d,%d]}\n",
                f->sequence, index, a->id, a->source, a->hash, a->x, a->y,
                a->x0, a->y0, a->x1, a->y1);
    }
  f->valid = true;
}
static PgFrame *PgAt(int sequence) {
  if (sequence < 1 || sequence > pg_sequence ||
      pg_sequence - sequence >= pg_count)
    return NULL;
  PgFrame *f =
      &pg_frames[(pg_head + PG_RING - (pg_sequence - sequence)) % PG_RING];
  return f->valid && f->queued && f->sequence == sequence ? f : NULL;
}
static bool PgSame(const PgActor *a, const PgActor *b) {
  return a->id && a->id == b->id && a->source == b->source &&
         abs(a->x - b->x) <= 64 && abs(a->y - b->y) <= 64;
}
static bool PgInsideWindow(const PgLine *l, int x, int extra) {
  unsigned flags = (l->windows >> 20) & 15;
  bool a = false, b = false;
  int l1 = l->wl == 0 ? -extra : l->wl, r1 = l->wr == 255 ? 255 + extra : l->wr;
  int l2 = l->wl2 == 0 ? -extra : l->wl2,
      r2 = l->wr2 == 255 ? 255 + extra : l->wr2;
  if (flags & 2) a = ((x >= l1 && x <= r1) != ((flags & 1) != 0));
  if (flags & 8) b = ((x >= l2 && x <= r2) != ((flags & 4) != 0));
  if (!(flags & 2)) return b;
  if (!(flags & 8)) return a;
  switch ((l->logic >> 10) & 3) {
    case 0:
      return a || b;
    case 1:
      return a && b;
    case 2:
      return a != b;
    default:
      return a == b;
  }
}
static bool PgWindowAllows(int mode, bool inside) {
  return mode == 0 || (mode == 1 && inside) || (mode == 2 && !inside);
}
static uint32_t PgPlaneColor(const PgLine *l, bool inside,
                             uint16_t main, uint16_t sub) {
  bool clip = PgWindowAllows(l->select >> 6, inside);
  bool math = PgWindowAllows((l->select >> 4) & 3, inside) &&
              ((l->math & 0x3f) & (1 << ((main >> 8) & 15)));
  unsigned color = l->palette[main & 255], other = l->fixed;
  bool half = false;
  if (math) {
    half = (l->math & 64) != 0;
    if (l->select & 2) {
      if (sub & 255)
        other = l->palette[sub & 255];
      else
        half = false;
    }
  }
  uint32_t out = 0;
  for (int c = 0; c < 3; c++) {
    int v = clip ? ((color >> (c * 5)) & 31) : 0;
    if (math) {
      int q = (other >> (c * 5)) & 31;
      v += (l->math & 128) ? -q : q;
      if (v < 0) v = 0;
    }
    if (half) v /= 2;
    if (v > 31) v = 31;
    v = ((v << 3) | (v >> 2)) * l->brightness / 15;
    out |= (uint32_t)v << (16 - c * 8);
  }
  return out;
}
static uint32_t PgColor(const PgSurface *s, int width, int x, int y,
                        uint16_t obj) {
  const PgLine *l = &s->line[y];
  int at = y * width + x;
  uint16_t main = s->bg[0][at], sub = s->bg[1][at];
  if ((l->main & 16) && obj > main) main = obj;
  if ((l->sub & 16) && obj > sub) sub = obj;
  bool inside = PgInsideWindow(l, x - (width - 256) / 2, (width - 256) / 2);
  return (x < l->left || x >= l->right) ? 0 : PgPlaneColor(l, inside, main, sub);
}
#include "dkc1_bggen.inc"
/* Index/alpha matching avoids treating changing VRAM at the same OAM address
 * as an identical cell. Transparency participates in the motion estimate. */
static int PgError(const PgActor *a, const PgActor *b, int x, int y, int dx,
                   int dy, int limit) {
  int err = 0;
  for (int yy = -2; yy < 6; yy++)
    for (int xx = -2; xx < 6; xx++) {
      int ax = x + xx, ay = y + yy, bx = ax + dx, by = ay + dy;
      unsigned av = ax >= 0 && ay >= 0 && ax < PG_SIDE && ay < PG_SIDE
                        ? a->tex[ay * PG_SIDE + ax]
                        : 0;
      unsigned bv = bx >= 0 && by >= 0 && bx < PG_SIDE && by < PG_SIDE
                        ? b->tex[by * PG_SIDE + bx]
                        : 0;
      err += (!av != !bv) ? 64 : abs((int)(av & 15) - (int)(bv & 15)) * 3;
      if (err > limit) return err;
    }
  return err;
}
static void PgBuildFlow(int index, const PgActor *a, const PgActor *b) {
  PgFlow *f = &pg_flow[index];
  if (f->a == a->hash && f->b == b->hash) return;
  f->a = a->hash;
  f->b = b->hash;
  memset(f->dx, 0, sizeof f->dx);
  memset(f->dy, 0, sizeof f->dy);
  int x0 = a->x0 < b->x0 ? a->x0 : b->x0, y0 = a->y0 < b->y0 ? a->y0 : b->y0;
  int x1 = a->x1 > b->x1 ? a->x1 : b->x1, y1 = a->y1 > b->y1 ? a->y1 : b->y1;
  for (int y = y0 / 4; y < (y1 + 3) / 4; y++)
    for (int x = x0 / 4; x < (x1 + 3) / 4; x++) {
      int best = PgError(a, b, x * 4, y * 4, 0, 0, 1 << 30),
          at = y * PG_GRID + x;
      if (!best) continue;
      for (int dy = -6; dy <= 6; dy++)
        for (int dx = -6; dx <= 6; dx++) {
          int penalty = (abs(dx) + abs(dy)) * 12;
          int err =
              PgError(a, b, x * 4, y * 4, dx, dy, best - penalty) + penalty;
          if (err < best) {
            best = err;
            f->dx[at] = (int8_t)dx;
            f->dy[at] = (int8_t)dy;
          }
        }
    }
}
/* A bilinear sample of one source pose: coverage, color and priority. At a
 * native keyframe the sample is integer and reproduces that pixel exactly. */
typedef struct PgSample {
  float r, g, b, a;
  uint16_t z;
} PgSample;
static PgSample PgSamplePose(const PgActor *a, float x, float y,
                             const PgLine *l) {
  PgSample s = {0};
  int ix = (int)floorf(x), iy = (int)floorf(y);
  float fx = x - ix, fy = y - iy;
  for (int dy = 0; dy < 2; dy++)
    for (int dx = 0; dx < 2; dx++) {
      int px = ix + dx, py = iy + dy;
      if (px < 0 || py < 0 || px >= PG_SIDE || py >= PG_SIDE) continue;
      uint16_t z = a->tex[py * PG_SIDE + px];
      if (!z) continue;
      float w = (dx ? fx : 1 - fx) * (dy ? fy : 1 - fy);
      unsigned c = l->palette[z & 255];
      s.a += w;
      s.r += w * (c & 31);
      s.g += w * ((c >> 5) & 31);
      s.b += w * ((c >> 10) & 31);
      if (w > 0) s.z = z;
    }
  return s;
}
static uint32_t PgShadeSample(const PgSurface *s, int width, int x, int y,
                              PgSample sample) {
  int at = y * width + x;
  const PgLine *l = &s->line[y];
  uint16_t obj = sample.z;
  uint32_t bg = PgBgColor(s, width, x, y);
  bool filtered = pg_bg[at].valid;
  if (!(l->main & 16) || (!filtered && obj <= s->bg[0][at]) || sample.a < 0.001f) return bg;
  /* Shade the palette-interpolated color with the exact current line's math.
   * Quantization happens only at the final output, retaining fractional pose
   * motion at 60/120 Hz even when the sprite advances less than one texel. */
  bool inside = PgInsideWindow(l, x - (width - 256) / 2, (width - 256) / 2);
  bool clip = PgWindowAllows(l->select >> 6, inside);
  bool math = PgWindowAllows((l->select >> 4) & 3, inside) &&
              ((l->math & 0x3f) & (1 << ((obj >> 8) & 15)));
  uint16_t sub = s->bg[1][at];
  if ((l->sub & 16) && obj > sub) sub = obj;
  if (filtered && (l->sub & 16)) sub = obj;
  unsigned other = l->fixed;
  bool half = math && (l->math & 64);
  if (math && (l->select & 2)) {
    if (sub & 255)
      other = l->palette[sub & 255];
    else
      half = false;
  }
  float channels[3] = {sample.r / sample.a, sample.g / sample.a,
                       sample.b / sample.a};
  uint32_t result = 0;
  for (int c = 0; c < 3; c++) {
    float v = clip ? channels[c] : 0;
    if (math) {
      int q = (other >> (c * 5)) & 31;
      v += (l->math & 128) ? -q : q;
    }
    if (half) v *= .5f;
    if (v < 0) v = 0;
    if (v > 31) v = 31;
    int lo = (int)v, hi = lo < 31 ? lo + 1 : 31;
    int low = ((lo << 3) | (lo >> 2)) * l->brightness / 15;
    int high = ((hi << 3) | (hi >> 2)) * l->brightness / 15;
    float native = low + (high - low) * (v - lo);
    if (filtered) {
      const PgBgOver *o = &pg_bg[at].over[(obj >> 14) & 3];
      native = native * o->transmit + o->rgb[c];
    }
    int shift = 16 - c * 8;
    int value =
        (int)(native * sample.a + ((bg >> shift) & 255) * (1 - sample.a) + .5f);
    result |= (uint32_t)value << shift;
  }
  return result;
}
static unsigned PgCompose(PgFrame *f, PgFrame *before, bool half,
                          uint32_t *out) {
  PgSurface *s = half ? &f->mid : &f->real;
  int width = f->width;
  memset(pg_bg, 0, (size_t)width * PG_H * sizeof *pg_bg);
  memcpy(out, s->raw, (size_t)width * PG_H * 4);
  if (!s->captured) return 0;
  PgBgStart(f, before, half, out);
  /* An independent re-composition gate: never replace pixels in a frame whose
   * palette, window, priority or math was not reconstructed exactly. */
  unsigned mismatch = 0;
  for (int y = 0; y < PG_H; y++) {
    if (!s->line[y].supported) {
      PgBgFinish();
      memcpy(out, s->raw, (size_t)width * PG_H * 4);
      memset(pg_bg, 0, (size_t)width * PG_H * sizeof *pg_bg);
      return 0;
    }
    uint32_t color_cache[2][2048];
    uint8_t cached[2][2048] = {{0}};
    const PgLine *l = &s->line[y];
    for (int x = 0; x < width; x++) {
      int at = y * width + x;
      uint16_t main = s->bg[0][at], sub = s->bg[1][at], obj = s->obj[at];
      if ((l->main & 16) && obj > main) main = obj;
      if ((l->sub & 16) && obj > sub) sub = obj;
      uint32_t actual;
      if (x < l->left || x >= l->right) actual = 0;
      else if ((main == sub || !(l->select & 2) || !(l->math & 63) ||
                ((l->select >> 4) & 3) == 3) && (main & 0xfff) < 2048) {
        bool inside = PgInsideWindow(l, x-f->extra, f->extra);
        int key = main & 0xfff;
        if (!cached[inside][key]) {
          cached[inside][key] = 1;
          color_cache[inside][key] = PgPlaneColor(l, inside, main, sub);
        }
        actual = color_cache[inside][key];
      } else actual = PgColor(s, width, x, y, obj);
      if (actual != (s->raw[at] & 0xffffff)) {
        if (!mismatch && getenv("DKC1_POSE_DEBUG"))
          fprintf(stderr,
                  "pose mismatch seq=%d xy=%d,%d obj=%04x bg=%04x,%04x "
                  "actual=%06x got=%06x cg=%04x fixed=%04x select=%02x "
                  "math=%02x main=%02x sub=%02x\n",
                  f->sequence, x, y, s->obj[at], s->bg[0][at], s->bg[1][at],
                  s->raw[at] & 0xffffff, PgColor(s, width, x, y, s->obj[at]),
                  s->line[y].palette[s->obj[at] & 255], s->line[y].fixed,
                  s->line[y].select, s->line[y].math, s->line[y].main,
                  s->line[y].sub);
        mismatch++;
      }
    }
  }
  pg_last_mismatch += mismatch;
  if (mismatch) {
    PgBgFinish();
    memcpy(out, s->raw, (size_t)width * PG_H * 4);
    memset(pg_bg, 0, (size_t)width * PG_H * sizeof *pg_bg);
    return 0;
  }
  static int8_t owner[PG_PIX];
  static uint8_t rank[PG_PIX];
  memset(owner, -1, (size_t)width * PG_H);
  memset(rank, 128, (size_t)width * PG_H);
  int ox[kActorCount], oy[kActorCount];
  float px[kActorCount], py[kActorCount];
  for (int i = 0; i < kActorCount; i++) {
    PgActor *a = &f->actors[i];
    ox[i] = a->x;
    oy[i] = a->y;
    px[i] = (float)a->x;
    py[i] = (float)a->y;
    if (half && before && PgSame(a, &before->actors[i])) {
      ox[i] -= (a->x - before->actors[i].x) / 2;
      oy[i] -= (a->y - before->actors[i].y) / 2;
      px[i] = (a->x + before->actors[i].x) * .5f;
      py[i] = (a->y + before->actors[i].y) * .5f;
    }
    if (!a->id) continue;
    for (int yy = a->y0; yy < a->y1; yy++)
      for (int xx = a->x0; xx < a->x1; xx++) {
        uint16_t z = a->tex[yy * PG_SIDE + xx];
        if (!z) continue;
        int x = ox[i] + xx - PG_HALF, y = oy[i] + yy - PG_HALF;
        if (x < 0 || y < 0 || x >= width || y >= PG_H) continue;
        int at = y * width + x;
        if (z == s->obj[at] && a->rank < rank[at]) {
          rank[at] = (uint8_t)a->rank;
          owner[at] = (int8_t)i;
        }
      }
  }
  bool active[kActorCount] = {false};
  const PgActor *start[kActorCount], *end[kActorCount];
  float alpha[kActorCount];
  float time = (float)f->sequence - (half ? .5f : 0);
  for (int i = 0; i < kActorCount; i++) {
    PgFrame *base = half ? before : f;
    if (!base) continue;
    const PgActor *a = &base->actors[i];
    if (!PgSame(a, &f->actors[i])) continue;
    int lo = base->sequence, hi = 0;
    const PgActor *b = NULL;
    while (lo > base->sequence - PG_DELAY * 2) {
      PgFrame *p = PgAt(lo - 1);
      if (!p || !PgSame(a, &p->actors[i]) || p->actors[i].hash != a->hash)
        break;
      lo--;
    }
    for (int n = base->sequence + 1; n <= pg_sequence; n++) {
      PgFrame *p = PgAt(n);
      if (!p || !PgSame(a, &p->actors[i])) break;
      if (p->actors[i].hash != a->hash) {
        hi = n;
        b = &p->actors[i];
        break;
      }
    }
    if (!hi) {
      if (half && a->hash == f->actors[i].hash &&
          (a->x != f->actors[i].x || a->y != f->actors[i].y)) {
        start[i] = end[i] = a;
        alpha[i] = .5f;
        PgBuildFlow(i, a, a);
        active[i] = true;
      }
      continue;
    }
    if (hi - lo > PG_DELAY) lo = hi - PG_DELAY;
    if (time <= lo) continue;
    start[i] = a;
    end[i] = b;
    alpha[i] = (time - lo) / (hi - lo);
    if (alpha[i] >= 1) continue;
    PgBuildFlow(i, a, b);
    active[i] = true;
    if (pg_log)
      fprintf(pg_log,
              "{\"display\":%d,\"half\":%d,\"actor\":%d,\"start\":%d,\"end\":%"
              "d,\"alpha\":%.4f}\n",
              f->sequence, half, i, lo, hi, alpha[i]);
  }
  /* Ambiguous overlaps retain cartridge output; a hidden actor cannot be
   * reconstructed from a frontmost-only evidence plane. */
  for (int i = 0; i < kActorCount; i++)
    if (active[i])
      for (int j = 0; j < kActorCount; j++)
        if (i != j && f->actors[j].id) {
          const PgActor *a = &f->actors[i], *b = &f->actors[j];
          if (ox[i] + a->x0 < ox[j] + b->x1 && ox[j] + b->x0 < ox[i] + a->x1 &&
              oy[i] + a->y0 < oy[j] + b->y1 && oy[j] + b->y0 < oy[i] + a->y1)
            active[i] = active[j] = false;
        }
  for (int at = 0; at < width * PG_H; at++)
    if (owner[at] < 0 && (s->obj[at] & 255)) rank[at] = 0;
  PgBgFinish();
  /* Remove only ownership-proven original pixels. The background comes from
   * a native PPU pass with OBJ disabled, not inferred/inpainted image data. */
  for (int y = 0; y < PG_H; y++)
    for (int x = 0; x < width; x++) {
      int at = y * width + x;
      if (owner[at] >= 0 && active[owner[at]]) {
        out[at] = PgBgColor(s, width, x, y);
        rank[at] = 128;
      }
    }
  unsigned changed = 0;
  for (int i = 0; i < kActorCount; i++)
    if (active[i]) {
      pg_last_actors++;
      const PgActor *a = start[i], *b = end[i];
      PgFlow *flow = &pg_flow[i];
      float t = alpha[i];
      int x0 = a->x0 < b->x0 ? a->x0 : b->x0,
          y0 = a->y0 < b->y0 ? a->y0 : b->y0;
      int x1 = a->x1 > b->x1 ? a->x1 : b->x1,
          y1 = a->y1 > b->y1 ? a->y1 : b->y1;
      for (int yy = y0 - 6; yy < y1 + 6; yy++)
        for (int xx = x0 - 6; xx < x1 + 6; xx++) {
          int base_x = (int)floorf(px[i]), base_y = (int)floorf(py[i]);
          int x = base_x + xx - PG_HALF, y = base_y + yy - PG_HALF;
          if (x < 0 || y < 0 || x >= width || y >= PG_H || xx < 0 || yy < 0 ||
              xx >= PG_SIDE || yy >= PG_SIDE)
            continue;
          int at = y * width + x;
          if (rank[at] < f->actors[i].rank) continue;
          int cell = (yy / 4) * PG_GRID + xx / 4;
          float dx = flow->dx[cell], dy = flow->dy[cell];
          float lx = xx + base_x - px[i], ly = yy + base_y - py[i];
          PgSample u = PgSamplePose(a, lx - t * dx, ly - t * dy, &s->line[y]);
          PgSample v = PgSamplePose(b, lx + (1 - t) * dx, ly + (1 - t) * dy,
                                    &s->line[y]);
          PgSample q = {u.r * (1 - t) + v.r * t, u.g * (1 - t) + v.g * t,
                        u.b * (1 - t) + v.b * t, u.a * (1 - t) + v.a * t,
                        v.z ? v.z : u.z};
          if (q.a < .001f) continue;
          if (!pg_bg[at].valid && q.z <= s->bg[0][at]) continue;
          out[at] = PgShadeSample(s, width, x, y, q);
          rank[at] = (uint8_t)f->actors[i].rank;
        }
    }
  for (int at = 0; at < width * PG_H; at++)
    if (out[at] != (s->raw[at] & 0xffffff)) changed++;
  return changed;
}
void Dkc1PoseGenPresent(const uint8_t *real, const uint8_t *mid, bool mid_valid,
                        uint8_t *out_real, uint8_t *out_mid,
                        Dkc1FrameGenStats *stats) {
  PgFrame *current = &pg_frames[pg_head];
  size_t bytes = (size_t)current->width * PG_H * 4;
  memcpy(current->real.raw, real, bytes);
  memcpy(current->mid.raw, mid_valid ? mid : real, bytes);
  current->mid_valid = mid_valid;
  current->queued = true;
  if (pg_count < PG_RING) pg_count++;
  int oldest = pg_sequence - pg_count + 1;
  int display_sequence = pg_sequence - PG_DELAY;
  if (display_sequence < oldest) display_sequence = oldest;
  PgFrame *f = PgAt(display_sequence);
  pg_last_actors = pg_last_pixels = pg_last_mismatch = 0;
  if (!f) {
    memcpy(out_real, real, bytes);
    memcpy(out_mid, mid_valid ? mid : real, bytes);
    return;
  }
  PgFrame *prev = PgAt(f->sequence - 1), *next = PgAt(f->sequence + 1);
  pg_last_pixels += PgCompose(f, prev, false, (uint32_t *)out_real);
  /* The presenter submits real F, then F+0.5 while the producer computes its
   * next frame. Look-ahead already contains both endpoints. During warm-up,
   * repeat the first image instead of advancing and then jumping backward. */
  if (pg_count > PG_DELAY && next && next->mid_valid)
    pg_last_pixels += PgCompose(next, f, true, (uint32_t *)out_mid);
  else
    memcpy(out_mid, out_real, bytes);
  if (stats) {
    stats->pose_actors = pg_last_actors;
    stats->pose_pixels = pg_last_pixels;
    stats->pose_mismatch = pg_last_mismatch;
    stats->pose_source_frame = f->sequence;
  }
}
