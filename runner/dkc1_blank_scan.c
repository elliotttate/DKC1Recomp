#include "dkc1_blank_scan.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Rendered-blank margin detector.
 *
 * Shadow-level counters see blank ENTRIES; this sees what actually hit the
 * framebuffer — a margin column collapsing to a single flat color while the
 * adjacent native column shows structure is a "tile didn't render" event
 * (layer mask, palette, priority, or fold interactions the tilemap-level
 * accounting cannot observe). Env-gated: DKC1_BLANK_SCAN=<jsonl path>.
 */

static FILE *s_log;
static int s_checked;
static long s_events;
static long s_last_logged_frame = -1000;

long Dkc1BlankScanEventCount(void) {
  return s_events;
}

static int ColumnProfile(const uint8_t *pixels, int width, int height,
                         int x, uint32_t *dominant) {
  /* Returns the dominant color's per-mille share for one column. */
  uint32_t best_color = 0;
  int best_run = 0;
  int matches = 0;
  const uint8_t *column = pixels + (size_t)x * 4;
  /* One pass: count matches of the first pixel, track the longest run for
   * a second candidate; margins are near-uniform when broken, so the first
   * pixel is almost always the dominant color when it matters. */
  uint32_t first;
  memcpy(&first, column, 4);
  uint32_t run_color = first;
  int run = 0;
  for (int y = 0; y < height; y++) {
    uint32_t value;
    memcpy(&value, column + (size_t)y * width * 4, 4);
    if (value == first)
      matches++;
    if (value == run_color) {
      run++;
      if (run > best_run) {
        best_run = run;
        best_color = run_color;
      }
    } else {
      run_color = value;
      run = 1;
    }
  }
  int share = matches * 1000 / height;
  int run_share = best_run * 1000 / height;
  if (run_share > share) {
    *dominant = best_color;
    return run_share;
  }
  *dominant = first;
  return share;
}

void Dkc1BlankScanFrame(long host_frame, const uint8_t *pixels, int width,
                        int height) {
  if (!s_checked) {
    s_checked = 1;
    const char *path = getenv("DKC1_BLANK_SCAN");
    if (path && *path)
      s_log = fopen(path, "wb");
  }
  if (!s_log || width <= 256 || height <= 0)
    return;
  const int extra = (width - 256) / 2;
  if (extra <= 0)
    return;

  int suspects = 0;
  int first_suspect_x = -1;
  uint32_t first_color = 0;
  for (int side = 0; side < 2; side++) {
    const int seam_x = side == 0 ? extra : width - extra - 1;
    uint32_t seam_color;
    const int seam_share = ColumnProfile(pixels, width, height, seam_x,
                                         &seam_color);
    if (seam_share >= 700)
      continue; /* flat native edge (sky) — margins may be legitimately flat */
    const int x0 = side == 0 ? 0 : width - extra;
    for (int x = x0; x < x0 + extra; x++) {
      uint32_t color;
      if (ColumnProfile(pixels, width, height, x, &color) >= 980) {
        suspects++;
        if (first_suspect_x < 0) {
          first_suspect_x = x;
          first_color = color;
        }
      }
    }
  }
  if (!suspects)
    return;
  /* Every margin column flat is presentation policy (pillarboxed menus,
   * fades), not a render failure. The bug signature is PARTIAL failure:
   * some columns render while others collapse. */
  if (suspects >= extra * 2)
    return;
  s_events++;
  /* One line per burst-second, not per frame: consecutive frames with the
   * same broken margin would otherwise flood the log. */
  if (host_frame - s_last_logged_frame < 60)
    return;
  s_last_logged_frame = host_frame;
  fprintf(s_log,
          "{\"schema\":\"dkc1.blank-scan.v1\",\"frame\":%ld,"
          "\"suspect_columns\":%d,\"first_x\":%d,"
          "\"color\":\"%08x\",\"width\":%d}\n",
          host_frame, suspects, first_suspect_x, first_color, width);
  fflush(s_log);
}
