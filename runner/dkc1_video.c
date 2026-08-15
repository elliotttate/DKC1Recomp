#include "dkc1_video.h"

bool g_ws_active;
int g_ws_extra;
static bool s_terrain_ready;

void Dkc1VideoSetWidescreen(bool enabled) {
  if (g_ws_active != enabled)
    s_terrain_ready = false;
  g_ws_active = enabled;
  g_ws_extra = enabled ? kDkc1VideoWidescreenExtra : 0;
}

bool Dkc1VideoIsWidescreen(void) {
  return g_ws_active;
}

void Dkc1VideoSetTerrainReady(bool ready) {
  s_terrain_ready = g_ws_active && ready;
}

bool Dkc1VideoTerrainReady(void) {
  return g_ws_active && s_terrain_ready;
}

int Dkc1VideoWidth(void) {
  return kDkc1VideoNativeWidth + 2 * g_ws_extra;
}

int Dkc1VideoExtra(void) {
  return g_ws_extra;
}

size_t Dkc1VideoPixelCount(void) {
  return (size_t)Dkc1VideoWidth() * kDkc1VideoHeight;
}
