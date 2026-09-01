/* Model test for the level-wall presentation policy (runner/dkc1_edge_policy.h).
 * No ROM, engine, or generated code is needed; tests/test_edge_policy_model.py
 * compiles and runs it. */
#include <stdio.h>

#include "../runner/dkc1_edge_policy.h"

static int failures;

static void check(int condition, const char *what) {
  if (!condition) {
    fprintf(stderr, "FAIL: %s\n", what);
    failures++;
  }
}

static Dkc1EdgePresentation present(Dkc1EdgePolicy policy, uint32_t camera,
                                    uint32_t lower, uint32_t upper) {
  Dkc1EdgePresentation out;
  Dkc1EdgePresent(policy, camera, lower, upper, 43, &out);
  return out;
}

int main(void) {
  /* Jungle Hijinxs entry: camera at its lower bound 0, upper bound 5120. */
  Dkc1EdgePresentation e = present(kDkc1EdgeShift, 0, 0, 5120);
  check(e.bias == 43 && e.left == 43 && e.right == 43 && !e.beyond_extent,
        "shift at the west wall slides the view one margin inward");
  check(e.left_axis == kDkc1EdgeAxisOffLeft &&
        e.right_axis == kDkc1EdgeAxisOffRight,
        "shift never sets a mirror axis");

  e = present(kDkc1EdgeReflect, 0, 0, 5120);
  check(e.bias == 0 && e.left == 43 && e.right == 43,
        "reflect keeps the view locked with full margins");
  check(e.beyond_extent && e.left_axis == kDkc1EdgeWallInset &&
        e.right_axis == kDkc1EdgeAxisOffRight,
        "reflect at the west wall mirrors about one tile inside it only");

  e = present(kDkc1EdgeBars, 0, 0, 5120);
  check(e.bias == 0 && e.left == 0 && e.right == 43 && e.beyond_extent,
        "bars at the west wall clamps only the left margin");

  /* Twenty pixels into the level: the wall sits inside the left margin. */
  e = present(kDkc1EdgeReflect, 20, 0, 5120);
  check(e.beyond_extent && e.left_axis == -20 + kDkc1EdgeWallInset,
        "reflect mirrors one tile inside the wall's margin column");
  e = present(kDkc1EdgeBars, 20, 0, 5120);
  check(e.left == 20 && e.right == 43, "bars shows the twenty authored pixels");
  e = present(kDkc1EdgeShift, 20, 0, 5120);
  check(e.bias == 23, "shift bias shrinks as the camera leaves the wall");

  /* glide: pinned at the wall like shift, then released one pixel per eight
   * pixels of camera travel, so the view is centered 344 pixels in. */
  e = present(kDkc1EdgeGlide, 0, 0, 5120);
  check(e.bias == 43 && e.left == 43 && e.right == 43 && !e.beyond_extent &&
        e.left_axis == kDkc1EdgeAxisOffLeft,
        "glide at the west wall equals shift at the wall");
  e = present(kDkc1EdgeGlide, 8, 0, 5120);
  check(e.bias == 42, "glide releases one pixel after eight of travel");
  e = present(kDkc1EdgeGlide, 172, 0, 5120);
  check(e.bias == 22, "glide is half released halfway through its span");
  e = present(kDkc1EdgeGlide, 343, 0, 5120);
  check(e.bias == 1, "glide keeps one pixel until the span ends");
  e = present(kDkc1EdgeGlide, 344, 0, 5120);
  check(e.bias == 0 && !e.beyond_extent, "glide is centered after eight margins");
  e = present(kDkc1EdgeGlide, 5120 - 8, 0, 5120);
  check(e.bias == -42, "glide mirrors the release at the east wall");
  e = present(kDkc1EdgeGlide, 5120, 0, 5120);
  check(e.bias == -43, "glide at the east wall equals shift at the wall");
  /* A level shorter than two spans: the ramps overlap and the pins hold. */
  e = present(kDkc1EdgeGlide, 0, 0, 200);
  check(e.bias == 43, "glide never shows past the west wall of a short level");
  e = present(kDkc1EdgeGlide, 200, 0, 200);
  check(e.bias == -43, "glide never shows past the east wall of a short level");
  e = present(kDkc1EdgeGlide, 100, 0, 200);
  check(e.bias >= 43 - 100 && e.bias <= 200 - 43 - 100,
        "glide stays inside a short level between its walls");

  /* Mid-level: every policy is the plain locked view. */
  for (int policy = 0; policy < kDkc1EdgePolicyCount; policy++) {
    e = present((Dkc1EdgePolicy)policy, 2000, 0, 5120);
    check(e.bias == 0 && e.left == 43 && e.right == 43 && !e.beyond_extent &&
          e.left_axis == kDkc1EdgeAxisOffLeft &&
          e.right_axis == kDkc1EdgeAxisOffRight,
          "mid-level frames are identical under every policy");
  }

  /* East wall: camera at the upper bound. */
  e = present(kDkc1EdgeReflect, 5120, 0, 5120);
  check(e.beyond_extent && e.left_axis == kDkc1EdgeAxisOffLeft &&
        e.right_axis == 256 - kDkc1EdgeWallInset,
        "reflect at the east wall mirrors about one tile inside it");
  e = present(kDkc1EdgeBars, 5100, 0, 5120);
  check(e.left == 43 && e.right == 20, "bars clamps the right margin");
  e = present(kDkc1EdgeShift, 5120, 0, 5120);
  check(e.bias == -43, "shift at the east wall slides the view outward");

  /* Bounds narrower than the extension are not trusted by any policy. */
  e = present(kDkc1EdgeReflect, 10, 0, 80);
  check(e.bias == 0 && e.left == 43 && e.right == 43 && !e.beyond_extent,
        "narrow bounds fail closed to the centered view");
  e = present(kDkc1EdgeShift, 10, 0, 80);
  check(e.bias == 0, "narrow bounds give no shift");

  /* Vocabulary. */
  Dkc1EdgePolicy p;
  check(Dkc1EdgePolicyFromName("reflect", &p) && p == kDkc1EdgeReflect &&
        Dkc1EdgePolicyFromName("mirror", &p) && p == kDkc1EdgeReflect &&
        Dkc1EdgePolicyFromName("0", &p) && p == kDkc1EdgeReflect,
        "reflect names");
  check(Dkc1EdgePolicyFromName("bars", &p) && p == kDkc1EdgeBars &&
        Dkc1EdgePolicyFromName("clamp", &p) && p == kDkc1EdgeBars &&
        Dkc1EdgePolicyFromName("1", &p) && p == kDkc1EdgeBars,
        "bars names");
  check(Dkc1EdgePolicyFromName("shift", &p) && p == kDkc1EdgeShift &&
        Dkc1EdgePolicyFromName("bias", &p) && p == kDkc1EdgeShift &&
        Dkc1EdgePolicyFromName("2", &p) && p == kDkc1EdgeShift,
        "shift names");
  check(Dkc1EdgePolicyFromName("glide", &p) && p == kDkc1EdgeGlide &&
        Dkc1EdgePolicyFromName("3", &p) && p == kDkc1EdgeGlide &&
        !strcmp(Dkc1EdgePolicyName(kDkc1EdgeGlide), "glide"),
        "glide names");
  check(!Dkc1EdgePolicyFromName("wrap", &p) &&
        !Dkc1EdgePolicyFromName("", &p) && !Dkc1EdgePolicyFromName(NULL, &p),
        "unknown names are rejected");
  check(!strcmp(Dkc1EdgePolicyName(kDkc1EdgeReflect), "reflect") &&
        !strcmp(Dkc1EdgePolicyName(kDkc1EdgeBars), "bars") &&
        !strcmp(Dkc1EdgePolicyName(kDkc1EdgeShift), "shift"),
        "policy names round-trip");

  if (failures) {
    fprintf(stderr, "edge_policy_model: %d failure(s)\n", failures);
    return 1;
  }
  puts("edge_policy_model: PASS");
  return 0;
}
