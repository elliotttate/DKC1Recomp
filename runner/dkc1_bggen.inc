/* Included by posegen. Fractional sampling of independently captured native
 * BG planes; never estimates hidden art or modifies guest scroll registers. */
#include <time.h>
#ifdef _WIN32
#undef HIBYTE
#include <windows.h>
#endif
typedef struct PgBgOver {
  float rgb[3], transmit;
} PgBgOver;
typedef struct PgBgPixel {
  uint32_t color;
  PgBgOver over[4];
  bool valid;
} PgBgPixel;
typedef struct PgBgPart {
  float rgb[3], a;
  int priority;
} PgBgPart;
static PgBgPixel pg_bg[PG_PIX];
static int PgScrollDelta(int a, int b) { return ((a - b + 512) & 1023) - 512; }

/* A centered eight-interval box filter removes the cartridge's 1/2, 1/4 and
 * 1/8-pixel scroll quantization. It uses the already accepted four-frame
 * look-ahead and preserves constant velocity. Starts, stops and turns use
 * the same continuous filter; a cut or missing history retains endpoints. */
static float PgBgPosition(PgFrame *f, PgFrame *before, bool half,
                           int axis, int layer, int y) {
  int reference = f->real.scroll[axis][layer][y];
  float fallback = (float)reference;
  if (half && before)
    fallback -= .5f * PgScrollDelta(reference, before->real.scroll[axis][layer][y]);
  /* Terrain and world-space OBJ use the same cartridge camera. Filtering
   * only the terrain over eight ticks makes fixed pickups slide against it,
   * and repeatedly blurs/sharpens fine patterns at native resolution. Keep
   * real terrain endpoints exact, and use the same two-frame midpoint as
   * OBJ. Only independently scrolling parallax planes need dequantization.
   * Unknown terrain ownership retains endpoints for every plane. */
  if (f->terrain_layer < 0 || f->terrain_layer == layer) return fallback;
  int first = f->sequence - 4, last = f->sequence + (half ? 3 : 4);
  int previous = 0;
  float sum = 0;
  for (int n = first; n <= last; n++) {
    PgFrame *p = PgAt(n);
    if (!p || p->layout != f->layout || p->width != f->width ||
        p->terrain_layer != f->terrain_layer ||
        !p->real.layers_valid[y]) return fallback;
    int value = PgScrollDelta(p->real.scroll[axis][layer][y], reference);
    if (n != first) {
      int delta = value - previous;
      if (abs(delta) > 8) return fallback;
    }
    previous = value;
    float weight = !half && (n == first || n == last) ? .5f : 1.f;
    sum += weight * value;
  }
  return reference + sum / 8.f;
}

/* The background and pose-flow jobs read completed immutable frames. Their
 * outputs do not overlap until PgBgFinish, before any sprite is shaded.
 * Keep a single sleeping worker; it never touches the live PPU or inputs. */
static void PgBgCompose(PgFrame *f, PgFrame *before, bool half, uint32_t *out);
#ifdef _WIN32
static struct {
  HANDLE ready, done, thread;
  PgFrame *frame, *before;
  uint32_t *out;
  bool half;
  int initialized;
} pg_bg_worker;
static DWORD WINAPI PgBgWorker(void *unused) {
  (void)unused;
  HMODULE avrt = LoadLibraryA("avrt.dll");
  typedef HANDLE (WINAPI *PgAvSetFn)(LPCSTR, LPDWORD);
  PgAvSetFn set_games = avrt ? (PgAvSetFn)GetProcAddress(avrt, "AvSetMmThreadCharacteristicsA") : NULL;
  DWORD task_index = 0;
  HANDLE mmcss = set_games ? set_games("Games", &task_index) : NULL;
  if (!mmcss) SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_ABOVE_NORMAL);
  for (;;) {
    WaitForSingleObject(pg_bg_worker.ready, INFINITE);
    PgBgCompose(pg_bg_worker.frame, pg_bg_worker.before,
                pg_bg_worker.half, pg_bg_worker.out);
    SetEvent(pg_bg_worker.done);
  }
}
#endif
static void PgBgStart(PgFrame *f, PgFrame *before, bool half, uint32_t *out) {
#ifdef _WIN32
  if (!pg_bg_worker.initialized) {
    pg_bg_worker.initialized = 1;
    const char *sync = getenv("DKC1_FRAMEGEN_BG_SYNC");
    if (!sync || atoi(sync) == 0) {
      pg_bg_worker.ready = CreateEvent(NULL, FALSE, FALSE, NULL);
      pg_bg_worker.done = CreateEvent(NULL, FALSE, FALSE, NULL);
      if (pg_bg_worker.ready && pg_bg_worker.done)
        pg_bg_worker.thread = CreateThread(NULL, 0, PgBgWorker, NULL, 0, NULL);
      if (!pg_bg_worker.thread) {
        if (pg_bg_worker.ready) CloseHandle(pg_bg_worker.ready);
        if (pg_bg_worker.done) CloseHandle(pg_bg_worker.done);
      }
    }
  }
  if (pg_bg_worker.thread) {
    pg_bg_worker.frame = f; pg_bg_worker.before = before;
    pg_bg_worker.half = half; pg_bg_worker.out = out;
    SetEvent(pg_bg_worker.ready);
    return;
  }
#endif
  PgBgCompose(f, before, half, out);
}
static void PgBgFinish(void) {
#ifdef _WIN32
  if (pg_bg_worker.thread) WaitForSingleObject(pg_bg_worker.done, INFINITE);
#endif
}
static uint32_t PgBgColor(const PgSurface *s, int width, int x, int y) {
  const PgBgPixel *p = &pg_bg[y * width + x];
  return p->valid ? p->color : PgColor(s, width, x, y, 0);
}
static uint32_t PgPackColor(const float *rgb) {
  uint32_t out = 0;
  for (int c = 0; c < 3; c++) {
    int value = (int)(rgb[c] + .5f);
    if (value < 0) value = 0;
    if (value > 255) value = 255;
    out |= (uint32_t)value << (16 - c * 8);
  }
  return out;
}
static void PgBgCompose(PgFrame *f, PgFrame *before, bool half, uint32_t *out) {
  static int enabled = -1;
  static FILE *log;
  if (enabled < 0) {
    const char *setting = getenv("DKC1_FRAMEGEN_BG");
    enabled = !setting || atoi(setting) != 0;
    const char *path = getenv("DKC1_BG_MOTION_LOG");
    if (path && *path) log = fopen(path, "wb");
  }
  if (!enabled) return;
  clock_t begin = log ? clock() : 0;
  PgSurface *s = half ? &f->mid : &f->real;
  int width = f->width, valid = 0, failed = 0;
  float reported[2][3] = {{0}};
  uint32_t colors[2][3][256], backdrop[2];
  const PgLine *cached_line = NULL;
  for (int y = 0; y < PG_H; y++) {
    const PgLine *l = &s->line[y];
    if (!s->layers_valid[y]) continue;
    /* Prove extraction reconstructed both screens used by color math.
     * A host margin enhancer may have contributed pixels not present in the
     * individual planes: keep that row native instead of inventing them. */
    bool sub_math = ((l->select >> 4) & 3) != 3 &&
                    (l->select & 2) && (l->math & 63);
    bool good = true;
    int maximum_priority[3] = {0};
    for (int x = l->left; x < l->right && x < width; x++) {
      if (x < 0) continue;
      int at = y * width + x;
      unsigned z = 0x0500;
      for (int b = 0; b < 3; b++) {
        if (s->layer[b][at] > z) z = s->layer[b][at];
        int priority = s->layer[b][at] >> 12;
        if (priority > maximum_priority[b]) maximum_priority[b] = priority;
      }
      if (z != s->bg[0][at] || (sub_math && z != s->bg[1][at])) {
        good = false;
        break;
      }
    }
    if (!good) { failed++; continue; }
    int ix[3], iy[3];
    float weights[3][4];
    int sample_offset[3][4], sample_count[3] = {0};
    float sample_weight[3][4];
    int xmin = l->left > 0 ? l->left : 0;
    int xmax = l->right < width ? l->right : width;
    bool row_bounded = true, unchanged[3];
    for (int b = 0; b < 3; b++) {
      float dx = PgBgPosition(f, before, half, 0, b, y);
      float dy = PgBgPosition(f, before, half, 1, b, y);
      if (y == 112) { reported[0][b] = dx; reported[1][b] = dy; }
      dx -= s->scroll[0][b][y];
      dy -= s->scroll[1][b][y];
      /* Keep the signed displacement small across the 10-bit wrap. */
      while (dx > 512) dx -= 1024;
      while (dx < -512) dx += 1024;
      while (dy > 512) dy -= 1024;
      while (dy < -512) dy += 1024;
      ix[b] = (int)floorf(dx); iy[b] = (int)floorf(dy);
      float fx = dx - ix[b], fy = dy - iy[b];
      weights[b][0] = (1-fx)*(1-fy); weights[b][1] = fx*(1-fy);
      weights[b][2] = (1-fx)*fy; weights[b][3] = fx*fy;
      unchanged[b] = dx == 0 && dy == 0;
      /* Adjacent rows may contain a different tile-priority mix. Do not
       * use this row's opaque-layer shortcut for a vertical sample. */
      if (dy != 0)
        maximum_priority[0] = maximum_priority[1] = maximum_priority[2] = 15;
      for (int k = 0; k < 4; k++) {
        if (weights[b][k] < .00001f) continue;
        int yy = y + iy[b] + (k >> 1), xx = ix[b] + (k & 1);
        if (yy < 0 || yy >= PG_H || !s->layers_valid[yy] ||
            s->scroll[0][b][yy] != s->scroll[0][b][y] ||
            s->scroll[1][b][yy] != s->scroll[1][b][y]) { row_bounded = false; break; }
        int left = s->line[yy].left > 0 ? s->line[yy].left : 0;
        int right = s->line[yy].right < width ? s->line[yy].right : width;
        if (left-xx > xmin) xmin = left-xx;
        if (right-xx < xmax) xmax = right-xx;
        int index = sample_count[b]++;
        sample_offset[b][index] = yy*width + xx;
        sample_weight[b][index] = weights[b][k];
      }
    }
    if (!row_bounded || (unchanged[0] && unchanged[1] && unchanged[2])) continue;
    /* Shade once per palette entry/window side, rather than per pixel. */
    if (!cached_line || memcmp(l, cached_line, sizeof *l)) {
      cached_line = l;
      for (int inside = 0; inside < 2; inside++) {
      backdrop[inside] = PgPlaneColor(l, inside != 0, 0x0500, 0x0500);
      for (int b = 0; b < 3; b++)
        for (int p = 0; p < 256; p++)
          colors[inside][b][p] = PgPlaneColor(l, inside != 0,
                                             (uint16_t)((b << 8) | p),
                                             (uint16_t)((b << 8) | p));
      }
    }
    for (int x = xmin; x < xmax; x++) {
      int at = y * width + x;
      int z0 = s->layer[0][at] >> 12;
      if (unchanged[0] && (s->layer[0][at] & 255) &&
          z0 > maximum_priority[1] && z0 > maximum_priority[2]) continue;
      bool inside = PgInsideWindow(l, x - f->extra, f->extra);
      PgBgPart part[3][2] = {0};
      for (int b = 0; b < 3; b++) {
        for (int k = 0; k < sample_count[b]; k++) {
          float weight = sample_weight[b][k];
          unsigned z = s->layer[b][sample_offset[b][k] + x];
          if (!(z & 255)) continue;
          int priority = z >> 12;
          int high = priority == 12 || priority == 11 || priority == 3 || priority == 15;
          PgBgPart *p = &part[b][high];
          p->priority = priority;
          p->a += weight;
          uint32_t color = colors[inside][b][z & 255];
          for (int c = 0; c < 3; c++)
            p->rgb[c] += weight * ((color >> (16 - c * 8)) & 255);
        }
        /* An opaque plane above every remaining layer cannot reveal any
         * of those layers, including underneath an interpolated sprite. */
        int remaining = 0;
        for (int q = b+1; q < 3; q++)
          if (maximum_priority[q] > remaining) remaining = maximum_priority[q];
        if ((part[b][0].a == 1 && part[b][0].priority > remaining) ||
            (part[b][1].a == 1 && part[b][1].priority > remaining)) break;
      }
      PgBgPixel *dest = &pg_bg[at];
      float background[3] = {0};
      const PgBgPart *front = NULL;
      for (int b = 0; b < 3; b++)
        for (int h = 0; h < 2; h++)
          if (part[b][h].a > 0 && (!front || part[b][h].priority > front->priority))
            front = &part[b][h];
      if (front && front->a == 1) {
        memcpy(background, front->rgb, sizeof background);
        for (int pr = 0; pr < 4; pr++) {
          PgBgOver *o = &dest->over[pr];
          if (front->priority > 2 + pr*4) {
            memcpy(o->rgb, background, sizeof background);
            o->transmit = 0;
          } else o->transmit = 1;
        }
      } else {
      float back_weight = 1;
      for (int b = 0; b < 3; b++) back_weight *= 1 - part[b][0].a - part[b][1].a;
      for (int c = 0; c < 3; c++)
        background[c] = back_weight * ((backdrop[inside] >> (16-c*8)) & 255);
      for (int pr = 0; pr < 4; pr++) {
        PgBgOver *o = &dest->over[pr];
        o->transmit = 1;
        for (int b = 0; b < 3; b++) {
          float covered = 0;
          for (int h = 0; h < 2; h++)
            if (part[b][h].priority > 2 + pr*4) covered += part[b][h].a;
          o->transmit *= 1 - covered;
        }
      }
      for (int b = 0; b < 3; b++)
        for (int h = 0; h < 2; h++) {
          const PgBgPart *p = &part[b][h];
          if (p->a == 0) continue;
          float visibility = 1;
          for (int other = 0; other < 3; other++) {
            if (other == b) continue;
            float covered = 0;
            for (int q = 0; q < 2; q++)
              if (part[other][q].priority > p->priority) covered += part[other][q].a;
            visibility *= 1 - covered;
          }
          for (int c = 0; c < 3; c++) {
            float color = p->rgb[c] * visibility;
            background[c] += color;
            for (int pr = 0; pr < 4; pr++)
              if (p->priority > 2 + pr*4) dest->over[pr].rgb[c] += color;
          }
        }
      }
      dest->color = PgPackColor(background);
      dest->valid = true;
      valid++;
      uint16_t obj = s->obj[at];
      if ((obj & 255) && (l->main & 16)) {
        uint32_t color = PgPlaneColor(l, inside, obj, (l->sub & 16) ? obj : 0x0500);
        const PgBgOver *o = &dest->over[(obj >> 14) & 3];
        for (int c = 0; c < 3; c++)
          background[c] = ((color >> (16-c*8)) & 255) * o->transmit + o->rgb[c];
        out[at] = PgPackColor(background);
      } else out[at] = dest->color;
    }
  }
  if (log)
    fprintf(log, "{\"source\":%.1f,\"pixels\":%d,\"oracle_failed_rows\":%d,"
                 "\"h\":[%.4f,%.4f,%.4f],\"v\":[%.4f,%.4f,%.4f],\"ms\":%.3f}\n",
            f->sequence - (half ? .5 : 0), valid, failed,
            reported[0][0], reported[0][1], reported[0][2],
            reported[1][0], reported[1][1], reported[1][2],
            1000.0 * (clock()-begin) / CLOCKS_PER_SEC);
}
