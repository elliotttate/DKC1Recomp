#include "desktop_crt.h"

#include <math.h>
#include <string.h>

/* Beam widths in line units. A white line is wide enough that its periodic
 * sum ripples by only a few percent (its lines vanish); the darkest line
 * width is the Scanlines slider. Above the knee the compose pass bends the
 * light gently toward 1 instead of clipping. */
enum {
  kSigmaDarkNonePercent = 50,  /* sigma_dark 0.50 at slider 0 */
  kSigmaDarkThinPercent = 18,  /* sigma_dark 0.18 at slider 100 */
};
static const float kSigmaBright = 0.50f;
static const float kSigmaHorizontalSoft = 0.60f;
static const float kSigmaHorizontalSharp = 0.25f;
static const float kKnee = 0.90f;
static const float kGlowMaximum = 0.20f;
static const float kHalationMaximum = 0.20f;
static const float kCurvatureMaximum = 0.03f;
static const float kCornerRadius = 0.015f;
static const float kVignette = 0.03f;
/* Below these output scales the beam and the mask cannot be represented
 * and fade toward the flat image (pixels per line, pixels per column). */
static const float kBeamFadeStart = 3.0f;
static const float kBeamFadeFull = 5.0f;
static const float kMaskFadeStart = 5.0f;
static const float kMaskFadeFull = 7.0f;

typedef struct CrtPreset {
  const char *name;
  Dkc1CrtSettings values;
} CrtPreset;

static const CrtPreset kPresets[kDkc1CrtPresetCount] = {
    {"living-room",
     {kDkc1CrtPresetLivingRoom, 55, 80, kDkc1CrtMaskGrilleFine, 30, 40, 25,
      50}},
    {"studio",
     {kDkc1CrtPresetStudio, 85, 95, kDkc1CrtMaskGrilleFine, 40, 25, 15, 0}},
    {"soft",
     {kDkc1CrtPresetSoft, 35, 50, kDkc1CrtMaskGrilleCoarse, 20, 60, 40, 67}},
    {"custom", {kDkc1CrtPresetCustom, 55, 80, kDkc1CrtMaskGrilleFine, 30, 40,
                25, 50}},
};

static int ClampInt(int value, int minimum, int maximum) {
  if (value < minimum) return minimum;
  if (value > maximum) return maximum;
  return value;
}

static float Smoothstep(float edge0, float edge1, float x) {
  if (edge1 <= edge0) return x >= edge1 ? 1.0f : 0.0f;
  float t = (x - edge0) / (edge1 - edge0);
  if (t < 0.0f) t = 0.0f;
  if (t > 1.0f) t = 1.0f;
  return t * t * (3.0f - 2.0f * t);
}

void Dkc1CrtSettingsDefault(Dkc1CrtSettings *settings) {
  if (!settings) return;
  *settings = kPresets[kDkc1CrtPresetLivingRoom].values;
}

bool Dkc1CrtSettingsApplyPreset(Dkc1CrtSettings *settings, int preset) {
  if (!settings || preset < 0 || preset >= kDkc1CrtPresetCustom)
    return false;
  *settings = kPresets[preset].values;
  return true;
}

void Dkc1CrtSettingsClamp(Dkc1CrtSettings *settings) {
  if (!settings) return;
  settings->preset = ClampInt(settings->preset, 0, kDkc1CrtPresetCount - 1);
  settings->scanlines = ClampInt(settings->scanlines, 0, 100);
  settings->sharpness = ClampInt(settings->sharpness, 0, 100);
  settings->mask = ClampInt(settings->mask, 0, kDkc1CrtMaskCount - 1);
  settings->mask_strength = ClampInt(settings->mask_strength, 0, 100);
  settings->glow = ClampInt(settings->glow, 0, 100);
  settings->halation = ClampInt(settings->halation, 0, 100);
  settings->curvature = ClampInt(settings->curvature, 0, 100);
}

static float MaskPitch(int mask) {
  switch (mask) {
    case kDkc1CrtMaskGrilleFine: return 3.0f;
    case kDkc1CrtMaskGrilleCoarse: return 6.0f;
    case kDkc1CrtMaskSlot: return 6.0f;
    default: return 0.0f;
  }
}

float Dkc1CrtMaskGain(int mask, float strength) {
  if (strength < 0.0f) strength = 0.0f;
  if (strength > 1.0f) strength = 1.0f;
  float transmission = 1.0f;
  switch (mask) {
    case kDkc1CrtMaskGrilleFine:
    case kDkc1CrtMaskGrilleCoarse:
      /* One stripe in three passes a channel fully, two pass 1 - m. */
      transmission = 1.0f - 2.0f * strength / 3.0f;
      break;
    case kDkc1CrtMaskSlot:
      /* The grille transmission, and one row in six is a slot gap. */
      transmission = (1.0f - 2.0f * strength / 3.0f) *
                     (1.0f - strength / 6.0f);
      break;
    default:
      return 1.0f;
  }
  if (transmission < 0.05f) transmission = 0.05f;
  return 1.0f / transmission;
}

float Dkc1CrtSoftKnee(float value, float knee) {
  if (value <= knee) return value;
  const float span = 1.0f - knee;
  if (span <= 0.0f) return value < 1.0f ? value : 1.0f;
  const float t = (value - knee) / span;
  return knee + span * tanhf(t);
}

bool Dkc1CrtDerive(const Dkc1CrtSettings *input, int viewport_width,
                   int viewport_height, int source_width,
                   int source_height, Dkc1CrtFrameParams *params) {
  if (!input || !params || viewport_width <= 0 || viewport_height <= 0 ||
      source_width <= 0 || source_height <= 0)
    return false;
  Dkc1CrtSettings settings = *input;
  Dkc1CrtSettingsClamp(&settings);
  memset(params, 0, sizeof *params);
  params->scale_x = (float)viewport_width / (float)source_width;
  params->scale_y = (float)viewport_height / (float)source_height;

  const float dark_none = (float)kSigmaDarkNonePercent / 100.0f;
  const float dark_thin = (float)kSigmaDarkThinPercent / 100.0f;
  params->sigma_dark =
      dark_none + (dark_thin - dark_none) * (float)settings.scanlines / 100.0f;
  params->sigma_bright =
      kSigmaBright > params->sigma_dark ? kSigmaBright : params->sigma_dark;
  params->sigma_h = kSigmaHorizontalSoft +
                    (kSigmaHorizontalSharp - kSigmaHorizontalSoft) *
                        (float)settings.sharpness / 100.0f;
  params->beam_fade = Smoothstep(kBeamFadeStart, kBeamFadeFull,
                                 params->scale_y);
  params->knee = kKnee;

  const float mask_fade =
      Smoothstep(kMaskFadeStart, kMaskFadeFull, params->scale_x);
  params->mask = settings.mask;
  params->mask_pitch = MaskPitch(settings.mask);
  params->mask_strength =
      (float)settings.mask_strength / 100.0f * mask_fade;
  if (settings.mask == kDkc1CrtMaskNone || params->mask_strength < 0.005f) {
    params->mask = kDkc1CrtMaskNone;
    params->mask_pitch = 0.0f;
    params->mask_strength = 0.0f;
  }
  params->mask_gain = Dkc1CrtMaskGain(params->mask, params->mask_strength);

  params->glow = kGlowMaximum * (float)settings.glow / 100.0f;
  params->halation = kHalationMaximum * (float)settings.halation / 100.0f;
  const float curvature =
      kCurvatureMaximum * (float)settings.curvature / 100.0f;
  params->curvature_x = curvature;
  params->curvature_y = 0.6f * curvature;
  params->corner_radius = curvature > 0.0f ? kCornerRadius : 0.0f;
  params->vignette = curvature > 0.0f ? kVignette : 0.0f;
  return true;
}

const char *Dkc1CrtDisplayName(int display) {
  return display == kDkc1DisplayCrt ? "crt" : "flat";
}

bool Dkc1CrtDisplayFromName(const char *name, int *display) {
  if (!name || !display) return false;
  if (strcmp(name, "flat") == 0 || strcmp(name, "0") == 0) {
    *display = kDkc1DisplayFlat;
    return true;
  }
  if (strcmp(name, "crt") == 0 || strcmp(name, "tube") == 0 ||
      strcmp(name, "1") == 0) {
    *display = kDkc1DisplayCrt;
    return true;
  }
  return false;
}

const char *Dkc1CrtPresetName(int preset) {
  if (preset < 0 || preset >= kDkc1CrtPresetCount) return "custom";
  return kPresets[preset].name;
}

bool Dkc1CrtPresetFromName(const char *name, int *preset) {
  if (!name || !preset) return false;
  for (int i = 0; i < kDkc1CrtPresetCount; i++) {
    if (strcmp(name, kPresets[i].name) == 0) {
      *preset = i;
      return true;
    }
  }
  if (strlen(name) == 1 && name[0] >= '0' &&
      name[0] < '0' + kDkc1CrtPresetCount) {
    *preset = name[0] - '0';
    return true;
  }
  return false;
}

static const char *const kMaskNames[kDkc1CrtMaskCount] = {
    "none", "grille", "grille-coarse", "slot"};

const char *Dkc1CrtMaskName(int mask) {
  if (mask < 0 || mask >= kDkc1CrtMaskCount) return "none";
  return kMaskNames[mask];
}

bool Dkc1CrtMaskFromName(const char *name, int *mask) {
  if (!name || !mask) return false;
  for (int i = 0; i < kDkc1CrtMaskCount; i++) {
    if (strcmp(name, kMaskNames[i]) == 0) {
      *mask = i;
      return true;
    }
  }
  if (strlen(name) == 1 && name[0] >= '0' &&
      name[0] < '0' + kDkc1CrtMaskCount) {
    *mask = name[0] - '0';
    return true;
  }
  return false;
}

float Dkc1CrtBeamSample(const float *lines, int count, float y,
                        float sigma_dark, float sigma_bright) {
  if (!lines || count <= 0) return 0.0f;
  const int first = (int)floorf(y - 0.5f) - 1;
  float acc = 0.0f;
  for (int k = 0; k < 4; k++) {
    const int i = first + k;
    if (i < 0 || i >= count) continue;
    float level = lines[i];
    if (level < 0.0f) level = 0.0f;
    if (level > 1.0f) level = 1.0f;
    const float sigma = sigma_dark + (sigma_bright - sigma_dark) * sqrtf(level);
    const float d = y - ((float)i + 0.5f);
    acc += level * expf(-d * d / (2.0f * sigma * sigma)) /
           (sigma * 2.5066283f);
  }
  return acc;
}
