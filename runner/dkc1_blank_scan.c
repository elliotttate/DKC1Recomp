#include "dkc1_blank_scan.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#define Dkc1StrCaseCmp _stricmp
#else
#include <strings.h>
#define Dkc1StrCaseCmp strcasecmp
#endif

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
static int s_active;
static long s_events;
static long s_last_logged_frame = -1000;

long Dkc1BlankScanEventCount(void) {
  return s_events;
}

static int ColumnProfile(const uint8_t *pixels, int width, int x,
                         int y0, int y1, uint32_t *dominant) {
  /* Returns the dominant color's per-mille share for one column. */
  uint32_t best_color = 0;
  int best_run = 0;
  int matches = 0;
  const uint8_t *column =
      pixels + ((size_t)y0 * width + (size_t)x) * 4;
  /* One pass: count matches of the first pixel, track the longest run for
   * a second candidate; margins are near-uniform when broken, so the first
   * pixel is almost always the dominant color when it matters. */
  uint32_t first;
  memcpy(&first, column, 4);
  uint32_t run_color = first;
  int run = 0;
  for (int y = y0; y < y1; y++) {
    uint32_t value;
    memcpy(&value, pixels + ((size_t)y * width + (size_t)x) * 4, 4);
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
  const int rows = y1 - y0;
  int share = matches * 1000 / rows;
  int run_share = best_run * 1000 / rows;
  if (run_share > share) {
    *dominant = best_color;
    return run_share;
  }
  *dominant = first;
  return share;
}

static int FindFlatMarginColumns(const uint8_t *pixels, int width, int extra,
                                 int y0, int y1,
                                 int *first_suspect_x,
                                 uint32_t *first_color) {
  int suspects = 0;
  for (int side = 0; side < 2; side++) {
    const int seam_x = side == 0 ? extra : width - extra - 1;
    uint32_t seam_color;
    const int seam_share = ColumnProfile(
        pixels, width, seam_x, y0, y1, &seam_color);
    if (seam_share >= 700)
      continue; /* flat native edge (sky) — margin may be legitimately flat */
    /* A legacy-width cut begins at the native/margin boundary and continues
     * toward the outside edge.  Counting arbitrary flat columns anywhere in
     * a margin misclassifies authored gaps between trees (the Expresso Bonus
     * return has a concrete 27-pixel example four pixels past the boundary).
     * Walk outward from the seam and stop at the first structured column. */
    const int first_x = side == 0 ? extra - 1 : width - extra;
    const int step = side == 0 ? -1 : 1;
    for (int i = 0, x = first_x; i < extra; i++, x += step) {
      uint32_t color;
      if (ColumnProfile(pixels, width, x, y0, y1, &color) < 980)
        break;
      suspects++;
      if (*first_suspect_x < 0) {
        *first_suspect_x = x;
        *first_color = color;
      }
    }
  }
  return suspects;
}

void Dkc1BlankScanFrame(long host_frame, const uint8_t *pixels, int width,
                        int height, bool extended_gameplay) {
  if (!s_checked) {
    s_checked = 1;
    const char *path = getenv("DKC1_BLANK_SCAN");
    const char *auto_export = getenv("DKC1_AUTO_EXPORT");
    s_active = (path && *path) ||
        (auto_export && *auto_export && strcmp(auto_export, "0") != 0 &&
         Dkc1StrCaseCmp(auto_export, "false") != 0 &&
         Dkc1StrCaseCmp(auto_export, "off") != 0 &&
         Dkc1StrCaseCmp(auto_export, "no") != 0);
    if (path && *path)
      s_log = fopen(path, "wb");
  }
  if (!s_active || width <= 256 || height <= 0)
    return;
  const int extra = (width - 256) / 2;
  if (extra <= 0)
    return;

  int first_suspect_x = -1;
  uint32_t first_color = 0;
  int suspects = FindFlatMarginColumns(
      pixels, width, extra, 0, height,
      &first_suspect_x, &first_color);
  int suspect_y0 = 0;
  int suspect_y1 = height;
  bool partial_height = false;
  if (!suspects) {
    /* A foreground or window plane can stop only over part of the screen;
     * whole-column profiling dilutes that failure with otherwise valid rows.
     * Sixteen-line bands align with two SNES tiles and remain large enough to
     * reject isolated transparent pixels. Require at least eight flat margin
     * columns before promoting the band. */
    for (int y0 = 0; y0 < height; y0 += 16) {
      const int y1 = y0 + 16 < height ? y0 + 16 : height;
      int band_x = -1;
      uint32_t band_color = 0;
      const int band_suspects = FindFlatMarginColumns(
          pixels, width, extra, y0, y1, &band_x, &band_color);
      if (band_suspects >= 8) {
        suspects = band_suspects;
        first_suspect_x = band_x;
        first_color = band_color;
        suspect_y0 = y0;
        suspect_y1 = y1;
        partial_height = true;
        break;
      }
    }
  }
  if (!suspects)
    return;
  /* A fully flat pair of margins is normal presentation policy only while
   * the host is deliberately centered (menus, logos, fades).  Once DKC1 has
   * proven a supported gameplay layout and published extended terrain, the
   * same shape is the strongest possible culling signature.  The old rule
   * discarded exactly that case and made two-sided gameplay failures
   * impossible to auto-capture. */
  const bool full_flat_gameplay = suspects >= extra * 2;
  if (full_flat_gameplay && !extended_gameplay)
    return;
  s_events++;
  /* One line per burst-second, not per frame: consecutive frames with the
   * same broken margin would otherwise flood the log. */
  if (host_frame - s_last_logged_frame < 60)
    return;
  s_last_logged_frame = host_frame;
  if (s_log) {
    fprintf(s_log,
            "{\"schema\":\"dkc1.blank-scan.v3\",\"frame\":%ld,"
            "\"kind\":\"%s\",\"extended_gameplay\":%s,"
            "\"suspect_columns\":%d,\"first_x\":%d,"
            "\"y0\":%d,\"y1\":%d,"
            "\"color\":\"%08x\",\"width\":%d}\n",
            host_frame, full_flat_gameplay ? "full_flat_gameplay" :
                        partial_height ? "partial_height_flat" : "partial_flat",
            extended_gameplay ? "true" : "false", suspects,
            first_suspect_x, suspect_y0, suspect_y1, first_color, width);
    fflush(s_log);
  }
}
