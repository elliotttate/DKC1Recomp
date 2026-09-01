/*
 * Widescreen level-wall presentation policy.
 *
 * Pure host arithmetic shared by the runtime and its model test. Nothing here
 * reads or writes cartridge state; the caller supplies the logical camera and
 * its published bounds, and receives only presentation decisions.
 *
 * A widescreen view centered on the cartridge camera asks for world pixels
 * beyond an authored level wall whenever the camera sits within one margin of
 * its bound. Three policies answer that:
 *
 *   reflect  Keep the view locked to the camera. The terrain layer continues
 *            past the wall with the authored columns mirrored about it, so
 *            the picture, sprites, and HUD always move together.
 *   bars     Keep the view locked to the camera and show nothing past the
 *            wall: that side's visible margin shrinks to the authored extent.
 *   shift    Slide the presented view inward by up to one margin so the wide
 *            frame stays inside the authored level. The picture then freezes
 *            for the first margin of camera travel away from a wall while
 *            everything drawn in screen space slides across it.
 *   glide    Like shift, the wide frame never leaves the authored level, but
 *            the inward slide is spread over eight margins of camera travel:
 *            the background scrolls at seven eighths of the camera speed
 *            until the view is centered again, so the relative motion is a
 *            gentle drift rather than a stop. The default.
 */
#ifndef DKC1_EDGE_POLICY_H
#define DKC1_EDGE_POLICY_H

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

typedef enum Dkc1EdgePolicy {
  kDkc1EdgeReflect = 0,
  kDkc1EdgeBars,
  kDkc1EdgeShift,
  kDkc1EdgeGlide,
  kDkc1EdgePolicyCount
} Dkc1EdgePolicy;

enum {
  /* Screen column beyond any rendered span: "leave this side alone". */
  kDkc1EdgeAxisOffLeft = -32768,
  kDkc1EdgeAxisOffRight = 32767,
  kDkc1EdgeNativeWidth = 256,
  /* reflect mirrors about a line one tile inside the wall rather than the
   * wall itself. DKC's map edge columns were hidden by CRT overscan and can
   * hold unfinished art (Jungle Hijinxs' first three BG1 columns are
   * transparent); a reflection exactly about the wall would double that
   * strip into the margin. The engine never rewrites native columns, so an
   * axis inside the view only changes which authored columns the margin
   * shows. */
  kDkc1EdgeWallInset = 8,
  /* glide spreads one margin of view shift over this many margins of
   * camera travel; the background moves at (n-1)/n of the camera speed. */
  kDkc1EdgeGlideSpan = 8
};

typedef struct Dkc1EdgePresentation {
  /* Presentation camera shift in pixels; nonzero only under shift. */
  int bias;
  /* Visible margin per side, 0..extra. Under bars a side past the wall is
   * clamped; the other policies always show the full margin. */
  int left, right;
  /* The locked view's margin runs past the authored level on some side. */
  bool beyond_extent;
  /* Reflection lines (screen columns one tile inside the authored west and
   * east walls) when the view runs past a wall under reflect; the off
   * sentinels otherwise. */
  int left_axis, right_axis;
} Dkc1EdgePresentation;

static inline const char *Dkc1EdgePolicyName(Dkc1EdgePolicy policy) {
  switch (policy) {
    case kDkc1EdgeReflect: return "reflect";
    case kDkc1EdgeBars: return "bars";
    case kDkc1EdgeShift: return "shift";
    case kDkc1EdgeGlide: return "glide";
    default: return "unknown";
  }
}

static inline bool Dkc1EdgePolicyFromName(const char *name,
                                          Dkc1EdgePolicy *policy) {
  if (!name || !policy)
    return false;
  if (!strcmp(name, "reflect") || !strcmp(name, "mirror") ||
      !strcmp(name, "0")) {
    *policy = kDkc1EdgeReflect;
    return true;
  }
  if (!strcmp(name, "bars") || !strcmp(name, "clamp") ||
      !strcmp(name, "1")) {
    *policy = kDkc1EdgeBars;
    return true;
  }
  if (!strcmp(name, "shift") || !strcmp(name, "bias") ||
      !strcmp(name, "2")) {
    *policy = kDkc1EdgeShift;
    return true;
  }
  if (!strcmp(name, "glide") || !strcmp(name, "3")) {
    *policy = kDkc1EdgeGlide;
    return true;
  }
  return false;
}

/* The published camera range must span the requested extension before any
 * policy trusts it; DKC publishes bounds a few frames after level entry. */
static inline bool Dkc1EdgeBoundsUsable(uint32_t lower, uint32_t upper,
                                        int extra) {
  return extra > 0 && upper >= lower &&
         upper - lower >= (uint32_t)extra * 2u;
}

static inline int Dkc1EdgeClamp(int64_t value, int64_t low, int64_t high) {
  if (value < low) value = low;
  if (value > high) value = high;
  return (int)value;
}

static inline void Dkc1EdgePresent(Dkc1EdgePolicy policy, uint32_t camera_x,
                                   uint32_t lower, uint32_t upper, int extra,
                                   Dkc1EdgePresentation *out) {
  out->bias = 0;
  out->left = extra;
  out->right = extra;
  out->beyond_extent = false;
  out->left_axis = kDkc1EdgeAxisOffLeft;
  out->right_axis = kDkc1EdgeAxisOffRight;
  if (!Dkc1EdgeBoundsUsable(lower, upper, extra))
    return;
  /* Screen columns of the authored walls for a view locked to the camera:
   * the west wall is at or left of column 0, the east wall at or right of
   * column 256. */
  const int64_t west = (int64_t)lower - (int64_t)camera_x;
  const int64_t east =
      (int64_t)upper + kDkc1EdgeNativeWidth - (int64_t)camera_x;
  const bool west_visible = west > -(int64_t)extra;
  const bool east_visible = east < kDkc1EdgeNativeWidth + (int64_t)extra;
  switch (policy) {
    case kDkc1EdgeShift: {
      int64_t target = camera_x;
      if (target < (int64_t)lower + extra) target = (int64_t)lower + extra;
      if (target > (int64_t)upper - extra) target = (int64_t)upper - extra;
      out->bias = (int)(target - (int64_t)camera_x);
      return;
    }
    case kDkc1EdgeGlide: {
      /* One margin of inward shift at the wall, released one pixel per
       * kDkc1EdgeGlideSpan pixels of camera travel, from each wall; then
       * pinned so the frame never leaves the level however short it is. */
      const int64_t span = (int64_t)extra * kDkc1EdgeGlideSpan;
      const int64_t west_travel = (int64_t)camera_x - (int64_t)lower;
      const int64_t east_travel = (int64_t)upper - (int64_t)camera_x;
      int64_t bias = 0;
      if (west_travel >= 0 && west_travel < span)
        bias += extra - west_travel / kDkc1EdgeGlideSpan;
      if (east_travel >= 0 && east_travel < span)
        bias -= extra - east_travel / kDkc1EdgeGlideSpan;
      const int64_t pin_low = (int64_t)lower + extra - (int64_t)camera_x;
      const int64_t pin_high = (int64_t)upper - extra - (int64_t)camera_x;
      if (bias < pin_low) bias = pin_low;
      if (bias > pin_high) bias = pin_high;
      out->bias = Dkc1EdgeClamp(bias, -extra, extra);
      return;
    }
    case kDkc1EdgeBars:
      out->left = Dkc1EdgeClamp(-west, 0, extra);
      out->right = Dkc1EdgeClamp(east - kDkc1EdgeNativeWidth, 0, extra);
      out->beyond_extent = out->left < extra || out->right < extra;
      return;
    case kDkc1EdgeReflect:
    default:
      out->beyond_extent = west_visible || east_visible;
      if (west_visible)
        out->left_axis = Dkc1EdgeClamp(west + kDkc1EdgeWallInset, -extra,
                                       kDkc1EdgeNativeWidth);
      if (east_visible)
        out->right_axis = Dkc1EdgeClamp(east - kDkc1EdgeWallInset, 0,
                                        kDkc1EdgeNativeWidth + extra);
      return;
  }
}

#endif
