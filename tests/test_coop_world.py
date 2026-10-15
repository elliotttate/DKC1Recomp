from pathlib import Path
import importlib.util
import re
import unittest

import test_coop_mod

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location(
    "coop_world_overrides", ROOT / "scripts/apply_dkc1_widescreen_overrides.py")
ADAPTER = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(ADAPTER)


class CoopWorldTests(unittest.TestCase):
    def test_historical_world_bytes_defaults_and_camera_migrations(self):
        game = (ROOT / "runner/dkc1_game.c").read_text()
        load = game.split("static bool Dkc1LoadWidescreenSnapshot(SaveLoadInfo *sli) {", 1)[1]
        size = re.search(r"size_t world_size = [^;]+;", load).group()
        versions = re.search(r"enum \{\s+kDkc1WsSaveMagic[^}]+\};", game).group()
        source = HARNESS[:HARNESS.index("int main(void)")] + versions + """
static size_t HostWorldBytes(unsigned version) {
  struct { unsigned version; } snapshot = {version};
  Dkc1CoopWorldSnapshot world;
  """ + size + "\nreturn world_size;\n}\n" + WORLD_WIRE_HARNESS
        test_coop_mod.CoopModTests().compile_harness(
            source, ["runner/dkc1_coop_movement.c", "snesrecomp/runner/src/snes/interp816.c"])

    def test_native_camera_culls_scanner_transactions_and_state_continuity(self):
        test_coop_mod.CoopModTests().compile_harness(
            HARNESS, ["runner/dkc1_coop_movement.c", "snesrecomp/runner/src/snes/interp816.c"])

    def test_camera_descriptors_match_completed_event_catalog(self):
        boundaries = sorted({int(h["pc"],16) for h in ADAPTER.COOP_EVENT_HOOKS
                             if h["call"] == "Dkc1CoopWorldNoteNativeCamera(cpu->ram)"})
        expected = ",".join(f"0x{x:x}" for x in boundaries)
        source = HARNESS[:HARNESS.index("int main(void)")] + f"""
int main(void) {{
 const uint32_t expected[]={{{expected}}};
 for(unsigned i=0;i<sizeof kCameraControllers/sizeof *kCameraControllers;i++) {{
  const NativeCameraController *c=kCameraControllers[i].controller;
  unsigned found=0;
  for(unsigned j=0;j<sizeof expected/sizeof *expected;j++) found|=c->completed==expected[j];
  if(!found || CameraController(kCameraControllers[i].mode)!=c) return 1;
 }}
 return CameraController(0x7fff)!=0;
}}
"""
        test_coop_mod.CoopModTests().compile_harness(
            source,["runner/dkc1_coop_movement.c","snesrecomp/runner/src/snes/interp816.c"])

    def test_water_camera_uses_cartridge_bounds_and_acceleration(self):
        rom = ROOT / "dkc1.sfc"
        if not rom.exists():
            self.skipTest("supported private ROM required for native camera oracle")
        source = HARNESS[:HARNESS.index("int main(void)")] + WATER_CAMERA_HARNESS
        source = source.replace("@ROM_PATH@", rom.as_posix())
        test_coop_mod.CoopModTests().compile_harness(
            source, ["runner/dkc1_coop_movement.c", "snesrecomp/runner/src/snes/interp816.c"])

    def test_land_camera_matches_native_controller_with_independent_history(self):
        rom = ROOT / "dkc1.sfc"
        if not rom.exists():
            self.skipTest("supported private ROM required for native camera oracle")
        # Millstone / Temple descriptors select 98FC, which calls the same
        # dry controller as the primary camera. Pin the clean-ROM evidence.
        raw = rom.read_bytes()
        self.assertEqual(bytes.fromhex("5c 90 ec 97"), raw[0xc659:0xc65d])
        self.assertEqual(bytes.fromhex("20 ec 9b"), raw[0x97f9:0x97fc])
        self.assertEqual(bytes.fromhex("26 93 fc 98"), raw[0xc719:0xc71d])
        self.assertEqual(bytes.fromhex("c3 90 21 98"), raw[0xc675:0xc679])
        self.assertEqual(bytes.fromhex("20 ec 9b"), raw[0x982e:0x9831])
        self.assertEqual(bytes.fromhex("43 8d 28 97"), raw[0xc5f1:0xc5f5])
        self.assertEqual(bytes.fromhex("20 ec 9b"), raw[0x9735:0x9738])
        self.assertEqual(bytes.fromhex("f5 91 cc 98"), raw[0xc6bd:0xc6c1])
        self.assertEqual(bytes.fromhex("20 ec 9b"), raw[0x98d9:0x98dc])
        self.assertEqual(bytes.fromhex("9c 8f 42 98"), raw[0xc679:0xc67d])
        self.assertEqual(bytes.fromhex("20 ec 9b"), raw[0x984f:0x9852])
        self.assertEqual(bytes.fromhex("26 93 fc 98"), raw[0xc6c5:0xc6c9])
        self.assertEqual(bytes.fromhex("26 93 fc 98"), raw[0xc6c9:0xc6cd])
        self.assertEqual(bytes.fromhex("20 ec 9b"), raw[0x9909:0x990c])
        source = HARNESS[:HARNESS.index("int main(void)")] + LAND_CAMERA_HARNESS
        source = source.replace("@ROM_PATH@", rom.as_posix())
        test_coop_mod.CoopModTests().compile_harness(
            source, ["runner/dkc1_coop_movement.c", "snesrecomp/runner/src/snes/interp816.c"])

    def test_platform_focus_does_not_capture_the_distant_kongs_camera(self):
        if not (ROOT / 'dkc1.sfc').exists():
            self.skipTest('supported private ROM required for camera focus oracle')
        source = HARNESS[:HARNESS.index('int main(void)')] + r'''
int main(void) {
  FILE *f=fopen("@ROM_PATH@","rb");CHECK(f!=NULL,"ROM unavailable");
  if(!f)return 1;CHECK(fread(rom,1,sizeof rom,f)==sizeof rom,"short ROM");fclose(f);
  Setup();Write(g_ram,0x32,6);Write(g_ram,0x3e,7);Write(g_ram,0x4a,512);
  Write(g_ram,0x1b23,0x1b00);Write(g_ram,0x1b25,0x3e80);
  Write(g_ram,0x88b,0x229b);Write(g_ram,0x895,0x40);Write(g_ram,0x56f,1);
  Write(g_ram,0xb1b,0x22e2);Write(g_ram,0xbc3,0xef);
  Write(g_ram,0xb1d,0x2490);Write(g_ram,0xbc5,0xef);
  Write(g_ram,0xd4f,0x58);Write(g_ram,0xb23,0x2490);Write(g_ram,0xbcb,0xef);
  static const uint8_t motion[]={0x62,0x40,0,0,0x80,0x40,0,0,0,0,0,0,0,0,0,0,
    0,0,0x3e,0x9b,0x22,0,0x4f,0x9b,0x22,0,0xe8,0xff,0xff,0xff,0x38,0};
  memcpy(g_ram+0x1a4b,motion,32);Write(g_ram,0x1af9,2);Write(g_ram,0x579,0x201);
  WaterCameraBus own={0},queued={0};
  memcpy(own.ram,g_ram,sizeof own.ram);queued=own;Write(queued.ram,0x1e29,10);
  CHECK(NativeCameraRun(&own,6,0) && NativeCameraRun(&queued,6,0),"focus fixture did not execute native camera");
  CHECK(Read(queued.ram,0x88b)==Read(own.ram,0x88b) &&
        Read(queued.ram,0x895)==Read(own.ram,0x895),
        "Diddy's nearby platform captured distant Donkey's camera");
  /* The shared request is consumed once, but remains available to P2 even
   * after a save between the primary operand and its completed boundary. */
  CpuState cpu={0};cpu.ram=g_ram;cpu.Y=2;Write(g_ram,0x1e29,10);
  CHECK(!Dkc1CoopWorldCameraFocus(&cpu,10) && !Read(g_ram,0x1e29),
        "distant primary retained the one-shot focus");
  Dkc1CoopWorldSnapshot saved;Dkc1CoopWorldSaveState(&saved);
  CHECK(saved.version==7 && sizeof saved==120 && (saved.reserved>>8)==10,
        "pending private focus lost its compatible snapshot");
  Dkc1CoopWorldReset();Dkc1CoopWorldLoadState(&saved);
  CHECK((s_world.reserved>>8)==10,"pending focus was not restored");
  CHECK(CameraFocusForKong(g_ram,10,4)==10,"nearby second Kong lost native platform focus");
  /* Execute the cartridge producer, then compare the private view with
   * its stock camera, including the moving target's native look-ahead. */
  WaterCameraBus produced={0};memcpy(produced.ram,g_ram,sizeof produced.ram);
  Write(produced.ram,0x56f,2);Write(produced.ram,0x82,10);
  Write(produced.ram,0xe93,0x180);Write(g_ram,0xe93,0x180);
  Interp816 producer={0};producer.mem=&produced;producer.read=WaterCameraRead;
  producer.write=WaterCameraWrite;producer.k=0xb6;producer.db=0x7e;
  producer.pc=0xad1e;producer.sp=0x1fd;
  produced.ram[0x1fe]=0xfe;produced.ram[0x1ff]=0xff;
  for(unsigned n=0;n<200 && producer.pc!=0xffff;n++)interp816_runOpcode(&producer);
  CHECK(producer.pc==0xffff && !produced.failed && Read(produced.ram,0x1e29)==10 &&
        Read(produced.ram,0x1a69)==0x50,"stock producer did not admit nearby Diddy");
  enabled=0;CHECK(NativeCameraRun(&produced,6,0),"stock focus camera rejected");enabled=1;
  StoreCameraHistory(g_ram,4,0,Read(g_ram,0x88b),Read(g_ram,0x895));
  Write(g_ram,0x28,Read(g_ram,0x28)+1);uint16_t x=0,y=0;
  CHECK(NativeCamera(g_ram,4,1,&x,&y) && x==Read(produced.ram,0x88b) &&
        y==Read(produced.ram,0x895),"private focus differs from stock producer/controller");
  /* Original +/-$40 cutoff, wrap-safe world X, mirror roles and shared focus. */
  const uint16_t deltas[]={0,0x3f,0x40,0xffc0,0xffc1,0xffff};
  for(unsigned i=0;i<sizeof deltas/sizeof *deltas;i++) {
    Write(g_ram,0xb1b,0x8010);Write(g_ram,0xb23,(uint16_t)(0x8010+deltas[i]));
    CHECK(CameraFocusForKong(g_ram,10,2)==((i==2||i==3)?0:10),"native focus cutoff or high-world arithmetic changed");
  }
  Write(g_ram,0xb1b,0x2490);Write(g_ram,0xb1d,0x22e2);Write(g_ram,0xb23,0x2490);
  CHECK(CameraFocusForKong(g_ram,10,2)==10 && !CameraFocusForKong(g_ram,10,4),
        "mirrored platform focus followed the wrong Kong");
  Write(g_ram,0xb1d,0x2490);
  CHECK(CameraFocusForKong(g_ram,10,2)==10 && CameraFocusForKong(g_ram,10,4)==10,
        "shared platform did not focus both nearby Kongs");
  enabled=0;Write(g_ram,0xb1b,0x22e2);Write(g_ram,0x1e29,10);
  CHECK(Dkc1CoopWorldCameraFocus(&cpu,10)==10 && Read(g_ram,0x1e29)==10,
        "co-op-off camera focus differs from stock");
  enabled=1;speculative=1;
  CHECK(Dkc1CoopWorldCameraFocus(&cpu,10)==10 && Read(g_ram,0x1e29)==10,
        "speculative preparation published camera ownership");
  return failures?1:0;
}
'''
        test_coop_mod.CoopModTests().compile_harness(
            source.replace('@ROM_PATH@', (ROOT/'dkc1.sfc').as_posix()),
            ['runner/dkc1_coop_movement.c', 'snesrecomp/runner/src/snes/interp816.c'])

    @staticmethod
    def sources():
        functions = {
            "CODE_B69F36_M0X0": ["9F36", "9F6E"],
            "CODE_809BEC_M0X0": ["9C4E"],
            "CODE_80C47B_M0X0": ["C4A3"],
            "CODE_80C263_M0X0": ["C263", "C27C"],
            "CODE_BDFE39_M0X0": ["FE39", "FE67"],
            "CODE_BDFDBD_M0X0": ["FDBD", "FE36"],
            "CODE_BDF502_M0X0": ["F570", "F561", "F564", "F5C5", "F67A"],
        }
        for symbol, failure, success, _ in ADAPTER.COOP_CULL_FAILURES:
            functions[symbol] = [failure, success]
        text = '#include "funcs.h"\n'
        for symbol, labels in functions.items():
            text += f"RecompReturn {symbol}(CpuState *cpu) {{\n"
            for label in labels:
                text += f"  L_{label}_M0X0:\n    cpu_trace_block(cpu, 0xBD{label});\n"
                if label in ("FE67", "C27C", "9F36", "C4A3", "9C4E"):
                    text += "    cpu->coprocessor_master_cycles = cpu->master_cycles;\n    (void)cpu;\n"
            text += "  return RECOMP_RETURN_NORMAL;\n}\n"
        return {Path("native.c"): text}

    def test_adapter_is_idempotent_and_only_hooks_proven_cull_failures(self):
        sources = self.sources()
        ADAPTER.adapt_coop_world(sources)
        first = dict(sources)
        ADAPTER.adapt_coop_world(sources)
        self.assertEqual(first, sources)
        text = sources[Path("native.c")]
        self.assertEqual(text.count("Dkc1CoopWorldNoteScan(cpu)"), 1)
        self.assertEqual(text.count("Dkc1CoopWorldNoteNativeCamera(cpu->ram)"), 3)
        self.assertEqual(text.count("Dkc1CoopWorldNoteInitialize()"), 1)
        self.assertEqual(text.count("Dkc1CoopWorldKeepActor(cpu,"), 8)
        self.assertEqual(text.count("Dkc1CoopWorldKeepSource(cpu,"), 2)
        self.assertEqual(text.count('#include "dkc1_coop_world.h"'), 1)
        self.assertIn("  L_F564_M0X0:\n    cpu_trace_block", text)
        self.assertNotIn("F8A2", text)  # no second gameplay timer tick

    def test_adapter_rejects_missing_or_duplicated_native_boundary(self):
        for changed in ("  L_FE67_M0X0:\n  L_FE67_M0X0:\n", ""):
            sources = self.sources()
            sources[Path("native.c")] = sources[Path("native.c")].replace(
                "  L_FE67_M0X0:\n", changed)
            with self.assertRaisesRegex(ValueError, "expected one"):
                ADAPTER.adapt_coop_world(sources)


WORLD_WIRE_HARNESS = r'''
static void ByteWord(uint8_t *p,unsigned a,unsigned v) {
  p[a]=(uint8_t)v;p[a+1]=(uint8_t)(v>>8);
}
static void ByteDword(uint8_t *p,unsigned a,int v) {
  uint32_t n=(uint32_t)v;
  for(unsigned i=0;i<4;i++) p[a+i]=(uint8_t)(n>>(i*8));
}
int main(void) {
  uint8_t raw[120], expected[120];
  Dkc1CoopWorldSnapshot decoded, actual;
  CHECK(sizeof decoded==120 && offsetof(Dkc1CoopWorldSnapshot,camera)==40 &&
      offsetof(Dkc1CoopWorldSnapshot,water)==64 &&
      offsetof(Dkc1CoopWorldSnapshot,land_region)==116,"world wire offsets changed");
  const unsigned host_lengths[]={40,40,40,40,40,64,116,120};
  for(unsigned version=3;version<=10;version++)
    CHECK(HostWorldBytes(version)==host_lengths[version-3],"DWS8 world prefix changed");
  const unsigned lengths[]={40,64,116,120,120,120};
  for(unsigned version=1;version<=6;version++) {
    for(unsigned i=0;i<120;i++) raw[i]=(uint8_t)(i*13+3);
    ByteWord(raw,0,version);ByteWord(raw,38,0x188);
    ByteDword(raw,40,1);ByteDword(raw,44,-16);ByteDword(raw,48,-4096);
    ByteDword(raw,52,271);ByteDword(raw,56,-131070);ByteDword(raw,60,2);
    ByteWord(raw,64,1);ByteWord(raw,68,3);ByteWord(raw,72,4);
    memset(&decoded,0,sizeof decoded);memcpy(&decoded,raw,lengths[version-1]);
    memcpy(expected,&decoded,120);ByteWord(expected,0,7);
    if(version<5) ByteWord(expected,38,0);
    Dkc1CoopWorldLoadState(&decoded);Dkc1CoopWorldSaveState(&actual);
    CHECK(!memcmp(&actual,expected,120),"historical world bytes/defaults changed");
  }
  /* Pre-v6 mode5 resets only the incompatible controller history, retaining
   * scanner bytes, generic framing, dry region and pending requests. */
  ByteWord(raw,0,5);ByteWord(raw,68,5);ByteWord(raw,38,0x188);
  memcpy(&decoded,raw,120);memcpy(expected,raw,120);ByteWord(expected,0,7);
  memset(expected+64,0,52);
  Dkc1CoopWorldLoadState(&decoded);Dkc1CoopWorldSaveState(&actual);
  CHECK(!memcmp(&actual,expected,120),"pre-v6 mode5 migration changed");
  ByteWord(raw,0,6);memcpy(&decoded,raw,120);
  Dkc1CoopWorldLoadState(&decoded);Dkc1CoopWorldSaveState(&actual);
  memcpy(expected,raw,120);ByteWord(expected,0,7);
  CHECK(!memcmp(&actual,expected,120),"current mode5 history changed");
  /* Invalid native history retains the accepted prefix and cold defaults. */
  ByteWord(raw,64,2);memcpy(&decoded,raw,120);memcpy(expected,raw,120);
  ByteWord(expected,0,7);
  memset(expected+64,0,56);
  Dkc1CoopWorldLoadState(&decoded);Dkc1CoopWorldSaveState(&actual);
  CHECK(!memcmp(&actual,expected,120),"world tolerant native defaults changed");
  const unsigned requests[]={0,0x100,0x108,0x180,0x188,8,0x80,0x200};
  for(unsigned i=0;i<sizeof requests/sizeof *requests;i++) {
    ByteWord(raw,38,requests[i]);memcpy(&decoded,raw,120);
    memcpy(expected,raw,120);memset(expected+64,0,56);
    ByteWord(expected,0,7);
    if(i>=5) ByteWord(expected,38,0);
    Dkc1CoopWorldLoadState(&decoded);Dkc1CoopWorldSaveState(&actual);
    CHECK(!memcmp(&actual,expected,120),"world request migration changed");
  }
  ByteWord(raw,0,7);ByteWord(raw,38,0x0a00);memcpy(&decoded,raw,120);
  Dkc1CoopWorldLoadState(&decoded);Dkc1CoopWorldSaveState(&actual);
  CHECK(actual.reserved==0x0a00 && sizeof actual==120,"v7 pending focus layout changed");
  return failures!=0;
}
'''

HARNESS = r'''
#include "dkc1_coop_world.c"
#include <stdio.h>

int interp816_opcode_hook(uint32_t pc) { return 0; }
uint8 g_ram[0x20000];
static uint8_t rom[0x400000];
const uint8 *g_rom = rom;
Ppu *g_ppu;
Dma *g_dma;
uint8 g_snesrecomp_last_hdmaen, g_memsel;
Snes *g_snes;
uint64_t g_main_cpu_cycles_estimate, g_apu_pace_cycles_estimate;
uint64_t g_apu_last_sync_cycles, g_apu_last_sync_master;
static int enabled = 1, speculative, violation, rejected, entities;
static int entity_backup, transaction, init_calls, scan_calls, failures;
static int graphics, graphics_backup;
static int held_camera;
static uint16_t held_x, held_y;
#define CHECK(c, m) do { if (!(c)) { puts(m); failures++; } } while (0)
int Dkc1CoopEnabled(void) { return enabled; }
void Dkc1CoopPrimaryCamera(const uint8_t *ram, uint16_t *x, uint16_t *y) {
  *x = held_camera ? held_x : Read(ram, 0x088b);
  *y = held_camera ? held_y : Read(ram, 0x0895);
}
bool RtlSpeculativeExecutionActive(void) { return speculative != 0; }
void RtlSpeculativeExecutionBegin(void) { speculative = 1; violation = 0; }
void RtlSpeculativeExecutionEnd(void) { speculative = 0; }
uint32_t RtlSpeculativeExecutionViolation(void) { return violation; }
void RtlSpeculativeExecutionReject(uint32_t v) { violation = (int)v; }
uint16_t Dkc1VideoObjectScannerCullLeft(uint16_t n) { return n; }
uint16_t Dkc1VideoObjectScannerCullSpan(uint16_t n) { return n; }
bool Dkc1VideoReadVerticalMetatile(uint8_t bank, uint16_t map_base,
    uint32_t x, uint32_t y, uint16_t *cell) {
  (void)bank; (void)map_base; (void)x; (void)y; (void)cell;
  return 0;
}
void cpu_write8(CpuState *cpu, uint8 bank, uint16 addr, uint8 value) {
  (void)bank; cpu->ram[addr] = value;
}
int Dkc1CoopEntityIsHandle(uint16_t h) { return h >= 2 && h <= 0x32 && !(h & 1); }
uint16_t Dkc1CoopEntityNextHandle(uint16_t h) { return h + 2; }
uint16_t Dkc1CoopEntityLastHandle(void) { return 0x32; }
uint16_t Dkc1CoopEntityRead16(const uint8_t *ram, uint16_t base, uint16_t h) {
  return Read(ram, base + h);
}
uint16_t Dkc1CoopMinecartForKong(const uint8_t *ram,uint16_t kong) {
  if (!enabled || Read(ram,0x32)!=5) return 0;
  for(uint16_t h=6;h<=0x32;h+=2)
    if(Read(ram,0xd45+h)==0x51) {
      uint16_t rider=Read(ram,0x1375+h);
      if ((rider==2 || rider==4) && Read(ram,0x1029+rider)==0x2e &&
          (rider==kong || (Read(ram,0x579)&1 && Read(ram,0x13e9+h)==kong &&
                          Read(ram,0x1029+kong)==0x0c))) return h;
    }
  return 0;
}
int Dkc1CoopEntitiesBeginTransaction(void) {
  if (transaction) return 0;
  transaction = 1; entity_backup = entities; return 1;
}
void Dkc1CoopEntitiesEndTransaction(int commit) {
  if (!commit) entities = entity_backup;
  transaction = 0;
}
int Dkc1CoopGraphicsBeginTransaction(void) { graphics_backup = graphics; return 1; }
void Dkc1CoopGraphicsEndTransaction(int commit) { if (!commit) graphics = graphics_backup; }
RecompReturn CODE_BDFDBD_M0X0(CpuState *cpu) {
  init_calls++;
  CHECK(cpu->D == 0 && cpu->m_flag == 0 && cpu->x_flag == 0,
        "initializer called with wrong CPU mode");
  Dkc1CoopWorldNoteInitialize();
  Write(g_ram, 0x1e03, 0x0201);
  cpu->S += 3;
  return RECOMP_RETURN_NORMAL;
}
RecompReturn CODE_BDFE39_M0X0(CpuState *cpu) {
  scan_calls++;
  uint16_t primary_x, primary_y;
  CHECK(Dkc1CoopWorldGetCamera(g_ram, 0, &primary_x, &primary_y) &&
        primary_x == 0 && primary_y == 200,
        "private scanner lost the canonical primary camera");
  CHECK(Read(g_ram, 0x088b) == 1024, "second scanner did not use second camera");
  CHECK(Read(g_ram, 0x1e03) == 0x0201 + scan_calls - init_calls,
        "second scanner lost its private vertical cursor");
  CHECK(Read(g_ram, 0xef) == 992 && Read(g_ram, 0xf1) == 1312,
        "scanner window was expanded across the gap between players");
  if (!g_ram[0x192b + 7]) {
    g_ram[0x192b + 7] = 6;
    Write(g_ram, 0x0d45 + 6, 0x12);
    entities++;
    graphics++;
  }
  Write(g_ram, 0x1e03, Read(g_ram, 0x1e03) + 1);
  Write(g_ram, 0x76, 0xabcd);
  g_main_cpu_cycles_estimate += 123;
  g_apu_pace_cycles_estimate += 123;
  Dkc1CoopWorldNoteScan(cpu);  /* nested pass must not schedule another */
  if (rejected) {
    entities += 50;
    graphics += 50;
    Write(g_ram, 0x1234, 0x1234);
    violation = RTL_SPECULATIVE_VIOLATION_MMIO_WRITE;
  }
  cpu->S += 3;
  return RECOMP_RETURN_NORMAL;
}
static void RomSet(uint16_t p, uint16_t v) { Write(rom, 0x3d0000u + p, v); }
static void TestCameraFrame(void) {
  Write(g_ram,0x28,(uint16_t)(Read(g_ram,0x28)+1));
  if (Read(g_ram,0x32)==3) Dkc1CoopWorldNoteNativeCamera(g_ram);
  Dkc1CoopWorldNoteFrame(g_ram);
}
uint16_t Dkc1CoopMinecartViewOwner(const uint8_t *ram) {
  uint16_t cart=Dkc1CoopMinecartForKong(ram,2);
  if(!cart)cart=Dkc1CoopMinecartForKong(ram,4);
  return cart?Read(ram,0xd45+Read(ram,0x1375+cart)):0;
}
static void Setup(void) {
  memset(g_ram, 0, sizeof g_ram);
  Dkc1CoopWorldReset();
  enabled = 1; init_calls = scan_calls = entities = graphics = 0; rejected = 0;
  held_camera = 0;
  /* Synthetic generic-camera fixtures have no clean-ROM controller. */
  /* Mode4 remains the deliberately unadmitted generic-camera fixture. */
  Write(g_ram, 0x32, 4);
  Write(g_ram, 0x1b23, 0); Write(g_ram, 0x1b25, 0x5000);
  Write(g_ram, 0x004a, 1024);
  Write(g_ram, 0x0d45 + 2, 1); Write(g_ram, 0x0d45 + 4, 2);
  Write(g_ram, 0x12a5 + 2, 0x101); Write(g_ram, 0x12a5 + 4, 0x101);
  Write(g_ram, 0x0b19 + 4, 1152); Write(g_ram, 0x0bc1 + 4, 712);
  Write(g_ram, 0x0895, 200); Write(g_ram, 0x1e03, 0x1122);
  Write(g_ram, 0xa0, 0x3344); Write(g_ram, 0x82, 6);
}
/* Generic-controller fallback keeps its original entry-row guard. Native
 * mode B now has separate clean-ROM coverage in LAND_CAMERA_HARNESS. */
static void BonusReturnCamera(void) {
  Setup();
  Write(g_ram,0x32,4); Write(g_ram,0x3e,0xba);
  Write(g_ram,0x4a,512);
  Write(g_ram,0x088b,0xaa8); Write(g_ram,0x0895,68);
  Write(g_ram,0x0b19+2,0xaf0); Write(g_ram,0x0bc1+2,511);
  Write(g_ram,0x0b19+4,0xaf0); Write(g_ram,0x0bc1+4,511);
  Write(g_ram,0x1029+2,0x0c);
  Write(g_ram,0x12a5+2,0); Write(g_ram,0x12a5+4,0);
  TestCameraFrame();
  Dkc1CoopWorldSnapshot state;
  Dkc1CoopWorldSaveState(&state);
  CHECK(!state.camera.ready,"bonus return acquired temporary offscreen row");
  uint16_t x,y;
  Dkc1CoopWorldGetCamera(g_ram,1,&x,&y);
  CHECK(x==0xaa8 && y==68,"bonus entry lost native framing");
  Write(g_ram,0x0bc1+4,303); Write(g_ram,0x12a5+4,0x101);
  TestCameraFrame();
  Dkc1CoopWorldGetCamera(g_ram,1,&x,&y);
  CHECK(x==0xaa8 && y==68,"bonus landing dragged camera below native picture");
  Dkc1CoopWorldSaveState(&state);
  CHECK(state.camera.ready && state.camera.row==141,
        "bonus landing did not acquire visible native row");
  state.camera.row=-67; /* host history from the supplied F11 */
  Dkc1CoopWorldLoadState(&state);
  Dkc1CoopWorldGetCamera(g_ram,1,&x,&y);
  CHECK(y==68,"old negative entry row still displaced loaded camera");
  TestCameraFrame();
  Dkc1CoopWorldSaveState(&state);
  CHECK(state.camera.row==141,"completed frame retained invalid entry history");
}
static void CameraContinuity(void) {
  Setup();
  Write(g_ram, 0x088b, 1000); Write(g_ram, 0x0895, 300);
  Write(g_ram, 0x0b19+2, 1080); Write(g_ram, 0x0bc1+2, 600);
  Write(g_ram, 0x0b19+4, 1200); Write(g_ram, 0x0bc1+4, 600);
  Write(g_ram, 0x0e9d+4, 0); /* velocity is set via its generated accessor below */
  TestCameraFrame();
  Dkc1Actor_SetSprXVelocity(g_ram, 4, 0xffc0);
  for(unsigned i=0;i<24;i++) TestCameraFrame();
  Dkc1CoopWorldSnapshot mid, after, expected;
  Dkc1CoopWorldSaveState(&mid);
  CHECK(mid.camera.column8 > 80*256 && mid.camera.column8 < 128*256,
        "fixture did not reach intermediate reverse ease");
  uint16_t x,y,x0,y0;
  Dkc1CoopWorldGetCamera(g_ram,1,&x0,&y0);
  Write(g_ram, 0x0032, 3);
  Dkc1CoopWorldGetCamera(g_ram,1,&x,&y);
  CHECK(x==1000 && y==300,"unknown water ROM did not retain stock camera");
  Write(g_ram, 0x0032, 4);
  for(unsigned i=0;i<100;i++) {
    Dkc1CoopWorldGetCamera(g_ram,1,&x,&y);
    CHECK(x==x0 && y==y0,"repeated camera read advanced camera");
  }
  Dkc1CoopWorldSaveState(&after);
  CHECK(!memcmp(&mid,&after,sizeof mid),"getter changed serialized camera history");
  for(unsigned i=0;i<13;i++) TestCameraFrame();
  Dkc1CoopWorldSaveState(&expected);
  Dkc1CoopWorldReset();Dkc1CoopWorldLoadState(&mid);
  Dkc1CoopWorldGetCamera(g_ram,1,&x,&y);
  CHECK(x==x0 && y==y0,"paused mid-ease load changed the camera");
  for(unsigned i=0;i<13;i++) TestCameraFrame();
  Dkc1CoopWorldSaveState(&after);
  CHECK(!memcmp(&expected,&after,sizeof after),"mid-ease split continuation diverged");
  /* A living DK jumps while his camera holds its vertical deadzone. */
  Write(g_ram,0x0bc1+2,640);TestCameraFrame();
  Dkc1CoopWorldGetCamera(g_ram,1,&x,&y);
  CHECK(y==y0,"DK jump moved stationary Diddy's view");
  Write(g_ram,0x1029+2,0x0d);Write(g_ram,0x0bc1+2,400);
  TestCameraFrame();Dkc1CoopWorldGetCamera(g_ram,1,&x,&y);
  CHECK(y==300,"dying DK still owned survivor camera row");
  Write(g_ram,0x0bc1+2,390);TestCameraFrame();
  Dkc1CoopWorldGetCamera(g_ram,1,&x,&y);
  CHECK(y==300,"falling DK dragged survivor camera");
  Dkc1Actor_SetSprXVelocity(g_ram,4,0);x0=x;
  Write(g_ram,0x088b,3000);Write(g_ram,0x0895,280);
  TestCameraFrame();Dkc1CoopWorldGetCamera(g_ram,1,&x,&y);
  CHECK(x==x0 && y==300,"death-camera handoff scrolled stationary survivor");
  Dkc1CoopWorldSaveState(&after);
  Dkc1CoopWorldReset();Dkc1CoopWorldLoadState(&after);
  Dkc1CoopWorldGetCamera(g_ram,1,&x,&y);
  CHECK(x==x0 && y==300,"off-camera survivor framing did not survive load");
  Write(g_ram,0x088b,1000);Write(g_ram,0x0895,300);
  TestCameraFrame();Dkc1CoopWorldGetCamera(g_ram,1,&x,&y);
  CHECK(x==x0 && y==300,"returning primary camera recentered stationary survivor");
  Write(g_ram,0x0bc1+4,520);TestCameraFrame();
  Dkc1CoopWorldGetCamera(g_ram,1,&x,&y);
  CHECK(y==380,"survivor movement lost its independent vertical framing");
  Write(g_ram,0x0bc1+4,600);
  Write(g_ram,0x1029+2,0);Write(g_ram,0x1029+4,0x0d);
  CHECK(!Dkc1CoopWorldGetCamera(g_ram,1,&x,&y),"dying Diddy retained playable camera");
  mid.version=1;Dkc1CoopWorldLoadState(&mid);Dkc1CoopWorldSaveState(&after);
  CHECK(!after.camera.ready,"legacy snapshot inherited new camera history");
}
int main(void) {
  for(uint16_t rider=2;rider<=4;rider+=2) {
    Setup();Write(g_ram,0x32,5);Write(g_ram,0x579,1);
    Write(g_ram,0xd4f,0x51);Write(g_ram,0x137f,rider);Write(g_ram,0x13f3,rider^6u);
    Write(g_ram,0x1029+rider,0x2e);Write(g_ram,0x1029+(rider^6u),0x0c);
    held_camera=1;held_x=900;held_y=138;
    uint16_t x,y;
    CHECK(Dkc1CoopWorldGetCamera(g_ram,1,&x,&y) && x==900 && y==138,
          "shared rear passenger view lost the cartridge camera");
  }
  Setup();
  Write(g_ram,0x32,5); Write(g_ram,0x102b,0x0c); Write(g_ram,0x102d,0x2e);
  Write(g_ram,0xd4f,0x51); Write(g_ram,0x137f,4);
  held_camera=1; held_x=900; held_y=138;
  uint16_t cart_x,cart_y;
  for(unsigned i=0;i<100;i++) {
    Write(g_ram,0xbc5,(uint16_t)(700+i));
    Dkc1CoopWorldGetCamera(g_ram,1,&cart_x,&cart_y);
    CHECK(cart_x==held_x && cart_y==held_y,
          "registered shared cart lost its native jump framing");
  }
  Write(g_ram,0x137f,2);
  /* An unbound passenger keeps independent native history, not the shared
   * cart camera. This fixture caches a completed native dry-controller tick. */
  memcpy(rom+0x9bec,"\x20\xb0\x9b\xad\x29\x19",6);
  s_world.water.ready=1;s_world.water.mode=5;s_world.water.height=1024;
  s_world.water.slot=4;s_world.water.x=1072;s_world.water.y=138;
  Dkc1CoopWorldGetCamera(g_ram,1,&cart_x,&cart_y);
  CHECK(cart_x!=held_x,"unregistered cart passenger borrowed native camera");
  BonusReturnCamera();
  CameraContinuity();
  Setup();
  uint16_t x, y;
  CpuState cpu = {0}; cpu.ram = g_ram; cpu.S = 0x019b;
  CHECK(Dkc1CoopWorldGetCamera(g_ram, 1, &x, &y) && x == 1024 && y == 200,
        "camera did not convert native reversed actor Y");
  static const uint16_t scripted[] = {0x1d, 0x2a, 0x31, 0x3d, 0x41, 0x4f};
  for (unsigned i = 0; i < sizeof scripted / sizeof scripted[0]; i++) {
    Write(g_ram, 0x1029 + 4, scripted[i]);
    CHECK(Dkc1CoopWorldGetCamera(g_ram, 1, &x, &y) && x == 0 && y == 200,
          "entry/exit script was chased by the secondary camera");
  }
  Write(g_ram, 0x1029 + 4, 0);
  Write(g_ram, 0x1b23, 512);
  CHECK(Dkc1CoopWorldGetCamera(g_ram, 1, &x, &y) && x == 1024,
        "camera added level lower bound to absolute actor X");
  Write(g_ram, 0x1b23, 0);
  Write(g_ram, 0x0b19 + 6, 1100); Write(g_ram, 0x0bc1 + 6, 712);
  CHECK(Dkc1CoopWorldKeepActor(&cpu, 0xf78c), "second-view actor was culled");
  Write(g_ram, 0x0b19 + 6, 500);
  CHECK(!Dkc1CoopWorldKeepActor(&cpu, 0xf78c), "invisible gap became active");
  held_camera = 1; held_x = 0; held_y = 200;
  Write(g_ram, 0x088b, 1024);
  Write(g_ram, 0x0b19 + 6, 100);
  CHECK(Dkc1CoopWorldGetCamera(g_ram, 0, &x, &y) && x == 0 &&
        Dkc1CoopWorldKeepActor(&cpu, 0xf78c),
        "P2 tick lost the saved primary camera or culled a P1 actor");
  held_camera = 0; Write(g_ram, 0x088b, 0);
  Write(g_ram, 0x0b19 + 6, 992);
  CHECK(!Dkc1CoopWorldKeepActor(&cpu, 0xf78c), "native exclusive left edge changed");
  Write(g_ram, 0x0b19 + 6, 1312);
  CHECK(Dkc1CoopWorldKeepActor(&cpu, 0xf78c), "native inclusive right edge changed");
  Write(g_ram, 0x0b19 + 6, 1100); Write(g_ram, 0x0bc1 + 6, 856);
  CHECK(!Dkc1CoopWorldKeepActor(&cpu, 0xf78c), "native exclusive vertical edge changed");
  Write(g_ram, 0x0bc1 + 6, 200);
  Write(g_ram, 0x0ef1 + 6, 0xffff);
  CHECK(!Dkc1CoopWorldKeepActor(&cpu, 0xf85b), "downward cull ignored its far edge");
  Write(g_ram, 0x0ef1 + 6, 0);
  CHECK(Dkc1CoopWorldKeepActor(&cpu, 0xf85b), "upward directional cull gained a far edge");
  Write(g_ram, 0x0bc1 + 6, 950);
  CHECK(!Dkc1CoopWorldKeepActor(&cpu, 0xf6ff), "special low-world Y escape accepted invalid actor Y");
  Write(g_ram, 0x0bc1 + 6, 712);
  Write(g_ram, 0x0b19 + 4, 0x8080); Write(g_ram, 0x1b25, 0x9000);
  Write(g_ram, 0x0b19 + 6, 0x8100);
  CHECK(Dkc1CoopWorldKeepActor(&cpu, 0xf78c), "world $8000 treated as camera underflow");

  Setup(); cpu.X = 6;
  Write(g_ram,0x82,6); Write(g_ram,0x0b19+6,1100);
  Write(g_ram,0x0bc1+6,0xffff); /* These two native predicates test only X. */
  CHECK(Dkc1CoopWorldKeepActor(&cpu,0xbc63), "secondary live tire was culled by primary X window");
  CHECK(Dkc1CoopWorldKeepActor(&cpu,0xbc77), "secondary expanded X window lost a live actor");
  Write(g_ram,0x0b19+6,992);
  CHECK(!Dkc1CoopWorldKeepActor(&cpu,0xbc63), "simple X cull changed its exclusive left edge");
  CHECK(Dkc1CoopWorldKeepActor(&cpu,0xbc77), "expanded X cull lost its additional left padding");
  Write(g_ram,0x0b19+6,960);
  CHECK(!Dkc1CoopWorldKeepActor(&cpu,0xbc77), "expanded X cull changed its exclusive left edge");
  Write(g_ram,0x0b19+6,1312);
  CHECK(Dkc1CoopWorldKeepActor(&cpu,0xbc63), "simple X cull changed its inclusive right edge");
  Write(g_ram,0x0b19+6,1344);
  CHECK(!Dkc1CoopWorldKeepActor(&cpu,0xbc63) && Dkc1CoopWorldKeepActor(&cpu,0xbc77),
        "expanded X cull lost its additional right padding");
  Write(g_ram,0x0b19+6,500);
  CHECK(!Dkc1CoopWorldKeepActor(&cpu,0xbc63) && !Dkc1CoopWorldKeepActor(&cpu,0xbc77),
        "simple culls retained an actor in the invisible gap");
  Write(g_ram,0x0b19+4,0x8180);Write(g_ram,0x1b25,0x9000);
  Write(g_ram,0x0b19+6,0x8000);
  CHECK(!Dkc1CoopWorldKeepActor(&cpu,0xbc63) && Dkc1CoopWorldKeepActor(&cpu,0xbc77),
        "expanded X cull changed the native signed-subtraction clamp");

  Setup(); cpu.X = 6;
  RomSet(0x8000, 0x9000); Write(g_ram, 0x15fd + 6, 7);
  RomSet(0x9038, 1); RomSet(0x903a, 1100); RomSet(0x903c, 712);
  CHECK(Dkc1CoopWorldKeepSource(&cpu, 0), "second-view source lost its bookmark");
  RomSet(0x903a, 500);
  CHECK(!Dkc1CoopWorldKeepSource(&cpu, 0), "source in invisible gap was retained");
  held_camera = 1; held_x = 0; held_y = 200;
  Write(g_ram, 0x088b, 1024); RomSet(0x903a, 100);
  CHECK(Dkc1CoopWorldKeepSource(&cpu, 0), "P2 tick discarded a P1 source bookmark");
  held_camera = 0; Write(g_ram, 0x088b, 0);
  Write(g_ram, 0x15fd + 6, 0x8000);
  CHECK(!Dkc1CoopWorldKeepSource(&cpu, 0), "explicit no-source cleanup was suppressed");

  Setup();
  uint8_t primary_low[0x200]; memcpy(primary_low, g_ram, sizeof primary_low);
  CpuState original = cpu;
  Dkc1CoopWorldNoteScan(&cpu);
  CHECK(Dkc1CoopWorldRunScanner(&cpu) == 1, "second scanner did not commit");
  CHECK(!memcmp(&cpu, &original, sizeof cpu), "scanner changed primary CPU registers/clock");
  CHECK(!memcmp(primary_low, g_ram, sizeof primary_low), "scanner leaked DP or stack");
  CHECK(Read(g_ram, 0x088b) == 0 && Read(g_ram, 0x1e03) == 0x1122,
        "scanner leaked secondary camera or section cursor");
  CHECK(g_ram[0x1932] == 6 && entities == 1, "shared source allocation was rolled back");
  CHECK(g_main_cpu_cycles_estimate == 0 && g_apu_pace_cycles_estimate == 0,
        "secondary scanner advanced main or audio clocks");
  CHECK(Dkc1CoopWorldRunScanner(&cpu) == 0 && scan_calls == 1,
        "redraw/idle call scanned twice without simulation");
  Dkc1CoopWorldSnapshot saved;
  Dkc1CoopWorldSaveState(&saved); Dkc1CoopWorldReset(); Dkc1CoopWorldLoadState(&saved);
  Dkc1CoopWorldNoteScan(&cpu);
  CHECK(Dkc1CoopWorldRunScanner(&cpu) == 1 && init_calls == 1 && entities == 1,
        "save/load lost second cursor or duplicated shared source");
  uint8_t before[sizeof g_ram]; memcpy(before, g_ram, sizeof before);
  Dkc1CoopWorldNoteScan(&cpu); rejected = 1;
  CHECK(Dkc1CoopWorldRunScanner(&cpu) == -1, "forbidden MMIO scanner was accepted");
  CHECK(!memcmp(before, g_ram, sizeof before) && entities == 1 && graphics == 1,
        "rejected scanner left partially allocated WRAM, host entities or graphics");
  Setup(); held_camera = 1; held_x = 0; held_y = 200;
  Write(g_ram, 0x088b, 1024);
  Dkc1CoopWorldNoteScan(&cpu);
  CHECK(Dkc1CoopWorldRunScanner(&cpu) == 1 && Read(g_ram, 0x088b) == 1024,
        "scanner did not restore the interrupted P2 controller camera");
  held_camera = 0;
  enabled = 0;
  CHECK(!Dkc1CoopWorldGetCamera(g_ram, 1, &x, &y) &&
        !Dkc1CoopWorldKeepActor(&cpu, 0xf78c) &&
        Dkc1CoopWorldRunScanner(&cpu) == 0,
        "disabled co-op modified native camera/lifecycle");
  return failures ? 1 : 0;
}
'''

WATER_CAMERA_HARNESS = r'''
int main(void) {
  FILE *f=fopen("@ROM_PATH@","rb"); CHECK(f!=NULL,"ROM unavailable");
  if (!f) return 1;
  CHECK(fread(rom,1,sizeof rom,f)==sizeof rom,"short ROM");fclose(f);
  Setup();
  Write(g_ram,0x32,3);Write(g_ram,0x3e,0xbf);Write(g_ram,0x4a,0x7000);
  Write(g_ram,0x1b25,0x700);Write(g_ram,0x88b,0x570);Write(g_ram,0x895,0x25f0);
  Write(g_ram,0xb1b,0x5fb);Write(g_ram,0xbc3,0x493f);
  Write(g_ram,0xb1d,0x6fc);Write(g_ram,0xbc5,0x4bbf);
  Write(g_ram,0x102b,0x2b);Write(g_ram,0x102d,0x30);
  Write(g_ram,0x28,1);
  uint8_t before[sizeof g_ram];memcpy(before,g_ram,sizeof before);
  Dkc1CoopWorldNoteNativeCamera(g_ram);uint16_t x,y;
  Dkc1CoopWorldGetCamera(g_ram,1,&x,&y);
  /* Original C501 initialization through C569, then C263, for this actor:
   * authored B8 camera region gives Y2390, not a nearest occupied tile row. */
  CHECK(x==0x67c && y==0x2390,"water camera disagrees with native X/Y bounds");
  CHECK(!memcmp(before,g_ram,sizeof before),"private camera changed guest WRAM");
  Dkc1CoopWorldSnapshot mid,expected,actual;
  Write(g_ram,0xb1d,0x57b);Write(g_ram,0xbc5,0x4c23);
  Dkc1Actor_SetSprYVelocity(g_ram,4,0x100);
  Dkc1CoopWorldSaveState(&mid);
  Dkc1CoopWorldNoteNativeCamera(g_ram); /* duplicate completed boundary */
  Dkc1CoopWorldSaveState(&actual);
  CHECK(!memcmp(&mid.water,&actual.water,sizeof mid.water),
        "water camera advanced twice in one cartridge frame");
  Write(g_ram,0x28,2);
  Dkc1CoopWorldNoteFrame(g_ram); /* new tick started, native camera not yet complete */
  Dkc1CoopWorldSaveState(&actual);
  CHECK(!memcmp(&mid.water,&actual.water,sizeof mid.water),
        "water camera stepped before the native camera completed");
  Write(g_ram,0x28,1);
  for(unsigned i=0;i<120;i++) {
    uint16_t old=y;TestCameraFrame();Dkc1CoopWorldGetCamera(g_ram,1,&x,&y);
    CHECK(abs((int)y-old)<=4,"water camera bypassed native four-pixel speed limit");
    if(i==30) Dkc1CoopWorldSaveState(&mid);
  }
  Dkc1CoopWorldSaveState(&expected);Dkc1CoopWorldLoadState(&mid);
  Write(g_ram,0x28,32);
  for(unsigned i=31;i<120;i++)TestCameraFrame();
  Dkc1CoopWorldSaveState(&actual);
  CHECK(!memcmp(&actual,&expected,sizeof actual),"water camera history lost across load");
  for(unsigned i=0;i<100;i++)Dkc1CoopWorldGetCamera(g_ram,1,&x,&y);
  Dkc1CoopWorldSaveState(&actual);
  CHECK(!memcmp(&actual,&expected,sizeof actual),"water camera advanced on redraw");
  /* Region cursor is private, but $1E11 is a live gameplay trigger input. */
  s_world.water.region[2] = 0x5a;Write(g_ram,0x1e11,0);
  TestCameraFrame();
  CHECK(s_world.water.region[2]==0,"private camera retained stale region trigger");
  s_world.water.region[0]=0xff;s_world.water.region[1]=0xff;
  TestCameraFrame();
  CHECK(!s_world.water.ready,"rejected camera kept a stale ready result");
  /* A saved generic vertical view may be far from P1. Read-only admission
   * must retain that view until its first completed native camera update. */
  Setup();Write(g_ram,0x32,9);Write(g_ram,0x3e,0x6d);Write(g_ram,0x4a,0x7000);
  Write(g_ram,0x1b23,0);Write(g_ram,0x1b25,0x700);
  Write(g_ram,0xb1d,0xa2);Write(g_ram,0xbc5,0x63ef);
  Write(g_ram,0x88b,0x370);Write(g_ram,0x895,0xa9f);
  s_world.initialized=1;s_world.entrance=0x6d;s_world.mode=9;s_world.height=0x7000;
  s_world.camera.ready=1;s_world.camera.row=176;s_world.camera.column8=89*256;
  uint8_t saved_ram[sizeof g_ram];memcpy(saved_ram,g_ram,sizeof g_ram);
  Dkc1CoopWorldSnapshot saved_world=s_world;
  uint16_t vx=0,vy=0;
  CHECK(NativeCamera(g_ram,4,0,&vx,&vy) && vx==0x49 && vy==0xb61,
        "vertical controller acquisition borrowed P1 viewport");
  CHECK(!memcmp(saved_ram,g_ram,sizeof g_ram) && !memcmp(&saved_world,&s_world,sizeof s_world),
        "read-only vertical acquisition advanced state");
  return failures?1:0;
}
'''

LAND_CAMERA_HARNESS = r'''
int main(void) {
  CHECK(CameraController(5)==&kDryCamera,"mode5 on-foot camera must use native dry controller");
  CHECK(CameraController(9)==&kVerticalCamera,
        "mode9 must use the cartridge C47B controller");
  FILE *f=fopen("@ROM_PATH@","rb");CHECK(f!=NULL,"ROM unavailable");
  if(!f)return 1;CHECK(fread(rom,1,sizeof rom,f)==sizeof rom,"short ROM");fclose(f);
  const unsigned modes[]={0,1,2,5,6,7,8,9,0x0a,0x0b};
  for(unsigned m=0;m<sizeof modes/sizeof modes[0];m++) {
  unsigned mode=modes[m];
  Setup();Write(g_ram,0x32,(uint16_t)mode);Write(g_ram,0x3e,7);Write(g_ram,0x4a,0x200);
  Write(g_ram,0x1b23,0x1b00);Write(g_ram,0x1b25,0x3e80);
  Write(g_ram,0x88b,0x229b);Write(g_ram,0x895,0x40);Write(g_ram,0x56f,1);
  Write(g_ram,0xb1b,0x22e2);Write(g_ram,0xb1d,0x22e2);
  Write(g_ram,0xbc3,0xef);Write(g_ram,0xbc5,0xef);
  static const uint8_t motion[]={0x62,0x40,0,0,0x80,0x40,0,0,0,0,0,0,0,0,0,0,
    0,0,0x3e,0x9b,0x22,0,0x4f,0x9b,0x22,0,0xe8,0xff,0xff,0xff,0x38,0};
  memcpy(g_ram+0x1a4b,motion,32);Write(g_ram,0x1af9,2);Write(g_ram,0x579,0x201);
  uint8_t before[sizeof g_ram];memcpy(before,g_ram,sizeof before);
  Dkc1CoopWorldNoteNativeCamera(g_ram);uint16_t x,y;
  Dkc1CoopWorldGetCamera(g_ram,1,&x,&y);
  CHECK(x==Read(g_ram,0x88b)&&y==Read(g_ram,0x895),"co-located cameras initialize differently");
  CHECK(!memcmp(before,g_ram,sizeof before),"camera leaked guest state");
  Dkc1CoopWorldSnapshot mid,end,actual;
  for(unsigned frame=1;frame<=120;frame++) {
    Write(g_ram,0x28,(uint16_t)frame);
    Write(g_ram,0xb1d,(uint16_t)(0x22e2+frame*2));
    Write(g_ram,0xbc5,(uint16_t)(frame<60?0xef:0x10f));
    Dkc1Actor_SetSprXVelocity(g_ram,4,0x200);
    /* Direct execution of clean-ROM controller for Diddy is the oracle. */
    WaterCameraBus oracle={0};memcpy(oracle.ram,g_ram,sizeof oracle.ram);
    Write(oracle.ram,0x56f,2);memcpy(oracle.ram+0x1a4b,s_world.water.motion,32);
    Write(oracle.ram,0x88b,s_world.water.x);Write(oracle.ram,0x895,s_world.water.y);
    memcpy(oracle.ram+0x1af9,s_world.water.region,2);
    memcpy(oracle.ram+0x1b1b,s_world.water.region+2,2);
    memcpy(oracle.ram+0x1e0f,s_world.land_region,2);oracle.ram[0x1e12]=s_world.land_region[3];
    CHECK(NativeCameraRun(&oracle,mode,0),"native land controller rejected");
    Dkc1CoopWorldNoteNativeCamera(g_ram);Dkc1CoopWorldGetCamera(g_ram,1,&x,&y);
    CHECK(x==Read(oracle.ram,0x88b)&&y==Read(oracle.ram,0x895),"land camera differs from native update");
    Dkc1CoopWorldSaveState(&end);Dkc1CoopWorldNoteNativeCamera(g_ram);
    Dkc1CoopWorldNoteFrame(g_ram);Dkc1CoopWorldSaveState(&actual);
    CHECK(!memcmp(&end,&actual,sizeof end),"land camera integrated twice");
    if(frame==60)Dkc1CoopWorldSaveState(&mid);
  }
  Dkc1CoopWorldSaveState(&end);Dkc1CoopWorldLoadState(&mid);
  for(unsigned frame=61;frame<=120;frame++) {
    Write(g_ram,0x28,(uint16_t)frame);Write(g_ram,0xb1d,(uint16_t)(0x22e2+frame*2));
    Write(g_ram,0xbc5,0x10f);Dkc1CoopWorldNoteNativeCamera(g_ram);
  }
  Dkc1CoopWorldSaveState(&actual);
  CHECK(!memcmp(&end.water,&actual.water,sizeof end.water) &&
        !memcmp(end.land_region,actual.land_region,sizeof end.land_region),
        "land camera split replay diverged");
  /* Same actors and same native history must produce the same framing.
   * The primary oracle uses Donkey ($056F=1); co-op follows Diddy. */
  Dkc1CoopWorldReset();Dkc1CoopWorldNoteNativeCamera(g_ram);
  for(unsigned frame=121;frame<=240;frame++) {
    Write(g_ram,0x28,(uint16_t)frame);
    Write(g_ram,0xb1b,(uint16_t)(0x22e2+frame));Write(g_ram,0xb1d,Read(g_ram,0xb1b));
    Write(g_ram,0xbc3,(uint16_t)(frame<180?0xef:0x11f));Write(g_ram,0xbc5,Read(g_ram,0xbc3));
    Dkc1Actor_SetSprXVelocity(g_ram,2,0x200);Dkc1Actor_SetSprXVelocity(g_ram,4,0x200);
    WaterCameraBus primary={0};memcpy(primary.ram,g_ram,sizeof primary.ram);
    CHECK(NativeCameraRun(&primary,mode,0),"primary oracle rejected");
    memcpy(g_ram+0x1a4b,primary.ram+0x1a4b,32);
    memcpy(g_ram+0x1af9,primary.ram+0x1af9,2);memcpy(g_ram+0x1b1b,primary.ram+0x1b1b,2);
    Write(g_ram,0x88b,Read(primary.ram,0x88b));Write(g_ram,0x895,Read(primary.ram,0x895));
    Dkc1CoopWorldNoteNativeCamera(g_ram);Dkc1CoopWorldGetCamera(g_ram,1,&x,&y);
    CHECK(x==Read(g_ram,0x88b)&&y==Read(g_ram,0x895),"co-located cameras follow different native rules");
  }
  }
  /* Player requests precede native camera consumption. The P2 bus must
   * receive its own requests, not the already-cleared primary $0579. */
  for(unsigned modeIndex=0;modeIndex<sizeof modes/sizeof modes[0];modeIndex++) {
    unsigned mode=modes[modeIndex];Write(g_ram,0x32,(uint16_t)mode);
    Dkc1CoopWorldReset();Dkc1CoopWorldNoteNativeCamera(g_ram);
    for(unsigned frame=241;frame<=300;frame++) {
      Write(g_ram,0x28,(uint16_t)frame);
      Write(g_ram,0xb1b,(uint16_t)(0x2400+frame));Write(g_ram,0xb1d,Read(g_ram,0xb1b));
      Dkc1Actor_SetSprXVelocity(g_ram,2,0x200);Dkc1Actor_SetSprXVelocity(g_ram,4,0x200);
      uint16_t requests=frame<280?0x80:0;
      Write(g_ram,0x579,requests);Dkc1CoopWorldNoteKongCamera(g_ram,4);
      WaterCameraBus primary={0};memcpy(primary.ram,g_ram,sizeof primary.ram);
      CHECK(NativeCameraRun(&primary,mode,0),"primary request oracle rejected");
      memcpy(g_ram+0x1a4b,primary.ram+0x1a4b,32);
      memcpy(g_ram+0x1af9,primary.ram+0x1af9,2);memcpy(g_ram+0x1b1b,primary.ram+0x1b1b,2);
      Write(g_ram,0x88b,Read(primary.ram,0x88b));Write(g_ram,0x895,Read(primary.ram,0x895));
      Write(g_ram,0x579,Read(primary.ram,0x579));
      Dkc1CoopWorldNoteNativeCamera(g_ram);uint16_t x,y;
      Dkc1CoopWorldGetCamera(g_ram,1,&x,&y);
      CHECK(x==Read(g_ram,0x88b)&&y==Read(g_ram,0x895),"consumed primary request changed P2 camera dynamics");
      Dkc1CoopWorldSnapshot saved;Dkc1CoopWorldSaveState(&saved);
      CHECK(saved.version==7 && saved.reserved==(0x100|requests),"P2 requests not serialized");
      Dkc1CoopWorldLoadState(&saved);
    }
  }
  /* A shared native cart view replaces stale private history each boundary. */
  Setup();Write(g_ram,0x32,5);Write(g_ram,0x3e,7);Write(g_ram,0x4a,512);
  Write(g_ram,0x88b,400);Write(g_ram,0x895,40);
  Write(g_ram,0xd4b,0x51);Write(g_ram,0x137b,4);Write(g_ram,0x102d,0x2e);
  s_world.water.ready=1;s_world.water.entrance=7;s_world.water.mode=5;
  s_world.water.height=512;s_world.water.slot=4;
  s_world.water.x=100;s_world.water.y=88;s_world.water.frame=0xffff;
  Dkc1CoopWorldSnapshot before_read,after_read;
  Dkc1CoopWorldSaveState(&before_read);uint16_t shared_x,shared_y;
  Dkc1CoopWorldGetCamera(g_ram,1,&shared_x,&shared_y);
  Dkc1CoopWorldSaveState(&after_read);
  CHECK(!memcmp(&before_read,&after_read,sizeof before_read),"shared camera redraw advanced history");
  Dkc1CoopWorldNoteNativeCamera(g_ram);
  CHECK(s_world.water.x==400 && s_world.water.y==40 &&
        !memcmp(s_world.water.motion,g_ram+0x1a4b,32),
        "shared cart retained stale private camera motion for exit");
  /* Release the shared view and apply exactly one cartridge update. */
  Write(g_ram,0x28,1);Write(g_ram,0x137b,0);
  Write(g_ram,0x102d,1);Write(g_ram,0xb1b,610);Write(g_ram,0xb1d,610);
  Write(g_ram,0xbc3,239);Write(g_ram,0xbc5,239);
  WaterCameraBus handoff_oracle={0};memcpy(handoff_oracle.ram,g_ram,sizeof handoff_oracle.ram);
  Write(handoff_oracle.ram,0x56f,2);
  CHECK(NativeCameraRun(&handoff_oracle,5,0),"handoff cartridge oracle rejected");
  Dkc1CoopWorldNoteNativeCamera(g_ram);
  Dkc1CoopWorldGetCamera(g_ram,1,&shared_x,&shared_y);
  CHECK(shared_x==Read(handoff_oracle.ram,0x88b) && shared_y==Read(handoff_oracle.ram,0x895),
        "cart handoff did not continue its completed native camera history");
  /* The old mode5 controller history cannot be continued by dry9BEC. */
  Dkc1CoopWorldSnapshot old=s_world,restored;
  old.version=5;old.water.ready=1;old.water.mode=5;old.water.slot=4;
  old.water.x=0x2300;old.water.y=0x900;old.cursor[0]=0x1234;
  Dkc1CoopWorldLoadState(&old);Dkc1CoopWorldSaveState(&restored);
  CHECK(!restored.water.ready && restored.cursor[0]==0x1234,
        "old mode5 camera history continued with a different controller");
  old.version=6;Dkc1CoopWorldLoadState(&old);Dkc1CoopWorldSaveState(&restored);
  CHECK(!memcmp(&restored.water,&old.water,sizeof old.water),
        "current mode5 camera history lost save/load continuity");
  old.version=5;old.water.mode=9;Dkc1CoopWorldLoadState(&old);Dkc1CoopWorldSaveState(&restored);
  CHECK(!memcmp(&restored.water,&old.water,sizeof old.water),
        "unrelated legacy vertical camera history was discarded");
  /* Migrating a valid generic continuation must not sweep horizontally
   * from the other player's camera before acquiring the native controller. */
  for(unsigned m=0;m<sizeof modes/sizeof modes[0];m++) {
    unsigned mode=modes[m];if(CameraController(mode)!=&kDryCamera)continue;
    Setup();Write(g_ram,0x32,(uint16_t)mode);Write(g_ram,0x3e,7);Write(g_ram,0x4a,512);
    Write(g_ram,0xb1d,0x2800);Write(g_ram,0xbc5,0xef);
    Write(g_ram,0x88b,0x229b);Write(g_ram,0x895,0x40);
    s_world.initialized=1;s_world.entrance=7;s_world.mode=(uint16_t)mode;s_world.height=512;
    s_world.camera.ready=1;s_world.camera.row=128;s_world.camera.column8=128*256;
    uint8_t before[sizeof g_ram];memcpy(before,g_ram,sizeof before);
    uint16_t x=Read(g_ram,0x88b),y=Read(g_ram,0x895);
    CHECK(NativeCamera(g_ram,4,1,&x,&y),"native continuation seed rejected");
    CHECK(x==0x2780 && y==0x40,"native acquisition displaced the existing horizontal viewport");
    CHECK(!memcmp(before,g_ram,sizeof before),"native acquisition leaked WRAM");
  }
  return failures?1:0;
}
'''
