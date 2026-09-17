#ifndef DKC1_DESKTOP_GRAPHICS_H
#define DKC1_DESKTOP_GRAPHICS_H
#include "desktop_crt.h"

enum { kDkc1UpscalerNearest, kDkc1UpscalerBilinear,
       kDkc1UpscalerReconstruct, kDkc1UpscalerSharpBilinear };
typedef struct Dkc1GraphicsSettings {
  int display, upscaler, screen;
  int reconstruct_mode, strength, softness, shading;
  Dkc1CrtSettings crt;
  int window_scale, fullscreen, aspect, edge;
  int audio_enabled, volume, state_slot;
  int hd_polish, hd_finish;
} Dkc1GraphicsSettings;
void Dkc1GraphicsDefault(Dkc1GraphicsSettings *s);
void Dkc1GraphicsClamp(Dkc1GraphicsSettings *s);
#endif
