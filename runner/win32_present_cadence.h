/* Windows presenter cadence arithmetic for the compositor-locked pacing
 * modes.  Pure functions so the split of a display divisor between a real
 * frame and its generated midpoint can be unit-tested without the host.
 *
 * Every emulated frame must occupy exactly `divisor` display refreshes
 * (two on a 120 Hz display).  With frame generation a real image and its
 * midpoint share them half and half.  A real image that no midpoint follows
 * (interpolation bypassed for a pixel inspect, provenance overlay, single
 * step or layer isolation; the worker cancelled by a UI change) must keep
 * the whole divisor: the flip-model swap chain releases the next slot as
 * soon as an image has been consumed, so a half-divisor present with no
 * partner lets emulation run at the display rate. */
#ifndef DKC1_WIN32_PRESENT_CADENCE_H
#define DKC1_WIN32_PRESENT_CADENCE_H

/* Sync interval (refreshes) for one presented image.  `paired` is non-zero
 * for both images of a real+midpoint pair.  Odd divisors cannot be split
 * and always keep the whole divisor. */
static int HostPresentRefreshes(int divisor, int paired) {
  int refreshes = divisor > 0 ? divisor : 1;
  if (paired && refreshes >= 2 && (refreshes % 2) == 0) refreshes /= 2;
  return refreshes;
}

/* Compositor passes the producer waits for before its own blit in DwmFlush
 * pacing.  The midpoint worker already consumed half of them when it
 * presented (`mid_presented`); without one the producer waits for all. */
static int HostProducerFlushPasses(int divisor, int mid_presented) {
  int passes = divisor > 0 ? divisor : 1;
  if (mid_presented && passes >= 2 && (passes % 2) == 0) passes -= passes / 2;
  return passes;
}

#endif
