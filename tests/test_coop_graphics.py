from pathlib import Path
import importlib.util
import unittest


ROOT = Path(__file__).resolve().parents[1]


def load_adapter():
    spec = importlib.util.spec_from_file_location(
        "coop_graphics_adapter", ROOT / "scripts/apply_dkc1_coop_graphics.py")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


class CoopGraphicsTests(unittest.TestCase):
    def test_legacy_scene_palette_selection_matches_native_property_bytes(self):
        rom = ROOT / 'dkc1.sfc'
        if not rom.exists():
            self.skipTest('supported private ROM required for palette selector')
        data = rom.read_bytes()
        self.assertEqual(data[0x358461:0x358465], bytes.fromhex('22 f0 f1 bd'))
        code = ','.join(str(b) for b in data[0x358424:0x358461])
        source = (ROOT / 'runner/dkc1_coop_graphics.c').read_text()
        helper = 'static uint16_t ScenePaletteSelection(' + source.split(
            'static uint16_t ScenePaletteSelection(', 1)[1].split(
            'int Dkc1CoopGraphicsLoadState(', 1)[0]
        from test_coop_mod import CoopModTests
        CoopModTests().compile_harness(r'''
#include <stdint.h>
#include <assert.h>
#include "snes/interp816.h"
static uint8_t ram[0x2000];
static const uint8_t code[]={''' + code + r'''};
static uint16_t Read16(unsigned a){return ram[a]|ram[a+1]<<8;}
static uint8_t Read(void *c,uint32_t a){
 if(a>=0xb58424 && a<0xb58461)return code[a-0xb58424];
 assert((uint16_t)a<sizeof ram);return ram[(uint16_t)a];
}
static void Write(void*c,uint32_t a,uint8_t v){assert(0);}
''' + helper + r'''
int main(void){
 for(unsigned e=0;e<0xe6;e++)for(unsigned flags=0;flags<4;flags++){
  ram[0x3e]=(uint8_t)e;ram[0x579]=(uint8_t)(flags&1);
  ram[0x57a]=(uint8_t)(flags>>1);
  Interp816 cpu={0};cpu.read=Read;cpu.write=Write;
  cpu.k=0xb5;cpu.db=0x7e;cpu.pc=0x8436;
  for(unsigned n=0;cpu.pc!=0x8461;n++){
   assert(n<40);interp816_runOpcode(&cpu);
  }
  assert(cpu.a==ScenePaletteSelection());
 }
 return 0;
}
''', ['snesrecomp/runner/src/snes/interp816.c'])

    def test_palette_upload_selects_native_ram_row_without_reblending(self):
        rom_path = ROOT / 'dkc1.sfc'
        if not rom_path.exists():
            self.skipTest('supported private ROM unavailable')
        rom = rom_path.read_bytes()
        # Execute the byte-exact queue routine, stopping before its RTS.
        data = ','.join(str(b) for b in rom[0x3df266:0x3df2d6])
        from test_coop_mod import CoopModTests
        CoopModTests().compile_harness(r'''
#include <assert.h>
#include <string.h>
#include "snes/interp816.h"
static uint8_t ram[0x2000];
static const uint8_t code[]={''' + data + r'''};
int interp816_opcode_hook(uint32_t pc){return 0;}
static uint8_t Read(void *c,uint32_t a){
 if(a>=0xbdf266 && a<0xbdf266+sizeof code)return code[a-0xbdf266];
 assert((uint16_t)a<sizeof ram);return ram[(uint16_t)a];
}
static void Write(void *c,uint32_t a,uint8_t v){assert((uint16_t)a<sizeof ram);ram[(uint16_t)a]=v;}
static void Put(unsigned a,uint16_t v){ram[a]=v;ram[a+1]=v>>8;}
static uint16_t Word(unsigned a){return ram[a]|ram[a+1]<<8;}
int main(void){
 for(unsigned active=0;active<2;active++){
  memset(ram,0,sizeof ram);Put(0x8c,0x849a);Put(0x76,12);Put(0x1e15,active?0x59:0);
  Interp816 cpu={0};cpu.read=Read;cpu.write=Write;cpu.k=cpu.db=0xbd;cpu.pc=0xf266;
  for(unsigned n=0;cpu.pc!=(active?0xf2d5:0xf29f);n++){
   assert(n<80);interp816_runOpcode(&cpu);
  }
  assert(Word(0x1a8f)==(active?(uint16_t)(0x849a+0xa5ad):0x849a));
  assert((Word(0x1a91)&255)==(active?0x7f:0xbc));
 }
 return 0;
}
''', ['snesrecomp/runner/src/snes/interp816.c'])

    def test_resources_do_not_share_physical_slots_and_restore_atomically(self):
        # Use the existing portable harness compiler; no ROM is required.
        from test_coop_mod import CoopModTests
        CoopModTests().compile_harness(r'''
#include <assert.h>
#include <stdlib.h>
#include "dkc1_coop_graphics.c"

uint8 g_ram[0x20000];
static uint8_t rom[0x400000];
static uint16_t entity_flags[128];
static int active;
static int mounted;
uint16_t Dkc1CoopActorPaletteOwner(const uint8_t *ram, uint16_t h) {
  return mounted && (h == 2 || h == 6) ? (uint16_t)(8 - h) : h;
}
int Dkc1CoopEntitiesActive(void) { return active; }
int Dkc1CoopEntityIsHandle(uint16_t h) {
  return !(h & 1u) && ((h >= 2 && h <= 0x32) || (h >= 0x80 && h <= 0xfe));
}
uint16_t Dkc1CoopEntityRead16(const uint8_t *ram, uint16_t base, uint16_t h) {
  assert(base == 0x0c69 && Dkc1CoopEntityIsHandle(h));
  return entity_flags[h >> 1];
}
void Dkc1CoopEntityWrite16(uint8_t *ram, uint16_t base, uint16_t h, uint16_t v) {
  assert(base == 0x0c69 && Dkc1CoopEntityIsHandle(h));
  entity_flags[h >> 1] = v;
}
uint16 cpu_read16(CpuState *cpu, uint8 bank, uint16 address) {
  assert(bank == 0); return Read16(address);
}
uint8 cpu_read8(CpuState *cpu, uint8 bank, uint16 address) {
  assert(bank == 0); return g_ram[address];
}
void cpu_write16(CpuState *cpu, uint8 bank, uint16 address, uint16 value) {
  assert(bank == 0);
  g_ram[address] = (uint8_t)value; g_ram[(uint16_t)(address + 1)] = value >> 8;
}
static void Put(unsigned address, uint16_t value) {
  g_ram[address] = (uint8_t)value; g_ram[address + 1] = value >> 8;
}
static void Palette(unsigned source, unsigned identity) {
  for (unsigned i = 0; i < 15; ++i) {
    unsigned offset = 0x3c0000 + source + i * 2;
    uint16_t value = (uint16_t)(identity + i);
    rom[offset] = (uint8_t)value; rom[offset + 1] = value >> 8;
  }
}

int main(void) {
  CpuState cpu = {0};
  Dkc1CoopGraphicsSetRom(rom, sizeof rom);
  Dkc1CoopGraphicsReset();
  cpu.X = 2; cpu.A = 0x7654; cpu.Y = 0x3456; cpu.P = 0x8b;
  CpuState before = cpu;
  uint8_t wram_before[sizeof g_ram];
  memcpy(wram_before, g_ram, sizeof g_ram);
  assert(!Dkc1CoopGraphicsAllocate(&cpu, 0));
  assert(!Dkc1CoopGraphicsAllocatePalette(&cpu, 0xb58141));
  assert(!Dkc1CoopGraphicsCapacity(&cpu));
  assert(!memcmp(&cpu, &before, sizeof cpu));
  assert(!memcmp(wram_before, g_ram, sizeof g_ram));

  active = 1;
  /* Kong initialization cancels its spawn upload. Virtual spawns did not
   * enqueue one: retain both ring indices, even across index zero. */
  for (unsigned index = 0; index < 16; ++index) {
    Put(0x1ad1, index); Put(0x1acf, index);
    cpu._flag_C = 1;
    assert(Dkc1CoopGraphicsCancelKongPaletteUpload(&cpu));
    assert(Read16(0x1ad1) == index && Read16(0x1acf) == index);
    assert(cpu.A == index && cpu._flag_C == 1);
  }
  /* Occupy all eleven hardware tile slots and eight hardware palettes.
   * Forty distinct actors must still own distinct graphics and colors. */
  memset(g_ram + 0x1a6f, 0xaa, 0x20);
  memset(g_ram + 0x1ad3, 0xbb, 0x20);
  uint8_t physical[0x84];
  memcpy(physical, g_ram + 0x1a6f, sizeof physical);
  for (uint16_t i = 0; i < 40; ++i) {
    const uint16_t h = (uint16_t)(0x80 + i * 2);
    cpu.X = h; entity_flags[h >> 1] = 0xf1ff;
    assert(Dkc1CoopGraphicsAllocate(&cpu, 0));
    assert(Dkc1CoopGraphicsBlocks(h) == 1);
    assert(entity_flags[h >> 1] == 0xf080);
    assert(cpu.A == 0xf080 && cpu.Y == 8 && !cpu._flag_C && !(cpu.P & 1));
    Put(0x86, h); cpu.A = (uint16_t)(0x8500 + i * 32);
    Palette(cpu.A, 0x100 + i * 16);
    assert(Dkc1CoopGraphicsAllocatePalette(&cpu, 0xb58141));
    assert(cpu.A == 0xc00 && cpu.X == 0xc && cpu._flag_Z && !cpu._flag_N);
    /* B58141's caller publishes the returned row in the actor flags. */
    entity_flags[h >> 1] = (entity_flags[h >> 1] & ~0xe00u) | cpu.A;
  }
  assert(!memcmp(physical, g_ram + 0x1a6f, sizeof physical));
  uint16_t colors[16];
  for (uint16_t i = 0; i < 40; ++i) {
    assert(Dkc1CoopGraphicsGetPalette((uint16_t)(0x80 + i * 2), colors));
    assert(colors[0] == 0 && colors[1] == 0x100 + i * 16);
    assert(colors[15] == 0x10e + i * 16);
  }
  cpu.X = 0x80; cpu.D = 0x100; Put(0x14c, 3);
  assert(Dkc1CoopGraphicsAllocate(&cpu, 1));
  assert(Dkc1CoopGraphicsBlocks(0x80) == 3);
  assert(cpu.X == 0x80 && cpu.A == 0x80 && cpu.Y == 12);
  assert(Read16(0x14c) == 0 && Read16(0x14e) == 0 && cpu._flag_Z);
  cpu.D = 0;
  /* A cave recreates Rambi, then updates the Kong drawn in the animal slot.
   * Its palette replacement must not destroy the new animal's own colors. */
  Put(0x86, 6); cpu.A = 0x8520;
  assert(Dkc1CoopGraphicsAllocatePalette(&cpu, 0xb58141));
  entity_flags[3] = entity_flags[1] = 0xc00;
  mounted = 1; cpu.X = 6; cpu.A = 0x8500;
  assert(Dkc1CoopGraphicsReplacePalette(&cpu, 0));
  assert(Dkc1CoopGraphicsGetPalette(6, colors) && colors[1] == 0x110);
  assert(Dkc1CoopGraphicsGetPalette(2, colors) && colors[1] == 0x100);
  /* Native room palette setup remaps only the leader to its animal slot;
   * the follower call still names the logical Kong, not the drawn animal. */
  cpu.S = 0x1f0; Put(0x1f1, 0x8108); g_ram[0x1f3] = 0xb8;
  cpu.X = 2; cpu.A = 0x8540;
  assert(Dkc1CoopGraphicsReplacePalette(&cpu, 0));
  assert(Dkc1CoopGraphicsGetPalette(6, colors) && colors[1] == 0x110);
  assert(Dkc1CoopGraphicsGetPalette(2, colors) && colors[1] == 0x120);
  mounted = 0;
  before = cpu;
  assert(!Dkc1CoopGraphicsAllocatePalette(&cpu, 0x80831b));
  assert(!memcmp(&before, &cpu, sizeof cpu));
  cpu.S = 0x1f0; cpu.PB = 0xbd; Put(0x1f1, 0x8140); g_ram[0x1f3] = 0xb5;
  Put(0x86, 4); cpu.A = 0x8500;
  assert(Dkc1CoopGraphicsAllocatePalette(&cpu, 0xffffffffu));
  entity_flags[2] = 0xc00;
  assert(Dkc1CoopGraphicsGetPalette(4, colors) && colors[1] == 0x100);

  /* Shared child colors are owned values, not dangling parent references. */
  Dkc1CoopGraphicsCopyPalette(2, 0x80);
  /* A child's native fixed-row selection supersedes inherited colors.
   * Reading it must neither alter ownership nor lose later virtual reuse. */
  entity_flags[1] = 0x2280; Put(0x1ad5, 0x8520);
  assert(!Dkc1CoopGraphicsGetPalette(2, colors));
  /* An empty physical source table does not prove a fixed replacement. */
  Put(0x1ad5, 0);
  assert(Dkc1CoopGraphicsGetPalette(2, colors) && colors[1] == 0x100);
  entity_flags[1] = 0x2c80;
  assert(Dkc1CoopGraphicsGetPalette(2, colors) && colors[1] == 0x100);
  Put(0x82, 0x80); cpu.A = 0xc00;
  assert(Dkc1CoopGraphicsReleasePalette(&cpu, 0xbdf483));
  assert(!Dkc1CoopGraphicsGetPalette(0x80, colors));
  assert(Dkc1CoopGraphicsGetPalette(2, colors) && colors[1] == 0x100);
  Dkc1CoopGraphicsResetEntity(0x80);
  assert(!Dkc1CoopGraphicsBlocks(0x80));
  assert(Dkc1CoopGraphicsGetPalette(2, colors) && colors[1] == 0x100);
  Dkc1CoopGraphicsUseFixedPalette(2);
  assert(!Dkc1CoopGraphicsGetPalette(2, colors));

  /* Mid-scene enable seeds authentic allocation and palette identity. */
  Put(0x0d47, 1); entity_flags[1] = 0x680;
  Put(0x1a77, 2); Put(0x1a79, 2); Put(0x1a7b, 0);
  Put(0x1ad9, 0x8500);
  Dkc1CoopGraphicsSeedNative();
  assert(Dkc1CoopGraphicsBlocks(2) == 2);
  assert(Dkc1CoopGraphicsGetPalette(2, colors) && colors[1] == 0x100);
  /* An explicit actor replacement does not update the physical source
   * table. That stale row must not override the actor's selected colors. */
  Put(0x1ad9, 0x8540); cpu.X = 2; cpu.A = 0x8500;
  assert(Dkc1CoopGraphicsReplacePalette(&cpu, 0));
  assert(Dkc1CoopGraphicsGetPalette(2, colors) && colors[1] == 0x100);
  Put(0x1ad9, 0x8500);
  /* The native scene property is an actor allocation, including existing
   * physical actors. The caller publishes the returned palette bits. */
  Put(0x86, 0x12); entity_flags[9] = 0x3280; cpu.A = 0x8cb0;
  Palette(0x8cb0, 0x1230); Palette(0x8cce, 0x4560);
  uint8_t rows[32]; memcpy(rows,g_ram+0x1ad3,sizeof rows);
  assert(Dkc1CoopGraphicsAllocatePalette(&cpu, 0xb58465));
  assert(cpu.A==0xc00 && cpu.X==12 && Read16(0x86)==0x12);
  entity_flags[9]=(entity_flags[9]&~0xe00u)|cpu.A;
  assert(!memcmp(rows,g_ram+0x1ad3,sizeof rows));
  assert(Dkc1CoopGraphicsGetPalette(0x12,colors) && colors[1]==0x1230);
  {
    GraphicsState old=s_state;
    old.version=2;
    for(unsigned i=0;i<128;i++)old.entities[i].palette_selection=0;
    entity_flags[9]=0x3280;Put(0x1ad5,0x8cce);Put(0x3e,0x36);Put(0x579,0);
    assert(Dkc1CoopGraphicsLoadState(&old,sizeof old));
    assert(Dkc1CoopGraphicsGetPalette(0x12,colors) && colors[1]==0x1230);
    /* A legacy source outside the current native selector is not guessed. */
    Put(0x579,0x100);
    assert(Dkc1CoopGraphicsLoadState(&old,sizeof old));
    assert(!Dkc1CoopGraphicsGetPalette(0x12,colors));
    Put(0x579,0);Put(0x1ad5,0);Put(0x3e,0);
  }
  Put(0x1e15, 0x10);
  const uint16_t ram_source = (uint16_t)(0x8500 + 0xa5ad);
  Put(0x10000u + ram_source, 0x1234);
  assert(Dkc1CoopGraphicsGetPalette(2, colors) && colors[1] == 0x1234);
  assert(Dkc1CoopGraphicsReadPalette(0x8500, colors) && colors[1] == 0x1234);
  Put(0x10000u + ram_source, 0x2345);
  assert(Dkc1CoopGraphicsReadPalette(0x8500, colors) && colors[1] == 0x2345);
  Put(0x1e15, 0);
  assert(Dkc1CoopGraphicsReadPalette(0x8500, colors) && colors[1] == 0x2345);
  Put(0x1dfd, 32); Put(0x1dff, 32);
  assert(Dkc1CoopGraphicsReadPalette(0x8500, colors) && colors[1] == 0x100);
  Put(0x1dfd, 0); Put(0x1dff, 0);

  /* Native bank-$7F rows already carry level colors. $7F:0204 is an
   * unrelated color component: varying it must never reblend these rows. */
  Palette(0x849A, 0x10);
  Palette(0x8350, 0x20);
  Put(0x1e15, 0x10);
  Put(0x3e, 8);
  Put(0x10204, 0x1000);
  Dkc1CoopGraphicsNoteFrame();
  {
    const uint16_t kong_weather = (uint16_t)(0x849Au + 0xa5adu);
    const uint16_t mount_weather = (uint16_t)(0x8350u + 0xa5adu);
    Put(0x10000u + kong_weather, 0x001f);
    Put(0x10000u + mount_weather, 0x001f);
    assert(Dkc1CoopGraphicsReadPalette(0x849A, colors) && colors[1] == 0x001f);
    assert(Dkc1CoopGraphicsReadPalette(0x8350, colors) && colors[1] == 0x001f);
    Put(0x10204, 0x0800);
    Dkc1CoopGraphicsNoteFrame();
    assert(Dkc1CoopGraphicsBeginTransaction());
    assert(Dkc1CoopGraphicsReadPalette(0x849A, colors) && colors[1] == 0x001f);
    assert(Dkc1CoopGraphicsReadPalette(0x8350, colors) && colors[1] == 0x001f);
    /* The packed native row is used without a second interpolation. */
    assert(Dkc1CoopGraphicsReadPalette(0x8500, colors) && colors[1] == 0x2345);
    Dkc1CoopGraphicsEndTransaction(0);
    assert(Dkc1CoopGraphicsReadPalette(0x8500,colors) && colors[1]==0x2345);
    size_t weather_size = Dkc1CoopGraphicsStateSize();
    uint8_t *weather_saved = malloc(weather_size);
    assert(weather_saved && Dkc1CoopGraphicsSaveState(weather_saved,weather_size));
    Dkc1CoopGraphicsReset();
    assert(Dkc1CoopGraphicsLoadState(weather_saved,weather_size));
    assert(Dkc1CoopGraphicsReadPalette(0x8500,colors) && colors[1]==0x2345);
    free(weather_saved);
    for(unsigned component=0;component<0x10000;component+=0x101) {
      Put(0x10204,(uint16_t)component);
      assert(Dkc1CoopGraphicsReadPalette(0x8500,colors) && colors[1]==0x2345);
    }
    Put(0x10204, 0);
    assert(Dkc1CoopGraphicsReadPalette(0x849A, colors) && colors[1] == 0x001f);
    assert(Dkc1CoopGraphicsReadPalette(0x8500, colors) && colors[1] == 0x2345);
  }
  Put(0x1e15, 0);
  Put(0x1dfd, 32); Put(0x1dff, 16);
  assert(Dkc1CoopGraphicsReadPalette(0x8500, colors) && colors[1] == 0x1223);
  Put(0x1dfd, 32); Put(0x1dff, 32);
  assert(Dkc1CoopGraphicsReadPalette(0x8500, colors) && colors[1] == 0x100);

  /* Native dry bonus selection ignores retained cave rows, including
   * snapshots from the withdrawn parent-lighting experiment. */
  Put(0x32, 1); Put(0x3e, 0x10); Put(0x1e15, 0x5a); Put(0x51a, 15);
  Dkc1CoopGraphicsNoteFrame();
  Put(0x51a, 0); Put(0x1e15, 0);
  Dkc1CoopGraphicsNoteFrame();
  Put(0x3e, 3); Put(0x1e15, 0x22);
  Dkc1CoopGraphicsNoteFrame();
  assert(Dkc1CoopGraphicsReadPalette(0x849a, colors) && colors[1] == 0x10);
  assert(Dkc1CoopGraphicsReadPalette(0x8350, colors) && colors[1] == 0x20);
  {
    size_t n = Dkc1CoopGraphicsStateSize();
    void *bonus = malloc(n);
    assert(Dkc1CoopGraphicsSaveState(bonus,n));
    ((GraphicsState*)bonus)->version = 3;
    for(unsigned i=0;i<128;i++)((GraphicsState*)bonus)->entities[i].palette_selection=0;
    ((GraphicsState*)bonus)->weather.reserved = 1;
    Dkc1CoopGraphicsReset();
    assert(Dkc1CoopGraphicsReadPalette(0x849a,colors) && colors[1] == 0x10);
    assert(Dkc1CoopGraphicsLoadState(bonus,n));
    assert(Dkc1CoopGraphicsReadPalette(0x849a,colors) && colors[1] == 0x10);
    free(bonus);
  }
  Put(0x3e, 0x10); Put(0x1e15, 0x5a);
  Dkc1CoopGraphicsNoteFrame();
  assert(Dkc1CoopGraphicsReadPalette(0x849a,colors) && colors[1] == 0x001f);
  Put(0x3e, 0x1a); Put(0x1e15, 0x22); Put(0x51a,15);
  Dkc1CoopGraphicsNoteFrame();
  assert(Dkc1CoopGraphicsReadPalette(0x849a,colors) && colors[1] == 0x10);
  Put(0x3e, 0x16); Put(0x1e15, 0); Put(0x51a, 15);
  Dkc1CoopGraphicsNoteFrame();
  Put(0x3e, 3); Put(0x1e15, 0x22);
  Dkc1CoopGraphicsNoteFrame();
  assert(Dkc1CoopGraphicsReadPalette(0x849a,colors) && colors[1] == 0x10);
  Put(0x1e15, 0); Put(0x1dfd, 32); Put(0x1dff, 32);

  const size_t size = Dkc1CoopGraphicsStateSize();
  uint8_t *saved = malloc(size), *check = malloc(size);
  assert(saved && check && Dkc1CoopGraphicsSaveState(saved, size));
  assert(Dkc1CoopGraphicsBeginTransaction());
  assert(!Dkc1CoopGraphicsBeginTransaction());
  Dkc1CoopGraphicsReset();
  Dkc1CoopGraphicsEndTransaction(0);
  assert(Dkc1CoopGraphicsSaveState(check, size) && !memcmp(saved, check, size));
  Dkc1CoopGraphicsResetEntity(2);
  assert(!Dkc1CoopGraphicsGetPalette(2, colors));
  assert(Dkc1CoopGraphicsLoadState(saved, size));
  assert(Dkc1CoopGraphicsGetPalette(2, colors) && colors[1] == 0x100);
  {
    GraphicsState invalid=*(GraphicsState*)saved;
    invalid.entities[1].palette_selection=0x88;
    assert(!Dkc1CoopGraphicsLoadState(&invalid,sizeof invalid));
    invalid=*(GraphicsState*)saved;invalid.version=2;
    assert(!Dkc1CoopGraphicsLoadState(&invalid,sizeof invalid));
  }
  saved[0] ^= 0x80;
  assert(!Dkc1CoopGraphicsLoadState(saved, size));
  assert(!Dkc1CoopGraphicsLoadState(saved, size - 1));
  assert(Dkc1CoopGraphicsGetPalette(2, colors) && colors[1] == 0x100);
  assert(Dkc1CoopGraphicsBeginTransaction());
  Dkc1CoopGraphicsResetEntity(2);
  Dkc1CoopGraphicsEndTransaction(1);
  assert(!Dkc1CoopGraphicsGetPalette(2, colors));
  /* Historical v1 graphics blocks retain all ownership fields and start
   * weather history cold; their length still identifies that wire layout. */
  saved[0]=1;
  for(unsigned i=0;i<128;i++)((GraphicsState*)saved)->entities[i].palette_selection=0;
  assert(Dkc1CoopGraphicsLoadState(saved,size-sizeof(Dkc1CoopWeatherState)));
  assert(Dkc1CoopGraphicsGetPalette(2,colors));
  free(saved); free(check);
  return 0;
}
''')

    def test_adapter_is_idempotent_and_preserves_native_returns(self):
        generated = ROOT / "generated/snesrecomp"
        paths = list(generated.glob("*.c"))
        if not paths:
            self.skipTest("private generated sources are unavailable")
        adapter = load_adapter()
        sources = {p: p.read_text(encoding="utf-8") for p in paths}
        adapter.apply_overrides(sources)
        patched = sources.copy()
        self.assertEqual({"coop_graphics": 0}, adapter.apply_overrides(sources))
        self.assertEqual(patched, sources)
        for text in sources.values():
            if 'L_COOP_GRAPHICS_RETURN_' not in text:
                continue
            self.assertRegex(text, r'/\* RT[SL] pop hardware return frame \*/')
        # A changed native return must reject the entire batch before writes.
        sources = {p: p.read_text(encoding="utf-8") for p in paths}
        path = next(p for p, t in sources.items()
                    if 'RecompReturn CODE_BDF346_M0X0(CpuState *cpu) {' in t)
        sources[path] = sources[path].replace('0xbdf36du, _entry_s',
                                             '0xbdf36eu, _entry_s')
        with self.assertRaisesRegex(ValueError, 'missing hardware return'):
            adapter.apply_overrides(sources)


if __name__ == "__main__":
    unittest.main()
