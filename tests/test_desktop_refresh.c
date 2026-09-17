#include "desktop_refresh.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>

int main(void) {
  Dkc1Refresh r = {0};
  const double rates[] = {59.94, 60, 119.88, 120, 180, 240};
  for (unsigned i = 0; i < sizeof rates / sizeof *rates; i++) {
    Dkc1RefreshReset(&r);
    for (int j = 0; j < 8; j++) Dkc1RefreshObserve(&r, 1.0 / rates[i]);
    assert(r.divisor == (int)round(rates[i] / 60));
    assert(!Dkc1RefreshAdvance(&r, 1, r.divisor - 1));
    assert(Dkc1RefreshAdvance(&r, 1, r.divisor));
  }
  Dkc1RefreshReset(&r);
  for (int j = 0; j < 8; j++) Dkc1RefreshObserve(&r, 1.0 / 120);
  for (int j = 0; j < 100; j++) {
    Dkc1RefreshObserve(&r, 1.0 / (j % 10 ? 120 : 60));
    assert(r.divisor == 2);
  }
  for (int j = 0; j < 8; j++) Dkc1RefreshObserve(&r, 1.0 / 60);
  assert(r.divisor == 1);
  Dkc1RefreshObserve(&r, NAN);
  assert(!r.divisor);
  int advances = 0;
  for (int j = 0; j <= 1440; j++) {
    Dkc1RefreshObserve(&r, 1.0 / 144);
    advances += Dkc1RefreshAdvance(&r, 1.0 + j / 144.0, 1);
  }
  assert(advances == 600);
  assert(!r.divisor);
  Dkc1RefreshObserve(&r, 0.5);
  assert(!Dkc1RefreshAdvance(&r, 20, 1));
  puts("desktop refresh tests passed");
}
