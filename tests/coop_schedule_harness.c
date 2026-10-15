#include <assert.h>
#define DKC1_COOP_INTERP_UNIT_TEST
#include "dkc1_coop_mod.c"
#include "dkc1_coop_interp.c"
#include "snes/dma.h"
#include "snes/snes.h"
#include "snes/cart.h"

uint8_t g_ram[0x20000], g_memsel, g_snesrecomp_last_hdmaen;
static uint8_t rom[0x400000];
const uint8_t *g_rom = rom;
Ppu *g_ppu;
Dma *g_dma;
Snes *g_snes;
static Ppu test_ppu;
static Dma test_dma;
static Snes test_snes;
static Cart test_cart;
static int mutate_devices;
uint64_t g_main_cpu_cycles_estimate, g_apu_pace_cycles_estimate;
uint64_t g_apu_last_sync_cycles, g_apu_last_sync_master;
static int spec;
static uint32_t violation;
static unsigned bus_devices, begins, ends;
static Dkc1CoopWorldSnapshot world;

bool RtlSpeculativeExecutionActive(void) { return spec != 0; }
uint32_t RtlSpeculativeExecutionViolation(void) { return violation; }
void RtlSpeculativeExecutionBegin(void) { assert(!spec); spec=1; violation=0; }
void RtlSpeculativeExecutionEnd(void) { assert(spec); spec=0; }
void Dkc1CoopWorldReset(void) { memset(&world, 0, sizeof world); }
void Dkc1CoopWorldSaveState(Dkc1CoopWorldSnapshot *s) { *s=world; }
void Dkc1CoopWorldLoadState(const Dkc1CoopWorldSnapshot *s) { world=*s; }
int Dkc1CoopWorldGetCamera(const uint8_t *r,int v,uint16_t *x,uint16_t *y) { return 0; }
int Dkc1CoopWorldKeepActor(CpuState *c,uint16_t r) { return 0; }
int Dkc1CoopWorldKeepSource(CpuState *c,int group) { return 0; }
void Dkc1CoopWorldNoteInitialize(void) { ++world.initialized; }
void Dkc1CoopWorldNoteNativeCamera(const uint8_t *ram) { (void)ram; }
uint16_t Dkc1CoopWorldCameraFocus(CpuState *cpu,uint16_t native) { return native; }
void Dkc1CoopWorldNoteScan(CpuState *c) { ++world.scan_pending; }
void Dkc1CoopHudBegin(CpuState *c) { assert(0); }
void Dkc1CoopHudEnd(CpuState *c) { assert(0); }
void Dkc1CoopHudSpriteBegin(CpuState *c) { assert(0); }
void Dkc1CoopHudSpriteEnd(CpuState *c) { assert(0); }
bool Dkc1VideoBeginPlacedActorDispatch(CpuState *c) { ++begins; return false; }
void Dkc1VideoEndPlacedActorDispatch(CpuState *c) { ++ends; }
int Dkc1VideoLoadCgramFromRom(Ppu *p,unsigned dst,uint8_t b,uint16_t a,unsigned n) { return 1; }
void Dkc1VideoCopyCgram(Ppu *p,unsigned dst,unsigned src,unsigned n) {}
int interp816_opcode_hook(uint32_t pc) { assert(0); return 0; }

uint8 cpu_read8(CpuState *c,uint8 b,uint16 a) {
  test_cart.cpuBusAddress=((uint32_t)b<<16)|a;
  if (mutate_devices) { ++test_ppu.inidisp; ++test_dma.dmaTimer; }
  if (b==0x7e || b==0x7f) return c->open_bus=c->ram[((b&1)<<16)|a];
  if (!(b&0x40) && a<0x2000) return c->open_bus=c->ram[a];
  if (!(b&0x40) && a<0x8000) { ++bus_devices; return 0; }
  return c->open_bus=rom[((b&0x3f)<<16)|a];
}
uint16 cpu_read16(CpuState *c,uint8 b,uint16 a) {
  uint16 v=cpu_read8(c,b,a); return v|((uint16)cpu_read8(c,b,(uint16)(a+1))<<8);
}
void cpu_write8(CpuState *c,uint8 b,uint16 a,uint8 v) {
  if (b==0x7e || b==0x7f) c->ram[((b&1)<<16)|a]=v;
  else if (!(b&0x40) && a<0x2000) c->ram[a]=v;
  else ++bus_devices;
  c->open_bus=v;
}
void cpu_write16(CpuState *c,uint8 b,uint16 a,uint16 v) {
  cpu_write8(c,b,a,(uint8)v); cpu_write8(c,b,(uint16)(a+1),(uint8)(v>>8));
}
static uint8_t Read(void *c,uint32_t a) { return Dkc1CoopInterpRead8(c,a>>16,(uint16)a); }
static void Write(void *c,uint32_t a,uint8_t v) { Dkc1CoopInterpWrite8(c,a>>16,(uint16)a,v); }

static CpuState Setup(void) {
  CpuState c={0};
  memset(g_ram,0,sizeof g_ram); memset(rom,0,sizeof rom); ResetOverlayState();
  memset(&test_ppu,0,sizeof test_ppu); memset(&test_dma,0,sizeof test_dma);
  memset(&test_snes,0,sizeof test_snes); memset(&test_cart,0,sizeof test_cart);
  g_ppu=&test_ppu; g_dma=&test_dma; g_snes=&test_snes; g_snes->cart=&test_cart;
  test_cart.rom=rom; test_cart.romSize=sizeof rom; mutate_devices=0;
  s_runtime.enabled=1; Dkc1CoopEntitiesReset(); Dkc1CoopGraphicsReset(); Dkc1CoopWorldReset();
  Dkc1CoopInterpSetRom(rom,sizeof rom); Dkc1CoopGraphicsSetRom(rom,sizeof rom);
  c.ram=g_ram; c.X=0x80; c.S=0x1ff; c.PB=c.DB=0xbf;
  c.cycles=100; c.master_cycles=600; c.coprocessor_master_cycles=598;
  c.host_return_valid=2;
  Dkc1WramSetU16(g_ram,0x82,0x80); Dkc1WramSetU16(g_ram,0x20,7);
  Dkc1CoopEntityWrite16(g_ram,0xd45,0x80,7);
  Dkc1Actor_SetSpriteIDLo(g_ram,2,1); Dkc1Actor_SetSpriteIDLo(g_ram,4,2);
  Dkc1WramSetU16(g_ram,0x1b25,0x1400); Dkc1CoopCacheKongSlots(g_ram,s_runtime.slot);
  g_main_cpu_cycles_estimate=31; g_apu_pace_cycles_estimate=32;
  g_apu_last_sync_cycles=33; g_apu_last_sync_master=34;
  g_memsel=1; g_snesrecomp_last_hdmaen=0x80;
  return c;
}

/* Untimed reference executes the same guest instructions directly, without
 * the private-call helper. The isolated path must preserve its entire WRAM,
 * stack residue, flags, RNG order, actor cells and graphics allocations. */
static void Reference(CpuState *c,uint32_t target,uint32_t ret) {
  const uint16_t sp=c->S;
  cpu_write8(c,0,c->S--,ret>>16); cpu_write8(c,0,c->S--,(ret-1)>>8); cpu_write8(c,0,c->S--,ret-1);
  Interp816 in={0}; in.mem=c; in.read=Read; in.write=Write; c->PB=target>>16;
  CoopPrivateFromCpu(c,&in); in.pc=(uint16_t)target;
  RtlSpeculativeExecutionBegin();
  unsigned steps=0;
  while (((uint32_t)in.k<<16|in.pc)!=ret || in.sp!=sp) {
    assert(++steps<1000); CoopPrivateToCpu(&in,c);
    uint32_t redirect=Dkc1CoopInterpBefore(c,(uint32_t)in.k<<16|in.pc);
    CoopPrivateFromCpu(c,&in);
    if (redirect) { in.k=redirect>>16; in.pc=(uint16_t)redirect; }
    else Dkc1CoopInterpRunOpcode(&in);
  }
  CoopPrivateToCpu(&in,c); RtlSpeculativeExecutionEnd();
}

static void Put(uint32_t pc,const uint8_t *bytes,size_t n) { memcpy(rom+(pc&0x3fffff),bytes,n); }
#define PROGRAM(pc,...) do { const uint8_t bytes[]={__VA_ARGS__}; Put(pc,bytes,sizeof bytes); } while(0)
static void Tick(void) {
  PROGRAM(0xbf9000, 0xad,0x20,0x00,0x1a,0x8d,0x20,0x00,0x49,0x5a,0x00,
          0x8d,0x22,0x00,0x9d,0x19,0x0b,0x22,0x46,0xf3,0xbd,0x6b);
  PROGRAM(0xbdf36d,0x6b);
  PROGRAM(0xbdf346,0xea);
}

int main(void) {
  CpuState c=Setup(); Tick();
  const size_t es=Dkc1CoopEntitiesStateSize(), gs=Dkc1CoopGraphicsStateSize();
  uint8_t *entities=malloc(es), *graphics=malloc(gs), expected_ram[sizeof g_ram];
  Reference(&c,0xbf9000,0xbf8033);
  CpuState expected_cpu=c;
  memcpy(expected_ram,g_ram,sizeof g_ram);
  assert(Dkc1CoopEntitiesSaveState(entities,es)); assert(Dkc1CoopGraphicsSaveState(graphics,gs));
  c=Setup(); Tick();
  /* A pending outer actor is host state, untouched by private dispatch. */
  memset(&s_runtime.actor.wait,0x35,sizeof s_runtime.actor.wait); s_runtime.actor.wait.pending=1;
  Dkc1CoopActorSnapshot pending=s_runtime.actor.wait;
  int committed=Dkc1CoopScheduleActor(&c,0xbf9000,0xbf8033);
  assert(committed);
  assert(!memcmp(&c,&expected_cpu,sizeof c)); assert(!memcmp(g_ram,expected_ram,sizeof g_ram));
  assert(!memcmp(&s_runtime.actor.wait,&pending,sizeof pending));
  uint8_t *actual=malloc(es>gs?es:gs);
  assert(Dkc1CoopEntitiesSaveState(actual,es) && !memcmp(actual,entities,es));
  assert(Dkc1CoopGraphicsSaveState(actual,gs) && !memcmp(actual,graphics,gs));
  assert(Dkc1CoopGraphicsBlocks(0x80)==1);
  assert(Dkc1CoopScheduleActor(&c,0xbf9000,0xbf8033));
  assert(Dkc1WramU16(g_ram,0x20)==9); /* exactly one RNG step per tick */
  assert(!bus_devices && !begins && !ends && c.master_cycles==600);

  /* Damage changes host leader_lock, then MMIO rejects the attempt. Neither
   * the damage, graphics allocation nor RNG/actor writes may escape. */
  c=Setup(); Tick();
  PROGRAM(0xbf9014,0xa2,0x04,0x00,0x5c,0xd4,0xa1,0xbf);
  PROGRAM(0xbfa1d4,0x8a);
  PROGRAM(0xbfa1ef,0xad,0x40,0x21,0x6b);
  Dkc1Actor_SetRAMTable1029Lo(g_ram,4,0x0d); Dkc1WramSetU16(g_ram,0x579,1);
  s_runtime.lock=2; s_runtime.actor.wait=pending; world.initialized=7;
  memcpy(expected_ram,g_ram,sizeof g_ram); expected_cpu=c;
  const uint32_t original_bus_address=test_cart.cpuBusAddress;
  assert(Dkc1CoopEntitiesSaveState(entities,es)); assert(Dkc1CoopGraphicsSaveState(graphics,gs));
  assert(!Dkc1CoopScheduleActor(&c,0xbf9000,0xbf8033));
  assert(s_runtime.lock==2 && !memcmp(&s_runtime.actor.wait,&pending,sizeof pending));
  assert(!memcmp(&c,&expected_cpu,sizeof c) && !memcmp(g_ram,expected_ram,sizeof g_ram));
  assert(Dkc1CoopEntitiesSaveState(actual,es) && !memcmp(actual,entities,es));
  assert(Dkc1CoopGraphicsSaveState(actual,gs) && !memcmp(actual,graphics,gs));
  assert(!bus_devices && world.initialized==7 && !spec);
  assert(test_cart.cpuBusAddress==original_bus_address);
  assert(g_main_cpu_cycles_estimate==31 && g_apu_pace_cycles_estimate==32 &&
         g_apu_last_sync_cycles==33 && g_apu_last_sync_master==34 && g_memsel==1 && g_snesrecomp_last_hdmaen==0x80);

  /* SRAM write, nonlocal return, nested dispatcher and instruction cap all
   * leave the main state exact, ready for its original timed call. */
  const uint8_t rejected[][5]={{0x8d,0,0x60,0x6b,0}, {0x68,0x6b,0,0,0},
      {0x5c,0x2f,0x80,0xbf,0}, {0x80,0xfe,0,0,0}};
  for (unsigned i=0;i<4;++i) {
    Put(0xbf9000,rejected[i],5);
    assert(!Dkc1CoopScheduleActor(&c,0xbf9000,0xbf8033));
    assert(!memcmp(&c,&expected_cpu,sizeof c) && !memcmp(g_ram,expected_ram,sizeof g_ram));
  }
  assert(!bus_devices && !begins && !ends && !spec);
  Dkc1WramSetU16(g_ram,0x82,2); assert(!Dkc1CoopScheduleActor(&c,0xbf9000,0xbf8033));
  Dkc1WramSetU16(g_ram,0x82,0x80); RtlSpeculativeExecutionBegin();
  assert(!Dkc1CoopScheduleActor(&c,0xbf9000,0xbf8033)); RtlSpeculativeExecutionEnd();

  /* Every semantic overlay cell, including a suspended native actor, must
   * survive failure; a nested scope must not replace the outer checkpoint. */
  assert(Dkc1CoopBeginTransaction());
  CoopRuntimeState host_before=s_runtime;
  memset(&s_runtime,0xa5,sizeof s_runtime);
  assert(!Dkc1CoopBeginTransaction()); Dkc1CoopEndTransaction(0);
  assert(Dkc1CoopBeginTransaction());
  assert(!memcmp(&host_before,&s_runtime,sizeof host_before)); Dkc1CoopEndTransaction(1);

  /* Reject even an unexpected host device mutation during an otherwise pure
   * call, restore concrete device objects and the cartridge bus address. */
  c=Setup(); Tick(); test_ppu.inidisp=5; test_dma.dmaTimer=23;
  test_cart.cpuBusAddress=0x123456; mutate_devices=1;
  expected_cpu=c; memcpy(expected_ram,g_ram,sizeof g_ram);
  assert(!Dkc1CoopScheduleActor(&c,0xbf9000,0xbf8033));
  mutate_devices=0;
  assert(test_ppu.inidisp==5 && test_dma.dmaTimer==23);
  assert(test_cart.cpuBusAddress==0x123456);
  assert(!memcmp(&c,&expected_cpu,sizeof c) && !memcmp(g_ram,expected_ram,sizeof g_ram));

  /* A failed trial followed by the original timed invocation advances the
   * RNG once and commits the survivor handoff once, with no trial residue. */
  c=Setup(); Tick();
  PROGRAM(0xbf9014,0xa2,0x04,0x00,0x5c,0xd4,0xa1,0xbf);
  PROGRAM(0xbfa1d4,0x8a); PROGRAM(0xbfa1ef,0xad,0x40,0x21,0x6b);
  Dkc1Actor_SetRAMTable1029Lo(g_ram,4,0x0d); Dkc1WramSetU16(g_ram,0x579,1); s_runtime.lock=2;
  assert(!Dkc1CoopScheduleActor(&c,0xbf9000,0xbf8033));
  assert(Dkc1WramU16(g_ram,0x20)==7 && s_runtime.lock==2);
  Reference(&c,0xbf9000,0xbf8033);
  assert(Dkc1WramU16(g_ram,0x20)==8 && s_runtime.lock==1);
  assert(bus_devices==2); /* one native 16-bit MMIO read, none in trial */
  free(entities); free(graphics); free(actual);
  return 0;
}

void Dkc1CoopWorldNoteKongCamera(const uint8_t *ram,uint16_t slot) { (void)ram;(void)slot; }
