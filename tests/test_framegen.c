/* Exercise the actual renderer and queue, not a second implementation. */
#include <assert.h>
#include <stdio.h>

#include "../runner/dkc1_framegen.c"
static uint32_t real_out[PG_PIX], mid_out[PG_PIX];
static uint32_t output_digest = 2166136261u;
static void fixture(int n, int changed) {
  Dkc1PoseGenBegin();
  PgFrame *f = &pg_frames[pg_head];
  f->width = 256;
  f->terrain_layer = 0;
  f->layout = 0;
  f->extra = 0;
  f->valid = true;
  f->guest_frame = n;
  memset(f->actors, 0, sizeof f->actors);
  PgActor *a = &f->actors[0];
  a->id = 1;
  a->source = 1;
  a->x = 48;
  a->y = 48;
  a->rank = 0;
  a->hash = changed ? 2 : 1;
  a->x0 = changed ? 48 : 44;
  a->x1 = a->x0 + 4;
  a->y0 = 44;
  a->y1 = 48;
  for (int y = a->y0; y < a->y1; y++)
    for (int x = a->x0; x < a->x1; x++) a->tex[y * PG_SIDE + x] = 0xe681;
  PgSurface *s = &f->real;
  memset(s, 0, sizeof *s);
  s->captured = true;
  for (int y = 0; y < PG_H; y++) {
    s->line[y].supported = true;
    s->line[y].brightness = 15;
    s->line[y].main = 16;
    s->line[y].right = 256;
    s->line[y].palette[0] = 0x7c00;
    s->line[y].palette[129] = 31;
    for (int x = 0; x < 256; x++) {
      int at = y * 256 + x;
      bool red = x >= a->x0 && x < a->x1 && y >= 44 && y < 48;
      s->obj[at] = red ? 0xe681 : 0;
      s->raw[at] = red ? 0xff0000 : 0x0000ff;
    }
  }
  f->mid = f->real;
}
static double centroid(const uint32_t *pixels) {
  double mass = 0, xsum = 0;
  for (int y = 40; y < 52; y++)
    for (int x = 36; x < 60; x++) {
      double r = (pixels[y * 256 + x] >> 16) & 255;
      mass += r;
      xsum += x * r;
    }
  assert(mass > 0);
  return xsum / mass;
}
static void queue_and_poses(void) {
  Dkc1FrameGenSetEnabled(true);
  PoseGenReset();
  double centers[5] = {0}, mids[5] = {0};
  for (int n = 1; n <= 9; n++) {
    fixture(n, n >= 5);
    PgFrame *f = &pg_frames[pg_head];
    Dkc1FrameGenStats stats = {0};
    Dkc1PoseGenPresent((uint8_t *)f->real.raw, (uint8_t *)f->mid.raw, true,
                       (uint8_t *)real_out, (uint8_t *)mid_out, &stats);
    assert(stats.pose_mismatch == 0);
    assert(stats.pose_source_frame == (n > 4 ? n - 4 : 1));
    if (n >= 5) {
      centers[n - 5] = centroid(real_out);
      mids[n - 5] = centroid(mid_out);
    }
    if (n <= 4) assert(memcmp(real_out, mid_out, 256 * 224 * 4) == 0);
    if (n == 5 || n == 9)
      assert(memcmp(real_out,
                    pg_frames[(pg_head + PG_RING - 4) % PG_RING].real.raw,
                    256 * 224 * 4) == 0);
  }
  for (int n = 1; n < 5; n++) {
    assert(centers[n] > centers[n - 1]);
    assert(mids[n - 1] > centers[n - 1] && mids[n - 1] < centers[n]);
  }
  assert(centers[0] == 45.5 && centers[4] == 49.5);
  Dkc1FrameGenInvalidate();
  assert(pg_count == 0 && pg_sequence == 0);
  fixture(1, 0);
  Dkc1FrameGenStats stats = {0};
  PgFrame *f = &pg_frames[pg_head];
  Dkc1PoseGenPresent((uint8_t *)f->real.raw, (uint8_t *)f->mid.raw, false,
                     (uint8_t *)real_out, (uint8_t *)mid_out, &stats);
  assert(stats.pose_source_frame == 1);
  assert(memcmp(mid_out, f->real.raw, 256 * 224 * 4) == 0);
}
static void fractional_translation(void) {
  PoseGenReset();
  for (int n = 1; n <= 9; n++) {
    fixture(n, 0);
    PgFrame *f = &pg_frames[pg_head];
    f->actors[0].x += n - 1;
    for (int y = 0; y < PG_H; y++) {
      for (int x = 0; x < 256; x++) {
        int at = y * 256 + x;
        bool red = x >= 44+n-1 && x < 48+n-1 && y >= 44 && y < 48;
        f->real.obj[at] = red ? 0xe681 : 0;
        f->real.raw[at] = red ? 0xff0000 : 0x0000ff;
      }
    }
    f->mid = f->real;
    Dkc1FrameGenStats stats = {0};
    Dkc1PoseGenPresent((uint8_t *)f->real.raw, (uint8_t *)f->mid.raw, true,
                      (uint8_t *)real_out, (uint8_t *)mid_out, &stats);
    assert(stats.pose_mismatch == 0);
    if (n >= 5) {
      assert(centroid(real_out) == 45.5+n-5);
      assert(fabs(centroid(mid_out)-centroid(real_out)-.5) < .01);
    }
  }
}

static void interrupted_capture(void) {
  PoseGenReset();
  fixture(1, 0);
  PgFrame *f = &pg_frames[pg_head];
  Dkc1PoseGenPresent((uint8_t *)f->real.raw, (uint8_t *)f->mid.raw, false,
                    (uint8_t *)real_out, (uint8_t *)mid_out, NULL);
  fixture(2, 0); /* An inspection render bypasses presentation. */
  fixture(3, 1);
  f = &pg_frames[pg_head];
  Dkc1FrameGenStats stats = {0};
  Dkc1PoseGenPresent((uint8_t *)f->real.raw, (uint8_t *)f->mid.raw, false,
                    (uint8_t *)real_out, (uint8_t *)mid_out, &stats);
  assert(stats.pose_source_frame == 3);
  assert(memcmp(real_out, f->real.raw, 256*224*4) == 0);
  assert(memcmp(mid_out, real_out, 256*224*4) == 0);
}

static void math_and_fallback(void) {
  PoseGenReset();
  fixture(1, 0);
  PgFrame *f = &pg_frames[pg_head];
  PgSurface *s = &f->real;
  /* OBJ palette 0..3 is layer 6: bit 6 of CGADSUB is half, not enable. */
  s->line[44].math = 0xc2;
  s->line[44].fixed = 0x1084;
  assert(PgColor(s, 256, 44, 44, 0xe681) == 0xff0000);
  s->raw[0] ^= 1;
  pg_last_mismatch = 0;
  assert(PgCompose(f, NULL, false, real_out) == 0);
  assert(pg_last_mismatch == 1);
  assert(memcmp(real_out, s->raw, 256 * 224 * 4) == 0);
  s->raw[0] ^= 1;
  s->line[44].supported = false;
  assert(PgCompose(f, NULL, false, real_out) == 0);
  assert(memcmp(real_out, s->raw, 256 * 224 * 4) == 0);
}
static void vram_identity_and_scene_cut(void) {
  static Ppu p;
  static Frame frame;
  memset(&p, 0, sizeof p);
  memset(&frame, 0, sizeof frame);
  p.bgmode = 1;
  p.inidisp = 15;
  p.screenEnabled[0] = 16;
  for (int i = 0; i < 128; i++) p.oam[i * 2] = 0xf000;
  p.oam[0] = 40 | (40 << 8);
  p.oam[1] = 0x3000;
  for (int y = 0; y < 8; y++) p.vram[y] = 0xff;
  frame.width = 256;
  frame.bgmode = 1;
  frame.inidisp = 15;
  PoseGenReset();
  uint32_t hash = 0;
  for (int n = 1; n <= 2; n++) {
    Dkc1PoseGenBegin();
    frame.frame_counter = n;
    memcpy(frame.oam, p.oam, sizeof frame.oam);
    DecodePieces(&frame);
    PoseGenActors(&frame, &p);
    assert(pg_frames[pg_head].actors[0].id);
    if (n == 1)
      hash = pg_frames[pg_head].actors[0].hash;
    else
      assert(hash != pg_frames[pg_head].actors[0].hash);
    pg_count++;
    pg_frames[pg_head].queued = true;
    p.vram[0] ^= 0xff;
  }
  Dkc1PoseGenBegin();
  frame.frame_counter = 100;
  PoseGenActors(&frame, &p);
  assert(pg_count == 0);
  pg_count = 1;
  pg_frames[pg_head].queued = true;
  Dkc1PoseGenBegin();
  frame.frame_counter++;
  frame.terrain_layer = -1;
  PoseGenActors(&frame, &p);
  assert(pg_count == 0);
}
static double bg_centroid(const uint32_t *pixels, int row, int channel) {
  double sum = 0, mass = 0;
  for (int x = 8; x < 240; x++) {
    double value = (pixels[row * 256 + x] >> (16 - channel * 8)) & 255;
    sum += value * x; mass += value;
  }
  assert(mass > 0);
  return sum / mass;
}
static void background_fixture(int n, int direction) {
  fixture(n, 0);
  PgFrame *f = &pg_frames[pg_head];
  memset(f->actors, 0, sizeof f->actors);
  memset(&f->real, 0, sizeof f->real);
  f->real.captured = true;
  int scroll[3] = {direction * 3*n, direction * (3*n/2), direction * (n/2)};
  int previous[3] = {direction * 3*(n-1), direction * (3*(n-1)/2), direction * ((n-1)/2)};
  for (int mid = 0; mid < 2; mid++) {
    PgSurface *s = mid ? &f->mid : &f->real;
    memset(s, 0, sizeof *s);
    s->captured = true;
    for (int y = 0; y < PG_H; y++) {
      PgLine *l = &s->line[y];
      l->supported = s->layers_valid[y] = true;
      l->brightness = 15; l->main = l->sub = 23; l->right = 256;
      l->palette[1] = 31; l->palette[2] = 31 << 5; l->palette[3] = 31 << 10;
      for (int b = 0; b < 3; b++)
        s->scroll[0][b][y] = (scroll[b] - (mid ? (scroll[b]-previous[b])/2 : 0)) & 1023;
      for (int x = 0; x < 256; x++) {
        int at = y * 256 + x;
        s->bg[0][at] = s->bg[1][at] = 0x0500;
        for (int b = 0; b < 3; b++) {
          int h = PgScrollDelta(s->scroll[0][b][y], 0);
          if (y == 50 + b*20 && x >= 120-h && x < 128-h) {
            s->layer[b][at] = (uint16_t)((b == 0 ? 0x8000 : b == 1 ? 0x7100 : 0x1200) | (b+1));
            s->bg[0][at] = s->bg[1][at] = s->layer[b][at];
          }
        }
        s->raw[at] = PgColor(s, 256, x, y, 0);
      }
    }
  }
}
static void fractional_backgrounds(void) {
  for (int direction = -1; direction <= 1; direction += 2) {
    PoseGenReset();
    double previous[3] = {0};
    for (int n = 1; n <= 14; n++) {
      background_fixture(n, direction);
      PgFrame *f = &pg_frames[pg_head];
      Dkc1FrameGenStats stats = {0};
      Dkc1PoseGenPresent((uint8_t *)f->real.raw, (uint8_t *)f->mid.raw, true,
                         (uint8_t *)real_out, (uint8_t *)mid_out, &stats);
      assert(stats.pose_mismatch == 0);
      for (int at = 0; at < 256*224; at++) {
        output_digest = (output_digest ^ real_out[at]) * 16777619u;
        output_digest = (output_digest ^ mid_out[at]) * 16777619u;
      }
      if (n < 10) continue;
      const double velocity[3] = {3, 1.5, .5};
      for (int b = 0; b < 3; b++) {
        double real = bg_centroid(real_out, 50+b*20, b);
        double mid = bg_centroid(mid_out, 50+b*20, b);
        if (fabs(mid-real + direction*velocity[b]/2) >= .01)
          fprintf(stderr, "bg n=%d dir=%d layer=%d real=%.5f mid=%.5f\n", n, direction, b, real, mid);
        assert(fabs(mid-real + direction*velocity[b]/2) < .01);
        if (n > 10) assert(fabs(real-previous[b] + direction*velocity[b]) < .01);
        previous[b] = real;
      }
    }
    /* A mismatched isolated plane fails closed; the native oracle stays exact. */
    PgFrame *f = PgAt(pg_sequence-4);
    f->real.layer[0][50*256+120] = 0x8001;
    PgCompose(f, PgAt(f->sequence-1), false, real_out);
    assert(!pg_bg[50*256+100].valid);
    assert(memcmp(real_out+50*256, f->real.raw+50*256, 256*4) == 0);
  }
}
static void background_priority_boundary(void) {
  PoseGenReset();
  for (int n = 1; n <= 14; n++) {
    background_fixture(n, 1);
    PgFrame *f = &pg_frames[pg_head];
    Dkc1PoseGenPresent((uint8_t *)f->real.raw, (uint8_t *)f->mid.raw, true,
                       (uint8_t *)real_out, (uint8_t *)mid_out, NULL);
  }
  PgFrame *f = PgAt(10);
  PgSurface *s = &f->mid;
  int y = 50;
  s->line[y].palette[129] = 31 << 5;
  for (int x = 0; x < 256; x++) {
    int at = y*256+x;
    s->layer[0][at] = x < 100 ? 0x8001 : 0xc001;
    s->layer[1][at] = 0;
    s->layer[2][at] = 0x1203;
    s->bg[0][at] = s->bg[1][at] = s->layer[0][at];
    s->obj[at] = 0xa681; /* between BG1's two tile priorities */
    s->raw[at] = PgColor(s,256,x,y,s->obj[at]);
  }
  pg_last_mismatch = 0;
  PgCompose(f,PgAt(9),true,mid_out);
  assert(pg_last_mismatch == 0);
  assert(mid_out[y*256+99] == 0x00ff00);
  assert(mid_out[y*256+100] == 0x808000);
  assert(mid_out[y*256+101] == 0xff0000);
  /* The same layer's high/low samples are mutually exclusive, so their
   * boundary must never leak the blue layer behind them. */
  assert(PgBgColor(s,256,100,y) == 0xff0000);
  s->layers_valid[y] = false;
  PgCompose(f,PgAt(9),true,mid_out);
  assert(memcmp(mid_out+y*256,s->raw+y*256,256*4)==0);
}
static double channel_center(const uint32_t *pixels, int channel, int axis) {
  double mass = 0, sum = 0;
  for (int y = 20; y < 190; y++)
    for (int x = 20; x < 230; x++) {
      double value = (pixels[y*256+x] >> (16-channel*8)) & 255;
      mass += value;
      sum += value * (axis ? y : x);
    }
  assert(mass > 0);
  return sum / mass;
}
/* Fixed pickup and a fine terrain pattern share a camera that accelerates,
 * stops, reverses, moves vertically and crosses the 10-bit scroll wrap.
 * The display must preserve their separation at real AND midpoint phases. */
static void terrain_object_registration(void) {
  static const int camera[] = {0,0,0,1,3,6,10,15,20,24,27,29,30,30,30,29,27,24,20,15,10,6,3,1,0,0};
  for (int terrain = -1; terrain < 2; terrain++) {
    PoseGenReset();
    for (int n = 1; n <= (int)(sizeof camera/sizeof *camera); n++) {
      fixture(n, 0);
      PgFrame *f = &pg_frames[pg_head];
      f->terrain_layer = terrain;
      int layer = terrain < 0 ? 0 : terrain;
      int cx = camera[n-1]-15, cy = camera[n-1]/2-7;
      int bx = n > 1 ? camera[n-2]-15 : cx;
      int by = n > 1 ? camera[n-2]/2-7 : cy;
      PgActor *a = &f->actors[0];
      a->x = 100-cx; a->y = 90-cy;
      a->x0 = a->y0 = 44; a->x1 = a->y1 = 52;
      memset(a->tex, 0, sizeof a->tex);
      for (int y = 44; y < 52; y++)
        for (int x = 44; x < 52; x++) a->tex[y*PG_SIDE+x] = 0xe681;
      for (int half = 0; half < 2; half++) {
        PgSurface *s = half ? &f->mid : &f->real;
        memset(s, 0, sizeof *s); s->captured = true;
        int hx = half ? cx-(cx-bx)/2 : cx;
        int hy = half ? cy-(cy-by)/2 : cy;
        int ax = half ? a->x-(a->x-(100-bx))/2 : a->x;
        int ay = half ? a->y-(a->y-(90-by))/2 : a->y;
        for (int y = 0; y < PG_H; y++) {
          PgLine *l = &s->line[y];
          l->supported = s->layers_valid[y] = true;
          l->brightness = 15; l->main = l->sub = 23; l->right = 256;
          l->palette[1] = 31; l->palette[129] = 31 << 5;
          s->scroll[0][layer][y] = hx & 1023;
          s->scroll[1][layer][y] = hy & 1023;
          for (int x = 0; x < 256; x++) {
            int at = y*256+x;
            int tx = x+hx-96, ty = y+hy-126;
            bool hatch = tx >= 0 && tx < 8 && ty >= 0 && ty < 8 && ((tx+ty)&1);
            s->layer[layer][at] = hatch ? (uint16_t)((layer ? 0x7100 : 0x8000)|1) : 0;
            s->bg[0][at] = s->bg[1][at] = hatch ? s->layer[layer][at] : 0x0500;
            s->obj[at] = x >= ax-4 && x < ax+4 && y >= ay-4 && y < ay+4 ? 0xe681 : 0;
            s->raw[at] = PgColor(s, 256, x, y, s->obj[at]);
          }
        }
      }
      Dkc1FrameGenStats stats = {0};
      Dkc1PoseGenPresent((uint8_t *)f->real.raw, (uint8_t *)f->mid.raw, true,
                         (uint8_t *)real_out, (uint8_t *)mid_out, &stats);
      assert(stats.pose_mismatch == 0);
      if (n < 6) continue;
      PgFrame *display = PgAt(n-4);
      /* Real camera endpoints must not soften or move either image. */
      assert(memcmp(real_out, display->real.raw, 256*224*4) == 0);
      for (int half = 0; half < 2; half++) {
        const uint32_t *pixels = half ? mid_out : real_out;
        for (int axis = 0; axis < 2; axis++) {
          double separation = channel_center(pixels, 0, axis)-channel_center(pixels, 1, axis);
          assert(fabs(separation-(axis ? 40 : 0)) < .01);
        }
      }
    }
  }
}
int main(void) {
  queue_and_poses();
  fractional_translation();
  interrupted_capture();
  math_and_fallback();
  vram_identity_and_scene_cut();
  fractional_backgrounds();
  background_priority_boundary();
  terrain_object_registration();
  printf("background digest: %08x\n", output_digest);
  puts(
      "framegen: PASS (held poses, fractional phases, reset, VRAM identity, "
      "math, fallback)");
  return 0;
}
