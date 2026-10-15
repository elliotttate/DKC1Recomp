#ifndef DKC1_COOP_CLOCK_H
#define DKC1_COOP_CLOCK_H
#include <stdint.h>
/* Include after the runtime clock declarations (common_rtl/cpu_state). */
typedef struct Dkc1CoopClockSnapshot {
  uint64_t main_cycles, apu_cycles, apu_sync, apu_master;
  uint8_t hdma;
} Dkc1CoopClockSnapshot;
static inline void Dkc1CoopClockCapture(Dkc1CoopClockSnapshot *s) {
  s->main_cycles = g_main_cpu_cycles_estimate;
  s->apu_cycles = g_apu_pace_cycles_estimate;
  s->apu_sync = g_apu_last_sync_cycles;
  s->apu_master = g_apu_last_sync_master;
  s->hdma = g_snesrecomp_last_hdmaen;
}
static inline void Dkc1CoopClockRestore(const Dkc1CoopClockSnapshot *s) {
  g_main_cpu_cycles_estimate = s->main_cycles;
  g_apu_pace_cycles_estimate = s->apu_cycles;
  g_apu_last_sync_cycles = s->apu_sync;
  g_apu_last_sync_master = s->apu_master;
  g_snesrecomp_last_hdmaen = s->hdma;
}
#endif
