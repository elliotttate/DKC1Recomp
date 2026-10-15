from pathlib import Path
import unittest

import test_coop_mod

ROOT = Path(__file__).resolve().parents[1]


class CoopViewTransactionTests(unittest.TestCase):
    def test_fog_update_ignores_other_camera_and_keeps_serialized_history(self):
        rom = ROOT / 'dkc1.sfc'
        if not rom.exists():
            self.skipTest('supported private ROM required for native fog oracle')
        test_coop_mod.CoopModTests().compile_harness(r'''
#include "dkc1_coop_fog.h"
#include <assert.h>
#include <stdio.h>
static uint8_t rom[0x400000], native[0x20000];
static Dkc1CoopScrollBus bus, peer_bus, oracle;
int main(void){
 FILE *f=fopen("@ROM@","rb");assert(f);
 assert(fread(rom,1,sizeof rom,f)==sizeof rom);fclose(f);
 Dkc1CoopFogState a={0},b={0},restored={0};
 for(unsigned buffer=0;buffer<2;buffer++)for(unsigned row=0;row<57;row++){
  unsigned p=0x10200+buffer*0x120+row*5;
  native[p]=row<56?4:0;
  Dkc1CoopFogWrite(native,p+1,(uint16_t)(0x300+row));
  Dkc1CoopFogWrite(native,p+3,(uint16_t)(0x100-row));
 }
 Dkc1CoopFogWrite(native,0x28,0xffff);Dkc1CoopFogWrite(native,0x1df5,1);
 assert(Dkc1CoopFogPrepare(&a,&bus,rom,native,10,0x300,0x100,0x500,0x80));
 b=a;
 for(unsigned frame=0;frame<80;frame++){
  uint16_t x=(uint16_t)(0x500+frame*3),y=(uint16_t)(0x80+frame/4);
  Dkc1CoopFogWrite(native,0x28,(uint16_t)frame);
  /* Independent stock-byte oracle starts at the scroll tail, with the
   * camera delta normally produced by B4EB..B4FE. */
  memset(&oracle,0,sizeof oracle);oracle.rom=rom;
  memcpy(oracle.ram,native,sizeof native);
  memcpy(oracle.ram+0x10200,a.tables,sizeof a.tables);
  Dkc1CoopFogWrite(oracle.ram,0x88b,x);Dkc1CoopFogWrite(oracle.ram,0x895,y);
  Dkc1CoopFogWrite(oracle.ram,0xa7b,a.previous_x);
  Dkc1CoopFogWrite(oracle.ram,0xa79,(uint16_t)(y-a.previous_y));
  assert(Dkc1CoopScrollRunAt(&oracle,0x80b547));
  assert(Dkc1CoopFogPrepare(&a,&bus,rom,native,10,0x300,0x100,x,y));
  assert(!memcmp(a.tables,oracle.ram+0x10200,sizeof a.tables));
  /* Arbitrary P1 coordinates, deltas and existing tables cannot leak. */
  Dkc1CoopFogWrite(native,0x88b,(uint16_t)(0x8000+frame*13));
  Dkc1CoopFogWrite(native,0x895,(uint16_t)(0x8000-frame*7));
  Dkc1CoopFogWrite(native,0xa79,0xfffe);Dkc1CoopFogWrite(native,0xa7b,0xffff);
  memset(native+0x10200,0xa5,0x240);
  assert(Dkc1CoopFogPrepare(&b,&peer_bus,rom,native,10,0x8000,0x8000,x,y));
  assert(!memcmp(&a,&b,sizeof a));assert(Dkc1CoopFogStateValid(&a));
  Dkc1CoopFogState before=a;
  assert(Dkc1CoopFogPrepare(&a,&bus,rom,native,10,0,0,x,y));
  assert(!memcmp(&a,&before,sizeof a)); /* repeated render does not tick */
  if(frame==39) restored=a;
  if(frame>39){
   assert(Dkc1CoopFogPrepare(&restored,&peer_bus,rom,native,10,0,0,x,y));
   assert(!memcmp(&a,&restored,sizeof a));
  }
 }
 return 0;
}
'''.replace('@ROM@', rom.as_posix()), ['snesrecomp/runner/src/snes/interp816.c'])

    def test_view_snapshot_accepts_legacy_fade_and_validates_fog_extension(self):
        test_coop_mod.CoopModTests().compile_harness(r'''
#include <assert.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
enum {kMaxWidth=2,kHeight=224};
''' + _fade_declarations + r'''
int main(void){
 uint8_t saved[sizeof s_fade+sizeof s_fog],legacy[sizeof s_fade];
 s_fade.version=1;s_fade.width=2;s_fade.valid=3;
 s_fog.version=1;s_fog.valid=1;s_fog.entrance=10;s_fog.tick=0xffff;
 for(unsigned buffer=0;buffer<2;buffer++)for(unsigned row=0;row<56;row++)
  s_fog.tables[buffer*0x120+row*5]=4;
 assert(Dkc1CoopViewStateSize()==sizeof saved);
 assert(Dkc1CoopViewSaveState(saved,sizeof saved));
 assert(Dkc1CoopViewLoadState(saved,sizeof saved));
 assert(s_fog.tick==0xffff && s_fog.valid==1 && s_fade.valid==3);
 memcpy(legacy,saved,sizeof legacy);
 assert(Dkc1CoopViewLoadState(legacy,sizeof legacy));
 assert(!s_fog.valid && s_fade.valid==3);
 Dkc1CoopFogState bad;memcpy(&bad,saved+sizeof s_fade,sizeof bad);
 bad.tables[0x120+55*5]=3;memcpy(saved+sizeof s_fade,&bad,sizeof bad);
 assert(!Dkc1CoopViewLoadState(saved,sizeof saved));
 assert(!Dkc1CoopViewLoadState(legacy,sizeof legacy-1));
 assert(!Dkc1CoopViewSaveState(legacy,sizeof legacy));
 return 0;
}
''')

    def test_fog_keeps_native_scroll_history_and_projects_both_buffers(self):
        source = (ROOT / 'runner/dkc1_coop_view.c').read_text()
        helper = 'static int PrepareFog(' + source.split(
            'static int PrepareFog(', 1)[1].split('static Dkc1CoopScrollBus s_flash;', 1)[0]
        helper += r'''
static int FixtureFog(int number,uint16_t x,uint16_t y,ViewHdma channels[8]) {
 Dkc1CoopBackgroundProtocol p=Dkc1CoopBackgroundDecode(entrance,descriptor,0);
 return PrepareFog(number,x,y,channels,&p);
}
#define PrepareFog FixtureFog
'''
        test_coop_mod.CoopModTests().compile_harness(r'''
#include <stdint.h>
#include <assert.h>
#include <string.h>
#include "dkc1_coop_fog.h"
#include "dkc1_coop_background.h"
static uint8_t memory[0x20000], original[0x20000], *g_ram=memory;
static uint8_t s_backup[0x20000];
static const uint8_t *g_rom;
static Dkc1CoopFogState s_fog;
static Dkc1CoopScrollBus s_fog_bus;
static uint16_t s_native_cam_x, s_native_cam_y, entrance=10;
static uint8_t descriptor[]={0xfe,0x8f};
static struct {void *cart;} snes,*g_snes=&snes;
typedef struct {uint16_t table;uint8_t bank,mode,reg,enabled;} ViewHdma;
static uint16_t Read16(unsigned a){return a==0x3e?entrance:memory[a]|memory[a+1]<<8;}
static void Write16(unsigned a,uint16_t v){memory[a]=v;memory[a+1]=v>>8;}
static const uint8_t *cart_getRomPtr(void*c,uint8_t b,uint16_t a){
 assert(b==0x80 && a==0xc5bd+entrance*4);return descriptor;
}
''' + helper + r'''
int main(void){
 const uint16_t cameras[]={0,1,3,4,0x672,0x7fff,0x8000,0xffff};
 ViewHdma channels[8]={0};
 for(unsigned buffer=0;buffer<2;buffer++)for(unsigned c=0;c<8;c++){
  memset(memory,0xa5,sizeof memory);
  memset(&s_fog,0,sizeof s_fog);
  ViewHdma *h=&channels[1];h->enabled=1;h->bank=0x7f;h->mode=3;h->reg=0xf;
  h->table=buffer?0x320:0x200;unsigned base=0x10000+h->table;
  for(unsigned row=0;row<56;row++){
   memory[base+row*5]=4;
   Write16(base+row*5+1,(uint16_t)(0xffeeu + row));
   Write16(base+row*5+3,(uint16_t)(0xffdd-row));
  }
  memcpy(original,memory,sizeof memory);memcpy(s_backup,memory,sizeof memory);
  s_native_cam_x=cameras[c];s_native_cam_y=0xfff0;
  assert(PrepareFog(0,0x8001,7,channels)==2);
  assert(!memcmp(original,memory,sizeof memory));
  assert(PrepareFog(1,0x8001,7,channels)==2);
  uint16_t dx=(uint16_t)(0x8001+0x8001/4-cameras[c]-cameras[c]/4);
  for(unsigned row=0;row<56;row++){
   unsigned a=base+row*5;
   assert(memory[a]==4 && Read16(a+1)==(uint16_t)(0xffeeu + row+dx));
   assert(Read16(a+3)==(uint16_t)(0xffdd-row+23));
   memcpy(original+a+1,memory+a+1,4);
  }
  memcpy(original+0x10200,memory+0x10200,0x240);
  assert(!memcmp(original,memory,sizeof memory)); /* only the private fog buffers change */
 }
 memcpy(original,memory,sizeof memory);descriptor[0]=0x9c;
 assert(PrepareFog(1,0,0,channels)==1 && !memcmp(original,memory,sizeof memory));
 descriptor[0]=0xfe;channels[1].mode|=64;
 assert(!PrepareFog(1,0,0,channels) && !memcmp(original,memory,sizeof memory));
 channels[1].mode=3;memory[0x10320+55*5]=3;memcpy(original,memory,sizeof memory);
 assert(!PrepareFog(1,0,0,channels) && !memcmp(original,memory,sizeof memory));
 entrance=0xe6;assert(PrepareFog(1,0,0,channels)==1);
 return 0;
}
''', ['snesrecomp/runner/src/snes/interp816.c'])
        rom = ROOT / 'dkc1.sfc'
        if rom.exists():
            data = rom.read_bytes()
            self.assertEqual(data[0xc5e5:0xc5e9], bytes.fromhex('fe 8f 6c 98'))
            self.assertEqual(data[0xb5da:0xb5e5], bytes.fromhex('ad 8b 08 4a 4a 18 6d 8b 08 85 6c'))
            self.assertEqual(data[0xb4b9:0xb4ca], bytes.fromhex(
                'a2 00 02 a5 28 29 01 00 d0 03 a2 20 03 8e 12 43 60'))

    def test_preparation_scope_keeps_multi_call_lifetime_and_rolls_back(self):
        source = (ROOT / 'runner/dkc1_coop_view.c').read_text()
        scope = 'typedef struct ViewPreparation {' + source.split(
            'typedef struct ViewPreparation {', 1)[1].split('static void PrepareMillstoneAnchor', 1)[0]
        test_coop_mod.CoopModTests().compile_harness(r'''
#include <assert.h>
#include <string.h>
static int active,execution,reject,events[16],n;
static int Dkc1CoopEntitiesBeginTransaction(void) {
  events[n++]=1;if(reject || active)return 0;active=1;return 1;
}
static void RtlSpeculativeExecutionBegin(void){assert(active && !execution);execution=1;events[n++]=2;}
static void RtlSpeculativeExecutionEnd(void){assert(execution && active);execution=0;events[n++]=5;}
static void Dkc1CoopEntitiesEndTransaction(int commit){assert(active && !execution && !commit);active=0;events[n++]=6;}
static void NativeCall(int event){assert(active && execution);events[n++]=event;}
''' + scope + r'''
int main(void) {
  ViewPreparation scope;
  assert(BeginViewPreparation(&scope));assert(active && !execution);
  ExecuteViewPreparation(&scope);NativeCall(3);NativeCall(4);
  EndViewPreparation(&scope);
  const int expected[]={1,2,3,4,5,6};
  assert(n==6 && !memcmp(events,expected,sizeof expected));
  EndViewPreparation(&scope);assert(n==6); /* no duplicate unwind */
  n=0;active=1;assert(!BeginViewPreparation(&scope));
  EndViewPreparation(&scope);assert(n==1 && active); /* failed nested acquire is not owned */
  active=0;reject=1;n=0;assert(!BeginViewPreparation(&scope));
  EndViewPreparation(&scope);assert(n==1 && !active && !execution);
  return 0;
}
''')

    def test_mine_overlay_rejects_unproven_margin_tiles(self):
        source = (ROOT / 'runner/dkc1_coop_view.c').read_text()
        private = 'static uint16_t TileAddress(' + source.split(
            'static uint16_t TileAddress(', 1)[1].split('/* Calibrate against', 1)[0]
        test_coop_mod.CoopModTests().compile_harness('#include "dkc1_coop_oam.h"\n' + r'''
#include <stdint.h>
#include <assert.h>
#include <string.h>
typedef struct { uint16_t vram[0x8000],hScroll[4],vScroll[4],bgTileAdr;
  uint8_t bgmode,bgXsc[4]; } Ppu;
static Ppu s_seed,original;
static struct { void *cart; } snes,*g_snes=&snes;
static uint8_t descriptor[4]={0xc3,0x90,0,0};
static uint16_t Read16(unsigned a){return 0x2e;}
static const uint8_t *cart_getRomPtr(void *cart,uint8_t b,uint16_t a){return descriptor;}
#define PPU_bigTiles(p,l) ((p)->bgmode & (0x10u<<(l)))
#define PPU_bgTilemapAdr(p,l) (((p)->bgXsc[l]&0xfcu)<<8)
#define PPU_bgTileAdr(p,l) ((((p)->bgTileAdr>>((l)*4))&15u)<<12)
''' + private + r'''
int main(void){
  s_seed.bgmode=1;s_seed.bgXsc[1]=0x79;s_seed.hScroll[1]=112;s_seed.vScroll[1]=94;
  /* These columns are outside the cartridge picture but inside wide output. */
  for(unsigned y=0;y<32;y++)for(unsigned x=48;x<64;x++)
    s_seed.vram[TileAddress(0x7800,1,x,y)]=1;
  s_seed.vram[16]=0xdead;original=s_seed;
  assert(MineOverlayNeedsNativeBounds());assert(!memcmp(&original,&s_seed,sizeof s_seed));
  s_seed.vram[TileAddress(0x7800,1,45,12)]=1;
  assert(MineOverlayNeedsNativeBounds()); /* native art does not validate margins */
  s_seed.vram[TileAddress(0x7800,1,45,12)]=0;
  s_seed.hScroll[1]=136;assert(MineOverlayNeedsNativeBounds());
  s_seed.hScroll[1]=112;s_seed.vram[0]=1;assert(MineOverlayNeedsNativeBounds());
  s_seed.vram[0]=0;s_seed.bgmode|=0x20;assert(!MineOverlayNeedsNativeBounds());
  s_seed.bgmode=1;descriptor[0]=0x9c;assert(!MineOverlayNeedsNativeBounds());
  return 0;
}
''')
        rom = ROOT / 'dkc1.sfc'
        if rom.exists():
            data = rom.read_bytes()
            self.assertEqual(data[0xc675:0xc677], bytes.fromhex('c3 90'))
            self.assertEqual(data[0x90ea:0x90fe], bytes.fromhex(
                'ad 8b 08 e2 20 8d 0d 21 eb 8d 0d 21 eb 8d 0f 21 eb 8d 0f 21'))

    def test_ceiling_light_long_hold_does_not_stream_palette_bytes(self):
        source = (ROOT / 'runner/dkc1_coop_view.c').read_text()
        private = 'static void HdmaLine(' + source.split('static void HdmaLine(', 1)[1].split('void Dkc1CoopViewInvalidate', 1)[0]
        test_coop_mod.CoopModTests().compile_harness('#include "dkc1_coop_oam.h"\n' + HDMA_SUPPORT + private + r'''
int main(void){
  const unsigned delays[]={128,129,149,255,256,257,355};
  for(unsigned mode=2;mode<=3;mode++)for(unsigned d=0;d<7;d++){
    unsigned n=mode==2?2:4,delay=delays[d];
    memset(ram,0,sizeof ram);memset(count,0,sizeof count);mode_output=0;
    ram[0x1200]=(uint8_t)delay;
    for(unsigned i=0;i<n;i++)ram[0x1201+i]=(uint8_t)(0x10+i);
    ram[0x1201+n]=1;
    for(unsigned i=0;i<n;i++)ram[0x1202+n+i]=(uint8_t)(0x30+i);
    ViewHdma h={0};h.enabled=1;h.bank=0x7e;h.mode=(uint8_t)mode;h.reg=0x21;
    h.table=0x1200;h.initial_hold=(uint16_t)delay;
    HdmaLine(&h);assert(count[0]==n && h.table==0x1201+n);
    for(unsigned line=1;line<delay;line++){
      HdmaLine(&h);assert(count[0]==n && h.table==0x1201+n);
    }
    assert(!h.initial_hold);
    HdmaLine(&h);assert(count[0]==n*2 && h.table==0x1202+n*2);
    HdmaLine(&h);assert(!h.enabled);
  }
  return 0;
}
''')

    def test_millstone_window_matches_native_vertical_selector(self):
        rom = ROOT / 'dkc1.sfc'
        if not rom.exists():
            self.skipTest('supported private ROM required for native wheel oracle')
        receipt = ','.join(str(b) for b in rom.read_bytes()[0x92de:0x9326])
        harness = r'''
#include <assert.h>
#include <stdint.h>
#include <string.h>
#include "dkc1_coop_scroll.h"
#include <stdio.h>
static uint8_t memory[0x2000],native[0x2000],rom_data[0x400000];
static Dkc1CoopScrollBus bus;
static struct {uint16_t vScroll[4];} s_view;
static struct {void *cart;} snes,*g_snes=&snes;
static uint8_t descriptor[]={0x26,0x93};
static uint16_t Read16(unsigned a){return memory[a]|memory[a+1]<<8;}
static void Write16(unsigned a,uint16_t v){memory[a]=v;memory[a+1]=v>>8;}
static const uint8_t *cart_getRomPtr(void *c,uint8_t b,uint16_t a){return descriptor;}
static const uint8_t code[]={/* SELECTOR */};
static uint8_t ReadNative(void *context,uint32_t a){
 if(a>=0x8092de&&a<0x809326)return code[a-0x8092de];
 assert((a&0xffff)<sizeof native);return native[a&0xffff];
}
static void WriteNative(void *context,uint32_t a,uint8_t v){
 a&=0xffff;assert(a>=0x687&&a<=0x68c);native[a]=v;
}
''' + r'''
int main(void){
 FILE *f=fopen("@ROM@","rb");assert(f);assert(fread(rom_data,1,sizeof rom_data,f)==sizeof rom_data);fclose(f);
 for(unsigned q=0;q<0x10000;q++){
  memset(memory,0x77,sizeof memory);memset(native,0x77,sizeof native);
  memset(&bus,0,sizeof bus);bus.rom=rom_data;
  bus.ram[0xdd]=0x80;
  uint16_t wheel_y=(uint16_t)(q+0x1c0u);
  bus.ram[0xdf]=(uint8_t)wheel_y;bus.ram[0xe0]=wheel_y>>8;
  assert(Dkc1CoopScrollRun(&bus,0x9326));
  Interp816 cpu={0};cpu.read=ReadNative;cpu.write=WriteNative;
  cpu.k=0x80;cpu.db=0x7e;cpu.pc=0x92de;cpu.a=(uint16_t)q;cpu.sp=0x1fd;
  native[0x1fe]=0xfe;native[0x1ff]=0xff;
  for(unsigned n=0;n<30&&cpu.pc!=0xffff;n++)interp816_runOpcode(&cpu);
  assert(cpu.pc==0xffff&&cpu.sp==0x1ff);
  /* A zero terminator makes later stale bytes irrelevant. */
  unsigned size=native[0x689]?6:4;
  assert(!memcmp(bus.ram+0x687,native+0x687,size));
 }
 return 0;
}
'''
        test_coop_mod.CoopModTests().compile_harness('#include "dkc1_coop_oam.h"\n' + harness.replace(
            '/* SELECTOR */', receipt).replace('@ROM@',rom.as_posix()), ['snesrecomp/runner/src/snes/interp816.c'])

    def test_ceiling_light_rebuilds_displayed_buffer_for_both_phases(self):
        source = (ROOT / 'runner/dkc1_coop_view.c').read_text()
        helper = 'static int PrepareCeilingLight(' + source.split(
            'static int PrepareCeilingLight(', 1)[1].split('static int PrepareFog(', 1)[0]
        helper += r'''
static int FixtureLight(int number,uint16_t x,uint16_t y,ViewHdma channels[8]) {
 Dkc1CoopBackgroundProtocol p=Dkc1CoopBackgroundDecode(Read16(0x3e),descriptor,0);
 return PrepareCeilingLight(number,x,y,channels,&p);
}
#define PrepareCeilingLight FixtureLight
'''
        test_coop_mod.CoopModTests().compile_harness('#include "dkc1_coop_oam.h"\n' + r'''
#include <assert.h>
#include <stdint.h>
#include <string.h>
#include "dkc1_coop_light.h"
#include "dkc1_coop_background.h"
static uint8_t memory[0x20000],s_backup[0x20000],*g_ram=memory;
static struct {uint8_t screenEnabled[2];uint16_t vScroll[4],fixedColor;} s_view;
static struct {void *cart;} snes,*g_snes=&snes;
typedef struct {uint16_t table,initial_hold;} ViewHdma;
static uint8_t descriptor[]={0x9c,0x8f};
static uint16_t Read16(unsigned a){return memory[a]|memory[a+1]<<8;}
static void Write16(unsigned a,uint16_t v){memory[a]=v;memory[a+1]=v>>8;}
static const uint8_t *cart_getRomPtr(void *c,uint8_t b,uint16_t a){return descriptor;}
static int Dkc1CoopEntityIsHandle(uint16_t h){return h==2;}
static uint16_t Dkc1CoopEntityNextHandle(uint16_t h){return h+2;}
static uint16_t Dkc1CoopEntityRead16(const uint8_t *r,unsigned a,uint16_t h){
  return a==0xd45?0x5a:a==0xb19?0xcfb0:0xd0;
}
static void RtlSpeculativeExecutionBegin(void){}
static void RtlSpeculativeExecutionEnd(void){}
static int CODE_80B788_M0X0, CODE_80B891_M0X0, rejected;
static unsigned preparations;
static int RunPreparation(int function,uint8_t bank,void *hud){
  preparations++;
  /* $80B90B: odd update writes $0200; even update writes $0500. */
  if(function==CODE_80B891_M0X0){
    unsigned table=(Read16(0x28)&1)?0x10203:0x10503;
    Write16(table,Read16(0x88b));
  }
  return !rejected;
}
''' + helper + r'''
int main(void){
  CODE_80B788_M0X0=1;CODE_80B891_M0X0=2;
  const uint16_t clocks[]={0,1,2,0xffff};
  for(unsigned c=0;c<4;c++)for(unsigned primary=0;primary<3;primary++){
    memset(memory,0x77,sizeof memory);Write16(0x32,6);Write16(0x3e,0xd9);
    Write16(0x28,clocks[c]);Write16(0x579,0x200);Write16(0x88b,0xcf07);
    /* Distinct inherited primary buffers must not survive in the displayed one. */
    Write16(0x10203,0xc000+primary);Write16(0x10503,0xd000+primary);
    ViewHdma channels[8]={0};
    assert(PrepareCeilingLight(1,0xcf07,0x101,channels)==2);
    assert(Read16(0x28)==clocks[c]);
    assert(Read16(0x10000+channels[1].table+3)==0xcf07);
  }
  Write16(0x28,1);
  ViewHdma long_hold[8]={0};
  assert(PrepareCeilingLight(1,0xcf07,0,long_hold)==2);
  assert(long_hold[1].initial_hold==297 && long_hold[2].initial_hold==299 &&
         long_hold[3].initial_hold==299);
  memset(long_hold,0,sizeof long_hold);
  assert(PrepareCeilingLight(1,0xcf07,170,long_hold)==2);
  assert(!long_hold[1].initial_hold && long_hold[2].initial_hold==129 &&
         long_hold[3].initial_hold==129);
  rejected=1;Write16(0x28,0);
  ViewHdma channels[8]={0};assert(!PrepareCeilingLight(1,0xcf07,0x101,channels));
  assert(Read16(0x28)==0);
  unsigned old=preparations;assert(PrepareCeilingLight(0,0xcf07,0x101,channels)==2);
  assert(old==preparations);
  rejected=0;descriptor[0]=0x37;descriptor[1]=0x91;
  Write16(0x32,2);Write16(0x3e,0x31);Write16(0x28,1);
  for(unsigned red=0;red<2;red++){
    Write16(0x579,red?0x200:0);
    memset(channels,0,sizeof channels);
    assert(PrepareCeilingLight(1,0xcf07,0,channels)==2);
    assert(s_view.screenEnabled[0]==4 && s_view.screenEnabled[1]==0x13);
    assert(channels[2].table==(red?0x2808:0x2000));
    assert(channels[3].table==(red?0x2c0a:0x2402));
    assert(memory[channels[2].table]==(uint8_t)299);
    assert(channels[2].initial_hold==299);
    old=preparations;
    assert(PrepareCeilingLight(0,0xcf07,0,channels)==2 && old==preparations);
  }
  descriptor[0]=0x96; /* Loopy Lights uses the same beam with different planes. */
  Write16(0x579,0x200);memset(channels,0,sizeof channels);
  assert(PrepareCeilingLight(1,0xcf07,0,channels)==2);
  assert(s_view.screenEnabled[0]==0x13 && s_view.screenEnabled[1]==4);
  assert(s_view.fixedColor==0x0c63);
  assert(channels[2].table==0x2000 && channels[3].table==0x2402);
  Write16(0x579,0);memset(channels,0,sizeof channels);
  assert(PrepareCeilingLight(1,0xcf07,0,channels)==2);
  assert(s_view.screenEnabled[0]==0x13 && s_view.screenEnabled[1]==0);
  assert(s_view.fixedColor==0x2529);
  old=preparations;
  assert(PrepareCeilingLight(1,0xc000,0,channels)==2 && preparations==old);
  assert(s_view.screenEnabled[0]==0x13 && s_view.screenEnabled[1]==0);
  descriptor[0]=0xf5; /* Unproven NMI retains its inherited tables. */
  old=preparations;
  assert(PrepareCeilingLight(1,0xcf07,0,channels)==1 && old==preparations);
  return 0;
}
''')

    def test_cart_keeps_first_riders_view_and_restores_guest_state(self):
        source = (ROOT / 'runner/dkc1_coop_view.c').read_text()
        draw = 'typedef struct CoopViewPlan {' + source.split('typedef struct CoopViewPlan {', 1)[1]
        test_coop_mod.CoopModTests().compile_harness('#include "dkc1_coop_oam.h"\n' + VIEW_SUPPORT + draw + r"""
int main(void) {
  uint8_t primary[8*224];memset(primary,0x77,sizeof primary);
  for(int rider=2;rider<=4;rider+=2){
    calls=0;shared_cart=1;cart_rider=rider;memset(g_ram,0x19,sizeof g_ram);
    Dkc1CoopViewDraw();assert(calls==1 && s_ready==(rider==2?1:2));
    assert(s_projection_seat==(rider==2?0:1));
    for(unsigned i=0;i<sizeof g_ram;i++)assert(g_ram[i]==0x19);
    assert(g_main_cpu_cycles_estimate==1 && g_snesrecomp_last_hdmaen==5);
    const uint8_t *pixels=Dkc1PresentationPixels(primary,8);
    for(unsigned i=0;i<8*224;i++){
      assert(pixels[i]==(rider==2?0x33:0));
      assert(pixels[i+8*224]==(rider==4?0x33:0));
    }
    /* Native jump releases the seat while retaining the trip owner. */
    shared_cart=0;cart_jump=1;calls=0;Dkc1CoopViewDraw();
    assert(calls==1 && s_ready==(rider==2?1:2) && s_projection_seat==rider/2-1);
    cart_jump=0;
    /* A final fall drops the gameplay binding. Fade only the formerly
     * visible view; the other buffer was scratch, not visible history. */
    alive[0]=alive[1]=0;shared_cart=0;calls=0;snes_frame_counter++;
    Dkc1CoopViewDraw();assert(calls==0 && s_ready==0);
    pixels=Dkc1PresentationPixels(primary,8);
    for(unsigned i=0;i<8*224;i++){
      unsigned faded=(i%4==3)?0x33:0x31;
      assert(pixels[i]==(rider==2?faded:0));
      assert(pixels[i+8*224]==(rider==4?faded:0));
    }
    alive[0]=alive[1]=1;
  }
  alive[0]=0;shared_cart=1;calls=0;Dkc1CoopViewDraw();assert(calls==1 && s_ready==2);
  alive[0]=1;shared_cart=0;calls=0;Dkc1CoopViewDraw();assert(calls==2 && s_projection_seat==-1);
  return 0;
}
""")

    def test_shared_cannon_uses_one_presentation_camera_and_keeps_native_origin(self):
        source = (ROOT / 'runner/dkc1_coop_view.c').read_text()
        draw = 'typedef struct CoopViewPlan {' + source.split('typedef struct CoopViewPlan {', 1)[1]
        test_coop_mod.CoopModTests().compile_harness('#include "dkc1_coop_oam.h"\n' + VIEW_SUPPORT + draw + r'''
int main(void){
  shared_cannon=1;
  Dkc1CoopViewDraw();
  assert(draw_x[0]==0 && draw_y[0]==2);
  assert(draw_x[1]==0 && draw_y[1]==2);
  assert(native_x[0]==0 && native_x[1]==0);
  shared_cannon=0;
  Dkc1CoopViewDraw();
  assert(draw_x[0]==0 && draw_y[0]==2);
  assert(draw_x[1]==1 && draw_y[1]==3);
  return 0;
}
''')

    def test_fixed_room_capability_and_hdma_wram_mirrors(self):
        rom = ROOT / 'dkc1.sfc'
        if rom.exists():
            # Giant banana state1 calls FC0A, which releases the control
            # flag without restoring the arena's equal camera limits.
            data = rom.read_bytes()
            self.assertEqual(data[0x36fbe3:0x36fbe7],bytes.fromhex('20 0a fc 6b'))
            self.assertEqual(data[0x36fc0a:0x36fc11],bytes.fromhex('9c 29 19 22 9c 9b 80'))
            self.assertEqual(data[0x3f835f:0x3f8363],bytes.fromhex('5c 07 d3 81'))
            self.assertEqual(data[0x35fe01:0x35fe09],bytes.fromhex('45 0d 77 00 29 10 00 00'))
        source = (ROOT / 'runner/dkc1_coop_view.c').read_text()
        fixed = 'static int FixedRoomGameplay(' + source.split('static int FixedRoomGameplay(', 1)[1].split('void Dkc1CoopViewCapture', 1)[0]
        movement = (ROOT / 'runner/dkc1_coop_movement.c').read_text()
        boss = 'int Dkc1CoopFixedBossRoom(' + movement.split('int Dkc1CoopFixedBossRoom(', 1)[1].split('int Dkc1CoopKongRole', 1)[0]
        bus = 'static int BusByte(' + source.split('static int BusByte(', 1)[1].split('static void HdmaLine', 1)[0]
        test_coop_mod.CoopModTests().compile_harness('#include "dkc1_coop_oam.h"\n' + r'''
#include <assert.h>
#include <stdint.h>
#include <stddef.h>
#include "dkc1_coop_movement.h"
static uint8_t memory[0x20000], *g_ram=memory, rom_byte=0x93;
static struct {void *cart;} snes, *g_snes=&snes;
static uint16_t Read16(unsigned a){return memory[a]|memory[a+1]<<8;}
static void wr(unsigned a,unsigned v){memory[a]=v;memory[a+1]=v>>8;}
static uint16_t Dkc1CoopEntityRead16(const uint8_t *r,unsigned a,unsigned h){return Read16(a+h);}
static const uint8_t *cart_getRomPtr(void *c,uint8_t bank,uint16_t a){return &rom_byte;}
static uint16_t Dkc1WramU16(const uint8_t *r,unsigned a){return Read16(a);}
static uint16_t Dkc1Actor_SpriteIDLo(const uint8_t *r,unsigned h){return Read16(0xd45+h);}
int Dkc1CoopEntityIsHandle(uint16_t h){return !(h&1) && ((h>=2 && h<=0x32) || (h>=0x80 && h<=0xfe));}
uint16_t Dkc1CoopEntityNextHandle(uint16_t h){return h==0x32?0x80:h+2;}
''' + bus + 'int Dkc1CoopInLevel(const uint8_t *r){return Read16(0x1b25)>Read16(0x1b23) && Read16(0x1b25)>=0x100;}\n' + boss + fixed + r'''
int main(void){
  uint8_t value; memory[0x1ffe]=0x37; memory[0x11ffe]=0x51;
  const uint8_t mirrors[]={0,0x3f,0x80,0xbf};
  for(unsigned i=0;i<4;i++){assert(BusByte(mirrors[i],0x1ffe,&value));assert(value==0x37);}
  assert(BusByte(0x7e,0x1ffe,&value)&&value==0x37);
  assert(BusByte(0x7f,0x1ffe,&value)&&value==0x51);
  assert(BusByte(0x40,0x1ffe,&value)&&value==0x93);
  assert(BusByte(0x80,0x2000,&value)&&value==0x93);
  wr(0x3e,0x1a);wr(0x1b23,0x5d60);wr(0x1b25,0x5d60);wr(0x4a,0x200);wr(0xd47,1);
  assert(FixedRoomGameplay());
  wr(0x1b25,0x5e00);assert(!FixedRoomGameplay());wr(0x1b25,0x5d60);
  wr(0x3e,0xeb);assert(!FixedRoomGameplay());wr(0x3e,0x1a);
  wr(0xd47,0);assert(!FixedRoomGameplay());wr(0xd49,2);assert(FixedRoomGameplay());
  wr(0x32,7);assert(!FixedRoomGameplay());
  /* Mode1 bonus uses the same native equal-limit framing (80:9E11).
   * Its Kongs must still receive private palettes despite shared CCC. */
  wr(0x32,1);wr(0x3e,3);wr(0x1e15,0x22);
  wr(0x1b23,0x6c00);wr(0x1b25,0x6c00);wr(0xd47,1);
  assert(FixedRoomGameplay());
  assert((Dkc1CoopSceneCapabilities(g_ram)&kDkc1CoopSceneControl) &&
         !(Dkc1CoopSceneCapabilities(g_ram)&(kDkc1CoopSceneScanner|kDkc1CoopSceneCamera)));
  wr(0x1b25,0x6d00);assert(!FixedRoomGameplay());wr(0x1b25,0x6c00);
  wr(0x1e15,0);assert(!FixedRoomGameplay());wr(0x1e15,0x22);
  /* The rotating-prize bonus also uses private actor graphics, while
   * retaining native script ownership of the room and its input. */
  wr(0x32,0x0b);assert(FixedRoomGameplay());
  assert(!(Dkc1CoopSceneCapabilities(g_ram)&kDkc1CoopSceneControl));
  wr(0x1e15,2);assert(!FixedRoomGameplay());wr(0x1e15,0x22);
  /* Fixed cannon bonus retains its native single view, but still needs
   * actor-owned palette/tile reconstruction after virtual allocation. */
  wr(0x32,4);assert(FixedRoomGameplay());
  assert(Dkc1CoopSceneCapabilities(g_ram)==
      (kDkc1CoopScenePresentation|kDkc1CoopSceneFixed|kDkc1CoopSceneNativeView));
  wr(0x1e15,2);assert(!FixedRoomGameplay());wr(0x1e15,0x22);
  /* Vertical-mode spinning prizes retain the cartridge's one view too. */
  wr(0x32,9);wr(0x3e,0xc5);wr(0x1b23,0x600);wr(0x1b25,0x600);
  assert(FixedRoomGameplay());
  assert(Dkc1CoopSceneCapabilities(g_ram)==
      (kDkc1CoopScenePresentation|kDkc1CoopSceneFixed|kDkc1CoopSceneNativeView));
  wr(0x1e15,2);assert(!FixedRoomGameplay());wr(0x1e15,0x22);
  /* Mode A's three changing prize barrels use the same native view. */
  wr(0x32,0x0a);wr(0x3e,0xd7);wr(0x1b23,0x2a00);wr(0x1b25,0x2a00);
  assert(FixedRoomGameplay());
  assert(Dkc1CoopSceneCapabilities(g_ram)==
      (kDkc1CoopSceneControl|kDkc1CoopScenePresentation|kDkc1CoopSceneFixed|kDkc1CoopSceneNativeView));
  wr(0x1e15,2);assert(!FixedRoomGameplay());wr(0x1e15,0x22);
  wr(0x32,1);
  wr(0x4a,0);assert(!FixedRoomGameplay());wr(0x4a,0x200);
  wr(0x32,3);assert(!FixedRoomGameplay());wr(0xd47,0);
  /* Native Very Gnawty victory fixes the bounds at $EC, below $100.
   * Actor-owned graphics must remain active while the reward is drawn. */
  wr(0x32,0x0c);wr(0x3e,0xe0);wr(0x1b23,0xec);wr(0x1b25,0xec);
  assert(FixedRoomGameplay());
  wr(0x1b25,0x100);assert(!FixedRoomGameplay());wr(0x1b25,0xec);
  wr(0x1b23,0);wr(0x1b25,0);assert(!FixedRoomGameplay());
  /* Victory at camera zero is valid while boss/reward/controller lives.
   * An empty room or the map avatar still fails closed. */
  static const unsigned scene_ids[]={0x70,0x74,0x6c};
  for(unsigned i=0;i<3;i++){
    wr(0xd4b,scene_ids[i]);assert(FixedRoomGameplay());
    wr(0xd45,0x10);assert(!FixedRoomGameplay());wr(0xd45,0);
  }
  wr(0xd4b,0);assert(!FixedRoomGameplay());
  wr(0x1b23,0xec);wr(0x1b25,0xec);
  for(unsigned e=0xe0;e<0xe6;++e){wr(0x3e,e);assert(FixedRoomGameplay());}
  wr(0x3e,0xe6);assert(!FixedRoomGameplay());
  wr(0x3e,0xe0);wr(0xd49,0);assert(!FixedRoomGameplay());
  /* K. Rool locks the ship with the same B6FC74 routine, but mode0D.
   * Zero is a real arena position while its controller/boss is alive. */
  wr(0x32,0x0d);wr(0x3e,0x68);wr(0xd47,1);wr(0xd4b,0x6c);wr(0x1929,1);
  for(unsigned x=0;x<=0x100;x+=0x100){
    wr(0x1b23,x);wr(0x1b25,x);assert(FixedRoomGameplay());
    assert(Dkc1CoopSceneCapabilities(g_ram)==
      (kDkc1CoopSceneControl|kDkc1CoopScenePresentation|
       kDkc1CoopSceneCamera|kDkc1CoopSceneFixed));
  }
  /* The reward releases1929 without changing the locked arena. Scene
   * admission must survive that phase and the boss/controller's removal. */
  static const unsigned ship_live_ids[]={0x6c,0x70,0x74};
  for(unsigned party=1;party<=3;party++){
    wr(0xd47,(party&1)?1:0);wr(0xd49,(party&2)?2:0);
    for(unsigned x=0;x<=0x100;x+=0x100){
      wr(0x1b23,x);wr(0x1b25,x);
      for(unsigned i=0;i<3;i++){
        wr(0xd4b,ship_live_ids[i]);
        wr(0x1929,0);assert(FixedRoomGameplay());
        wr(0x1929,1);assert(FixedRoomGameplay());
      }
    }
  }
  wr(0x3e,0x69);assert(!FixedRoomGameplay());wr(0x3e,0x68);
  wr(0xd45,0x10);assert(!FixedRoomGameplay());wr(0xd45,0);
  wr(0xd4b,0);assert(!FixedRoomGameplay());
  /* Native cast77 parks both Kongs and walks the credit actors. It needs
   * owned palettes even though there is no playable Kong/camera target. */
  wr(0x32,0x0d);wr(0x3e,0x5e);wr(0x1b23,0x100);wr(0x1b25,0x100);
  wr(0xd47,0);wr(0xd49,0);wr(0xd4b,0x77);wr(0x1e15,0x22);
  assert(FixedRoomGameplay());
  assert(Dkc1CoopSceneCapabilities(g_ram)==(2u|16u|64u));
  wr(0x32,1);assert(FixedRoomGameplay());
  wr(0xd4b,0);assert(!FixedRoomGameplay());wr(0xd4b,0x77);
  wr(0x1e15,2);assert(!FixedRoomGameplay());wr(0x1e15,0x22);
  wr(0xd45,0x10);assert(!FixedRoomGameplay());
  return 0;
}
''')

    def test_animal_bonus_collector_owns_the_single_native_picture(self):
        source = (ROOT / 'runner/dkc1_coop_view.c').read_text()
        draw = 'typedef struct CoopViewPlan {' + source.split('typedef struct CoopViewPlan {', 1)[1]
        test_coop_mod.CoopModTests().compile_harness('#include "dkc1_coop_oam.h"\n' + VIEW_SUPPORT + draw + r'''
int main(void) {
  uint8_t primary[8*224]={0};
  for(unsigned owner=1;owner<=2;owner++) {
    memset(&s_fade,0,sizeof s_fade);memset(g_ram,0,sizeof g_ram);
    bonus_owner=owner;calls=0;alive[0]=alive[1]=1;shared_cart=0;
    Dkc1CoopViewDraw();
    assert(calls==1 && s_projection_seat==(int)owner-1 && s_ready==(1u<<(owner-1)));
    assert(s_hidden==((~(1u<<(owner-1)))&3u));
    const uint8_t *pixels=Dkc1PresentationPixels(primary,8);
    for(unsigned view=0;view<2;view++) for(unsigned i=0;i<8*224;i++)
      assert(pixels[view*8*224+i]==(view==owner-1?0x33:0));
  }
  return 0;
}
''')

    def test_fixed_bonus_views_follow_party_with_shared_native_camera(self):
        source = (ROOT / 'runner/dkc1_coop_view.c').read_text()
        draw = 'typedef struct CoopViewPlan {' + source.split('typedef struct CoopViewPlan {', 1)[1]
        support = VIEW_SUPPORT.replace('return view==0;', 'return 1;').replace(
            'static int FixedRoomGameplay(void) { return 0; }',
            'static int fixed_scene; static int FixedRoomGameplay(void) { return fixed_scene; }')
        test_coop_mod.CoopModTests().compile_harness('#include "dkc1_coop_oam.h"\n' + support + draw + r'''
int main(void){
  uint8_t primary[8*224]={0};
  /* Camera eligibility excludes the native parked partner, but he is alive.
   * Both native-resource and ordinary fixed bonuses use party ownership. */
  alive[0]=alive[1]=0;shared_cart=1;
  for(unsigned native=0;native<2;native++)
  for(unsigned selected=1;selected<=2;selected++)
  for(unsigned both=0;both<=1;both++) {
    memset(&s_fade,0,sizeof s_fade);memset(g_ram,0,sizeof g_ram);
    native_scene=native;fixed_scene=!native;calls=0;
    g_ram[0x1e15]=0x20;g_ram[0x056f]=(uint8_t)selected;g_ram[0x0579]=(uint8_t)both;
    unsigned seats=both?3u:1u<<(selected-1);
    uint8_t saved[sizeof g_ram];memcpy(saved,g_ram,sizeof g_ram);
    CoopViewPlan plan=BuildViewPlan();
    assert(plan.hidden==((~seats)&3u) && plan.projection_seat==-1);
    assert(plan.x[0]==plan.x[1] && plan.y[0]==plan.y[1]);
    Dkc1CoopViewDraw();
    assert(calls==(both?2:1) && s_ready==seats && s_hidden==((~seats)&3u));
    assert(!memcmp(saved,g_ram,sizeof g_ram));
    const uint8_t *pixels=Dkc1PresentationPixels(primary,8);
    for(unsigned view=0;view<2;view++) {
      if(seats&(1u<<view))assert(draw_x[view]==0 && draw_y[view]==2);
      for(unsigned i=0;i<8*224;i++)
        assert(pixels[view*8*224+i]==((seats&(1u<<view))?(view?0x55:0x33):0));
    }
  }
  return 0;
}
''')

    def test_hdma_matches_pinned_host_and_wraps_within_bank(self):
        view = (ROOT / 'runner/dkc1_coop_view.c').read_text()
        private = 'static void HdmaLine(' + view.split('static void HdmaLine(', 1)[1].split('void Dkc1CoopViewInvalidate', 1)[0]
        source = (ROOT / 'snesrecomp/runner/src/common_rtl.c').read_text()
        native = 'void SimpleHdma_DoLine(' + source.split('void SimpleHdma_DoLine(', 1)[1].split('\n}', 1)[0] + '\n}'
        test_coop_mod.CoopModTests().compile_harness('#include "dkc1_coop_oam.h"\n' + HDMA_SUPPORT + private + native + HDMA_CHECKS)

    def test_oversized_custom_width_preserves_primary_and_skips_private_buffers(self):
        source = (ROOT / 'runner/dkc1_coop_view.c').read_text()
        draw = 'typedef struct CoopViewPlan {' + source.split('typedef struct CoopViewPlan {', 1)[1]
        support = VIEW_SUPPORT.replace('static int Dkc1VideoWidth(void){return 2;}',
            'static int render_width=2;static int Dkc1VideoWidth(void){return render_width;}')
        test_coop_mod.CoopModTests().compile_harness('#include "dkc1_coop_oam.h"\n' + support + draw + r'''
int main(void) {
  uint8_t primary[12*224];memset(primary,0x77,sizeof primary);
  memset(g_ram,0x19,sizeof g_ram);memcpy(s_backup,g_ram,sizeof g_ram);
  render_width=3;
  Dkc1CoopViewDraw();assert(!calls && !s_ready);
  assert(!memcmp(g_ram,s_backup,sizeof g_ram));
  assert(Dkc1PresentationHeight()==224);
  assert(Dkc1PresentationPixels(primary,12)==primary);
  render_width=2;
  Dkc1CoopViewDraw();assert(calls==2 && s_ready==1);
  assert(Dkc1PresentationHeight()==448);
  assert(Dkc1PresentationPixels(primary,12)!=primary);
  return 0;
}
''')

    def test_lower_view_rejection_restores_machine_and_blacks_only_lower(self):
        source = (ROOT / 'runner/dkc1_coop_view.c').read_text()
        draw = 'typedef struct CoopViewPlan {' + source.split('typedef struct CoopViewPlan {', 1)[1]
        test_coop_mod.CoopModTests().compile_harness('#include "dkc1_coop_oam.h"\n' + VIEW_SUPPORT + draw + VIEW_CHECKS)

    def test_death_fades_each_view_once_per_frame_and_restores_mid_fade(self):
        source = (ROOT / 'runner/dkc1_coop_view.c').read_text()
        draw = 'typedef struct CoopViewPlan {' + source.split('typedef struct CoopViewPlan {', 1)[1]
        support = VIEW_SUPPORT.replace('return view==0;', 'return 1;')
        test_coop_mod.CoopModTests().compile_harness('#include "dkc1_coop_oam.h"\n' + support + draw + r'''
int main(void) {
  uint8_t primary[8*224]={0}; static uint8_t saved[sizeof s_fade+sizeof s_fog];
  Dkc1CoopViewDraw(); alive[0]=0;
  for(unsigned age=1;age<=30;age++) {
    snes_frame_counter=(int)age; Dkc1CoopViewDraw();
    const uint8_t *p=Dkc1PresentationPixels(primary,8);
    assert(p[0]==0x33*(30-age)/30); assert(p[8*224]==0x55);
    Dkc1CoopViewDraw(); assert(s_fade.fade[0]==age);
    if(age==10) assert(Dkc1CoopViewSaveState(saved,sizeof saved));
  }
  assert(Dkc1CoopViewLoadState(saved,sizeof saved));
  snes_frame_counter=10;Dkc1CoopViewDraw();
  assert(Dkc1PresentationPixels(primary,8)[0]==0x33*20/30);
  alive[0]=1;alive[1]=0;snes_frame_counter=11;Dkc1CoopViewDraw();
  const uint8_t *p=Dkc1PresentationPixels(primary,8);
  assert(p[0]==0x33);assert(p[8*224]==0x55*29/30);
  g_ram[0x3e]++;Dkc1CoopViewDraw();
  assert(Dkc1PresentationPixels(primary,8)[8*224]==0);
  return 0;
}
''')

    def test_final_fall_waits_for_native_balloon_countdown_on_native_camera(self):
        source = (ROOT / 'runner/dkc1_coop_view.c').read_text()
        draw = 'typedef struct CoopViewPlan {' + source.split('typedef struct CoopViewPlan {', 1)[1]
        support = VIEW_SUPPORT.replace('return view==0;', 'return 1;')
        support = support.replace('static uint16_t Dkc1CoopEntityRead16(',
            'static uint16_t death_state[2],death_timer[2];\nstatic uint16_t Dkc1CoopEntityRead16(')
        support = support.replace('return base==0x1375?cart_rider:',
            'if(base==0x1029)return death_state[slot==4];'
            'if(base==0x1375 && slot!=6)return death_timer[slot==4];'
            'return base==0x1375?cart_rider:')
        test_coop_mod.CoopModTests().compile_harness('#include "dkc1_coop_oam.h"\n' + support + draw + r'''
int main(void) {
  uint8_t primary[8*224]={0};static uint8_t saved[sizeof s_fade+sizeof s_fog];
  for(unsigned player=0;player<2;player++) {
    memset(&s_fade,0,sizeof s_fade);memset(death_state,0,sizeof death_state);
    alive[0]=alive[1]=1;Dkc1CoopViewDraw();
    alive[player]=0;death_state[player]=0x3b;g_ram[0x1929]=1;
    /* Includes before pop, pop trigger and the remaining animation time. */
    for(int timer=96;timer>=0;timer--) {
      death_timer[player]=(uint16_t)timer;snes_frame_counter++;
      Dkc1CoopViewDraw();assert(s_fade.fade[player]==0);
      assert(!(s_hidden&(1u<<player)));
      if(player==1)assert(draw_x[1]==0 && draw_y[1]==2);
      if(timer==40)assert(Dkc1CoopViewSaveState(saved,sizeof saved));
    }
    death_timer[player]=0xffff;snes_frame_counter++;Dkc1CoopViewDraw();
    assert(s_fade.fade[player]==1 && (s_hidden&(1u<<player)));
    assert(Dkc1CoopViewLoadState(saved,sizeof saved));
    death_timer[player]=40;Dkc1CoopViewDraw();assert(s_fade.fade[player]==0);
  }
  return 0;
}
''')

    def test_final_death_never_reactivates_previously_hidden_partner(self):
        source = (ROOT / 'runner/dkc1_coop_view.c').read_text()
        draw = 'typedef struct CoopViewPlan {' + source.split('typedef struct CoopViewPlan {', 1)[1]
        support = VIEW_SUPPORT.replace('return view==0;', 'return 1;')
        support = support.replace('static uint16_t Dkc1CoopEntityRead16(',
            'static uint16_t death_state[2],death_timer[2];\nstatic uint16_t Dkc1CoopEntityRead16(')
        support = support.replace('return base==0x1375?cart_rider:',
            'if(base==0x1029)return death_state[slot==4];'
            'if(base==0x1375 && slot!=6)return death_timer[slot==4];'
            'return base==0x1375?cart_rider:')
        test_coop_mod.CoopModTests().compile_harness('#include "dkc1_coop_oam.h"\n' + support + draw + r'''
int main(void) {
  uint8_t primary[8*224]={0};static uint8_t saved[sizeof s_fade+sizeof s_fog];
  for(unsigned absent=0;absent<2;absent++) {
    memset(&s_fade,0,sizeof s_fade);memset(death_state,0,sizeof death_state);
    g_ram[0x1929]=0;alive[0]=alive[1]=1;Dkc1CoopViewDraw();
    alive[absent]=0;death_state[absent]=0x3b;death_timer[absent]=96;
    for(unsigned age=1;age<=30;age++){snes_frame_counter++;Dkc1CoopViewDraw();}
    assert(s_fade.valid&(1u<<absent)); /* history is not a live view */
    assert(Dkc1CoopViewSaveState(saved,sizeof saved));
    assert(Dkc1CoopViewLoadState(saved,sizeof saved));
    /* A later fatal hit starts the shared death flag while the previously
     * lost partner still has its native fall state and positive timer. */
    death_state[absent^1]=0x11;g_ram[0x1929]=1;
    for(int timer=96;timer>=0;timer--){
      death_timer[absent]=(uint16_t)timer;snes_frame_counter++;Dkc1CoopViewDraw();
      const uint8_t *pixels=Dkc1PresentationPixels(primary,8);
      assert(s_hidden&(1u<<absent));assert(s_fade.fade[absent]==30);
      assert(pixels[absent*8*224]==0);
    }
    /* Authentic revival may still reopen this player's view. */
    g_ram[0x1929]=0;death_state[absent]=0;alive[absent]=1;
    snes_frame_counter++;Dkc1CoopViewDraw();assert(!(s_hidden&(1u<<absent)));
    assert(s_fade.fade[absent]==0);
  }
  return 0;
}
''')

    def test_dead_donkey_blacks_upper_and_revives_without_hiding_diddy(self):
        source = (ROOT / 'runner/dkc1_coop_view.c').read_text()
        draw = 'typedef struct CoopViewPlan {' + source.split('typedef struct CoopViewPlan {', 1)[1]
        support = VIEW_SUPPORT.replace('return view==0;', 'return 1;')
        test_coop_mod.CoopModTests().compile_harness('#include "dkc1_coop_oam.h"\n' + support + draw + r'''
int main(void) {
  uint8_t primary[8*224];memset(primary,0x77,sizeof primary);
  memset(s_upper,0x33,sizeof s_upper);memset(s_lower,0x55,sizeof s_lower);
  alive[0]=0;
  Dkc1CoopViewDraw();
  const uint8_t *pixels=Dkc1PresentationPixels(primary,8);
  for(unsigned i=0;i<8*224;i++){assert(pixels[i]==0);assert(pixels[i+8*224]==0x55);}
  alive[0]=1;
  Dkc1CoopViewDraw();pixels=Dkc1PresentationPixels(primary,8);
  for(unsigned i=0;i<8*224;i++){assert(pixels[i]==0x33);assert(pixels[i+8*224]==0x55);}
  /* Disabling co-op restores the single shared screen even with DK absent. */
  alive[0]=0;enabled=0;
  Dkc1CoopViewDraw();pixels=Dkc1PresentationPixels(primary,8);
  assert(Dkc1PresentationHeight()==224);
  for(unsigned i=0;i<8*224;i++)assert(pixels[i]==0x33);
  return 0;
}
''')

    def test_fixed_ship_reward_routes_the_surviving_diddy_to_lower(self):
        source = (ROOT / 'runner/dkc1_coop_view.c').read_text()
        draw = 'typedef struct CoopViewPlan {' + source.split('typedef struct CoopViewPlan {', 1)[1]
        support = VIEW_SUPPORT.replace('return view==0;', 'return 1;')
        support = support.replace('static int FixedRoomGameplay(void) { return 0; }',
                                 'static int FixedRoomGameplay(void) { return 1; }')
        support = support.replace('*y=view+2;return 1;', '*y=view+2;return view==0;')
        test_coop_mod.CoopModTests().compile_harness('#include "dkc1_coop_oam.h"\n' + support + draw + r'''
int main(void){
  uint8_t primary[8*224]={0};
  for(unsigned party=1;party<=3;party++)for(unsigned lock=0;lock<=1;lock++){
    memset(&s_fade,0,sizeof s_fade);memset(g_ram,0,sizeof g_ram);
    alive[0]=(party&1)!=0;alive[1]=(party&2)!=0;g_ram[0x1929]=(uint8_t)lock;
    calls=0;
    CoopViewPlan p=BuildViewPlan();
    assert(p.destinations==party && p.hidden==((~party)&3u));
    assert(p.draw_primary==alive[0] && p.draw_secondary==alive[1]);
    if(alive[1])assert(p.x[1]==p.x[0] && p.y[1]==p.y[0]);
    Dkc1CoopViewDraw();assert(s_ready==party && calls==(party==3?2:1));
    const uint8_t *pixels=Dkc1PresentationPixels(primary,8);
    for(unsigned i=0;i<8*224;i++){
      assert(pixels[i]==(alive[0]?0x33:0));
      assert(pixels[i+8*224]==(alive[1]?0x55:0));
    }
  }
  return 0;
}
''')

    def test_scripted_cast_uses_one_native_picture_with_parked_kongs(self):
        source = (ROOT / 'runner/dkc1_coop_view.c').read_text()
        draw = 'typedef struct CoopViewPlan {' + source.split('typedef struct CoopViewPlan {', 1)[1]
        support = VIEW_SUPPORT.replace('return view==0;', 'return 1;')
        test_coop_mod.CoopModTests().compile_harness('#include "dkc1_coop_oam.h"\n' + support + draw + r'''
int main(void){
 uint8_t primary[8*224]={0};
 for(unsigned selected=1;selected<=2;selected++)for(unsigned both=0;both<=1;both++){
  memset(&s_fade,0,sizeof s_fade);memset(g_ram,0,sizeof g_ram);
  alive[0]=alive[1]=0;single_scene=1;native_scene=0;bonus_owner=2;calls=0;
  g_ram[0x1e15]=0x22;g_ram[0x56f]=(uint8_t)selected;g_ram[0x579]=(uint8_t)both;
  CoopViewPlan p=BuildViewPlan();
  assert(p.destinations==1 && p.hidden==2 && p.draw_primary && !p.draw_secondary);
  assert(p.projection_seat==0 && p.picture_source==kCoopSharedNativePicture);
  Dkc1CoopViewDraw();assert(calls==1 && s_ready==1 && s_hidden==2);
  const uint8_t *pixels=Dkc1PresentationPixels(primary,8);
  for(unsigned i=0;i<8*224;i++){assert(pixels[i]==0x33);assert(pixels[i+8*224]==0);}
 }
 return 0;
}
''')


HDMA_SUPPORT = r'''
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
typedef uint8_t uint8;
typedef uint16_t uint16;
typedef struct {uint16_t table,indirect;uint8_t bank,indirect_bank,mode,reg,repeat,enabled;
  uint16_t initial_hold;uint8_t initial_transferred;} ViewHdma;
typedef struct {const uint8 *table,*indir_ptr;uint8 rep_count,mode,ppu_addr,indir_bank;} SimpleHdma;
static uint8_t ram[0x20000];
static unsigned mode_output, count[2], events[2][8192], s_view, *g_ppu=&s_view;
static int BusByte(uint8_t bank,uint16_t at,uint8_t *out) {
  if(bank!=0x7e && bank!=0x7f)return 0;
  *out=ram[((bank&1u)<<16)|at]; return 1;
}
static const uint8 *SimpleHdma_GetPtr(uint32_t at) {return ram+(at&0x1ffffu);}
static bool SimpleHdma_PtrRangeValid(const uint8 *p,size_t n) {return p>=ram && p+n<=ram+sizeof ram;}
static void ppu_write(unsigned *p,uint8_t reg,uint8_t value) {
  assert(count[mode_output]<8192);
  events[mode_output][count[mode_output]++]=((unsigned)reg<<8)|value;
}
#define Dkc1Private_ppu_write ppu_write
static void debug_server_on_reg_write(uint16_t reg,uint8_t value) {(void)reg;(void)value;}
'''

HDMA_CHECKS = r'''
int main(void) {
  static const unsigned lengths[8]={1,2,2,4,4,4,2,4};
  static const uint8_t descriptors[]={1,2,0x7f,0x80,0x81,0x82,0xff};
  for(unsigned mode=0;mode<8;mode++) for(unsigned indirect=0;indirect<2;indirect++)
  for(unsigned d=0;d<sizeof descriptors;d++) {
    memset(ram,0,sizeof ram);memset(count,0,sizeof count);
    unsigned pos=0x1200, data=0x2200, desc=descriptors[d];
    for(unsigned chunk=0;chunk<3;chunk++) {
      ram[pos++]=(uint8_t)desc;
      unsigned transfers=(desc&0x80) && (desc&0x7f)?(desc&0x7f):1;
      if(indirect) {ram[pos++]=(uint8_t)data;ram[pos++]=(uint8_t)(data>>8);}
      for(unsigned j=0;j<lengths[mode]*transfers;j++) {
        if(indirect)ram[0x10000+data++]=(uint8_t)(j+chunk*17);
        else ram[pos++]=(uint8_t)(j+chunk*17);
      }
    }
    ram[pos]=0;
    ViewHdma h={0x1200,0,0x7e,0x7f,(uint8_t)(mode|(indirect?64:0)),0xfe,0,1};
    SimpleHdma native={ram+0x1200,NULL,0,h.mode,h.reg,h.indirect_bank};
    for(unsigned line=0;line<400;line++) {
      mode_output=0;HdmaLine(&h);mode_output=1;SimpleHdma_DoLine(&native);
      assert(count[0]==count[1]);assert(!memcmp(events[0],events[1],count[0]*sizeof(unsigned)));
      assert(h.repeat==native.rep_count);
      assert(h.enabled==(native.table!=NULL));
    }
  }
  /* Table and indirect words wrap in their own banks, never into a neighbor. */
  memset(ram,0,sizeof ram);memset(count,0,sizeof count);mode_output=0;
  ram[0xffff]=1;ram[0]=0x51;ram[1]=0;
  ViewHdma direct={0xffff,0,0x7e,0,0,0x10,0,1};
  HdmaLine(&direct);assert(count[0]==1 && events[0][0]==0x1051 && direct.table==1);
  HdmaLine(&direct);assert(!direct.enabled && count[0]==1);
  memset(ram,0,sizeof ram);memset(count,0,sizeof count);
  ram[0xfffe]=1;ram[0xffff]=0xfe;ram[0]=0xff;ram[1]=0;
  ram[0x1fffe]=0x41;ram[0x1ffff]=0x42;ram[0x10000]=0x43;ram[0x10001]=0x44;
  ViewHdma indirect={0xfffe,0,0x7e,0x7f,64|4,0x10,0,1};
  HdmaLine(&indirect);assert(count[0]==4 && indirect.table==1 && indirect.indirect==2);
  for(unsigned i=0;i<4;i++)assert(events[0][i]==(((0x10+i)<<8)|(0x41+i)));
  HdmaLine(&indirect);assert(!indirect.enabled && count[0]==4);
  return 0;
}
'''

VIEW_SUPPORT = r'''
#include "dkc1_coop_decision.h"
static int FixedRoomGameplay(void) { return 0; }
#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
enum {kDkc1CoopSceneNativeView=32u,kDkc1CoopSceneSingleView=64u};
static unsigned native_scene,single_scene;
static unsigned bonus_owner;
static unsigned Dkc1CoopAnimalBonusOwner(const uint8_t *ram){return bonus_owner;}
static unsigned Dkc1CoopSceneCapabilities(const uint8_t *ram){return (native_scene?32u:0u)|(single_scene?64u:0u);}
enum {kHeight=224,kMaxWidth=2};
static uint8_t g_ram[0x20000],s_backup[0x20000],s_composite[8*448];
/* FADE_DECLARATIONS */
static int snes_frame_counter;
static uint32_t s_rope_chr_mask;
static uint16_t s_rope_chr_owner;
static uint16_t Read16(unsigned a){return g_ram[a]|g_ram[a+1]<<8;}
static uint64_t g_main_cpu_cycles_estimate=1,g_apu_pace_cycles_estimate=2,g_apu_last_sync_cycles=3,g_apu_last_sync_master=4;
static uint8_t g_snesrecomp_last_hdmaen=5;
#include "dkc1_coop_clock.h"
static Dkc1CoopClockSnapshot s_clock;
static int s_ready,s_seed_valid=1,s_width,enabled=1,calls,s_hidden,alive[2]={1,1};
static int shared_cannon,shared_cart,cart_jump,s_projection_seat=-1,cart_rider=2;
static uint16_t Dkc1CoopMinecartForKong(const uint8_t *ram,uint16_t kong){return shared_cart?6:0;}
static uint16_t Dkc1CoopMinecartViewOwner(const uint8_t *ram){return shared_cart||cart_jump?cart_rider/2:0;}
static uint16_t draw_x[2],draw_y[2],native_x[2];
static int Dkc1CoopSharedCannonCamera(const uint8_t *ram){return shared_cannon;}
static const char *s_status;
static void *g_ppu;
static int Dkc1CoopEnabled(void){return enabled;}
static int Dkc1CoopEntitiesActive(void){return 1;}
static int Dkc1VideoWidth(void){return 2;}
static void Dkc1CoopCacheKongSlots(const uint8_t *ram,uint32_t slots[2]){slots[0]=2;slots[1]=4;}
static int Dkc1CoopKongCam(const uint8_t *ram,uint32_t slot){return alive[slot==4];}
static uint16_t Dkc1CoopEntityRead16(const uint8_t *ram,uint16_t base,uint16_t slot){return base==0x1375?cart_rider:slot==4?2:1;}
static int Dkc1CoopWorldGetCamera(const uint8_t *ram,int view,uint16_t *x,uint16_t *y){*x=view;*y=view+2;return 1;}
static void Dkc1CoopViewCapture(const void *ppu){assert(0);}
static int DrawView(int view,uint16_t a,uint16_t b,uint16_t x,uint16_t y){
  draw_x[view]=x;draw_y[view]=y;native_x[view]=a;
  calls++;memset(view?s_lower:s_upper,view?0x55:0x33,sizeof s_upper);memset(g_ram,view+2,sizeof g_ram);
  g_main_cpu_cycles_estimate=10;g_apu_pace_cycles_estimate=20;
  g_apu_last_sync_cycles=30;g_apu_last_sync_master=40;g_snesrecomp_last_hdmaen=50;
  return view==0;
}
'''

VIEW_CHECKS = r'''
int main(void) {
  uint8_t primary[8*224];memset(primary,0x77,sizeof primary);
  memset(g_ram,0x19,sizeof g_ram);memset(s_upper,0x33,sizeof s_upper);memset(s_lower,0x55,sizeof s_lower);
  Dkc1CoopViewDraw();assert(calls==2 && s_ready==1);
  for(unsigned i=0;i<sizeof g_ram;i++)assert(g_ram[i]==0x19);
  assert(g_main_cpu_cycles_estimate==1 && g_apu_pace_cycles_estimate==2 && g_apu_last_sync_cycles==3 && g_apu_last_sync_master==4 && g_snesrecomp_last_hdmaen==5);
  const uint8_t *pixels=Dkc1PresentationPixels(primary,8);assert(Dkc1PresentationHeight()==448);
  for(unsigned i=0;i<8*224;i++){assert(pixels[i]==0x33);assert(pixels[i+8*224]==0);}
  return 0;
}
'''

# Compile the production snapshot layout and validation, not a duplicate model.
_view_source = (ROOT / 'runner/dkc1_coop_view.c').read_text()
_fade_declarations = '#include "dkc1_coop_fog.h"\n' + 'enum { kFadeFrames' + _view_source.split('enum { kFadeFrames', 1)[1].split('static uint8_t s_composite', 1)[0]
VIEW_SUPPORT = VIEW_SUPPORT.replace('/* FADE_DECLARATIONS */', _fade_declarations)
