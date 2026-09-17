#include "desktop_refresh.h"
#include <math.h>

void Dkc1RefreshReset(Dkc1Refresh *refresh) {
  *refresh = (Dkc1Refresh){0};
}

void Dkc1RefreshObserve(Dkc1Refresh *refresh, double interval) {
  if (!isfinite(interval) || interval <= 0.0 || interval > 0.100) {
    Dkc1RefreshReset(refresh);
    return;
  }
  int divisor = 0;
  for (int n = 1; n <= 4; n++) {
    if (fabs(1.0 / (interval * n * 60.0) - 1.0) <= 0.02) {
      divisor = n;
      break;
    }
  }
  /* Eight consecutive observations promote a change. A missed callback
   * cannot demote an otherwise stable 120 Hz display to 60 Hz. Zero denotes
   * an incompatible cadence, which uses absolute target timestamps. */
  if (divisor != refresh->candidate) {
    refresh->candidate = divisor;
    refresh->observations = 0;
  }
  if (++refresh->observations >= 8) {
    if (refresh->divisor != divisor)
      refresh->next_frame_time = 0.0;
    refresh->divisor = divisor;
    refresh->observations = 8;
  }
}

int Dkc1RefreshAdvance(Dkc1Refresh *refresh, double target,
                      unsigned repeats) {
  if (refresh->divisor)
    return repeats >= (unsigned)refresh->divisor;
  /* Unknown/non-integer refresh: distribute immutable 60 Hz frames over
   * display targets without accelerating the producer or catching up after
   * a stall. This naturally gives a 2/3 pattern at 144 Hz. */
  if (!refresh->next_frame_time ||
      fabs(target - refresh->next_frame_time) > 0.100)
    refresh->next_frame_time = target + 1.0 / 60.0;
  if (target + 0.000001 < refresh->next_frame_time)
    return 0;
  refresh->next_frame_time += 1.0 / 60.0;
  if (refresh->next_frame_time <= target)
    refresh->next_frame_time = target + 1.0 / 60.0;
  return 1;
}
