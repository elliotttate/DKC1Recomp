from pathlib import Path
import importlib.util
import re
import unittest

import test_coop_mod

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location(
    "coop_hud_adapter", ROOT / "scripts/apply_dkc1_widescreen_overrides.py")
ADAPTER = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(ADAPTER)


class CoopHudTests(unittest.TestCase):
    def test_historical_hud_bytes_and_missing_graphics_defaults(self):
        game = (ROOT / "runner/dkc1_game.c").read_text()
        load = game.split("static bool Dkc1LoadWidescreenSnapshot(SaveLoadInfo *sli) {", 1)[1]
        size = re.search(r"size_t hud_size = [^;]+;", load).group()
        versions = re.search(r"enum \{\s+kDkc1WsSaveMagic[^}]+\};", game).group()
        source = HARNESS[:HARNESS.index("int main(void)")] + versions + """
static size_t HostHudBytes(unsigned version) {
  struct { unsigned version; } snapshot = {version};
  Dkc1CoopHudSnapshot hud;
  """ + size + "\nreturn hud_size;\n}\n" + HUD_WIRE_HARNESS
        test_coop_mod.CoopModTests().compile_harness(source)

    def test_capture_rebase_owner_filter_and_paused_save_state(self):
        rom = ROOT / "dkc1.sfc"
        if rom.exists():
            data = rom.read_bytes()
            self.assertEqual(data[0x9a55:0x9a63],bytes.fromhex(
                "a4 dd f0 0a a2 a0 10 a9 10 0e 22 72 a7 bb"))
            self.assertEqual(data[0x3f8443:0x3f844e],bytes.fromhex(
                "a6 82 86 dd a9 00 50 9d c1 0b 6b"))
        test_coop_mod.CoopModTests().compile_harness(HARNESS)

    def test_hooks_are_after_deadline_idempotent_and_fail_closed(self):
        text = '#include "funcs.h"\nRecompReturn OAM_BeginFrameAndDrawHUD_M0X0(CpuState *cpu) {\n'
        for label in ("A1AE", "A1B9"):
            text += f'  L_{label}_M0X0:\n    if (deadline) return RECOMP_RETURN_YIELD;\n'
            text += '    cpu->coprocessor_master_cycles = cpu->master_cycles;\n'
            text += '    cpu->cycles += 6;\n'
        text += '  return RECOMP_RETURN_NORMAL;\n}\n'
        text += 'RecompReturn CODE_BBA772_M0X0(CpuState *cpu) {\n'
        text += '  L_A772_M0X0:\n    if (deadline) return RECOMP_RETURN_YIELD;\n'
        text += '    cpu->coprocessor_master_cycles = cpu->master_cycles;\n'
        text += ('    { uint16 _ret_s = cpu->S;  /* RTL pop hardware return frame */ }\n' * 3)
        text += '}\n'
        sources = {Path("hud.c"): text}
        ADAPTER.adapt_coop_hud(sources)
        first = dict(sources)
        ADAPTER.adapt_coop_hud(sources)
        self.assertEqual(first, sources)
        for helper in ("Begin", "End"):
            self.assertIn('cpu->master_cycles;\n    Dkc1CoopHud' + helper, sources[Path("hud.c")])
        self.assertEqual(sources[Path("hud.c")].count('Dkc1CoopHudSpriteEnd(cpu);'), 3)
        self.assertIn('cpu->master_cycles;\n    Dkc1CoopHudSpriteBegin', sources[Path("hud.c")])
        for label in ("A1AE", "A1B9"):
            bad = {Path("hud.c"): text.replace(f'  L_{label}_M0X0:', '')}
            with self.assertRaisesRegex(ValueError, "expected one"):
                ADAPTER.adapt_coop_hud(bad)


HUD_WIRE_HARNESS = r'''
static void ByteWord(uint8_t *p,unsigned a,unsigned v) {
  p[a]=(uint8_t)v;p[a+1]=(uint8_t)(v>>8);
}
int main(void) {
  uint8_t raw[12864],expected[12864];
  Dkc1CoopHudSnapshot decoded,actual;
  CHECK(sizeof decoded==12864 && offsetof(Dkc1CoopHudSnapshot,graphics)==568 &&
      offsetof(Dkc1CoopHudSnapshot,capture)==6716,"HUD wire offsets changed");
  const unsigned host_lengths[]={568,568,12864,12864,12864,12864,12864};
  for(unsigned version=4;version<=10;version++)
    CHECK(HostHudBytes(version)==host_lengths[version-4],"DWS8 HUD prefix changed");
  for(unsigned version=1;version<=2;version++) {
    for(unsigned i=0;i<12864;i++) raw[i]=(uint8_t)(i*23+5);
    ByteWord(raw,0,version);ByteWord(raw,6,3);ByteWord(raw,8,2);
    /* One completed batch and one pending capture; all unused bytes persist. */
    ByteWord(raw,568,1);ByteWord(raw,570,0);
    ByteWord(raw,572,3);ByteWord(raw,574,2);ByteWord(raw,576,2);
    ByteWord(raw,6716,0);ByteWord(raw,6718,1);
    ByteWord(raw,6720,5);ByteWord(raw,6722,1);ByteWord(raw,6724,4);
    memset(&decoded,0,sizeof decoded);memcpy(&decoded,raw,version==1?568:12864);
    memcpy(expected,&decoded,12864);ByteWord(expected,0,2);
    Dkc1CoopHudLoadState(&decoded);Dkc1CoopHudSaveState(&actual);
    CHECK(!memcmp(&actual,expected,12864),"historical HUD bytes/defaults changed");
  }
  /* Nested v1 ignores graphics even when the enclosing host supplied them. */
  ByteWord(raw,0,1);memcpy(&decoded,raw,12864);
  memcpy(expected,raw,568);memset(expected+568,0,12296);ByteWord(expected,0,2);
  Dkc1CoopHudLoadState(&decoded);Dkc1CoopHudSaveState(&actual);
  CHECK(!memcmp(&actual,expected,12864),"HUD v1 invented graphics history");
  ByteWord(raw,0,2);ByteWord(raw,6716,128);ByteWord(raw,6718,1);
  memcpy(&decoded,raw,12864);memset(expected,0,12864);ByteWord(expected,0,2);
  Dkc1CoopHudLoadState(&decoded);Dkc1CoopHudSaveState(&actual);
  CHECK(!memcmp(&actual,expected,12864),"HUD malformed capture reset changed");
  return 0;
}
'''

HARNESS = r'''
#include "dkc1_coop_hud.c"
#include <stdio.h>
static int enabled=1, speculative, retained;
int Dkc1CoopEnabled(void) { return enabled; }
int Dkc1CoopEntitiesActive(void) { return enabled || retained; }
bool RtlSpeculativeExecutionActive(void) { return speculative != 0; }
unsigned Dkc1CoopEffectView(uint16_t h) { return h==0x36; }
int Dkc1CoopEntityIsHandle(uint16_t h) { return h>=2 && h<=0x32 && !(h&1); }
uint16_t Dkc1CoopEntityRead16(const uint8_t *r,uint16_t a,uint16_t h) { return Word(r,a+h); }
int Dkc1CoopGraphicsGetPalette(uint16_t h,uint16_t *p) {
  for(unsigned i=0;i<16;i++) p[i]=(uint16_t)(h*16+i);
  return 1;
}
int Dkc1CoopWorldGetCamera(const uint8_t *r,int view,uint16_t *x,uint16_t *y) {
  *x = view ? 110 : 100; *y = view ? 23 : 20; return 1;
}
static uint8_t ram[0x20000], before[0x20000], oam[512], high[32];
static void wr(unsigned a, unsigned v) { ram[a]=(uint8_t)v; ram[a+1]=(uint8_t)(v>>8); }
#define CHECK(c,m) do { if(!(c)) { puts(m); return 1; } } while(0)
int main(void) {
  CpuState cpu={0}; cpu.ram=ram;
  wr(0x32,4); wr(0x3e,7); wr(0x28,15); wr(0x8e,0x20c);
  /* Three effects precede the HUD; high bits cross a packed-byte boundary. */
  Dkc1CoopHudBegin(&cpu);
  for(unsigned i=0;i<2;i++) {
    unsigned a=0x20c+i*4;
    ram[a]=(uint8_t)(220+i); ram[a+1]=(uint8_t)(10+i);
    wr(a+2,0x4321+i);
  }
  ram[0x400]=0xc0; ram[0x401]=1;
  wr(0x8e,0x214); Dkc1CoopHudEnd(&cpu);
  wr(0x0d45+0x34,0x42); wr(0x0b19+0x34,0x1400); wr(0x0bc1+0x34,0x1800); wr(0x0c69+0x34,0x31e0);
  wr(0x0d45+0x36,0x42); wr(0x0b19+0x36,0x2800); wr(0x0bc1+0x36,0x3800); wr(0x0c69+0x36,0x31e2);
  wr(0x0d45+0x38,1); wr(0x0b19+0x38,0x4000); wr(0x0bc1+0x38,0x5000); wr(0x0c69+0x38,0x3150);
  memcpy(before,ram,sizeof ram);
  CHECK(Dkc1CoopHudBuildOam(ram,0,oam,high)==4,"P1 OAM count");
  CHECK(oam[0]==20 && oam[1]==24 && oam[4]==64,"P1 effect owner/order");
  CHECK(oam[8]==220 && oam[12]==221 && high[0]==0x7a,"HUD rebase lost size/X-high");
  CHECK(Dkc1CoopHudBuildOam(ram,1,oam,high)==4,"P2 OAM count");
  CHECK(oam[0]==40 && oam[1]==56 && oam[4]==54 && oam[5]==77,"P2 effect owner/order");
  CHECK(oam[17]==0xf0 && !memcmp(ram,before,sizeof ram),"HUD redraw changed game or left visible tail");
  /* A previous KONG letter is born in state2 with stale pool coordinates.
   * BFD4F5 initializes its screen placement on the following tick. */
  wr(0xd45+0x3a,0x41);wr(0x1029+0x3a,2);
  wr(0xb19+0x3a,0x8adc);wr(0xbc1+0x3a,0x4901);wr(0xc69+0x3a,0x302a);
  CHECK(Dkc1CoopHudBuildOam(ram,0,oam,high)==4,
        "uninitialized KONG letter flashed stale pool coordinates");
  wr(0x1029+0x3a,4);wr(0xb19+0x3a,0x6c00);wr(0xbc1+0x3a,0x0800);
  CHECK(Dkc1CoopHudBuildOam(ram,0,oam,high)==5 && oam[8]==108 && oam[9]==8,
        "initialized KONG letter lost its native animation");
  CHECK(Dkc1CoopHudBuildOam(ram,1,oam,high)==4,
        "Donkey's KONG animation leaked into Diddy's view");
  wr(0xd45+0x3a,0);wr(0xd45+0x36,0x41);wr(0x1029+0x36,5);
  wr(0xb19+0x36,0x6c00);wr(0xbc1+0x36,0x4600);
  CHECK(Dkc1CoopHudBuildOam(ram,1,oam,high)==4 && oam[0]==108 && oam[1]==70,
        "Diddy's letter flight was rebased away from his screen");
  CHECK(Dkc1CoopHudBuildOam(ram,0,oam,high)==4,
        "Diddy's letter flight leaked into Donkey's view");
  enabled=0;retained=1;
  /* Retained effect storage must not hide Diddy's stock single-screen flight. */
  CHECK(Dkc1CoopHudBuildOam(ram,0,oam,high)==5 && oam[4]==108 && oam[5]==70,
        "retained ownership hid Diddy's single-screen flight when co-op was disabled");
  enabled=1;retained=0;
  wr(0xd45+0x36,0x42);
  wr(0xd45+0x3a,0);
  Dkc1CoopHudSnapshot saved, after;
  Dkc1CoopHudSaveState(&saved);
  Dkc1CoopHudEnd(&cpu); Dkc1CoopHudSaveState(&after);
  CHECK(!memcmp(&saved,&after,sizeof saved),"repeated native boundary lost completed capture");
  Dkc1CoopHudReset();
  CHECK(Dkc1CoopHudBuildOam(ram,1,oam,high)==-1,"reset reused stale HUD");
  Dkc1CoopHudLoadState(&saved);
  CHECK(Dkc1CoopHudBuildOam(ram,1,oam,high)==4,"paused load lost native HUD");
  speculative=1; wr(0x8e,0x200); Dkc1CoopHudBegin(&cpu); Dkc1CoopHudEnd(&cpu);
  Dkc1CoopHudSaveState(&after);
  CHECK(!memcmp(&saved,&after,sizeof saved),"private pass overwrote native HUD capture");
  speculative=0;
  /* Two HUD icons reuse tile/palette addresses. Each captured OAM range
   * must retain its own pose/palette even after the actor is freed/reused. */
  wr(0x8e,0x200); Dkc1CoopHudBegin(&cpu);
  for(unsigned h=2;h<=4;h+=2) {
    cpu.Y=(uint16_t)h; cpu.X=(uint16_t)(0x1000+h); cpu.A=0x1200;
    wr(0xd11+h,0x2000+h); wr(0xc69+h,0x3c80);
    Dkc1CoopHudSpriteBegin(&cpu);
    wr(0x8e,0x200+h*4); Dkc1CoopHudSpriteEnd(&cpu);
  }
  Dkc1CoopHudEnd(&cpu);
  memset(ram+0xd11,0,0x34);
  Dkc1CoopHudSaveState(&saved); Dkc1CoopHudReset(); Dkc1CoopHudLoadState(&saved);
  CHECK(Dkc1CoopHudBuildOam(ram,0,oam,high)==6,"HUD effects/captured icon total");
  const Dkc1CoopHudBatch *b=Dkc1CoopHudBatchForSlot(2,6);
  CHECK(b && b->pose==0x2002 && b->palette[1]==33 && b->x==0x1002,"first icon lost graphics ownership");
  b=Dkc1CoopHudBatchForSlot(4,6);
  CHECK(b && b->pose==0x2004 && b->palette[1]==65,"second icon inherited first palette");
  CHECK(!Dkc1CoopHudBatchForSlot(0,6),"effect acquired HUD owner");
  /* Native life glyph borrows logical DK's palette, even if DK's display
   * slot is Rambi and the glyph has no palette allocation of its own. */
  wr(0x8e,0x200); Dkc1CoopHudBegin(&cpu);
  cpu.Y=8; wr(0xd45+8,0x69); wr(0xc69+8,0x3c80);
  wr(0x512,6); wr(0xd45+6,9);
  Dkc1CoopHudSpriteBegin(&cpu);
  wr(0x8e,0x204); Dkc1CoopHudSpriteEnd(&cpu); Dkc1CoopHudEnd(&cpu);
  b=Dkc1CoopHudBatchForSlot(0,1);
  CHECK(b && b->palette_valid && b->palette[1]==33,
        "life glyph did not borrow logical DK palette");
  /* 809A59 draws ship sprite75 after the HUD, at fixed screen arguments.
   * Its native range must retain resources when world actors reuse tiles. */
  wr(0x32,0x0d);wr(0x3e,0x68);wr(0xdd,10);
  wr(0xd45+10,0x75);wr(0xd11+10,0x1800);wr(0xc69+10,0x2c80);
  wr(0x8e,0x200);Dkc1CoopHudBegin(&cpu);
  wr(0x8e,0x204);Dkc1CoopHudEnd(&cpu);
  wr(0x8e,0x240); /* world OAM was emitted after the HUD */
  cpu.Y=10;cpu.X=0x10a0;cpu.A=0x0e10;
  Dkc1CoopHudSaveState(&saved);
  wr(0x3e,0x69);Dkc1CoopHudSpriteBegin(&cpu);wr(0x3e,0x68);
  Dkc1CoopHudSaveState(&after);
  CHECK(!memcmp(&saved,&after,sizeof saved),"foreign direct job admitted");
  Dkc1CoopHudSpriteBegin(&cpu);
  Dkc1CoopHudSaveState(&saved);Dkc1CoopHudReset();Dkc1CoopHudLoadState(&saved);
  for(unsigned i=0;i<4;i++){ram[0x240+i*4]=100+i;ram[0x241+i*4]=70;wr(0x242+i*4,0x2c80+i);}
  wr(0x8e,0x250);Dkc1CoopHudSpriteEnd(&cpu);
  b=Dkc1CoopHudBatchForSlot(3,7);
  CHECK(b && b->pose==0x1800 && b->count==4 && b->x==0x10a0 &&
        b->y==0x0e10 && b->palette[1]==161,
        "ship direct sprite lost pose/palette after the HUD");
  CHECK(Dkc1CoopHudBuildOam(ram,0,oam,high)==7 && oam[12]==100,
        "ship direct OAM was not appended to completed HUD");
  Dkc1CoopHudSaveState(&saved);Dkc1CoopHudReset();Dkc1CoopHudLoadState(&saved);
  CHECK(Dkc1CoopHudBatchForSlot(3,7),"paused ship direct job lost ownership");
  saved.capture.count=128; saved.capture.pending=1; Dkc1CoopHudLoadState(&saved);
  CHECK(Dkc1CoopHudBuildOam(ram,0,oam,high)==-1,"malformed pending capture accepted");
  saved.version=1; Dkc1CoopHudLoadState(&saved);
  CHECK(!Dkc1CoopHudBatchForSlot(2,6),"legacy state invented graphics metadata");
  wr(0x3e,8);
  CHECK(Dkc1CoopHudBuildOam(ram,1,oam,high)==-1,"scene transition reused old HUD");
  Dkc1CoopHudBegin(&cpu);
  CHECK(Dkc1CoopHudBuildOam(ram,1,oam,high)==-1,"incomplete new-scene capture relabeled old HUD");
  wr(0x8e,0x204); Dkc1CoopHudEnd(&cpu);
  enabled=0;
  CHECK(Dkc1CoopHudBuildOam(ram,1,oam,high)==-1,"disabled mod retained private HUD");
  return 0;
}
'''
