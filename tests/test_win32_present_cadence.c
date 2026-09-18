#include <assert.h>
#include <stdio.h>
#include "win32_present_cadence.h"

int main(void) {
  /* 60 Hz display: one refresh per emulated frame, nothing to split. */
  assert(HostPresentRefreshes(1, 0) == 1);
  assert(HostPresentRefreshes(1, 1) == 1);
  /* 120 Hz: a real frame shown alone keeps both refreshes (60 Hz cadence);
   * a real+midpoint pair takes one each.  A lone half-divisor present is
   * the double-speed bug: emulation would advance every refresh. */
  assert(HostPresentRefreshes(2, 0) == 2);
  assert(HostPresentRefreshes(2, 1) == 1);
  /* 240 Hz */
  assert(HostPresentRefreshes(4, 0) == 4);
  assert(HostPresentRefreshes(4, 1) == 2);
  /* Odd divisors (180 Hz) and unknown displays never split. */
  assert(HostPresentRefreshes(3, 1) == 3);
  assert(HostPresentRefreshes(0, 1) == 1);
  assert(HostPresentRefreshes(0, 0) == 1);

  /* DwmFlush pacing: the producer waits for whatever the worker did not. */
  assert(HostProducerFlushPasses(1, 0) == 1);
  assert(HostProducerFlushPasses(1, 1) == 1);
  assert(HostProducerFlushPasses(2, 0) == 2);
  assert(HostProducerFlushPasses(2, 1) == 1);
  assert(HostProducerFlushPasses(4, 0) == 4);
  assert(HostProducerFlushPasses(4, 1) == 2);
  assert(HostProducerFlushPasses(3, 1) == 3);
  assert(HostProducerFlushPasses(0, 1) == 1);
  /* Whole frames always add up to the divisor. */
  for (int divisor = 1; divisor <= 8; divisor++) {
    const int mid = HostPresentRefreshes(divisor, 1);
    const int real = HostPresentRefreshes(divisor, 1);
    if (divisor % 2 == 0)
      assert(mid + real == divisor);
    else
      assert(real == divisor);
    assert(HostProducerFlushPasses(divisor, 1) +
               (divisor % 2 == 0 ? divisor / 2 : 0) == divisor);
  }
  puts("present cadence: PASS");
  return 0;
}
