#ifndef DKC1_COOP_INTERP_H
#define DKC1_COOP_INTERP_H

#include <stddef.h>
#include <stdint.h>
struct CpuState;
struct Interp816;

void Dkc1CoopInterpSetRom(const uint8_t *rom, size_t size);
/* Used by the prepared bridge before call/return classification, and by its
 * ordinary opcode execution respectively. Zero means no PC redirect. */
uint32_t Dkc1CoopInterpBefore(struct CpuState *cpu, uint32_t pc24);
int Dkc1CoopInterpRunOpcode(struct Interp816 *interpreter);

/* Bounded, isolated core execution. The caller owns WRAM/host transactions
 * and the speculative scope. No bridge scheduler, deadlines, interrupts,
 * AOT invocation or device clock is entered by this function. */
enum Dkc1CoopActorResult {
  DKC1_COOP_ACTOR_COMPLETE = 1,
  DKC1_COOP_ACTOR_INVALID = -1,
  DKC1_COOP_ACTOR_BUS = -2,
  DKC1_COOP_ACTOR_CONTROL = -3,
  DKC1_COOP_ACTOR_LIMIT = -4,
};
int Dkc1CoopInterpRunActor(struct CpuState *cpu, uint32_t target,
                          uint32_t return_pc, unsigned instruction_limit,
                          uint32_t *stopped_pc);

#endif
