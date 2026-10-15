#ifndef DKC1_COOP_SCHEDULE_H
#define DKC1_COOP_SCHEDULE_H

#include <stdint.h>
struct CpuState;

/* Execute only a host continuation actor at its original dispatch position.
 * Success commits one complete tick; failure leaves every state untouched so
 * the caller executes the original timed JSL. Native slots always return 0. */
int Dkc1CoopScheduleActor(struct CpuState *cpu, uint32_t target,
                          uint32_t return_pc);

/* Observational only: never serialized or consulted by simulation. */

#endif
