#ifndef DKC1_LINUX_PLATFORM_H
#define DKC1_LINUX_PLATFORM_H
/* Native Linux implementation of the SDL host's platform UI contract.
 *
 * Mac-prefixed entry points retain ABI compatibility with the untouched
 * Mac/Windows UI contract declared in macos_file_picker.h,
 * macos_pause_menu.h, macos_controls.h and macos_metal_presenter.h --
 * this file does not redeclare them, it only adds the small set of
 * Linux-only helpers sdl_host.c's __linux__ branch calls directly.
 *
 * Settings live under $XDG_CONFIG_HOME (via SDL_GetPrefPath), never in the
 * ROM or the repo.
 */
#include <SDL.h>
#include "desktop_graphics.h"

/* mach_absolute_time()/mach_wait_until()/mach_timebase_info() compatibility
 * shim for the frame pacer in sdl_host.c (FramePacerNow/WaitUntil/Init),
 * which otherwise only has a macOS implementation. Ticks here are raw
 * nanoseconds, so a 1:1 timebase (numer=denom=1) is exact -- no scaling
 * needed, unlike real Mach ticks. */
#include <time.h>
#include <stdint.h>

typedef struct mach_timebase_info_data_t {
  uint32_t numer;
  uint32_t denom;
} mach_timebase_info_data_t;

static inline uint64_t mach_absolute_time(void) {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

static inline int mach_timebase_info(mach_timebase_info_data_t *info) {
  info->numer = 1;
  info->denom = 1;
  return 0;
}

static inline int mach_wait_until(uint64_t deadline_ns) {
  uint64_t now = mach_absolute_time();
  if (deadline_ns <= now) return 0;
  uint64_t remaining = deadline_ns - now;
  struct timespec ts;
  ts.tv_sec = (time_t)(remaining / 1000000000ull);
  ts.tv_nsec = (long)(remaining % 1000000000ull);
  return clock_nanosleep(CLOCK_MONOTONIC, 0, &ts, NULL);
}

/* Runs the small startup self-tests exercised by --platform-test on the
 * other hosts. Returns 0 on success. */
int Dkc1LinuxPlatformTest(const char *directory);

#endif
