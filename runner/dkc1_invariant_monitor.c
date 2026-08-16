#include "dkc1_invariant_monitor.h"

#include "dkc1_video.h"

#include "common_rtl.h"
#include "snes/ppu.h"
#include "snes/snes.h"
#include "snes/ws_shadow.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* WRAM semantics: SuperZSNES DkcTraceModel, opcode-verified; identical to
 * the offline auditors (first_divergence.py / audit_prefetch_wram.py). */
enum {
  kAddrScannerWindowLeft = 0x00EF,
  kAddrScannerWindowRight = 0x00F1,
  kAddrOamShadow = 0x0200,
  kAddrLayerX = 0x088B,
  kAddrLayerY = 0x0895,
  kAddrActorPose = 0x0AE5,
  kAddrActorX = 0x0B19,
  kAddrActorY = 0x0BC1,
  kAddrActorId = 0x0D45,
  kAddrActorState = 0x1029,
  kAddrActorAnim = 0x10D1,
  kAddrActorSource = 0x15FD,
  kAddrBookkeeping = 0x192B,
  kAddrCameraUpper = 0x1B25,
  kActorFirst = 0x02,
  kActorCount = 25,
  kBookkeepingLength = 0x100,
  kOamBytes = 544,
  kNativeScanWidth = 0x140, /* stock scanner window span */
};

enum VerdictClass {
  kVerdictActorNoOam,
  kVerdictOamLag,
  kVerdictXHighLoss,
  kVerdictSimOutsideWide,
  kVerdictBookmarkNoActor,
  kVerdictTileNeverStreamed,
  kVerdictStaleMargin,
  kVerdictCacheWindow,
  kVerdictScannerWindow,
  kVerdictClassCount,
};

static const char *const kClassNames[kVerdictClassCount] = {
    "actor_visible_but_no_oam",
    "ppu_oam_stalled",
    "oam_xhigh_lost",
    "simulated_outside_wide_window",
    "bookmark_advanced_without_actor",
    "margin_tile_never_streamed",
    "stale_generated_margin",
    "cache_window_violation",
    "scanner_window_unexpected",
};

typedef struct SlotState {
  uint16_t source;
  uint16_t id;
  uint16_t state;
  uint16_t anim;
  int frames_with_sprites;
  int frames_without_sprites;
  int outside_advancing_streak;
  int reported_no_oam;
  int reported_outside;
} SlotState;

typedef struct PendingBookmark {
  int record;
  long deadline_frame;
} PendingBookmark;

static int s_mode = -1; /* -1 unchecked, 0 off, 1 counters, 2 +jsonl */
static FILE *s_log;
static long s_counts[kVerdictClassCount];
static long s_total;

static SlotState s_slots[kActorCount];
static uint8_t s_bookkeeping[kBookkeepingLength];
static int s_bookkeeping_primed;
static PendingBookmark s_pending[16];
static int s_pending_count;
static long s_source_last_seen[256];
static uint8_t s_prev_shadow[kOamBytes];
static int s_prev_shadow_valid;
static int s_oam_stall_streak;
static WsShadowMarginStat s_prev_stats[2];
static int s_prev_stats_valid;
static uint16_t s_last_scan_width = 0xFFFF;
static long s_last_raw_report = -1000000;
static long s_last_stale_report = -1000000;

static uint16_t W16(unsigned address) {
  return (uint16_t)(g_ram[address] | ((uint16_t)g_ram[address + 1] << 8));
}

static int SignedRel(uint16_t value, uint16_t origin) {
  int rel = (int)((uint16_t)(value - origin));
  if (rel >= 0x8000)
    rel -= 0x10000;
  return rel;
}

static void Verdict(long host_frame, enum VerdictClass verdict_class,
                    const char *detail_json_fragment) {
  s_counts[verdict_class]++;
  s_total++;
  if (s_mode < 2 || !s_log)
    return;
  fprintf(s_log,
          "{\"schema\":\"dkc1.invariant.v1\",\"frame\":%ld,"
          "\"snes_frame\":%d,\"class\":\"%s\"%s%s}\n",
          host_frame, snes_frame_counter, kClassNames[verdict_class],
          detail_json_fragment && *detail_json_fragment ? "," : "",
          detail_json_fragment ? detail_json_fragment : "");
  fflush(s_log);
}

typedef struct Sprite {
  int x; /* signed 9-bit, native screen space */
  int y;
  uint8_t low_x;
  uint8_t high;
} Sprite;

static void DecodeShadowSprites(const uint8_t *shadow, Sprite out[128]) {
  for (int i = 0; i < 128; i++) {
    const uint8_t x_low = shadow[i * 4];
    const uint8_t y = shadow[i * 4 + 1];
    const uint8_t extra = (uint8_t)((shadow[512 + i / 4] >> ((i % 4) * 2)) & 3);
    int x = x_low | ((extra & 1) << 8);
    if (x >= 256)
      x -= 512;
    out[i].x = x;
    out[i].y = y;
    out[i].low_x = x_low;
    out[i].high = (uint8_t)(extra & 1);
  }
}

static void BuildPpuOamBytes(uint8_t out[kOamBytes]) {
  for (int i = 0; i < 256; i++) {
    out[i * 2] = (uint8_t)g_ppu->oam[i];
    out[i * 2 + 1] = (uint8_t)(g_ppu->oam[i] >> 8);
  }
  memcpy(out + 512, g_ppu->highOam, 32);
}

void Dkc1InvariantMonitorFrame(long host_frame) {
  if (s_mode < 0) {
    const char *setting = getenv("DKC1_INVARIANT_MONITOR");
    if (!setting || !*setting || strcmp(setting, "0") == 0) {
      s_mode = 0;
    } else if (strcmp(setting, "1") == 0) {
      s_mode = 1;
    } else {
      s_log = fopen(setting, "wb");
      s_mode = s_log ? 2 : 1;
    }
  }
  if (!s_mode)
    return;

  /* Gameplay gate: in-level bounds present and rendering not blanked. */
  const int gameplay = W16(kAddrCameraUpper) >= 0x100 &&
                       !(g_ppu->inidisp & 0x80);
  const uint16_t cam_x = W16(kAddrLayerX);
  const uint16_t cam_y = W16(kAddrLayerY);
  const int extra = Dkc1VideoIsWidescreen() ? Dkc1VideoExtra() : 0;

  const uint8_t *shadow = g_ram + kAddrOamShadow;
  Sprite sprites[128];
  DecodeShadowSprites(shadow, sprites);

  if (!gameplay) {
    /* Reset transition state so re-entry starts clean episodes. */
    memset(s_slots, 0, sizeof s_slots);
    s_bookkeeping_primed = 0;
    s_pending_count = 0;
    s_prev_shadow_valid = 0;
    s_oam_stall_streak = 0;
    s_last_scan_width = 0xFFFF;
  } else {
    /* --- scanner window shape ------------------------------------- */
    const uint16_t width =
        (uint16_t)(W16(kAddrScannerWindowRight) - W16(kAddrScannerWindowLeft));
    const uint16_t expected =
        (uint16_t)(kNativeScanWidth + 2 * extra);
    if (width != s_last_scan_width) {
      /* Report only transitions to a WRONG width; entry frames while the
       * game still carries the previous scene's window are skipped by
       * requiring a prior in-gameplay width. */
      if (s_last_scan_width != 0xFFFF && width != expected &&
          s_last_scan_width == expected) {
        char detail[96];
        snprintf(detail, sizeof detail,
                 "\"width\":%u,\"expected\":%u", width, expected);
        Verdict(host_frame, kVerdictScannerWindow, detail);
      }
      s_last_scan_width = width;
    }

    /* --- per-actor invariants -------------------------------------- */
    for (int slot = 0; slot < kActorCount; slot++) {
      const unsigned index = (unsigned)(kActorFirst + slot * 2);
      SlotState *st = &s_slots[slot];
      const uint16_t id = W16(kAddrActorId + index);
      const uint16_t source = W16(kAddrActorSource + index);
      if (id == 0 || source == 0 || source >= 0x8000) {
        memset(st, 0, sizeof *st);
        continue;
      }
      if (source < 256)
        s_source_last_seen[source] = host_frame;
      if (st->id != id || st->source != source) {
        memset(st, 0, sizeof *st);
        st->id = id;
        st->source = source;
        st->state = W16(kAddrActorState + index);
        st->anim = W16(kAddrActorAnim + index);
        continue; /* first frame of an episode: nothing to compare yet */
      }
      const uint16_t x = W16(kAddrActorX + index);
      const uint16_t y = W16(kAddrActorY + index);
      const int rel_x = SignedRel(x, cam_x);
      const int rel_y = SignedRel(y, cam_y);
      const uint16_t state = W16(kAddrActorState + index);
      const uint16_t anim = W16(kAddrActorAnim + index);
      const int advancing = state != st->state || anim != st->anim;
      st->state = state;
      st->anim = anim;

      /* Comfortably on-screen only: sprites for actors hugging the
       * screen edges legitimately park offscreen (OAM y wrap), which
       * reads as absence to a naive check. */
      const int in_view = rel_x >= -extra + 16 &&
                          rel_x < 256 + extra - 16 &&
                          rel_y >= 16 && rel_y < 200;
      const int far_outside = rel_x < -(extra + 160) ||
                              rel_x >= 256 + extra + 160;

      if (in_view) {
        int near_sprite = 0;
        for (int i = 0; i < 128 && !near_sprite; i++) {
          if (sprites[i].y >= 0xF0)
            continue;
          if (sprites[i].x - rel_x <= 80 && rel_x - sprites[i].x <= 80 &&
              sprites[i].y - rel_y <= 96 && rel_y - sprites[i].y <= 96)
            near_sprite = 1;
        }
        if (near_sprite) {
          st->frames_with_sprites++;
          st->frames_without_sprites = 0;
        } else if (st->frames_with_sprites >= 30) {
          if (++st->frames_without_sprites >= 3 && !st->reported_no_oam) {
            st->reported_no_oam = 1;
            char detail[160];
            snprintf(detail, sizeof detail,
                     "\"source\":%u,\"id\":%u,\"rel_x\":%d,\"rel_y\":%d,"
                     "\"established_frames\":%d",
                     source, id, rel_x, rel_y, st->frames_with_sprites);
            Verdict(host_frame, kVerdictActorNoOam, detail);
            st->frames_with_sprites = 0;
          }
        }
        st->outside_advancing_streak = 0;
      } else if (far_outside && advancing) {
        if (++st->outside_advancing_streak >= 3 && !st->reported_outside) {
          st->reported_outside = 1;
          char detail[128];
          snprintf(detail, sizeof detail,
                   "\"source\":%u,\"id\":%u,\"rel_x\":%d", source, id, rel_x);
          Verdict(host_frame, kVerdictSimOutsideWide, detail);
        }
      } else {
        st->outside_advancing_streak = 0;
      }
    }

    /* --- X-high loss (live form of the sweep signature) ------------- */
    if (s_prev_shadow_valid) {
      Sprite previous[128];
      DecodeShadowSprites(s_prev_shadow, previous);
      for (int i = 0; i < 128; i++) {
        if (sprites[i].y >= 0xF0 || previous[i].y >= 0xF0)
          continue;
        const int dy = sprites[i].y - previous[i].y;
        const int dl = (int)sprites[i].low_x - (int)previous[i].low_x;
        if (previous[i].high && !sprites[i].high &&
            previous[i].low_x < 64 && dl <= 8 && dl >= -8 &&
            dy <= 2 && dy >= -2) {
          char detail[128];
          snprintf(detail, sizeof detail,
                   "\"oam_index\":%d,\"prev_x\":%d,\"x\":%d,\"y\":%d",
                   i, previous[i].low_x | 256, sprites[i].low_x,
                   sprites[i].y);
          Verdict(host_frame, kVerdictXHighLoss, detail);
        }
      }
    }

    /* --- PPU OAM cadence -------------------------------------------- */
    if (s_prev_shadow_valid) {
      uint8_t ppu_now[kOamBytes];
      BuildPpuOamBytes(ppu_now);
      if (memcmp(ppu_now, s_prev_shadow, kOamBytes) == 0 ||
          memcmp(ppu_now, shadow, kOamBytes) == 0) {
        s_oam_stall_streak = 0;
      } else if (++s_oam_stall_streak == 3) {
        Verdict(host_frame, kVerdictOamLag,
                "\"note\":\"PPU OAM matches neither the current nor the "
                "previous WRAM shadow for 3 frames\"");
      }
    }

    /* --- bookkeeping vs actor pool ---------------------------------- */
    if (!s_bookkeeping_primed) {
      memcpy(s_bookkeeping, g_ram + kAddrBookkeeping, kBookkeepingLength);
      s_bookkeeping_primed = 1;
      s_pending_count = 0;
    } else {
      for (int record = 0; record < kBookkeepingLength; record++) {
        const uint8_t now = g_ram[kAddrBookkeeping + record];
        if (now == s_bookkeeping[record])
          continue;
        s_bookkeeping[record] = now;
        /* Bookmarks routinely advance right AFTER their actor frees
         * (collection/despawn bookkeeping). Only a record whose source
         * has not been embodied for several seconds is suspicious. */
        if (s_source_last_seen[record] &&
            host_frame - s_source_last_seen[record] < 240)
          continue;
        if (s_pending_count < (int)(sizeof s_pending / sizeof s_pending[0])) {
          s_pending[s_pending_count].record = record;
          s_pending[s_pending_count].deadline_frame = host_frame + 8;
          s_pending_count++;
        }
      }
      for (int p = 0; p < s_pending_count;) {
        int matched = 0;
        for (int slot = 0; slot < kActorCount && !matched; slot++) {
          const unsigned index = (unsigned)(kActorFirst + slot * 2);
          if (W16(kAddrActorId + index) &&
              (int)W16(kAddrActorSource + index) == s_pending[p].record)
            matched = 1;
        }
        if (matched) {
          s_pending[p] = s_pending[--s_pending_count];
        } else if (host_frame >= s_pending[p].deadline_frame) {
          char detail[64];
          snprintf(detail, sizeof detail, "\"record\":%d",
                   s_pending[p].record);
          Verdict(host_frame, kVerdictBookmarkNoActor, detail);
          s_pending[p] = s_pending[--s_pending_count];
        } else {
          p++;
        }
      }
    }
  }

  memcpy(s_prev_shadow, shadow, kOamBytes);
  s_prev_shadow_valid = 1;

  /* --- BG margin integrity (always, widescreen only) ----------------- */
  if (extra > 0) {
    WsShadowMarginStat stats[2];
    for (int layer = 0; layer < 2; layer++)
      WsShadowGetMarginStats(layer, &stats[layer]);
    if (s_prev_stats_valid) {
      for (int layer = 0; layer < 2; layer++) {
        const WsShadowMarginStat *now = &stats[layer];
        const WsShadowMarginStat *prev = &s_prev_stats[layer];
        const uint64_t raw =
            (now->westRawFallback + now->eastRawFallback) -
            (prev->westRawFallback + prev->eastRawFallback);
        if (raw && gameplay && host_frame - s_last_raw_report >= 60) {
          s_last_raw_report = host_frame;
          char detail[96];
          snprintf(detail, sizeof detail,
                   "\"layer\":%d,\"raw_serves\":%llu", layer,
                   (unsigned long long)raw);
          Verdict(host_frame, kVerdictTileNeverStreamed, detail);
        }
        const uint64_t stale = now->retrodictMismatch -
                               prev->retrodictMismatch;
        if (stale && host_frame - s_last_stale_report >= 60) {
          s_last_stale_report = host_frame;
          char detail[96];
          snprintf(detail, sizeof detail,
                   "\"layer\":%d,\"mismatches\":%llu", layer,
                   (unsigned long long)stale);
          Verdict(host_frame, kVerdictStaleMargin, detail);
        }
        const uint64_t oob =
            (now->outOfRangeRead + now->outOfRangeWrite) -
            (prev->outOfRangeRead + prev->outOfRangeWrite);
        if (oob) {
          char detail[96];
          snprintf(detail, sizeof detail,
                   "\"layer\":%d,\"events\":%llu", layer,
                   (unsigned long long)oob);
          Verdict(host_frame, kVerdictCacheWindow, detail);
        }
      }
    }
    memcpy(s_prev_stats, stats, sizeof s_prev_stats);
    s_prev_stats_valid = 1;
  }
}

long Dkc1InvariantMonitorTotal(void) {
  return s_total;
}

const char *Dkc1InvariantMonitorSummary(char *buffer, unsigned size) {
  snprintf(buffer, size,
           "no-oam:%ld stall:%ld xhigh:%ld sim-out:%ld book:%ld raw:%ld "
           "stale:%ld cache:%ld scan:%ld",
           s_counts[kVerdictActorNoOam], s_counts[kVerdictOamLag],
           s_counts[kVerdictXHighLoss], s_counts[kVerdictSimOutsideWide],
           s_counts[kVerdictBookmarkNoActor],
           s_counts[kVerdictTileNeverStreamed],
           s_counts[kVerdictStaleMargin], s_counts[kVerdictCacheWindow],
           s_counts[kVerdictScannerWindow]);
  return buffer;
}
