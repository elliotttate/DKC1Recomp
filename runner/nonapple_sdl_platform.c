#include "macos_file_picker.h"
#include "macos_metal_presenter.h"

#include <stddef.h>

/* The SDL frontend is shared with macOS, where these entry points provide
 * native menus, file pickers, preferences, and display-linked Metal output.
 * Linux deliberately keeps the command-line/environment configuration and
 * ordinary SDL renderer path, so its platform adapters are inert. */

char *Dkc1MacChooseRom(void) { return NULL; }
char *Dkc1MacChooseBabyKongRom(void) { return NULL; }
char *Dkc1MacSavedBabyKongRom(void) { return NULL; }
void Dkc1MacSetBabyKongRom(const char *path) { (void)path; }
int Dkc1MacSavedBabyKongEnabled(void) { return 0; }
void Dkc1MacSetBabyKongEnabled(int enabled) { (void)enabled; }

char *Dkc1MacChooseMsu1(void) { return NULL; }
char *Dkc1MacSavedMsu1(void) { return NULL; }
void Dkc1MacClearMsu1(void) {}

Dkc1MacFullscreenScaling Dkc1MacSavedFullscreenScaling(void) {
  return kDkc1MacFullscreenSharpBilinear;
}

void Dkc1MacSetFullscreenScaling(Dkc1MacFullscreenScaling scaling) {
  (void)scaling;
}

Dkc1EdgePolicy Dkc1MacSavedWidescreenEdge(void) {
  return kDkc1EdgeGlide;
}

void Dkc1MacSetWidescreenEdge(Dkc1EdgePolicy policy) { (void)policy; }

void Dkc1MacInstallMenu(void) {}

void Dkc1MacUpdateMenuState(int paused, int fullscreen,
                            Dkc1MacFullscreenScaling fullscreen_scaling,
                            Dkc1VideoAspect aspect, Dkc1EdgePolicy edge,
                            unsigned char layer_mask, int provenance,
                            int replacement_music, int baby_kong_enabled,
                            int baby_kong_ready) {
  (void)paused;
  (void)fullscreen;
  (void)fullscreen_scaling;
  (void)aspect;
  (void)edge;
  (void)layer_mask;
  (void)provenance;
  (void)replacement_music;
  (void)baby_kong_enabled;
  (void)baby_kong_ready;
}

int Dkc1MacDisplayLinkStart(void *native_window, double preferred_fps) {
  (void)native_window;
  (void)preferred_fps;
  return 0;
}

int Dkc1MacDisplayLinkWait(unsigned long long after_callback_number,
                           double timeout_seconds, double *timestamp,
                           double *target_timestamp, double *duration,
                           unsigned long long *callback_number) {
  (void)after_callback_number;
  (void)timeout_seconds;
  (void)timestamp;
  (void)target_timestamp;
  (void)duration;
  (void)callback_number;
  return 0;
}

void Dkc1MacDisplayLinkStop(void) {}

int Dkc1MacMetalPresenterStart(void *native_window, double preferred_hz,
                               Dkc1MacFullscreenScaling scaling,
                               int fullscreen) {
  (void)native_window;
  (void)preferred_hz;
  (void)scaling;
  (void)fullscreen;
  return 0;
}

void Dkc1MacMetalPresenterQueueFrame(
    const uint32_t *pixels, int width, int height, int presentation_width,
    const Dkc1MacPresentationFrameInfo *info) {
  (void)pixels;
  (void)width;
  (void)height;
  (void)presentation_width;
  (void)info;
}

void Dkc1MacMetalPresenterSetGeometry(int presentation_width,
                                      int fullscreen) {
  (void)presentation_width;
  (void)fullscreen;
}

void Dkc1MacMetalPresenterSetScaling(Dkc1MacFullscreenScaling scaling) {
  (void)scaling;
}

void Dkc1MacMetalPresenterSetActive(int active) { (void)active; }
void Dkc1MacMetalPresenterStop(void) {}
