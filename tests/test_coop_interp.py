from pathlib import Path
import importlib.util
import sys
import unittest

import test_coop_mod as native_contract

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))
import prepare_coop_interp_bridge as prepare


class CoopInterpreterTests(unittest.TestCase):
    def test_event_catalog_has_identical_backend_boundaries_and_pins_rom_bytes(self):
        if not (ROOT / "dkc1.sfc").exists():
            self.skipTest("supported private ROM required for hook byte contracts")
        hooks = prepare.world.COOP_EVENT_HOOKS
        rules = prepare.rules((ROOT / "dkc1.sfc").read_bytes())
        damaged = bytearray((ROOT / "dkc1.sfc").read_bytes())
        damaged[0x3fa0a9] ^= 1
        with self.assertRaisesRegex(ValueError, "native rope ceiling operand changed"):
            prepare.rules(bytes(damaged))
        seen = set()
        for hook in hooks:
            pc = int(hook["pc"],16)
            self.assertNotIn(pc,seen);seen.add(pc)
            self.assertIn(f"case 0x{pc:06x}u:",rules)
            self.assertIn(hook["call"],rules)
            rom = bytearray((ROOT / "dkc1.sfc").read_bytes())
            rom[pc & 0x3fffff] ^= 1
            with self.assertRaisesRegex(ValueError,"co-op event contract changed"):
                prepare.rules(bytes(rom))

    def test_catalog_culls_pin_every_native_boundary(self):
        if not (ROOT / "dkc1.sfc").exists():
            self.skipTest("supported private ROM required")
        original = (ROOT / "dkc1.sfc").read_bytes()
        for hook in prepare.world.COOP_CATALOG["culls"]:
            rom = bytearray(original)
            rom[int(hook["pc"],16) & 0x3fffff] ^= 1
            with self.assertRaises(ValueError):
                prepare.rules(bytes(rom))

    def test_residual_catalog_covers_native_boundaries_and_pins_each_source_byte(self):
        rom_path = ROOT / "dkc1.sfc"
        if not rom_path.exists():
            self.skipTest("supported private ROM required")
        original = rom_path.read_bytes()
        catalog = prepare.world.COOP_CATALOG
        self.assertEqual({h["pc"] for h in catalog["entity_hooks"]}, {
            "BFA1D4", "BFA3F9", "BFA6D1", "80A1E6", "BDF3B1", "BDF3B5",
            "BDF3D2", "BDF3D6", "B6E78D", "B6E79B", "BCADC4", "BBA59B",
            "BBA671", "BBA592", "BBA668", "BCB78D"})
        rules = prepare.rules(original)
        pins = []
        for section in ("entity_hooks", "resource_hooks", "immediates"):
            for hook in catalog[section]:
                self.assertIn(f"case 0x{hook['pc'].lower()}u:", rules)
                pins.append((hook.get("source_pc", hook["pc"]), hook["bytes"]))
        for _owner, pc, _target, _ret, overlay in prepare.schedule.CALLS:
            if overlay:
                at = (pc & 0x3fffff) - 2
                pins.append((f"{pc - 2:06X}", original[at:at + 6].hex(" ")))
        for pc, expected in pins:
            for byte in range(len(bytes.fromhex(expected))):
                with self.subTest(pc=pc, byte=byte):
                    rom = bytearray(original)
                    rom[(int(pc, 16) & 0x3fffff) + byte] ^= 1
                    with self.assertRaises(ValueError):
                        prepare.rules(bytes(rom))

    def test_residual_catalog_rejects_overlapping_backend_boundaries(self):
        rom_path = ROOT / "dkc1.sfc"
        if not rom_path.exists():
            self.skipTest("supported private ROM required")
        # Moving hook switches is inert only while their PCs are disjoint.
        # Retain RETURN_HOOKS as its existing source instead of duplicating it.
        hook = prepare.world.COOP_CATALOG["entity_hooks"][0]
        pc = hook["pc"]
        try:
            hook["pc"] = f"{prepare.graphics.RETURN_HOOKS[0][1]:06X}"
            with self.assertRaisesRegex(ValueError, "duplicate co-op boundary"):
                prepare.rules(rom_path.read_bytes())
        finally:
            hook["pc"] = pc

    def test_bridge_hook_precedes_native_opcode_classification(self):
        original = (ROOT / "snesrecomp/runner/src/snes/interp_bridge.c").read_text()
        adapted = prepare.adapt(original)
        self.assertEqual(adapted.count("Dkc1CoopInterpBefore(cpu, pc_before)"), 1)
        self.assertLess(adapted.index("Dkc1CoopInterpBefore(cpu, pc_before)"),
                        adapted.index("if (s_pre_opcode_hook_count > 0)"))
        self.assertIn("sync_interp_to_cpu(&in, cpu);", adapted)
        self.assertIn("sync_cpu_to_interp(cpu, &in);", adapted)
        with self.assertRaises(ValueError):
            prepare.adapt(adapted.replace("if (s_pre_opcode_hook_count > 0)", "if (0)"))

    def test_rules_share_aot_graphics_culls_and_rom_proven_loop_sites(self):
        rom_path = ROOT / "dkc1.sfc"
        if not rom_path.exists():
            self.skipTest("private supported ROM unavailable")
        rules = prepare.rules(rom_path.read_bytes())
        self.assertEqual(len(prepare.loop_rules(rom_path.read_bytes())), 20)
        self.assertIn("{0xbf8037u, 0x34u, 'X', 1}", rules)
        self.assertIn("{0xb69738u, 0x1eu, 'X', 0}", rules)
        self.assertIn("{0xbf848fu, 0x34u, 'X', 0}", rules)
        live_rows = [line for line in rules.splitlines() if line.endswith("'X', 1},")]
        self.assertEqual(len(live_rows), 5)
        for pc in (0xbf8037, 0xbf8071, 0xbf80b9, 0xbf80e8, 0xbf811b):
            self.assertIn(f"{{0x{pc:06x}u, 0x34u, 'X', 1}}", rules)
        for _, entry, ret, _ in prepare.graphics.RETURN_HOOKS:
            self.assertIn(f"case 0x{entry:06x}u:", rules)
            self.assertIn(f"? 0x{ret:06x}u : 0", rules)
        for symbol, failure, success, _ in prepare.world.COOP_CULL_FAILURES:
            bank = symbol.split('_')[1][:2].lower()
            self.assertIn(f"case 0x{bank}{failure.lower()}u:", rules)
            self.assertIn(f"? 0x{bank}{success.lower()}u : 0", rules)
        self.assertIn("Dkc1CoopIndependentDamage(cpu) ? 0xbfa1efu : 0", rules)
        self.assertIn("Dkc1CoopIndependentDamage(cpu) ? 0xbfa40du : 0", rules)
        self.assertIn("case 0xbfa3f9u:", rules)

    def test_actual_interpreter_executes_extended_memory_and_semantics(self):
        if not (ROOT / "dkc1.sfc").exists():
            self.skipTest("supported private ROM required for native interpreter rules")
        source = r'''
#include <assert.h>
#include <string.h>
#define DKC1_COOP_INTERP_UNIT_TEST
#include "dkc1_coop_interp.c"
static int enabled, resource_calls, cull, begins, ends, hud_begins, hud_ends, resumes;
static int speculative;
static uint16_t released_handle, copied_from, copied_to, fixed_handle, allocated_handle;
static uint8_t rom[0x400000], ram[0x20000];
static const uint8_t search_rom[] = { /* SEARCH_ROM */ };
static const uint8_t rope_rom[] = { /* ROPE_ROM */ };
static const uint8_t horizontal_cull_rom[] = { /* CULL_ROM */ };
int Dkc1CoopEnabled(void) { return enabled; }
void Dkc1CoopRememberAnimalBonus(struct CpuState *c) {}
void Dkc1CoopActorAllocated(uint16_t h) {}
void Dkc1CoopActorCreated(CpuState *c,uint16_t h) {}
void Dkc1CoopCrateHit(const uint8_t *r,uint16_t h) {}
uint16_t Dkc1CoopCompanionBinding(void){return 0;}
void Dkc1CoopRestoreCompanionBinding(uint16_t b) {}

uint16_t Dkc1CoopRopeClimbCameraY(const uint8_t *r,uint16_t v) {
  return enabled?0xa8:v;
}
int Dkc1CoopIndependentDamage(CpuState *c) { return cull; }
int Dkc1CoopIndependentFall(CpuState *c) { return cull; }
int Dkc1CoopPrepareMountedPartner(CpuState *c) { return 0; }
void Dkc1CoopRememberRoomMount(CpuState *c) {}
void Dkc1CoopSaveCarriedMount(CpuState *c) { c->Y=4; }
void Dkc1CoopRestoreCarriedMount(CpuState *c) { c->Y=2; }
void Dkc1CoopRestoreRoomMount(CpuState *c) { c->Y=4; }
void Dkc1CoopFinishMountedPartner(uint8_t *ram) {}
int Dkc1CoopRestoreMountedExit(CpuState *c) { return 0; }
int Dkc1CoopBlockTeamSwap(CpuState *c) { return 0; }
int Dkc1CoopBlockRopeDeparture(CpuState *c) { return cull; }
int Dkc1CoopRopeWaitCamera(CpuState *c) { return cull; }
int Dkc1CoopBlockWheelDeparture(CpuState *c) { return cull; }
int Dkc1CoopBlockCroctopusDeparture(CpuState *c) { return cull; }
void Dkc1CoopRememberRopeContact(CpuState *c) { c->Y=4; }
void Dkc1CoopSelectMapAvatar(CpuState *c) { c->Y=4; }
void Dkc1CoopPublishMapPads(CpuState *c) { c->Y=4; }
uint16_t Dkc1CoopNativePadOwner(CpuState *c,uint16_t native) { return native^1u; }
int Dkc1CoopAnimalMountOccupied(CpuState *c,uint16_t r,uint16_t a) {
  return r==4 && a==6;
}
bool RtlSpeculativeExecutionActive(void) { return speculative != 0; }
uint32_t RtlSpeculativeExecutionViolation(void) { return 0; }
int Dkc1CoopScheduleActor(CpuState *c,uint32_t target,uint32_t ret) { return 0; }
void Dkc1CoopResumeActor(uint8_t *ram,uint32_t pc,uint16_t sp) { ++resumes; }
void Dkc1CoopPublishPickupBounds(CpuState *cpu) {}
void Dkc1CoopFinishBarrelRescue(CpuState *cpu) {}
void Dkc1CoopHudBegin(CpuState *c) { ++hud_begins; }
void Dkc1CoopHudEnd(CpuState *c) { ++hud_ends; }
void Dkc1CoopHudSpriteBegin(CpuState *c) { ++hud_begins; }
void Dkc1CoopHudSpriteEnd(CpuState *c) { ++hud_ends; }
void Dkc1CoopGraphicsSeedNative(void) {}
void Dkc1CoopGraphicsReset(void) {}
void Dkc1CoopGraphicsResetEntity(uint16_t h) { allocated_handle=h; }
int Dkc1CoopGraphicsAllocate(CpuState *c,int n) { ++resource_calls; c->A=0x80; return 1; }
int Dkc1CoopGraphicsRelease(CpuState *c,uint16_t h) { released_handle=h; c->Y=0xface; return 1; }
int Dkc1CoopGraphicsCapacity(CpuState *c) { return 1; }
int Dkc1CoopGraphicsCancelKongPaletteUpload(CpuState *c) { return 1; }
int Dkc1CoopGraphicsAllocatePalette(CpuState *c,uint32_t p) { return 1; }
int Dkc1CoopGraphicsReplacePalette(CpuState *c,int n) { return 1; }
int Dkc1CoopGraphicsReleasePalette(CpuState *c,uint32_t p) { return 1; }
void Dkc1CoopGraphicsCopyPalette(uint16_t a,uint16_t b) { copied_from=a; copied_to=b; }
void Dkc1CoopGraphicsUseFixedPalette(uint16_t h) { fixed_handle=h; }
int Dkc1CoopWorldKeepActor(CpuState *c,uint16_t r) { return cull; }
int Dkc1CoopWorldKeepSource(CpuState *c,int g) { return cull; }
void Dkc1CoopWorldNoteInitialize(void) {}
void Dkc1CoopWorldNoteNativeCamera(const uint8_t *r) {}
uint16_t Dkc1CoopWorldCameraFocus(CpuState *c,uint16_t v) { return v; }
void Dkc1CoopWorldNoteScan(CpuState *c) {}
bool Dkc1VideoBeginPlacedActorDispatch(CpuState *c) { ++begins; return false; }
void Dkc1VideoEndPlacedActorDispatch(CpuState *c) { ++ends; }
int interp816_opcode_hook(uint32_t a) { return 0; }
uint8 cpu_read8(CpuState *c,uint8 b,uint16 a) {
  uint8 v;
  if (b==0x7e || b==0x7f) v=c->ram[((b&1)<<16)|a];
  else if ((b&0x40)==0 && a<0x2000) v=c->ram[a];
  else v=rom[((b&0x3f)<<16)|a];
  return c->open_bus=v;
}
uint16 cpu_read16(CpuState *c,uint8 b,uint16 a) {
  uint16 v=cpu_read8(c,b,a);return v|((uint16)cpu_read8(c,b,a+1)<<8);
}
void cpu_write8(CpuState *c,uint8 b,uint16 a,uint8 v) {
  if (b==0x7f) c->ram[0x10000+a]=v; else c->ram[a]=v;
  c->open_bus=v;
}
void cpu_write16(CpuState *c,uint8 b,uint16 a,uint16 v) {
  cpu_write8(c,b,a,(uint8)v);cpu_write8(c,b,a+1,(uint8)(v>>8));
}
static uint8_t Read(void *m,uint32_t a) { return Dkc1CoopInterpRead8(m,a>>16,(uint16)a); }
static void Write(void *m,uint32_t a,uint8_t v) { Dkc1CoopInterpWrite8(m,a>>16,(uint16)a,v); }
static void Opcode(Interp816 *in,uint32_t pc,uint8_t op,uint16_t operand) {
  in->k=pc>>16;in->pc=(uint16_t)pc;uint32_t o=pc&0x3fffff;
  rom[o]=op;rom[o+1]=(uint8_t)operand;rom[o+2]=(uint8_t)(operand>>8);
}
int main(void) {
  CpuState c; Interp816 in;
  memset(&c,0,sizeof c);memset(&in,0,sizeof in);c.ram=ram;
  in.mem=&c;in.read=Read;in.write=Write;in.sp=0x1ff;
  Dkc1CoopEntitiesReset();Dkc1CoopInterpSetRom(rom,sizeof rom);
  cull=1;assert(CoopEventRedirect(&c,0xbfee82)==0xbfee7f);
  cull=0;assert(!CoopEventRedirect(&c,0xbfee82));
  cull=1;assert(CoopEventRedirect(&c,0xbf951b)==0xbf9526);
  cull=0;assert(!CoopEventRedirect(&c,0xbf951b));
  cull=1;assert(CoopEventRedirect(&c,0xb6f4e9)==0xb6f4ef);
  cull=0;assert(!CoopEventRedirect(&c,0xb6f4e9));
  c.Y=2;assert(!CoopEventRedirect(&c,0xbfef51) && c.Y==4);
  c.Y=2;assert(!CoopEventRedirect(&c,0x80cf2d) && c.Y==4);
  c.Y=2;assert(!CoopEventRedirect(&c,0x80c1c8) && c.Y==4);
  /* Disabled residual hooks retain CPU, bus, flags and guest memory. Actor
   * resume remains the existing outer bookkeeping operation. */
  {
    const uint32_t pcs[]={ /* RESIDUAL_PCS */ };
    c.A=0x2345;c.X=0x7e;c.Y=0x7e;c.S=0x1ff;c.open_bus=0x5a;
    c.P=0xa3;c._flag_C=c._flag_N=1;c._flag_Z=0;
    CpuState before=c;
    for(unsigned i=0;i<sizeof pcs/sizeof pcs[0];i++) {
      assert(!Dkc1CoopInterpBefore(&c,pcs[i]));
      assert(!memcmp(&c,&before,sizeof c));
    }
  }
  enabled=1;
  /* Fallback allocation and its successful native notification remain
   * separate boundaries, with the same register and bus effects. */
  for(int i=0;i<2;i++) {
    Dkc1CoopEntitiesReset();allocated_handle=0;c.open_bus=0x5a;
    assert(Dkc1CoopInterpBefore(&c,i?0xbdf3d2:0xbdf3b1)==(i?0xbdf3d6:0xbdf3b5));
    assert(c.X==0x80 && !allocated_handle && c.open_bus==0x5a);
    assert(!Dkc1CoopInterpBefore(&c,i?0xbdf3d6:0xbdf3b5));
    assert(allocated_handle==0x80 && c.open_bus==0x5a);
  }
  Dkc1CoopEntityWrite16(ram,0xd45,0x80,7);
  c.D=0xd45;c.X=0;c.A=0x34;c.open_bus=0x5a;
  assert(!Dkc1CoopInterpBefore(&c,0xbcb78d));
  assert(!Dkc1CoopEntityRead16(ram,0xd45,0x80) && c.A==0x34 && c.X==0 && c.open_bus==0x5a);
  c.D=0;
  /* Backward substitution follows native DEX/DEY; it does not recreate
   * their flags or touch the cartridge bus. */
  for(int i=0;i<3;i++) {
    const uint32_t pcs[]={0xb6e78d,0xbcadc4,0xb6e79b};
    c.X=c.Y=0x7e;c.P=0xa3;c._flag_C=c._flag_N=1;c._flag_Z=0;c.open_bus=0x5a;
    assert(!Dkc1CoopInterpBefore(&c,pcs[i]));
    assert((i==2?c.Y:c.X)==0x32 && c.P==0xa3 && c._flag_C && c._flag_N && !c._flag_Z && c.open_bus==0x5a);
  }
  for(int i=0;i<2;i++) {
    c.A=0x1e;c.P=0xa3;c._flag_C=c._flag_N=1;c._flag_Z=0;c.open_bus=0x5a;
    assert(!Dkc1CoopInterpBefore(&c,i?0xbba668:0xbba592));
    assert(c.A==0x100 && c.P==0xa3 && c._flag_C && c._flag_N && !c._flag_Z && c.open_bus==0x5a);
  }
  c.Y=0x80;assert(Dkc1CoopInterpBefore(&c,0x81d894)==0x81d8ae);
  assert(c.Y==0x80 && released_handle==0x80);
  ram[0x86]=0x82;ram[0x87]=0;ram[0x82]=4;ram[0x83]=0;c.open_bus=0x5a;
  for(int i=0;i<2;i++) {
    copied_from=copied_to=0;
    assert(!Dkc1CoopInterpBefore(&c,i?0xb582bf:0xb5821e));
    assert(copied_from==0x82 && copied_to==4 && c.open_bus==0x5a);
  }
  assert(!Dkc1CoopInterpBefore(&c,0xb582a5));
  assert(fixed_handle==0x82 && c.open_bus==0x5a);
  cull=1;assert(Dkc1CoopInterpBefore(&c,0xbfa3f9)==0xbfa40d);
  assert(Dkc1CoopInterpBefore(&c,0xbfa6d1)==0xbfa6fc);
  cull=0;assert(!Dkc1CoopInterpBefore(&c,0xbfa6d1));
  for(int i=0;i<5;i++) {
    const uint32_t pcs[]={0x80a1ae,0x80a1b9,0x80a1e6,0xbdfdbd,0xbdfe67};
    assert(CoopPrivateBoundary(pcs[i]) && !CoopPrivateBoundary(pcs[i]+1));
  }
  Dkc1CoopEntitiesPoolReset();enabled=0;
  /* Exact stock rope SBC operand: preserve arithmetic, change only ceiling. */
  for(int coop=0;coop<2;coop++)for(int adjacent=0;adjacent<2;adjacent++) {
    enabled=coop; in.db=0x80;in.a=0x200;in.c=true;
    ram[0x895]=0x10;ram[0x896]=1;
    Opcode(&in,0xbfa0a9+adjacent,0xed,0x0895);
    Dkc1CoopInterpRunOpcode(&in);
    assert(in.a==(coop && !adjacent?0x158:0xf0) && in.c && !in.n && !in.z);
    assert(c.open_bus==1); /* original camera high-byte read, same as AOT */
  }
  enabled=0;in.db=0;Dkc1CoopEntitiesReset();
  in.x=0x80;ram[0xb99]=0x67;ram[0xb9a]=0x45;
  Opcode(&in,0xbf9000,0xbd,0x0b19);
  assert(Dkc1CoopInterpRunOpcode(&in)>0 && in.a==0x4567);
  assert(!Dkc1CoopInterpBefore(&c,0xbdf346)); assert(!resource_calls);
  enabled=1;
  Dkc1CoopEntityWrite16(ram,0x0b19,0x80,0x1234);
  Opcode(&in,0xbf9000,0xbd,0x0b19);
  Dkc1CoopInterpRunOpcode(&in);assert(in.a==0x1234 && c.open_bus==0x12);
  assert(ram[0xb99]==0x67);
  /* The native input-owner operand is intercepted before ASL/TAY on
   * either HiROM mirror; the original turn word remains untouched. */
  in.dp=0;in.db=0;in.mf=false;ram[0x44]=ram[0x45]=0;
  Opcode(&in,0xbfb2c7,0xa5,0x44);
  Dkc1CoopInterpRunOpcode(&in);assert(in.a==1 && !in.z && !in.n);
  assert(!ram[0x44] && !ram[0x45]);
  Opcode(&in,0x3fb2c7,0xa5,0x44);
  Dkc1CoopInterpRunOpcode(&in);assert(in.a==1);
  /* Bank 7F and ROM banks must never select the host actor continuation. */
  in.db=0x7f;ram[0x10b99]=0xab;ram[0x10b9a]=0xcd;
  Opcode(&in,0xbf9000,0xbd,0x0b19);Dkc1CoopInterpRunOpcode(&in);assert(in.a==0xcdab);
  in.db=0xc0;rom[0xb99]=0x78;rom[0xb9a]=0x56;
  Opcode(&in,0xbf9000,0xbd,0x0b19);Dkc1CoopInterpRunOpcode(&in);assert(in.a==0x5678);
  in.db=0x80; in.mf=true;in.a=0x5500;
  Dkc1CoopEntityWrite16(ram,0x0c69,0x80,0xab34);
  Opcode(&in,0xbf9000,0xbd,0x0c6a);Dkc1CoopInterpRunOpcode(&in);
  assert(in.a==0x55ab && in.n && !in.z);
  in.mf=false;Dkc1CoopEntityWrite16(ram,0x0b19,0x80,0xffff);
  Opcode(&in,0xbf9000,0xfe,0x0b19);Dkc1CoopInterpRunOpcode(&in);
  assert(Dkc1CoopEntityRead16(ram,0x0b19,0x80)==0 && in.z);
  assert(ram[0xb99]==0x67 && ram[0xb9a]==0x45);
  /* Real initializer opcode uses D as the field rather than a bus address. */
  in.dp=0xbc1;in.a=0x8001;
  Opcode(&in,0xb58068,0x95,0);Dkc1CoopInterpRunOpcode(&in);
  assert(Dkc1CoopEntityRead16(ram,0xbc1,0x80)==0x8001);
  in.dp=0;in.x=0xbc1+0x80;in.a=0x9999;ram[0x50]=0x80;
  Opcode(&in,0xb6e868,0x95,0);Dkc1CoopInterpRunOpcode(&in);
  assert(Dkc1CoopEntityRead16(ram,0xbc1,0x80)==0x9999);
  Dkc1CoopEntityWrite16(ram,0x0d45,0x80,7);
  c.X=0x34;assert(!Dkc1CoopInterpBefore(&c,0xbf8037));assert(c.X==0x80);
  in.x=c.X;Opcode(&in,0xbf8037,0xe0,0x34);Dkc1CoopInterpRunOpcode(&in);
  assert(!in.c && !in.z); /* CPX #$100, not the original #$34 */
  Dkc1CoopEntityWrite16(ram,0x0d45,0x80,0);
  c.X=0x34;Dkc1CoopInterpBefore(&c,0xbf8037);assert(c.X==0x100);
  c.X=0x34;Dkc1CoopInterpBefore(&c,0xbf848f);assert(c.X==0x80);
  assert(Dkc1CoopInterpBefore(&c,0x3df346)==0x3df36d && c.A==0x80);
  ram[0x82]=4;c.A=6;
  assert(Dkc1CoopInterpBefore(&c,0xbf88b4)==0xbf88ab);
  ram[0x82]=6;ram[0x88]=4;
  assert(Dkc1CoopInterpBefore(&c,0xbfcc4d)==0xbfcc31);
  ram[0x88]=2;
  assert(!Dkc1CoopInterpBefore(&c,0xbfcc4d));
  c.Y=2;assert(!Dkc1CoopInterpBefore(&c,0x8083a8) && c.Y==4);
  c.Y=2;assert(!Dkc1CoopInterpBefore(&c,0xbf9ec2) && c.Y==4);
  c.Y=4;assert(!Dkc1CoopInterpBefore(&c,0xb8840c) && c.Y==2);
  cull=1;assert(Dkc1CoopInterpBefore(&c,0xbdf74f)==0xbdf735);
  assert(Dkc1CoopInterpBefore(&c,0xbcbc75)==0xbcbc73);
  assert(Dkc1CoopInterpBefore(&c,0xbcbc96)==0xbcbc94);
  cull=0;assert(!Dkc1CoopInterpBefore(&c,0xbcbc75));
  assert(!Dkc1CoopInterpBefore(&c,0xbcbc96));cull=1;
  assert(Dkc1CoopInterpBefore(&c,0xbfa1d4)==0xbfa1ef);
  assert(Dkc1CoopInterpBefore(&c,0xbdf570)==0xbdf561);
  Dkc1CoopInterpBefore(&c,0xbf802f);Dkc1CoopInterpBefore(&c,0xbf8033);
  assert(begins==1 && ends==1);
  Dkc1CoopInterpBefore(&c,0x80a1ae);Dkc1CoopInterpBefore(&c,0x80a1b9);
  assert(hud_begins==1 && hud_ends==1 && resumes>0);
  int previous_resumes=resumes;speculative=1;
  Dkc1CoopInterpBefore(&c,0xbf9000);assert(resumes==previous_resumes);speculative=0;

  /* Execute the stock collision scanner against one extended Kritter.
   * An otherwise identical second-pool actor must remain excluded. */
  if (sizeof search_rom > 1) {
    memcpy(rom+0x3b8000,search_rom,sizeof search_rom);
    for (int scanner=0;scanner<2;scanner++) {
    for (int adapted=0;adapted<2;adapted++) {
      memset(ram,0,sizeof ram); Dkc1CoopEntitiesReset(); enabled=1;
      assert(Dkc1CoopEntityAllocate(&c,0x1e)); uint16_t enemy=c.X;
      Dkc1CoopEntityWrite16(ram,0xd45,enemy,5);
      Dkc1CoopEntityWrite16(ram,0x11a1,enemy,0x27);
      Dkc1CoopEntityWrite16(ram,0xd11,enemy,0x1df8);
      Dkc1CoopEntityWrite16(ram,0xb19,enemy,0x3100);
      Dkc1CoopEntityWrite16(ram,0xbc1,enemy,0xc0);
      assert(Dkc1CoopEntityAllocate(&c,0x34)); uint16_t excluded=c.X;
      for (int field=0;field<5;field++) {
        const uint16_t fields[]={0xd45,0x11a1,0xd11,0xb19,0xbc1};
        Dkc1CoopEntityWrite16(ram,fields[field],excluded,
          Dkc1CoopEntityRead16(ram,fields[field],enemy));
      }
      cpu_write16(&c,0,0xa6,0x30e0);cpu_write16(&c,0,0xaa,0x3120);
      cpu_write16(&c,0,0xa8,0xf0);cpu_write16(&c,0,0xac,0xa0);
      cpu_write16(&c,0,0xb6,4);
      memset(&in,0,sizeof in);in.mem=&c;in.read=Read;in.write=Write;
      in.sp=0x1ff;in.k=0xbb;in.a=0x22;in.pc=scanner?0xa663:0xa58d;
      /* Fixed-box variant receives the same cartridge hitbox table in Y. */
      if (scanner) in.y=rom[0x3b8000+0x1df8/2]|
                                  (rom[0x3b8001+0x1df8/2]<<8);
      unsigned count=0;
      while (in.pc!=0xa60e && in.pc!=0xa611 && in.pc!=0xa6da && in.pc!=0xa6dd) {
        assert(++count<2000);
        CoopPrivateToCpu(&in,&c);
        if (adapted)
          assert(!Dkc1CoopInterpBefore(&c,((uint32_t)in.k<<16)|in.pc));
        CoopPrivateFromCpu(&c,&in);
        Dkc1CoopInterpRunOpcode(&in);
      }
      assert(adapted ? (in.c && cpu_read16(&c,0,0x88)==enemy) :
                        (!in.c && cpu_read16(&c,0,0x88)==0));
    }
    }
  }

  /* The actual BC culls return SEC outside the primary scanner window.
   * A secondary-visible actor must instead execute the native CLC/RTS,
   * including its ordinary return-frame consumption. */
  if (sizeof horizontal_cull_rom > 1) {
    memcpy(rom+0x3cbc63,horizontal_cull_rom,sizeof horizontal_cull_rom);
    for(int extra=0;extra<2;extra++)for(int high=0;high<2;high++)
    for(int expanded=0;expanded<2;expanded++)
    for(int adapted=0;adapted<2;adapted++){
      memset(ram,0,sizeof ram);memset(&in,0,sizeof in);enabled=1;cull=adapted;
      uint16_t actor=extra?0x80:6;
      cpu_write16(&c,0,0x82,actor);cpu_write16(&c,0,0xef,high?0x80e0:2000);
      cpu_write16(&c,0,0xf1,high?0x8220:2300);
      Dkc1CoopEntityWrite16(ram,0xb19,actor,high?0x8000:1100);
      ram[0x1f1]=0xff;ram[0x1f2]=0x7f;
      in.mem=&c;in.read=Read;in.write=Write;in.k=in.db=0xbc;
      in.pc=expanded?0xbc77:0xbc63;in.sp=0x1f0;
      unsigned steps=0;
      while(in.pc!=0x8000){
        assert(++steps<32);CoopPrivateToCpu(&in,&c);
        if(adapted){uint32_t next=Dkc1CoopInterpBefore(&c,((uint32_t)in.k<<16)|in.pc);
          if(next){in.pc=(uint16_t)next;in.k=next>>16;}}
        CoopPrivateFromCpu(&c,&in);Dkc1CoopInterpRunOpcode(&in);
      }
      assert(in.c==(!adapted && !(expanded && high)) && in.sp==0x1f2);
    }
  }
  /* Execute the complete clean-ROM clamp, including moving-rope velocity
   * and the native carry/borrow behavior. Downward travel remains unrestricted. */
  if(sizeof rope_rom>1) {
    memcpy(rom+0x3fa095,rope_rom,sizeof rope_rom);
    for(int coop=0;coop<2;coop++)for(int rider=2;rider<=4;rider+=2)
    for(int climb=-1;climb<=1;climb++)for(int motion=-2;motion<=2;motion+=2) {
      memset(ram,0,sizeof ram);memset(&in,0,sizeof in);enabled=coop;
      in.mem=&c;in.read=Read;in.write=Write;in.k=0xbf;in.db=0x80;
      in.pc=0xa095;in.sp=0x1ff;
      cpu_write16(&c,0,0x82,rider);cpu_write16(&c,0,0x1b01,6);
      cpu_write16(&c,0,0x14cb,(uint16_t)motion);
      cpu_write16(&c,0,0xf8d+rider,(uint16_t)climb);
      cpu_write16(&c,0,0xbc1+rider,0x180);
      cpu_write16(&c,0,0x4a,0x200);cpu_write16(&c,0,0x895,0x10);
      unsigned steps=0;
      while(in.pc!=0xa0b2 && in.pc!=0xa0bd) {
        assert(++steps<32);Dkc1CoopInterpRunOpcode(&in);
      }
      bool clamp=coop && climb+motion>=0;
      unsigned carry=(unsigned)(uint16_t)motion+(unsigned)(uint16_t)climb>0xffffu;
      assert(cpu_read16(&c,0,0xbc1+rider)==(clamp?0x12f+carry:0x180));
      assert(cpu_read16(&c,0,0xf8d+rider)==(clamp?0:(uint16_t)climb));
      assert(cpu_read16(&c,0,0x895)==0x10);
    }
  }
  enabled=0;assert(Dkc1CoopEntitiesActive());Dkc1CoopEntitiesPoolReset();
  assert(!Dkc1CoopEntitiesActive());
  return 0;
}
'''
        rom_path = ROOT / 'dkc1.sfc'
        data = rom_path.read_bytes()[0x3b8000:0x3ba6ef] if rom_path.exists() else bytes(1)
        source = source.replace('/* SEARCH_ROM */', ','.join(str(b) for b in data))
        data = rom_path.read_bytes()[0x3cbc63:0x3cbc98] if rom_path.exists() else bytes(1)
        source = source.replace('/* CULL_ROM */', ','.join(str(b) for b in data))
        data = rom_path.read_bytes()[0x3fa095:0x3fa0be] if rom_path.exists() else bytes(1)
        source = source.replace('/* ROPE_ROM */', ','.join(str(b) for b in data))
        catalog = prepare.world.COOP_CATALOG
        pcs = [int(h["pc"], 16) for key in ("entity_hooks", "resource_hooks")
               for h in catalog[key]]
        pcs += [int(h["pc"], 16) for h in catalog["hooks"] if h["group"] == "scanner"]
        pcs += [pc for _owner, call, _target, ret, overlay in prepare.schedule.CALLS
                if overlay for pc in (call, ret)]
        source = source.replace('/* RESIDUAL_PCS */', ','.join(hex(pc) for pc in pcs))
        # Compile current rules in memory so stale private generated output
        # cannot mask a changed source-owned hook boundary.
        interpreter = (ROOT / "runner/dkc1_coop_interp.c").read_text()
        interpreter = interpreter.replace(
            '#include "../generated/host/dkc1_coop_interp_rules.inc"',
            prepare.rules(rom_path.read_bytes()))
        source = source.replace('#include "dkc1_coop_interp.c"', interpreter)
        native_contract.CoopModTests.compile_harness(self, source,
            ["runner/dkc1_coop_entities.c", "snesrecomp/runner/src/snes/interp816.c"])
