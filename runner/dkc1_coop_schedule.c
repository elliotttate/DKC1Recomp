#include "dkc1_coop_schedule.h"

#include "common_rtl.h"
#include "dkc1_coop_machine.h"
#include "dkc1_coop_transaction.h"
#include "common_cpu_infra.h"
#include "cpu_state.h"
#include "dkc1_coop_entities.h"
#include "dkc1_coop_graphics.h"
#include "dkc1_coop_interp.h"
#include "dkc1_coop_mod.h"
#include "dkc1_coop_world.h"
#include "snes/dma.h"
#include "snes/ppu.h"

#include <string.h>

static const Dkc1CoopTransactionParticipant kParticipants[] = {
  {Dkc1CoopEntitiesBeginTransaction,Dkc1CoopEntitiesEndTransaction},
  {Dkc1CoopGraphicsBeginTransaction,Dkc1CoopGraphicsEndTransaction},
  {Dkc1CoopBeginTransaction,Dkc1CoopEndTransaction},
};

static uint8_t s_before[0x20000];
static Dkc1CoopMachineSnapshot s_machine;
int Dkc1CoopScheduleActor(CpuState *cpu, uint32_t target, uint32_t return_pc) {
  if (!cpu || cpu->ram != g_ram || !Dkc1CoopEntitiesActive() ||
      RtlSpeculativeExecutionActive() || cpu->D > 0x1f7cu)
    return 0;
  const unsigned address = cpu->D + 0x82u;
  const uint16_t handle = (uint16_t)(cpu->ram[address] |
      ((uint16_t)cpu->ram[address + 1] << 8));
  if (handle < 0x80u || !Dkc1CoopEntityIsHandle(handle)) return 0;
  Dkc1CoopTransaction transaction;
  if (!Dkc1CoopTransactionBegin(&transaction,kParticipants,
      sizeof kParticipants/sizeof *kParticipants)) return 0;


  /* The main interpreter may be suspended in an AOT call at this exact loop
   * position. A private interp816 core never enters or alters that bridge.
   * RNG, sound queues, children, collision and scratch are ordinary shared
   * WRAM, committed once in this original actor order, not in a second pass. */
  Dkc1CoopWorldSnapshot world;
  Dkc1CoopWorldSaveState(&world);
  memcpy(s_before, cpu->ram, sizeof s_before);
  Dkc1CoopMachineCapture(&s_machine);
  CpuState scratch = *cpu;
  uint32_t stopped_pc = target;
  RtlSpeculativeExecutionBegin();
  const int result = Dkc1CoopInterpRunActor(&scratch, target, return_pc,
                                           100000u, &stopped_pc);
  const int commit = result == DKC1_COOP_ACTOR_COMPLETE &&
      !RtlSpeculativeExecutionViolation() && scratch.S == cpu->S &&
      scratch.cycles == cpu->cycles && scratch.master_cycles == cpu->master_cycles &&
      scratch.coprocessor_master_cycles == cpu->coprocessor_master_cycles &&
      Dkc1CoopMachineDevicesUnchanged(&s_machine);
  RtlSpeculativeExecutionEnd();
  if (commit) {
    /* The AOT caller retains its return ABI and program-bank ownership. */
    scratch.PB = cpu->PB;
    scratch.host_return_valid = cpu->host_return_valid;
    *cpu = scratch;
  } else {
    memcpy(cpu->ram, s_before, sizeof s_before);
    Dkc1CoopWorldLoadState(&world);
    Dkc1CoopMachineRestore(&s_machine, kCoopRestoreCartBus);
  }
  Dkc1CoopMachineRestore(&s_machine, kCoopRestoreClocks | kCoopRestoreDevices | kCoopRestoreMemsel);
  Dkc1CoopTransactionEnd(&transaction,commit);
  return commit;
}
