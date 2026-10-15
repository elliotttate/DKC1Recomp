#include "dkc1_coop_interp.h"

#include "cpu_state.h"
#include "common_rtl.h"
#include "snes/interp816.h"
#include "snes/interp_bridge.h"
#include "dkc1_coop_entities.h"
#include "dkc1_coop_graphics.h"
#include "dkc1_coop_hud.h"
#include "dkc1_coop_mod.h"
#include "dkc1_coop_schedule.h"
#include "dkc1_coop_world.h"
#include "dkc1_video.h"

typedef struct CoopLoopRule {
  uint32_t pc;
  uint16_t bound;
  char index;
  uint8_t live;
} CoopLoopRule;

static uint16_t CoopRaw16(const CpuState *cpu, uint16_t address);

#include "../generated/host/dkc1_coop_interp_rules.inc"

static const uint8_t *s_coop_interp_rom;
static size_t s_coop_interp_rom_size;

typedef struct CoopOpcodeBus {
  CpuState *cpu;
  uint32_t pc, data_address;
  uint16_t base, index, immediate;
  uint8_t bank, data_valid, immediate_valid;
  struct CoopOpcodeBus *parent;
} CoopOpcodeBus;
static CoopOpcodeBus *s_coop_opcode;

void Dkc1CoopInterpSetRom(const uint8_t *rom, size_t size) {
  s_coop_interp_rom = rom;
  s_coop_interp_rom_size = size;
}

static const CoopLoopRule *CoopLoop(uint32_t pc) {
  unsigned i;
  pc |= 0x800000u;
  for (i = 0; i < sizeof kCoopLoopRules / sizeof kCoopLoopRules[0]; ++i)
    if (kCoopLoopRules[i].pc == pc)
      return &kCoopLoopRules[i];
  return NULL;
}

static uint16_t CoopRaw16(const CpuState *cpu, uint16_t address) {
  return (uint16_t)(cpu->ram[address] |
      ((uint16_t)cpu->ram[(uint16_t)(address + 1u)] << 8));
}

uint32_t Dkc1CoopInterpBefore(CpuState *cpu, uint32_t pc24) {
  const uint32_t pc = pc24 | 0x800000u;
  uint32_t redirect;
  const CoopLoopRule *loop;
  if (!RtlSpeculativeExecutionActive())
    Dkc1CoopResumeActor(cpu->ram, pc24, cpu->S);
  if (!Dkc1CoopEntitiesActive())
    return 0;
  loop = CoopLoop(pc);
  if (loop) {
    uint16_t *index = loop->index == 'X' ? &cpu->X : &cpu->Y;
    *index = loop->live ? Dkc1CoopEntityLiveLoopIndex(*index) :
        loop->bound == 0x1eu ?
        Dkc1CoopEntityPoolLoopIndex(*index, loop->bound) :
        Dkc1CoopEntityLoopIndex(*index);
  }
  redirect = CoopResourceRedirect(cpu, pc);
  if (!redirect) redirect = CoopEventRedirect(cpu, pc);
  if (!redirect) redirect = CoopNativeRedirect(cpu, pc);
  if (!redirect) redirect = CoopScheduledRedirect(cpu, pc);
  return redirect ? (redirect & 0x7fffffu) | (pc24 & 0x800000u) : 0;
}

static int CoopOperandBytes(uint32_t pc, uint8_t *op, uint16_t *operand) {
  uint32_t offset = pc & 0x3fffffu;
  /* DKC1 code uses HiROM's system-bank upper halves. Do not reinterpret
   * executed RAM, low system registers, or another cartridge mapping. */
  if (!s_coop_interp_rom || (pc & 0xffffu) < 0x8000u ||
      ((pc >> 16) & 0x7fu) >= 0x40u ||
      offset + 2u >= s_coop_interp_rom_size)
    return 0;
  *op = s_coop_interp_rom[offset];
  *operand = (uint16_t)(s_coop_interp_rom[offset + 1] |
                       ((uint16_t)s_coop_interp_rom[offset + 2] << 8));
  return 1;
}

static void CoopCaptureBus(CoopOpcodeBus *bus, Interp816 *in) {
  uint8_t op;
  uint16_t operand;
  uint32_t pc = bus->pc | 0x800000u;
  const CoopLoopRule *loop = CoopLoop(pc);
  if (loop) {
    bus->immediate = Dkc1CoopEntityLoopEnd(loop->bound);
    bus->immediate_valid = 1;
  } else {
    bus->immediate_valid = (uint8_t)CoopSearchImmediate(pc, &bus->immediate);
  }
  if (!CoopOperandBytes(bus->pc, &op, &operand))
    return;
  switch (op) {
    /* All absolute,X/Y data modes, including 8/16-bit RMW operations. */
    case 0x1d: case 0x1e: case 0x3c: case 0x3d: case 0x3e:
    case 0x5d: case 0x5e: case 0x7d: case 0x7e: case 0x9d:
    case 0x9e: case 0xbc: case 0xbd: case 0xdd: case 0xde:
    case 0xfd: case 0xfe:
      bus->base = operand; bus->index = in->x; bus->bank = in->db; break;
    case 0x19: case 0x39: case 0x59: case 0x79: case 0x99:
    case 0xb9: case 0xbe: case 0xd9: case 0xf9:
      bus->base = operand; bus->index = in->y; bus->bank = in->db; break;
    case 0x95:
      if (pc == 0xb58068u || pc == 0xb5829eu) {
        bus->base = (uint16_t)(in->dp + (operand & 0xffu));
        bus->index = in->x; bus->bank = 0;
      } else if (pc == 0xb6e868u) {
        bus->index = CoopRaw16(bus->cpu, (uint16_t)(in->dp + 0x50u));
        bus->base = (uint16_t)(in->dp + in->x - bus->index);
        bus->bank = 0;
      } else return;
      break;
    default: return;
  }
  if (bus->index <= 0xffu && Dkc1CoopEntityIsField(bus->base)) {
    bus->data_address = ((uint32_t)bus->bank << 16) + bus->base + bus->index;
    bus->data_valid = 1;
  }
}

static uint8 Dkc1CoopInterpRead8(CpuState *cpu, uint8 bank, uint16 address) {
  CoopOpcodeBus *bus = s_coop_opcode;
  uint32_t full = ((uint32_t)bank << 16) | address;
  if (bus && bus->cpu == cpu) {
    uint8_t operand;
    if (CoopOperandRead(cpu,bus->pc,bank,address,&operand)) return operand;
    uint32_t delta = full - bus->data_address;
    if (bus->data_valid && delta <= 1u)
      return Dkc1CoopIndexedRead8(cpu, bus->bank, bus->base,
                                 (uint16_t)(bus->index + delta));
    delta = full - bus->pc;
    if (bus->immediate_valid && (delta == 1u || delta == 2u)) {
      cpu->open_bus = (uint8_t)(bus->immediate >> (8u * (delta - 1u)));
      return cpu->open_bus;
    }
  }
  return cpu_read8(cpu, bank, address);
}

static void Dkc1CoopInterpWrite8(CpuState *cpu, uint8 bank, uint16 address, uint8 value) {
  CoopOpcodeBus *bus = s_coop_opcode;
  uint32_t full = ((uint32_t)bank << 16) | address;
  if (bus && bus->cpu == cpu && bus->data_valid && full - bus->data_address <= 1u) {
    Dkc1CoopIndexedWrite8(cpu, bus->bank, bus->base,
        (uint16_t)(bus->index + full - bus->data_address), value);
    return;
  }
  cpu_write8(cpu, bank, address, value);
}

/* The engine's optional word handlers claim only MMIO, never actor WRAM.
 * Keeping them on the original path preserves atomic APU/PPU operations. */
int Dkc1CoopInterpRunOpcode(Interp816 *in) {
  int result;
  CoopOpcodeBus bus = {0};
  if (!Dkc1CoopEntitiesActive())
    return interp816_runOpcode(in);
  bus.cpu = (CpuState *)in->mem;
  bus.pc = ((uint32_t)in->k << 16) | in->pc;
  bus.parent = s_coop_opcode;
  CoopCaptureBus(&bus, in);
  s_coop_opcode = &bus;
  result = interp816_runOpcode(in);
  s_coop_opcode = bus.parent;
  return result;
}

/* These callbacks run the interpreter core directly. Unlike the shared
 * bridge, the core does not advance master/APU clocks, bounce into AOT, or
 * own the main interpreter's pending return/deadline context. Reject device
 * access BEFORE cpu_read/write: even a rejected MMIO touch would otherwise
 * add synthetic APU credit. SRAM and ROM writes are also nontransactional. */
static int s_private_actor, s_private_bus_failure;

static int CoopPrivateWram(uint32_t address) {
  unsigned bank = (address >> 16) & 0xffu;
  return bank == 0x7eu || bank == 0x7fu ||
      ((bank & 0x40u) == 0 && (address & 0xffffu) < 0x2000u);
}

static uint8_t CoopPrivateRead(void *memory, uint32_t address) {
  CpuState *cpu = memory;
  unsigned bank = (address >> 16) & 0xffu;
  /* Checksum-locked HiROM: $40..$7D/$C0..$FF are full ROM banks.
   * Its SRAM is only $6000..$7FFF in system banks, rejected here. The
   * superficially similar $70/$F0 low-half SRAM map belongs to LoROM. */
  if (!CoopPrivateWram(address) &&
      !((bank & 0x40u) || (address & 0xffffu) >= 0x8000u))
    s_private_bus_failure = 1;
  if (s_private_bus_failure) return cpu->open_bus;
  return Dkc1CoopInterpRead8(cpu, (uint8_t)bank, (uint16_t)address);
}

static void CoopPrivateWrite(void *memory, uint32_t address, uint8_t value) {
  if (!CoopPrivateWram(address)) s_private_bus_failure = 1;
  if (!s_private_bus_failure)
    Dkc1CoopInterpWrite8(memory, (uint8_t)(address >> 16), (uint16_t)address, value);
}

static void CoopPrivateFromCpu(const CpuState *cpu, Interp816 *in) {
  in->a = cpu->A; in->x = cpu->X; in->y = cpu->Y;
  in->sp = cpu->S; in->dp = cpu->D; in->k = cpu->PB; in->db = cpu->DB;
  in->c = cpu->_flag_C; in->z = cpu->_flag_Z; in->v = cpu->_flag_V;
  in->n = cpu->_flag_N; in->i = cpu->_flag_I; in->d = cpu->_flag_D;
  in->mf = cpu->m_flag; in->xf = cpu->x_flag; in->e = cpu->emulation;
}

static void CoopPrivateToCpu(const Interp816 *in, CpuState *cpu) {
  cpu->A = in->a; cpu->X = in->x; cpu->Y = in->y;
  cpu->S = in->sp; cpu->D = in->dp; cpu->PB = in->k; cpu->DB = in->db;
  cpu->_flag_C = in->c; cpu->_flag_Z = in->z; cpu->_flag_V = in->v;
  cpu->_flag_N = in->n; cpu->_flag_I = in->i; cpu->_flag_D = in->d;
  cpu->m_flag = in->mf; cpu->x_flag = in->xf; cpu->emulation = in->e;
  cpu_mirrors_to_p(cpu);
}

static int CoopPrivateBoundary(uint32_t pc) {
  /* An actor may request a transition by writing WRAM. It may not execute
   * another frame/scanner/reset transaction inside this atomic invocation. */
  pc |= 0x800000u;
  return CoopIsDispatchBoundary(pc) || CoopPrivateEventBoundary(pc);
}

int Dkc1CoopInterpRunActor(CpuState *cpu, uint32_t target,
                          uint32_t return_pc, unsigned instruction_limit,
                          uint32_t *stopped_pc) {
  Interp816 in = {0};
  uint32_t pc = target;
  int result = DKC1_COOP_ACTOR_INVALID;
  if (stopped_pc) *stopped_pc = target;
  if (!cpu || !cpu->ram || !s_coop_interp_rom || s_private_actor ||
      !RtlSpeculativeExecutionActive() || cpu->emulation ||
      cpu->S < 0x0103u || cpu->S >= 0x2000u)
    return result;
  const uint16_t stack = cpu->S;
  s_private_actor = 1;
  s_private_bus_failure = 0;
  /* Exact JSL frame, including its dormant stack bytes after return. */
  CoopPrivateWrite(cpu, cpu->S--, (uint8_t)(return_pc >> 16));
  CoopPrivateWrite(cpu, cpu->S--, (uint8_t)((return_pc - 1u) >> 8));
  CoopPrivateWrite(cpu, cpu->S--, (uint8_t)(return_pc - 1u));
  cpu->PB = (uint8_t)(target >> 16);
  in.mem = cpu; in.read = CoopPrivateRead; in.write = CoopPrivateWrite;
  CoopPrivateFromCpu(cpu, &in);
  in.pc = (uint16_t)target;
  result = DKC1_COOP_ACTOR_LIMIT;
  for (unsigned step = 0; step < instruction_limit; ++step) {
    uint8_t opcode;
    uint16_t operand;
    pc = ((uint32_t)in.k << 16) | in.pc;
    if (pc == return_pc && in.sp == stack) {
      CoopPrivateToCpu(&in, cpu);
      result = DKC1_COOP_ACTOR_COMPLETE;
      break;
    }
    if (in.sp >= stack || in.sp < 0x0100u || in.e ||
        CoopPrivateBoundary(pc) || !CoopOperandBytes(pc, &opcode, &operand) ||
        opcode == 0x00u || opcode == 0x02u || opcode == 0x40u ||
        opcode == 0xcbu || opcode == 0xdbu) {
      result = DKC1_COOP_ACTOR_CONTROL;
      break;
    }
    CoopPrivateToCpu(&in, cpu);
    const uint32_t redirect = Dkc1CoopInterpBefore(cpu, pc);
    CoopPrivateFromCpu(cpu, &in);
    if (RtlSpeculativeExecutionViolation()) {
      result = DKC1_COOP_ACTOR_BUS;
      break;
    }
    if (redirect) {
      in.k = (uint8_t)(redirect >> 16); in.pc = (uint16_t)redirect;
      continue;
    }
    Dkc1CoopInterpRunOpcode(&in);
    if (s_private_bus_failure || RtlSpeculativeExecutionViolation()) {
      result = DKC1_COOP_ACTOR_BUS;
      break;
    }
  }
  if (stopped_pc) *stopped_pc = pc;
  s_private_actor = 0;
  return result;
}

#ifndef DKC1_COOP_INTERP_UNIT_TEST
/* Compile the pinned bridge with scoped bus/opcode policies. Its original
 * timing, APU pacing, write epochs, return handling and dispatch stay intact. */
#define cpu_read8 Dkc1CoopInterpRead8
#define cpu_write8 Dkc1CoopInterpWrite8
#define interp816_runOpcode Dkc1CoopInterpRunOpcode
#include "../generated/host/dkc1_coop_interp_bridge.inc"
#undef interp816_runOpcode
#undef cpu_write8
#undef cpu_read8
#endif
