#include "desktop_crt.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

static int failures;

static void Fail(const char *message) {
  fprintf(stderr, "FAIL: %s\n", message);
  failures++;
}

/* The beam is a normalised Gaussian per line, so the average of the
 * periodic sum over one line pitch must equal the flat field's brightness
 * for every width, and a white field must never rise far above 1 before
 * the knee. */
static void CheckBeamConservation(void) {
  static const float widths[] = {0.15f, 0.18f, 0.26f, 0.36f, 0.50f, 0.60f};
  static const float levels[] = {0.05f, 0.30f, 1.00f};
  enum { kLines = 64, kSamples = 2000 };
  float lines[kLines];
  for (size_t w = 0; w < sizeof widths / sizeof widths[0]; w++) {
    const float sigma_dark = widths[w];
    const float sigma_bright = sigma_dark > 0.5f ? sigma_dark : 0.5f;
    for (size_t l = 0; l < sizeof levels / sizeof levels[0]; l++) {
      for (int i = 0; i < kLines; i++) lines[i] = levels[l];
      double sum = 0.0;
      float peak = 0.0f;
      for (int s = 0; s < kSamples; s++) {
        const float y = 20.0f + (float)s / (float)kSamples;
        const float v = Dkc1CrtBeamSample(lines, kLines, y, sigma_dark,
                                          sigma_bright);
        sum += v;
        if (v > peak) peak = v;
      }
      const double mean = sum / kSamples;
      if (fabs(mean - levels[l]) > 1e-3) {
        char message[160];
        (void)snprintf(message, sizeof message,
                       "beam mean %.4f for level %.2f sigma %.2f",
                       mean, levels[l], sigma_dark);
        Fail(message);
      }
      if (levels[l] >= 1.0f && peak > 1.02f) {
        char message[160];
        (void)snprintf(message, sizeof message,
                       "white peak %.3f exceeds 1.02 at sigma %.2f", peak,
                       sigma_dark);
        Fail(message);
      }
    }
  }
  /* A dark line between bright ones stays dark: the neighbours' tails do
   * not fill a black line at the default widths. */
  for (int i = 0; i < kLines; i++) lines[i] = (i == 30) ? 0.0f : 1.0f;
  const float middle = Dkc1CrtBeamSample(lines, kLines, 30.5f, 0.28f, 0.5f);
  if (middle > 0.35f) Fail("black line between white lines is not dark");
  /* Lines fade with brightness: a black field has visible ripple, a white
   * field almost none. */
  for (int i = 0; i < kLines; i++) lines[i] = 0.10f;
  const float dark_peak = Dkc1CrtBeamSample(lines, kLines, 30.5f, 0.28f, 0.5f);
  const float dark_gap = Dkc1CrtBeamSample(lines, kLines, 31.0f, 0.28f, 0.5f);
  for (int i = 0; i < kLines; i++) lines[i] = 1.0f;
  const float white_peak = Dkc1CrtBeamSample(lines, kLines, 30.5f, 0.28f, 0.5f);
  const float white_gap = Dkc1CrtBeamSample(lines, kLines, 31.0f, 0.28f, 0.5f);
  if (dark_gap / dark_peak > 0.7f) Fail("dim lines do not show scanlines");
  if (white_gap / white_peak < 0.9f) Fail("white lines still show scanlines");
}

static void CheckMaskAndKnee(void) {
  if (fabsf(Dkc1CrtMaskGain(kDkc1CrtMaskNone, 1.0f) - 1.0f) > 1e-6f)
    Fail("no mask has gain");
  /* Grille at 0.3: mean transmission 0.8, gain 1.25. */
  if (fabsf(Dkc1CrtMaskGain(kDkc1CrtMaskGrilleFine, 0.3f) - 1.25f) > 1e-5f)
    Fail("grille gain at 0.3");
  /* The gain restores the mean of a white field over one triad. */
  const float m = 0.3f;
  const float gain = Dkc1CrtMaskGain(kDkc1CrtMaskGrilleFine, m);
  const float mean = gain * (1.0f + 2.0f * (1.0f - m)) / 3.0f;
  if (fabsf(mean - 1.0f) > 1e-5f) Fail("grille gain does not conserve");
  if (Dkc1CrtMaskGain(kDkc1CrtMaskSlot, 0.3f) <= gain)
    Fail("slot mask gain is not above the grille gain");
  /* The knee passes values below it, bends the rest under 1. */
  if (Dkc1CrtSoftKnee(0.5f, 0.9f) != 0.5f) Fail("knee changed a low value");
  if (Dkc1CrtSoftKnee(1.27f, 0.9f) > 1.0f) Fail("knee exceeds 1");
  if (Dkc1CrtSoftKnee(1.27f, 0.9f) < 0.99f) Fail("knee crushes a white stripe");
  if (Dkc1CrtSoftKnee(0.95f, 0.9f) <= 0.9f ||
      Dkc1CrtSoftKnee(0.95f, 0.9f) >= 0.95f)
    Fail("knee is not monotone just above the knee");
}

static void CheckDerive(void) {
  Dkc1CrtSettings settings;
  Dkc1CrtSettingsDefault(&settings);
  Dkc1CrtFrameParams full;
  /* The 16-inch panel at 16:9: 3456x1940 for 342x224. */
  if (!Dkc1CrtDerive(&settings, 3456, 1940, 342, 224, &full))
    Fail("derive rejected the fullscreen viewport");
  if (full.beam_fade != 1.0f) Fail("beam not fully on at fullscreen");
  if (full.mask != kDkc1CrtMaskGrilleFine || full.mask_pitch != 3.0f)
    Fail("default mask is not the fine grille at fullscreen");
  if (fabsf(full.mask_strength - 0.3f) > 1e-6f)
    Fail("default mask strength is not 0.3 at fullscreen");
  if (fabsf(full.mask_gain - 1.25f) > 1e-5f) Fail("fullscreen mask gain");
  if (fabsf(full.sigma_dark - (0.50f - 0.32f * 0.55f)) > 1e-5f)
    Fail("default sigma_dark");
  if (full.sigma_bright < full.sigma_dark) Fail("sigma_bright below dark");
  if (fabsf(full.sigma_h - (0.60f - 0.35f * 0.80f)) > 1e-5f)
    Fail("default sigma_h");
  if (fabsf(full.glow - 0.08f) > 1e-6f || fabsf(full.halation - 0.05f) > 1e-6f)
    Fail("default glow and halation");
  if (fabsf(full.curvature_x - 0.015f) > 1e-6f || full.corner_radius <= 0.0f ||
      full.vignette <= 0.0f)
    Fail("default curvature geometry");
  if (fabsf(full.scale_y - 1940.0f / 224.0f) > 1e-4f) Fail("scale_y");

  /* A 1x window (256x224 -> 298x224 at 7:6): nothing can be drawn. */
  Dkc1CrtFrameParams tiny;
  if (!Dkc1CrtDerive(&settings, 298, 224, 256, 224, &tiny))
    Fail("derive rejected the 1x viewport");
  if (tiny.beam_fade != 0.0f) Fail("beam did not fade out at 1x");
  if (tiny.mask != kDkc1CrtMaskNone || tiny.mask_gain != 1.0f)
    Fail("mask did not fade out at 1x");
  /* A 4x window (1194x896): 4 px per line is inside the beam fade and
   * 4.7 px per column is below the mask fade. */
  Dkc1CrtFrameParams small;
  if (!Dkc1CrtDerive(&settings, 1194, 896, 256, 224, &small))
    Fail("derive rejected the 4x viewport");
  if (small.beam_fade <= 0.0f || small.beam_fade >= 1.0f)
    Fail("beam fade is not partial at 4 px per line");
  if (small.mask != kDkc1CrtMaskNone) Fail("mask still on at 4.7 px per pixel");

  /* Studio preset: flat geometry. */
  Dkc1CrtSettings studio;
  if (!Dkc1CrtSettingsApplyPreset(&studio, kDkc1CrtPresetStudio))
    Fail("studio preset");
  Dkc1CrtFrameParams flat;
  if (!Dkc1CrtDerive(&studio, 3456, 1940, 342, 224, &flat))
    Fail("derive rejected the studio preset");
  if (flat.curvature_x != 0.0f || flat.corner_radius != 0.0f ||
      flat.vignette != 0.0f)
    Fail("studio preset has geometry");
  if (Dkc1CrtSettingsApplyPreset(&studio, kDkc1CrtPresetCustom))
    Fail("custom is not a preset");
  if (Dkc1CrtDerive(&settings, 0, 1940, 342, 224, &flat) ||
      Dkc1CrtDerive(&settings, 3456, 1940, 342, 0, &flat) ||
      Dkc1CrtDerive(NULL, 3456, 1940, 342, 224, &flat))
    Fail("invalid derive input accepted");

  /* Out-of-range settings are clamped, not rejected. */
  Dkc1CrtSettings wild = {99, 250, -5, 42, 400, -1, 1000, 77};
  Dkc1CrtSettingsClamp(&wild);
  if (wild.preset != kDkc1CrtPresetCustom || wild.scanlines != 100 ||
      wild.sharpness != 0 || wild.mask != kDkc1CrtMaskSlot ||
      wild.mask_strength != 100 || wild.glow != 0 || wild.halation != 100 ||
      wild.curvature != 77)
    Fail("settings clamp");
}

static void CheckNames(void) {
  int value = -1;
  if (strcmp(Dkc1CrtDisplayName(kDkc1DisplayCrt), "crt") != 0 ||
      strcmp(Dkc1CrtDisplayName(kDkc1DisplayFlat), "flat") != 0 ||
      !Dkc1CrtDisplayFromName("crt", &value) || value != kDkc1DisplayCrt ||
      !Dkc1CrtDisplayFromName("flat", &value) || value != kDkc1DisplayFlat ||
      !Dkc1CrtDisplayFromName("1", &value) || value != kDkc1DisplayCrt ||
      Dkc1CrtDisplayFromName("plasma", &value) ||
      Dkc1CrtDisplayFromName(NULL, &value))
    Fail("display names");
  for (int i = 0; i < kDkc1CrtPresetCount; i++) {
    if (!Dkc1CrtPresetFromName(Dkc1CrtPresetName(i), &value) || value != i)
      Fail("preset name round trip");
  }
  if (!Dkc1CrtPresetFromName("2", &value) || value != kDkc1CrtPresetSoft ||
      Dkc1CrtPresetFromName("9", &value) || Dkc1CrtPresetFromName("", &value))
    Fail("preset numeric names");
  for (int i = 0; i < kDkc1CrtMaskCount; i++) {
    if (!Dkc1CrtMaskFromName(Dkc1CrtMaskName(i), &value) || value != i)
      Fail("mask name round trip");
  }
  if (Dkc1CrtMaskFromName("shadow", &value)) Fail("unknown mask accepted");
}

int main(void) {
  CheckBeamConservation();
  CheckMaskAndKnee();
  CheckDerive();
  CheckNames();
  if (failures) {
    fprintf(stderr, "%d failure(s)\n", failures);
    return 1;
  }
  printf("desktop_crt: ok\n");
  return 0;
}
