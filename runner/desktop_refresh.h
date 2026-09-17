#ifndef DKC1_DESKTOP_REFRESH_H
#define DKC1_DESKTOP_REFRESH_H

/* Display-only qualification. Never changes the cartridge clock. */
typedef struct Dkc1Refresh {
  int divisor, candidate, observations;
  double next_frame_time;
} Dkc1Refresh;

void Dkc1RefreshReset(Dkc1Refresh *refresh);
void Dkc1RefreshObserve(Dkc1Refresh *refresh, double interval);
int Dkc1RefreshAdvance(Dkc1Refresh *refresh, double target,
                      unsigned repeats);
#endif
