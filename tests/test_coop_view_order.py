from pathlib import Path
import unittest

import test_coop_mod

ROOT = Path(__file__).resolve().parents[1]


class CoopViewOrderTests(unittest.TestCase):
    def test_oam_hardware_bytes_and_rope_replay_boundaries(self):
        test_coop_mod.CoopModTests().compile_harness(r'''
#include "dkc1_coop_oam.h"
#include <assert.h>
int main(void) {
  uint8_t src[512],high[32],out[512],out_high[32];
  memset(src,0xcc,sizeof src);memset(high,0xa5,sizeof high);
  Dkc1CoopOamWrite(src,high,1,256,255,0xabcd,1);
  const uint8_t packed[]={0,255,0xcd,0xab};
  assert(!memcmp(src+4,packed,4) && high[0]==0xad);
  assert(src[0]==0xcc && src[8]==0xcc && high[1]==0xa5);
  Dkc1CoopOamWrite(src,high,4,-1,17,0x1234,0);
  assert(src[16]==255 && high[1]==0xa5); /* full X9, peer size bits intact */
  Dkc1CoopOamClear(src,high);
  const uint8_t links[]={12,240,1,2, 0,255,0xcd,0xab,
                       80,224,3,4, 85,16,5,6};
  memcpy(src,links,sizeof links);high[0]=0xcc; /* slots1/3: X9 and large */
  assert(Dkc1CoopOamReplay(src,high,out,out_high,20,43)==1);
  const uint8_t expected[]={0,235,0xcd,0xab};
  assert(!memcmp(out,expected,4) && out_high[0]==3);
  for(unsigned i=1;i<128;i++)assert(out[i*4+1]==240);
  /* All128 accepted links retain order and the final packed pair. */
  for(unsigned i=0;i<128;i++) {
    src[i*4]=20;src[i*4+1]=10;src[i*4+2]=(uint8_t)i;src[i*4+3]=0x7e;
  }
  memset(high,0xaa,sizeof high);
  assert(Dkc1CoopOamReplay(src,high,out,out_high,0,0)==128);
  assert(!memcmp(src,out,512) && !memcmp(high,out_high,32));
  return 0;
}
''')

    def test_graphics_queue_wire_record_and_bank_wrap(self):
        source = (ROOT / "runner/dkc1_coop_view.c").read_text()
        functions = "typedef struct GraphicsRecord {" + source.split(
            "typedef struct GraphicsRecord {", 1)[1].split("static int UploadStaticGraphics", 1)[0]
        test_coop_mod.CoopModTests().compile_harness(r'''
#include <assert.h>
#include <stdint.h>
#include <string.h>
static uint8_t ram[0x2000],rom[0x10000];
static struct {uint16_t vram[0x8000];} s_view;
static struct {void *cart;} snes,*g_snes=&snes;
static uint16_t Read16(unsigned a){return ram[a]|ram[a+1]<<8;}
static const uint8_t *cart_getRomPtr(void *c,uint8_t bank,uint16_t a){
  assert(bank==0xd6);return rom+a;
}
''' + functions + r'''
int main(void) {
  const uint8_t raw[]={4,0,3,0,254,255,214,128};
  memcpy(ram+0x170f,raw,8);memset(s_view.vram,0x99,sizeof s_view.vram);
  rom[65534]=0x12;rom[65535]=0x34;rom[0]=0x56;rom[1]=0x78;
  assert(UploadGraphics());
  const uint8_t expected[]={0x12,0x34,0x56,0x78};
  assert(!memcmp((uint8_t*)s_view.vram+6,expected,4));
  assert(((uint8_t*)s_view.vram)[5]==0x99 && ((uint8_t*)s_view.vram)[10]==0x99);
  ram[0x170f]=3;assert(!UploadGraphics()); /* odd size */
  ram[0x170f]=0;assert(!UploadGraphics()); /* empty active record */
  ram[0x170f]=4;ram[0x1711]=255;ram[0x1712]=127;
  assert(!UploadGraphics()); /* valid word address, byte extent crosses VRAM */
  ram[0x1716]=0;assert(UploadGraphics()); /* terminator ignores other words */
  for(unsigned offset=0;offset<0x218;offset+=8)memcpy(ram+0x170f+offset,raw,8);
  assert(!UploadGraphics()); /* missing terminator fails at the existing queue bound */
  return 0;
}
''')

    def test_rope_batch_keeps_owned_palette_and_restores_scratch(self):
        source=(ROOT / "runner/dkc1_coop_view.c").read_text(encoding="utf-8")
        function="static void AccumulateWithRopeSheet(int paint) {"+source.split(
            "static void AccumulateWithRopeSheet(int paint) {",1)[1].split(
            "static void ReplayNativeBubbles",1)[0]
        test_coop_mod.CoopModTests().compile_harness('#include "dkc1_coop_oam.h"\n#include "dkc1_coop_raster.h"\n' + r"""
#include <assert.h>
#include <stdint.h>
#include <string.h>
static struct {uint16_t vram[0x8000],cgram[256];} s_view,s_seed;
static uint8_t s_backup[0x20000];
static unsigned s_rope_chr_owner=10,s_rope_chr_mask=1;
static int s_obj_depth,s_obj_color,own=1;
static uint16_t Dkc1CoopEntityRead16(const uint8_t*r,unsigned a,unsigned h){return 0x3c80;}
static int Dkc1CoopGraphicsGetPalette(unsigned h,uint16_t*c){assert(h==10);if(!own)return 0;for(int i=0;i<16;i++)c[i]=0x1234+i;return 1;}
static void PaintRopeSheet(void){s_view.vram[0]=0x5678;}
static void Dkc1Private_AccumulateSprites(void*v,int d,int c){
 assert(s_view.vram[0]==0x5678);
 assert(s_view.cgram[224]==(own?0x1234:7));
 assert(s_view.cgram[100]==7);
}
"""+function+r"""
int main(void){for(int i=0;i<256;i++){s_view.cgram[i]=9;s_seed.cgram[i]=7;}
 s_view.vram[0]=3;AccumulateWithRopeSheet(1);
 assert(s_view.vram[0]==3 && s_view.cgram[224]==9);
 own=0;AccumulateWithRopeSheet(1);assert(s_view.cgram[224]==9);return 0;}
""")

    def test_signed_flashlight_window_clips_margins_without_wrap(self):
        source=(ROOT / "runner/dkc1_coop_view.c").read_text(encoding="utf-8")
        fn="void Dkc1CoopViewWindow("+source.split("void Dkc1CoopViewWindow(",1)[1].split("static int NativeFlashBackdrop",1)[0]
        test_coop_mod.CoopModTests().compile_harness('#include "dkc1_coop_oam.h"\n' + r"""
#include <stdint.h>
#include <assert.h>
typedef struct {unsigned window1left,window1right;} Ppu;
static Ppu s_view;
static int s_flash_native;
static struct {int flash_ready,flash_origin,flash_edge[224];unsigned char flash_left[224];} s_flash;
"""+fn+r"""
int main(void){
 int l=8,r=12;s_flash.flash_ready=1;
 s_flash.flash_edge[0]=319;Dkc1CoopViewWindow(&s_view,1,-43,299,&l,&r);
 assert(l==-43 && r==-44);
 s_flash.flash_edge[0]=290;Dkc1CoopViewWindow(&s_view,1,-43,299,&l,&r);assert(l==290&&r==298);
 s_flash.flash_left[0]=1;s_flash.flash_edge[0]=-30;
 Dkc1CoopViewWindow(&s_view,1,-43,299,&l,&r);assert(l==-43&&r==-30);
 s_flash_native=1;s_flash.flash_origin=60;s_flash.flash_left[0]=0;
 s_view.window1left=100;s_view.window1right=255;s_flash.flash_edge[0]=101;
 Dkc1CoopViewWindow(&s_view,1,-43,299,&l,&r);assert(l==100&&r==298);
 s_view.window1left=255;s_view.window1right=254;s_flash.flash_left[0]=1;s_flash.flash_edge[0]=-20;
 Dkc1CoopViewWindow(&s_view,1,-43,299,&l,&r);assert(l==-43&&r==-20);
 Ppu other={0};l=7;r=8;Dkc1CoopViewWindow(&other,1,-43,299,&l,&r);assert(l==7&&r==8);
 s_flash.flash_ready=0;Dkc1CoopViewWindow(&s_view,1,-43,299,&l,&r);assert(l==7&&r==8);return 0;
}
""")

    def test_flash_backdrop_requires_exact_native_protocol(self):
        source=(ROOT / "runner/dkc1_coop_view.c").read_text(encoding="utf-8")
        fn="static int NativeFlashBackdrop(void) {"+source.split("static int NativeFlashBackdrop(void) {",1)[1].split("static int DrawView",1)[0]
        test_coop_mod.CoopModTests().compile_harness('#include "dkc1_coop_oam.h"\n' + r"""
#include <stdint.h>
#include <assert.h>
static struct {unsigned screenEnabled[2],cgwsel,cgadsub,cgram[256];} s_seed;
static uint8_t s_backup[0x20000],nmi[2]={0x38,0x8e},colors[]={0x73,0x4e,0xff,0x7f,0x18,0x53};
static struct{void*cart;} snes,*g_snes=&snes;
static unsigned Dkc1WramU16(const uint8_t*r,unsigned a){return r[a]|r[a+1]<<8;}
static const uint8_t*cart_getRomPtr(void*c,unsigned b,unsigned a){return a==0x8f20?colors:nmi;}
"""+fn+r"""
int main(void){s_backup[0x1e15]=4;
 for(unsigned i=0;i<3;i++){s_seed.cgram[0]=colors[i*2]|colors[i*2+1]<<8;assert(NativeFlashBackdrop());}
 s_seed.screenEnabled[1]=1;assert(!NativeFlashBackdrop());s_seed.screenEnabled[1]=0;
 s_seed.cgwsel=1;assert(!NativeFlashBackdrop());s_seed.cgwsel=0;
 s_seed.cgram[0]=0;assert(!NativeFlashBackdrop());s_seed.cgram[0]=0x7fff;
 nmi[0]^=1;assert(!NativeFlashBackdrop());nmi[0]^=1;
 s_backup[0x1e15]=0;assert(!NativeFlashBackdrop());return 0;}
""")

    def test_vertical_rope_shaft_survives_offscreen_anchor(self):
        source = (ROOT / "runner/dkc1_coop_view.c").read_text(encoding="utf-8")
        visible = "static int ActorVisible(" + source.split(
            "static int ActorVisible(", 1)[1].split("static unsigned BuildActorOrder", 1)[0]
        test_coop_mod.CoopModTests().compile_harness('#include "dkc1_coop_oam.h"\n' + r'''
#include <assert.h>
#include <stdint.h>
static uint8_t ram[0x20000];
static uint16_t Dkc1CoopEntityRead16(const uint8_t *r,unsigned a,unsigned h){return r[a+h]|r[a+h+1]<<8;}
static void put(unsigned a,unsigned v){ram[a]=v;ram[a+1]=v>>8;}
''' + visible + r'''
int main(void){
  put(0x4a,0x7000);put(0xd45+12,0x30);put(0xd11+12,0x296c);
  put(0xb19+12,0x2e6);put(0xbc1+12,0x63e0);
  assert(ActorVisible(ram,12,0x28d,0xa43,43));
  put(0xd45+12,0x38);assert(!ActorVisible(ram,12,0x28d,0xa43,43));
  put(0xd45+12,0x30);put(0xb19+12,0x1000);
  assert(!ActorVisible(ram,12,0x28d,0xa43,43));
  put(0xb19+12,0x2e6);put(0xbc1+12,0x6800);
  assert(!ActorVisible(ram,12,0x28d,0xa43,43));
  return 0;
}
''')
        rom = ROOT / "dkc1.sfc"
        if rom.exists():
            data = rom.read_bytes()
            self.assertEqual(data[0xa80e:0xa822], bytes.fromhex(
                "a5 4a fd c1 0b 38 ed 95 08 38 e5 52 10 19 c9 f1 ff 30 56 85"))

    def test_rope_dma_sheet_is_owned_by_the_prepared_actor(self):
        source = (ROOT / "runner/dkc1_coop_view.c").read_text()
        decoder = "typedef struct GraphicsRecord {" + source.split(
            "typedef struct GraphicsRecord {", 1)[1].split("static int UploadGraphics", 1)[0]
        capture = "static void CaptureRopeSheet(" + source.split(
            "static void CaptureRopeSheet(", 1)[1].split("static void RestoreSharedRopeTiles", 1)[0]
        test_coop_mod.CoopModTests().compile_harness('#include "dkc1_coop_oam.h"\n' + r'''
#include <stdint.h>
#include <string.h>
#include <assert.h>
static uint32_t s_rope_chr_mask;
static uint16_t s_rope_chr_owner;
static uint8_t s_rope_chr[32][32],rom[1024];
static int sheet=1;
static struct {int unused;} s_view;
static struct {void *cart;} snes,*g_snes=&snes;
#define PPU_objTileAdr1(p) 0
static uint16_t Read16(unsigned a) {
 if(!sheet || a>=0x171d)return 0;
 switch(a){case 0x1715:return 0x80d6;case 0x170f:return 512;
 case 0x1711:return 0x800;case 0x1713:return sheet==1?0:512;}
 return 0;
}
static const uint8_t *cart_getRomPtr(void *c,uint8_t bank,uint16_t a){return rom+a;}
''' + decoder + capture + r'''
int main(void){
 memset(rom,0x11,512);memset(rom+512,0x22,512);
 CaptureRopeSheet(6);assert(s_rope_chr_mask==0xffff && s_rope_chr_owner==6);
 sheet=2;CaptureRopeSheet(8);assert(s_rope_chr_owner==8 && s_rope_chr[0][0]==0x22);
 sheet=0;CaptureRopeSheet(10);assert(!s_rope_chr_mask && s_rope_chr_owner==10);
 return 0;
}
''')

    def test_bubbles_are_read_from_the_native_particle_producer(self):
        source = (ROOT / "runner/dkc1_coop_view.c").read_text()
        function = "static void ReplayNativeBubbles(" + source.split(
            "static void ReplayNativeBubbles(", 1)[1].split("static void EmitCulledRope", 1)[0]
        test_coop_mod.CoopModTests().compile_harness('#include "dkc1_coop_oam.h"\n' + r'''
#include <assert.h>
#include <stdint.h>
#include <string.h>
static uint8_t s_backup[0x20000];
static uint16_t s_native_cam_x=100,s_native_cam_y=40;
static int s_bubble_n;
static struct {int x,y;uint16_t attributes;} s_bubble[32];
static uint16_t Dkc1WramU16(const uint8_t *r,unsigned a){return r[a]|r[a+1]<<8;}
static void put(unsigned a,uint16_t v){s_backup[a]=v;s_backup[a+1]=v>>8;}
''' + function + r'''
int main(void){
  put(0x200,0x5a78);put(0x202,0x3c80); /* actor reuses bubble tile and pixels */
  put(0x136b5,0x3c84);put(0x136fd,120*4);put(0x13745,90*4);
  ReplayNativeBubbles();assert(!s_bubble_n); /* dry scene cannot gain particles */
  put(0x32,3);ReplayNativeBubbles();
  assert(s_bubble_n==1 && s_bubble[0].x==20 && s_bubble[0].y==50 &&
         s_bubble[0].attributes==0x3c84);
  put(0x136b5+0x22,0x3c86);put(0x136fd+0x22,400*4);put(0x13745+0x22,35*4);
  ReplayNativeBubbles();assert(s_bubble_n==2 && s_bubble[1].x==44 && s_bubble[1].y==-5);
  put(0x136b5,0);ReplayNativeBubbles();assert(s_bubble_n==1);
  return 0;
}
''')

    def test_static_sparkles_restore_own_sheet_after_other_actor_uploads(self):
        source = (ROOT / "runner/dkc1_coop_view.c").read_text()
        function = "static int UploadStaticGraphics(" + source.split(
            "static int UploadStaticGraphics(", 1)[1].split("/* HDMA", 1)[0]
        test_coop_mod.CoopModTests().compile_harness('#include "dkc1_coop_oam.h"\n' + r'''
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <assert.h>
typedef struct {uint16_t vram[0x8000];} Ppu;
static Ppu s_view;
static uint8_t s_backup[1],rom[0x400000];
static struct {void *cart;} snes,*g_snes=&snes;
static uint16_t sprite=0x3c,flags=0x3c80;
#define PPU_objTileAdr1(p) 0
#define PPU_objTileAdr2(p) 0x1000
static uint16_t Dkc1CoopEntityRead16(const uint8_t *r,unsigned b,uint16_t h){return b==0xd45?sprite:flags;}
static const uint8_t *cart_getRomPtr(void *c,uint8_t bank,uint16_t at){return rom+((bank&0x3f)*0x10000u)+at;}
''' + function + r'''
int main(void){
  const uint8_t record[]={0,0x94,0x3c,0,0x7c,0xa5,0xde,0x0d};
  memcpy(rom+0x358c5f,record,8);
  for(unsigned i=0;i<416;i++)rom[0x1ea57c+i]=(uint8_t)(i*7+3);
  for(unsigned pass=0;pass<3;pass++){
    memset(s_view.vram,0xab+pass,sizeof s_view.vram);
    assert(UploadStaticGraphics(0x80));
    assert(!memcmp((uint8_t*)s_view.vram+0x1000,rom+0x1ea57c,416));
    assert(((uint8_t*)s_view.vram)[0x1000-1]==0xab+pass);
    assert(((uint8_t*)s_view.vram)[0x1000+416]==0xab+pass);
  }
  rom[0x358c60]=0;assert(!UploadStaticGraphics(0x80));
  sprite=0x16;assert(UploadStaticGraphics(0x80));
  return 0;
}
''')
        rom = ROOT / "dkc1.sfc"
        if rom.exists():
            data = rom.read_bytes()
            for sprite, record in enumerate((0x8663,0x8c5f,0x8c87,0x8cab,0xe375,0x8ccd),0x3b):
                self.assertEqual(data[0x350000+record:0x350004+record],bytes((0,0x94,sprite,0)))

    def test_cast_caption_keeps_native_font_oam_and_restores_raster_resources(self):
        source = (ROOT / 'runner/dkc1_coop_view.c').read_text()
        helper = 'static int StampCastText(' + source.split('static int StampCastText(',1)[1].split('static void PaintRopeSheet',1)[0]
        rom = ROOT / 'dkc1.sfc'
        if rom.exists():
            data = rom.read_bytes()
            self.assertEqual(data[0x1d323:0x1d33e],bytes.fromhex(
                'a9 00 1a 8d 16 21 a0 60 00 a2 00 00 bf dc d6 e9 8d 18 21 e8 e8 8a 29 0f 00 d0 f1'))
            self.assertEqual(data[0x1d33e:0x1d356],bytes.fromhex('9c 18 21')*8)
        test_coop_mod.CoopModTests().compile_harness('#include "dkc1_coop_oam.h"\n#include "dkc1_coop_raster.h"\n' + r'''
#include <assert.h>
#include <stdint.h>
#include <string.h>
typedef struct {uint16_t vram[0x8000],cgram[256],oam[256];uint8_t highOam[32],wsOamRightHintStrict;unsigned wsHudOamSlots,wsHudOamSlots2;} Ppu;
static Ppu s_seed,s_view;
static uint8_t s_backup[0x20000],font_rom[96*16],old[96*32];
static uint16_t old_colors[256],s_obj_depth[1],s_obj_color[1];
static struct {void*cart;} snes,*g_snes=&snes;
static int calls,missing;
#define PPU_objTileAdr2(p) 0x1000
static uint16_t Dkc1WramU16(const uint8_t*r,unsigned a){return r[a]|r[a+1]<<8;}
static void ClearOam(void){Dkc1CoopOamClear((uint8_t*)s_view.oam,s_view.highOam);}
static const uint8_t*cart_getRomPtr(void*c,uint8_t b,uint16_t a){assert(b==0xe9&&a>=0xd6dc&&a<0xdcdc);return missing?0:font_rom+a-0xd6dc;}
static void Dkc1Private_AccumulateSprites(Ppu*p,uint16_t*d,uint16_t*c){
 const uint8_t*r=(const uint8_t*)p->oam;
 assert(r[0]==240&&r[1]==200&&r[2]==0xc7&&r[3]==3);
 assert(Dkc1CoopOamHigh(p->highOam,0)==1); /* full9-bit X survives */
 assert(r[5]==0xf0&&p->cgram[0x91]==0x7fff);
 for(unsigned t=0;t<96;t++){
  assert(!memcmp((uint8_t*)p->vram+0x3400+t*32,font_rom+t*16,16));
  for(unsigned i=16;i<32;i++)assert(!((uint8_t*)p->vram)[0x3400+t*32+i]);
 }
 calls++;
}
''' + helper + r'''
int main(void){
 s_backup[0x136bb]=3;memset(&s_view,0xa5,sizeof s_view);
 s_seed.cgram[0x91]=0x7fff;
 Dkc1CoopOamClear((uint8_t*)s_seed.oam,s_seed.highOam);
 Dkc1CoopOamWrite((uint8_t*)s_seed.oam,s_seed.highOam,0,0x1f0,200,0x3c7,0);
 Dkc1CoopOamWrite((uint8_t*)s_seed.oam,s_seed.highOam,1,100,180,0x3c8,1); /* large actor */
 Dkc1CoopOamWrite((uint8_t*)s_seed.oam,s_seed.highOam,2,110,180,0x3cc7,0); /* virtual palette */
 Dkc1CoopOamWrite((uint8_t*)s_seed.oam,s_seed.highOam,3,110,180,0x380,0); /* outside font */
 for(unsigned i=0;i<sizeof font_rom;i++)font_rom[i]=(uint8_t)(i*7+1);
 memcpy(old,(uint8_t*)s_view.vram+0x3400,sizeof old);memcpy(old_colors,s_view.cgram,sizeof old_colors);
 assert(StampCastText()&&calls==1);
 assert(!memcmp(old,(uint8_t*)s_view.vram+0x3400,sizeof old)&&!memcmp(old_colors,s_view.cgram,sizeof old_colors));
 missing=1;assert(!StampCastText()&&calls==1);
 assert(!memcmp(old,(uint8_t*)s_view.vram+0x3400,sizeof old));
 return 0;
}
''')

    def test_all_entrances_private_scroll_ignore_primary_camera_motion(self):
        rom = ROOT / "dkc1.sfc"
        if not rom.exists():
            self.skipTest("supported private ROM required")
        source = (ROOT / "runner/dkc1_coop_view.c").read_text()
        functions = "static void PrepareMillstoneAnchor(" + source.split(
            "static void PrepareMillstoneAnchor(", 1)[1].split(
                "static int UploadGraphics", 1)[0]
        test_coop_mod.CoopModTests().compile_harness('#include "dkc1_coop_oam.h"\n' + r'''
#include "dkc1_coop_scroll.h"
#include "dkc1_coop_background.h"
#include <stdio.h>
#include <assert.h>
int interp816_opcode_hook(uint32_t pc){return 0;}
static uint8_t rom[0x400000],memory[0x20000],*g_ram=memory;
static const uint8_t *s_backup=memory;
static uint16_t Dkc1WramU16(const uint8_t*r,unsigned a){return r[a]|r[a+1]<<8;}
static const uint8_t *g_rom=rom;
static struct {void *cart;} snes,*g_snes=&snes;
static struct {uint16_t hScroll[4],vScroll[4];uint8_t scrollPrev,scrollPrev2;} s_view;
typedef struct {uint8_t mode,fixed,decrement,indirect,fromB,bAdr,aBank,indBank;
 uint16_t aAdr,size;} DmaChannel;
static struct {DmaChannel channel[8];} dma,*g_dma=&dma;
static uint8_t g_snesrecomp_last_hdmaen;
static char s_detail[128];static const char *s_status;
static uint16_t Read16(unsigned a){return g_ram[a]|((uint16_t)g_ram[a+1]<<8);}
static void Dkc1WramSetU16(uint8_t *r,unsigned a,uint16_t v){r[a]=v;r[a+1]=v>>8;}
static void Write16(unsigned a,uint16_t v){Dkc1WramSetU16(g_ram,a,v);}
static int Dkc1CoopEntityIsHandle(uint16_t h){return h>=2&&h<=0x80&&!(h&1);}
static uint16_t Dkc1CoopEntityNextHandle(uint16_t h){return h+2;}
static uint16_t Dkc1CoopEntityRead16(const uint8_t *r,unsigned a,uint16_t h){return 0;}
static const uint8_t *cart_getRomPtr(void *c,uint8_t bank,uint16_t a){return rom+((bank&63)*0x10000u)+a;}
''' + functions + r'''
int main(void){
 FILE *f=fopen("@ROM@","rb");assert(f);assert(fread(rom,1,sizeof rom,f)==sizeof rom);fclose(f);
 for(unsigned entrance=0;entrance<0xe6;entrance++) {
  uint16_t xs[4],ys[4];uint8_t hdma[128],tables[32];
  for(unsigned trial=0;trial<2;trial++) {
   memset(memory,0,sizeof memory);memset(&s_view,0,sizeof s_view);
   Write16(0x3e,(uint16_t)entrance);
   Write16(0x889,trial?0xffff:0);Write16(0x893,trial?0xffff:0);
   Write16(0x88b,trial?0x7f00:0x10);Write16(0x895,trial?0x1f0:0x10);
   Dkc1CoopBackgroundProtocol protocol=ReadBackgroundProtocol();
   assert(PrepareParallax(0x26c5,0x99,&protocol));
   if(Read16(0x3e)==0x68){
    assert(Dkc1WramU16(g_ram,0x10201)==(0x26c5u>>2));
    assert(Dkc1WramU16(g_ram,0x103e1)==(0x26c5u>>2));
   }
   if(!trial){memcpy(xs,s_view.hScroll,sizeof xs);memcpy(ys,s_view.vScroll,sizeof ys);
    memcpy(hdma,s_scroll.dma,sizeof hdma);memcpy(tables,g_ram+0x680,sizeof tables);}
   else {assert(!memcmp(xs,s_view.hScroll,sizeof xs));assert(!memcmp(ys,s_view.vScroll,sizeof ys));
    assert(!memcmp(hdma,s_scroll.dma,sizeof hdma));assert(!memcmp(tables,g_ram+0x680,sizeof tables));}
  }
 }
 /* Invalid reads reject the transaction; they cannot borrow a device. */
 rom[0x8000]=0xad;rom[0x8001]=0;rom[0x8002]=0x42;
 memset(&s_scroll,0,sizeof s_scroll);s_scroll.rom=rom;
 assert(!Dkc1CoopScrollRun(&s_scroll,0x8000)&&s_scroll.failed);
 rom[0x8000]=0x80;rom[0x8001]=0xfe; /* bounded infinite branch */
 memset(&s_scroll,0,sizeof s_scroll);s_scroll.rom=rom;
 assert(!Dkc1CoopScrollRun(&s_scroll,0x8000));
 return 0;
}
'''.replace("@ROM@",rom.as_posix()),["snesrecomp/runner/src/snes/interp816.c"])

    def test_private_millstone_anchor_uses_visible_live_owner(self):
        source = (ROOT / "runner/dkc1_coop_view.c").read_text()
        helper = "static void PrepareMillstoneAnchor(" + source.split(
            "static void PrepareMillstoneAnchor(", 1)[1].split(
                "static Dkc1CoopScrollBus", 1)[0]
        test_coop_mod.CoopModTests().compile_harness('#include "dkc1_coop_oam.h"\n' + r'''
#include <stdint.h>
#include <assert.h>
static uint8_t memory[1],*g_ram=memory;
static uint16_t wheel_x=0xef0,wheel_y=0x11e;
static uint16_t id[129],wx[129],wy[129];
static uint16_t Read16(unsigned a){return a==0xdd?wheel_x:wheel_y;}
static void Write16(unsigned a,uint16_t v){if(a==0xdd)wheel_x=v;else wheel_y=v;}
static int Dkc1CoopEntityIsHandle(uint16_t h){return h>=2&&h<=0x80&&!(h&1);}
static uint16_t Dkc1CoopEntityNextHandle(uint16_t h){return h+2;}
static uint16_t Dkc1CoopEntityRead16(const uint8_t *r,unsigned a,uint16_t h){
 return a==0xd45?id[h]:a==0xb19?wx[h]:wy[h];
}
''' + helper + r'''
int main(void){
 id[6]=id[0x80]=0x71;wx[6]=0xc70;wy[6]=0x10e;wx[0x80]=0xef0;wy[0x80]=0x11e;
 PrepareMillstoneAnchor(0xbd0);assert(wheel_x==0xc70&&wheel_y==0x10e);
 PrepareMillstoneAnchor(0xde9);assert(wheel_x==0xef0&&wheel_y==0x11e);
 wx[6]=0xfff0;PrepareMillstoneAnchor(0xff70);assert(wheel_x==0xfff0);
 return 0;
}
''')

    def test_native_ship_hdma_span_keeps_headers_and_stops_before_gameplay(self):
        rom = ROOT / "dkc1.sfc"
        if not rom.exists():
            self.skipTest("supported private ROM required")
        data = rom.read_bytes()
        self.assertEqual(data[0xc5bd+0x68*4:0xc5bd+0x69*4],bytes.fromhex("04 96 02 9a"))
        self.assertEqual(data[0x9a6a:0x9aa5],bytes.fromhex(
            "f4 7f 80 ab af 8b 08 00 4a 4a 85 6c 4a 4a 18 6f 8b 08 00 "
            "49 ff ff 85 4c a5 6c a2 01 00 a0 e1 01 18 9d 00 02 99 00 02 "
            "eb 18 65 4c eb 88 88 88 e8 e8 e8 e0 c1 00 d0 ea ab 4c ba af"))
        test_coop_mod.CoopModTests().compile_harness(r'''
#include "dkc1_coop_scroll.h"
#include <stdio.h>
#include <assert.h>
int interp816_opcode_hook(uint32_t pc){return 0;}
static uint8_t rom[0x400000],before[0x20000];
static Dkc1CoopScrollBus bus;
static uint16_t word(unsigned a){return bus.ram[a]|bus.ram[a+1]<<8;}
static uint16_t swap(uint16_t v){return (uint16_t)((v<<8)|(v>>8));}
int main(void){
 FILE*f=fopen("@ROM@","rb");assert(f);assert(fread(rom,1,sizeof rom,f)==sizeof rom);fclose(f);
 /* Include both signed boundaries, fine scroll, and the last unsigned X. */
 for(unsigned x=0;x<=0xffff;x+=257){
  memset(&bus,0,sizeof bus);memset(bus.ram,0xa5,sizeof bus.ram);bus.rom=rom;
  bus.ram[0x88b]=(uint8_t)x;bus.ram[0x88c]=(uint8_t)(x>>8);
  memcpy(before,bus.ram,sizeof before);
  assert(Dkc1CoopScrollRunUntil(&bus,0x809a6a,0x809aa2));
  uint16_t value=(uint16_t)(x>>2),step=(uint16_t)~(x+(x>>4));
  for(unsigned i=0;i<64;i++){
   assert(word(0x10201+3*i)==value && word(0x103e1-3*i)==value);
   value=swap((uint16_t)(swap(value)+step));
  }
  for(unsigned a=0;a<sizeof before;a++){
   int table=a>=0x10201&&a<=0x103e2&&((a-0x10200)%3);
   if(!table && a!=0x4c && a!=0x4d && a!=0x6c && a!=0x6d &&
      (a<0x1fc||a>0x1ff))assert(bus.ram[a]==before[a]);
  }
  assert(!bus.written&&!bus.hdma&&!bus.failed);
 }
 /* A span cannot silently return early or accept unbalanced stack use. */
 memset(&bus,0,sizeof bus);bus.rom=rom;rom[0x8000]=0x60;
 assert(Dkc1CoopScrollRunAt(&bus,0x808000));
 assert(!Dkc1CoopScrollRunUntil(&bus,0x808000,0x808001));
 rom[0x8000]=0x48;rom[0x8001]=0xea; /* PHA; NOP */
 assert(!Dkc1CoopScrollRunUntil(&bus,0x808000,0x808001));
 assert(!Dkc1CoopScrollRunUntil(&bus,0x808000,0x818001));
 assert(!Dkc1CoopScrollRunUntil(&bus,0x808000,0x808000));
 return 0;
}
'''.replace("@ROM@",rom.as_posix()),["snesrecomp/runner/src/snes/interp816.c"])

    def test_all_native_nmi_scrolls_execute_on_isolated_bus(self):
        rom = ROOT / "dkc1.sfc"
        if not rom.exists():
            self.skipTest("supported private ROM required")
        test_coop_mod.CoopModTests().compile_harness('#include "dkc1_coop_oam.h"\n' + r'''
#include "dkc1_coop_scroll.h"
#include <stdio.h>
#include <assert.h>
int interp816_opcode_hook(uint32_t pc) { return 0; }
static uint8_t rom[0x400000];
static Dkc1CoopScrollBus bus;
int main(void) {
  FILE *f=fopen("@ROM@","rb");assert(f);assert(fread(rom,1,sizeof rom,f)==sizeof rom);fclose(f);
  uint8_t seen[0x10000]={0};
  for(unsigned entrance=0;entrance<0xe6;entrance++) {
    unsigned at=0xc5bd+entrance*4;
    uint16_t routine=rom[at]|((uint16_t)rom[at+1]<<8);
    if(seen[routine])continue;seen[routine]=1;
    uint16_t scroll_x[4],scroll_y[4];uint8_t dma[128],hdma=0,written=0;
    for(unsigned pending=0;pending<2;pending++) {
    memset(&bus,0,sizeof bus);bus.rom=rom;
    bus.ram[0x1ad1]=(uint8_t)pending; /* A queued native CGRAM transfer. */
    bus.ram[0x1e43]=7;
    bus.ram[0x88b]=0xa0;bus.ram[0x88c]=0x10;
    bus.ram[0x895]=0x80;bus.ram[0x896]=0;
    if(!Dkc1CoopScrollRun(&bus,routine)) {
      printf("NMI %04x failed at %06x\n",routine,bus.failure_address);return 1;
    }
    assert(bus.ram[0x1acf]==0 && bus.ram[0x1e43]==7);
    if(!pending) {
      memcpy(scroll_x,bus.x,sizeof scroll_x);memcpy(scroll_y,bus.y,sizeof scroll_y);
      memcpy(dma,bus.dma,sizeof dma);hdma=bus.hdma;written=bus.written;
    } else {
      assert(!memcmp(scroll_x,bus.x,sizeof scroll_x) && !memcmp(scroll_y,bus.y,sizeof scroll_y));
      assert(!memcmp(dma,bus.dma,sizeof dma) && hdma==bus.hdma && written==bus.written);
    }
    }
  }
  /* Unsupported timing reads still reject; only the pinned uploader is inert. */
  memset(&bus,0,sizeof bus);bus.rom=rom;
  assert(Dkc1CoopScrollRead(&bus,0x802137)==0 && bus.failed);
  rom[0xa2cd+67]^=1;memset(&bus,0,sizeof bus);bus.rom=rom;
  assert(!Dkc1CoopScrollRun(&bus,0x8d43));
  return 0;
}
'''.replace("@ROM@", rom.as_posix()), ["snesrecomp/runner/src/snes/interp816.c"])

    def test_native_flashlight_builder_projects_world_position(self):
        rom=ROOT / "dkc1.sfc"
        if not rom.exists(): self.skipTest("supported private ROM required")
        test_coop_mod.CoopModTests().compile_harness('#include "dkc1_coop_oam.h"\n' + r"""
#include "dkc1_coop_scroll.h"
#include <assert.h>
#include <stdio.h>
int interp816_opcode_hook(uint32_t pc){return 0;}
static uint8_t rom[0x400000];
static Dkc1CoopScrollBus a,b,c;
static void put(unsigned x,unsigned v){a.ram[x]=v;a.ram[x+1]=v>>8;}
int main(void){
 FILE*f=fopen("@ROM@","rb");assert(f);assert(fread(rom,1,sizeof rom,f)==sizeof rom);fclose(f);
 a.rom=rom;put(0x4a,0x200);put(0xdd,0x10);put(0x88b,0x7100);put(0x895,0x74);
 put(0xd21,0x248c);put(0xc79,0x3c80);put(0xb29,0x7180);put(0xbd1,0x120);
 b=c=a;b.ram[0x88b]=32;b.ram[0xb29]+=32;c.ram[0x88b]=32;
 assert(Dkc1CoopScrollRunAt(&a,0x819374));
 assert(Dkc1CoopScrollRunAt(&b,0x819374));
 assert(Dkc1CoopScrollRunAt(&c,0x819374));
 assert(!memcmp(a.ram+0x11c28,b.ram+0x11c28,512));
 assert(memcmp(a.ram+0x11c28,c.ram+0x11c28,512));
 assert(a.ram[0xb29]==0x80 && b.ram[0xb29]==0xa0);
 for(unsigned face=0;face<2;face++)for(unsigned x=24;x<232;x+=8) {
  memset(&a,0,sizeof a);a.rom=rom;put(0x4a,0x200);put(0xdd,0x10);
  put(0x88b,0x7100);put(0x895,0x74);put(0xd21,0x248c);
  put(0xc79,0x3c80|(face?0x4000:0));put(0xb29,0x7100+x);put(0xbd1,0x120);
  assert(Dkc1CoopScrollRunAt(&a,0x819374) && a.flash_ready);
  for(unsigned y=0;y<223;y++) {
   int e=a.flash_edge[y];unsigned l,r;
   if(a.flash_left[y]){l=0;r=e>255?255:e;if(e<0){l=255;r=254;}}
   else{l=e<0?0:e;r=255;if(e>255){l=255;r=254;}}
   assert(a.ram[0x11c29+y*3]==l && a.ram[0x11c2a+y*3]==r);
  }
 }
 b=a;unsigned wx=b.ram[0xb29]|b.ram[0xb2a]<<8;wx+=256;
 b.ram[0xb29]=wx;b.ram[0xb2a]=wx>>8;
 assert(Dkc1CoopScrollRunAt(&b,0x819374));
 for(unsigned y=0;y<224;y++)assert(b.flash_edge[y]==a.flash_edge[y]+256);
 assert(!Dkc1CoopScrollRunAt(&c,0x7e9374));return 0;
}
""".replace("@ROM@",rom.as_posix()),["snesrecomp/runner/src/snes/interp816.c"])

    def test_parallax_native_projection_and_anchor_selection(self):
        rom = ROOT / "dkc1.sfc"
        if not rom.exists():
            self.skipTest("supported private ROM required")
        test_coop_mod.CoopModTests().compile_harness('#include "dkc1_coop_oam.h"\n' + r'''
#include "dkc1_coop_scroll.h"
#include <assert.h>
#include <stdio.h>
int interp816_opcode_hook(uint32_t pc){return 0;}
static uint8_t rom[0x400000];
static Dkc1CoopScrollBus b;
int main(void){
 FILE *f=fopen("@ROM@","rb");assert(f);assert(fread(rom,1,sizeof rom,f)==sizeof rom);fclose(f);
 static const unsigned values[]={0,1,127,128,223,255,256,383,384,511,512,0x7fff,0x8000,0xfff0,0xffff};
 for(unsigned i=0;i<sizeof values/sizeof values[0];i++) {
  unsigned x=values[i],y=values[(i+3)%(sizeof values/sizeof values[0])];
  static const uint16_t routines[]={0x8c6c,0x8f9c,0x9137,0x9543};
  for(unsigned j=0;j<4;j++) {
   memset(&b,0,sizeof b);b.rom=rom;
   b.ram[0x88b]=(uint8_t)x;b.ram[0x88c]=x>>8;
   b.ram[0x895]=(uint8_t)y;b.ram[0x896]=y>>8;
   assert(Dkc1CoopScrollRun(&b,routines[j]));
   if(j==0)assert(b.x[1]==((x/2)&0x3ff)&&b.y[1]==((y/4)&0x3ff));
   if(j==1)assert(b.x[1]==((x/2)&0x3ff)&&b.y[1]==(y/8<31?y/8:31));
   if(j==2)assert(b.x[1]==((x/2)&0x3ff)&&b.y[1]==((y/2)&0xff));
   if(j==3)assert(b.x[1]==((x/2)&0xff)&&b.x[2]==((x/4)&0xff)&&
    b.y[1]==((((uint16_t)(y-0x180))/8)&0xff)&&b.y[2]==0);
  }
 }
 return 0;
}
'''.replace("@ROM@",rom.as_posix()),["snesrecomp/runner/src/snes/interp816.c"])
        rom = ROOT / "dkc1.sfc"
        if rom.exists():
            data = rom.read_bytes()
            self.assertEqual(data[0xc5bd + 0x16*4:0xc5bf + 0x16*4], b'\x6c\x8c')
            projection = bytes.fromhex('ad 8b 08 4a e2 20 8d 0f 21 eb 8d 0f 21 c2 20 ad 95 08 4a 4a e2 20 8d 10 21 eb 8d 10 21')
            self.assertIn(projection, data[0x8c6c:0x8cdc])
            self.assertEqual(data[0xc5bd+0xd9*4:0xc5bf+0xd9*4], b'\x9c\x8f')
            self.assertEqual(data[0x8fb7:0x8fc4], bytes.fromhex(
                'ad 8b 08 4a e2 20 8d 0f 21 eb 8d 0f 21'))
            self.assertEqual(data[0x8fde:0x8ff5], bytes.fromhex(
                'ad 95 08 4a 4a 4a c9 1f 00 30 03 a9 1f 00 e2 20 8d 10 21 eb 8d 10 21'))
            self.assertEqual(data[0xc5bd+0x31*4:0xc5c1+0x31*4],
                             bytes.fromhex('37 91 97 98'))
            self.assertEqual(data[0x9178:0x9193], bytes.fromhex(
                'ad 8b 08 4a e2 20 8d 0f 21 eb 8d 0f 21 c2 20 '
                'ad 95 08 4a e2 20 8d 10 21 9c 10 21'))
            self.assertEqual(data[0xc5bd+0xba*4:0xc5c1+0xba*4],
                             bytes.fromhex('43 95 b7 99'))
            self.assertEqual(data[0x957e:0x95b6], bytes.fromhex(
                'ad 8b 08 4a e2 20 8d 0f 21 9c 0f 21 c2 20 '
                'ad 8b 08 4a 4a e2 20 8d 11 21 9c 11 21 c2 20 '
                'ad 95 08 38 e9 80 01 4a 4a 4a e2 20 8d 10 21 9c 10 21 '
                '9c 12 21 9c 12 21 c2 20 60'))

            self.assertEqual(data[0xc5bd+0x42*4:0xc5c1+0x42*4], bytes.fromhex('26 93 fc 98'))
            self.assertEqual(data[0x92a6:0x92c8], bytes.fromhex(
                'ad 8b 08 18 69 80 00 38 e5 dd c9 c0 00 90 08 c9 40 ff b0 03 a9 00 01 '
                'e2 20 8d 0f 21 eb 8d 0f 21 c2 20'))

    def test_stationary_secondary_terrain_does_not_inherit_primary_ppu_lag(self):
        source = (ROOT / "runner/dkc1_coop_view.c").read_text()
        terrain = "static uint16_t TileAddress(" + source.split(
            "static uint16_t TileAddress(", 1)[1].split("static int RunPreparation", 1)[0]
        test_coop_mod.CoopModTests().compile_harness('#include "dkc1_coop_oam.h"\n' + r'''
#include <assert.h>
#include <stdint.h>
typedef enum {kDkc1LayoutUnknown,kDkc1LayoutHorizontal,kDkc1LayoutVertical} Dkc1LevelLayout;
typedef struct {uint16_t hScroll[4],vScroll[4],vram[0x8000];uint8_t bgmode,bgXsc[4],screenEnabled[2];} Ppu;
static Ppu s_seed,s_view;
static struct {void *cart;} snes,*g_snes=&snes;
static const uint8_t *cart_getRomPtr(void *cart,uint8_t bank,uint16_t address){return 0;}
static uint8_t g_ram[0x20000];
static uint16_t Read16(unsigned a){return 0;}
#define PPU_bigTiles(p,l) 0
#define PPU_bgTilemapAdr(p,l) 0
#define PPU_bgTileAdr(p,l) 0
static uint8_t Dkc1VideoPpuWideLayerMask(int a,const uint8_t *b,int c,int d){return 1;}
static int Dkc1VideoTerrainLayer(int a,const uint8_t *b,int c){return 0;}
static uint32_t Dkc1VideoUnwrapPpuScroll(uint16_t a,uint16_t b){return a;}
static int patterned;
static int Dkc1VideoDecodeLevelTile(Dkc1LevelLayout a,int b,int c,int d,int e,uint32_t x,uint32_t y,uint16_t *t){*t=patterned?(uint16_t)y:7;return 1;}
static int Dkc1VideoFindTransparent4bppTile(const uint16_t *v,int n,int b,uint16_t *t){*t=0;return 1;}
static int Dkc1VideoExtra(void){return 0;}
static void PpuSetExtraSpace(Ppu *p,uint8_t n){}
static void PpuSetWidescreenPresentationXBias(Ppu *p,int n){}
static void PpuSetWidescreenLayerMask(Ppu *p,int n){}
static void PpuSetWidescreenLayerMirror(Ppu *p,int n){}
static void PpuSetWidescreenLayerClamp(Ppu *p,int n){}
static uint8_t s_repeat;
static void PpuSetWidescreenLayerRepeat(Ppu *p,uint8_t n){(void)p;s_repeat=n;}
''' + terrain + r'''
int main(void){
  g_ram[0xd5]=0x80;
  for(unsigned i=0;i<0x8000;i++)s_seed.vram[i]=7;
  /* Observed frames 8074 and 8085: canonical camera moves before the
   * previous frame's PPU scroll is replaced. P2 remains at (829,239). */
  const uint16_t camera[]={766,775}, scroll[]={765,773};
  for(unsigned i=0;i<2;i++){
    s_seed.hScroll[0]=scroll[i];s_seed.vScroll[0]=117;
    assert(PrepareTerrain(1,camera[i],116,701,161));
    assert(s_view.hScroll[0]==701 && s_view.vScroll[0]==161);
    assert(PrepareTerrain(0,camera[i],116,camera[i],116));
    assert(s_view.hScroll[0]==scroll[i] && s_view.vScroll[0]==117);
    /* Coincident cameras must not switch P2 back to P1's stale phase. */
    assert(PrepareTerrain(1,camera[i],116,camera[i],116));
    assert(s_view.hScroll[0]==camera[i] && s_view.vScroll[0]==116);
  }
  /* 64-column BG1/BG2 stay widened; 32-column BG3 (Ropey rain) repeats. */
  s_view.screenEnabled[0]=0x17; s_view.screenEnabled[1]=0;
  s_view.bgXsc[0]=0x6d; s_view.bgXsc[1]=0x75; s_view.bgXsc[2]=0x2c;
  assert(PrepareTerrain(0,766,116,766,116));
  assert(s_repeat==0x04);
  s_view.bgXsc[2]=0x2d;
  assert(PrepareTerrain(0,766,116,766,116));
  assert(s_repeat==0);
  /* Slip-Slide Ride: BG1 has a 256-pixel vertical ring. Its scroll $43
   * names camera $A43, not $843 plus an artificial 64-tile map offset. */
  patterned=1;s_seed.bgXsc[0]=1;
  s_seed.hScroll[0]=0x28d;s_seed.vScroll[0]=0x43;
  for(unsigned r=328;r<356;r++)for(unsigned c=81;c<113;c++)
    s_seed.vram[TileAddress(0,1,c,r)]=(uint16_t)r;
  assert(PrepareTerrain(1,0x28d,0xa43,0x49,0xb61));
  assert(s_view.vram[TileAddress(0,1,9,364)]==364);
  assert(s_view.vScroll[0]==0x161);
  assert(PrepareTerrain(0,0x28d,0xa43,0x28d,0xa43));
  assert(s_view.vScroll[0]==0x43);
  assert(UnwrapTilemapScroll(3,0x8203,512)==0x8203);
  assert(UnwrapTilemapScroll(0x3ff,0x8200,512)==0x81ff);
  assert(UnwrapTilemapScroll(0xff,0x8000,256)==0x7fff);
  return 0;
}
''')

    def test_first_capture_preserves_restored_world_cursors(self):
        source = (ROOT / "runner/dkc1_coop_view.c").read_text()
        function = "void Dkc1CoopViewCapture(" + source.split(
            "void Dkc1CoopViewCapture(", 1)[1].split("static void ResetScratch", 1)[0]
        test_coop_mod.CoopModTests().compile_harness('#include "dkc1_coop_oam.h"\n' + r'''
#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
typedef struct { unsigned token; uint16_t oam[256]; uint8_t highOam[32]; } Ppu;
static uint8_t s_native_oam[512], s_native_high[32];
static uint16_t s_native_cam_x, s_native_cam_y;
static int s_native_oam_valid;
static uint16_t Read16(unsigned a){return a==0x088b?91:137;}
static int FixedRoomGameplay(void){return 0;}
static int s_ready, s_seed_valid, enabled=1, world_cursor=37;
static Ppu s_seed;
static const char *s_status;
static uint8_t g_ram[1];
static int Dkc1CoopEnabled(void) { return enabled; }
static int Dkc1CoopEntitiesActive(void) { return 1; }
static int Dkc1CoopInLevel(const uint8_t *ram) { return 1; }
static void Dkc1CoopWorldReset(void) { world_cursor=0; }
''' + function + r'''
int main(void) {
  Ppu p={123}; p.oam[0]=0x3456; p.highOam[0]=0xab;
  Dkc1CoopViewCapture(&p);
  assert(world_cursor==37 && s_seed.token==123 && s_seed_valid);
  Dkc1CoopViewCapture(&p);
  assert(world_cursor==37);
  assert(s_native_cam_x==91 && s_native_cam_y==137);
  assert(!memcmp(&s_seed,&p,sizeof p));
  return 0;
}
''')

    def test_private_tile_addresses_match_native_ppu_all_map_sizes(self):
        source = (ROOT / "runner/dkc1_coop_view.c").read_text()
        address = "static uint16_t TileAddress(" + source.split(
            "static uint16_t TileAddress(", 1)[1].split("\n}", 1)[0] + "\n}\n"
        ppu = (ROOT / "snesrecomp/runner/src/snes/ppu.c").read_text()
        native = "static uint16 PpuReadTilemapEntry(" + ppu.split(
            "static uint16 PpuReadTilemapEntry(", 1)[1].split("\n}", 1)[0] + "\n}"
        test_coop_mod.CoopModTests().compile_harness('#include "dkc1_coop_oam.h"\n' + r'''
#include <assert.h>
#include <stdint.h>
typedef uint16_t uint16;
typedef unsigned uint;
typedef struct { uint16_t vram[0x8000], base; uint8_t size; } Ppu;
#define PPU_bgTilemapAdr(p,l) ((p)->base)
#define PPU_bgTilemapHigher(p,l) ((p)->size & 2)
#define PPU_bgTilemapWider(p,l) ((p)->size & 1)
''' + address + native + r'''
int main(void) {
  Ppu p;
  for(unsigned i=0;i<0x8000;i++) p.vram[i]=(uint16_t)i;
  for(unsigned size=0;size<4;size++) {
    p.size=(uint8_t)size;
    for(unsigned b=0;b<0x8000;b+=0x400) {
      p.base=(uint16_t)b;
      for(unsigned y=0;y<128;y++) for(unsigned x=0;x<128;x++)
        assert(TileAddress(p.base,p.size,x,y)==PpuReadTilemapEntry(&p,0,x,y));
    }
  }
  return 0;
}
''')

    def test_native_subsequence_and_unsigned_stable_extra_priority(self):
        source = (ROOT / "runner/dkc1_coop_view.c").read_text(encoding="utf-8")
        functions = "static int ActorVisible(" + source.split(
            "static int ActorVisible(", 1)[1].split("static void StampBubbles(", 1)[0]
        test_coop_mod.CoopModTests().compile_harness('#include "dkc1_coop_oam.h"\n' +
            HARNESS.replace("/* ORDER FUNCTIONS */", functions))

    def test_bubbles_keep_rom_frames_palette_and_oam_attributes(self):
        source = (ROOT / "runner/dkc1_coop_view.c").read_text()
        stamp = "static void StampBubbles(" + source.split(
            "static void StampBubbles(", 1)[1].split("static int StampCastText", 1)[0]
        test_coop_mod.CoopModTests().compile_harness('#include "dkc1_coop_oam.h"\n#include "dkc1_coop_raster.h"\n' + r'''
#include <assert.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
enum {kHeight=224};
typedef struct {uint16_t vram[0x8000],cgram[256],oam[256];uint8_t highOam[32];
 uint8_t *renderBuffer; unsigned renderPitch,extraLeftRight;} Ppu;
static Ppu s_view;
static struct {int x,y;uint16_t attributes;} s_bubble[32];
static int s_bubble_n;
static uint8_t s_backup[0x20000],rom[0x400000],pixels[398*224*4];
static uint16_t s_obj_depth[1],s_obj_color[1];
static struct {void *cart;} snes,*g_snes=&snes;
static unsigned batches;
#define PPU_objTileAdr1(p) 0
#define PPU_objTileAdr2(p) 0x1000
static uint16_t Dkc1WramU16(const uint8_t *r,unsigned at){return r[at]|(r[at+1]<<8);}
static const uint8_t *cart_getRomPtr(void *c,uint8_t bank,uint16_t at){return rom+((bank&0x3f)*0x10000u)+at;}
static int Dkc1CoopGraphicsReadPalette(uint16_t source,uint16_t colors[16]){
 assert(source==0x86f2);colors[0]=0;for(int i=1;i<16;i++)colors[i]=(uint16_t)(i*37);return 1;
}
static void ClearOam(void){memset(s_view.oam,0,sizeof s_view.oam);memset(s_view.highOam,0,32);
 for(int i=0;i<128;i++)s_view.oam[i*2]=0xf000;}
static void Dkc1Private_AccumulateSprites(Ppu *p,uint16_t *depth,uint16_t *colors){
 batches++;assert(!memcmp((uint8_t*)p->vram+0x1000,rom+0x130600,7*32));
 for(int i=0;i<16;i++)assert(p->cgram[0xe0+i]==(i?i*37:0));
 for(int i=0;i<7;i++){
  assert(p->oam[i*2+1]==s_bubble[i].attributes);
  assert((p->oam[i*2]&255)==(s_bubble[i].x&255));
  assert((p->oam[i*2]>>8)==(s_bubble[i].y&255));
  assert(((p->highOam[i>>2]>>((i&3)*2))&3)==((s_bubble[i].x>>8)&1));
 }
 assert((p->oam[14]>>8)==240);
}
''' + stamp + r'''
int main(void){
 const uint8_t record[]={0,0x94,0x40,0,0,6,0xd3,7,0,0x8f,0,0x80,0,0,0x8d,0xb,0xf8,0,0,0x88,0xf2,0x86};
 snes.cart=rom;
 memcpy(rom+0x358ccd,record,sizeof record);
 for(int i=0;i<224;i++)rom[0x130600+i]=(uint8_t)(i*11+7);
 s_backup[0x1e23]=0x80;s_backup[0x1e24]=0x3c;
 const unsigned extras[]={0,43,71};
 for(int e=0;e<3;e++){
  memset(&s_view,0xa5,sizeof s_view);s_view.renderBuffer=pixels;
  s_view.renderPitch=(256+extras[e]*2)*4;s_view.extraLeftRight=extras[e];
  memset(pixels,0x28,sizeof pixels);s_bubble_n=7;
  for(int i=0;i<7;i++){s_bubble[i].x=i==0?-4:i==6?268:20*i;s_bubble[i].y=17+i;
   s_bubble[i].attributes=(uint16_t)(0x3c80+i+(i==3?0xc000:0));}
  uint16_t vram[0x8000],pal[256];memcpy(vram,s_view.vram,sizeof vram);memcpy(pal,s_view.cgram,sizeof pal);
  StampBubbles();assert(batches==(unsigned)e+1);
  assert(!memcmp(vram,s_view.vram,sizeof vram));assert(!memcmp(pal,s_view.cgram,sizeof pal));
  for(unsigned i=0;i<sizeof pixels;i++)assert(pixels[i]==0x28); /* no post-composition painting */
 }
 s_bubble_n=0;StampBubbles();assert(batches==3);
 s_bubble_n=7;rom[0x358cd4]=8;StampBubbles();assert(batches==3); /* unknown sheet fails closed */
 return 0;
}
''')
        rom = ROOT / "dkc1.sfc"
        if rom.exists():
            data = rom.read_bytes()
            self.assertEqual(data[0x358ccd:0x358cd5], bytes.fromhex("00 94 40 00 00 06 d3 07"))
            self.assertEqual(data[0x97a5:0x97ac], bytes.fromhex("a0 cd 8c 22 1c 80 b5"))

    def test_native_comparison_bytes_when_local_rom_is_present(self):
        rom = ROOT / "build/dkc1.sfc"
        if not rom.exists():
            self.skipTest("local ROM required to verify native sorting seam")
        data = rom.read_bytes()
        self.assertEqual(data[0x388355:0x388363],
                         bytes.fromhex("c5 6c 85 6c 90 08 f0 06 b5 02 95 04 94 02"))


HARNESS = r'''
#include <stdint.h>
#include <stdio.h>
#include <string.h>
static uint8_t ram[0x20000], before[0x20000];
static uint16_t field[128][5];
static int Dkc1CoopEntityIsHandle(uint16_t h) {
  return !(h&1) && ((h>=2 && h<=0x32) || (h>=0x80 && h<=0xfe));
}
static uint16_t Dkc1CoopEntityNextHandle(uint16_t h) { return h==0x32?0x80:h+2; }
static uint16_t Dkc1CoopEntityRead16(const uint8_t *r, uint16_t base, uint16_t h) {
  (void)r;
  switch(base) {
    case 0x0d45: return field[h>>1][0]; case 0x0d11: return field[h>>1][1];
    case 0x0b19: return field[h>>1][2]; case 0x0bc1: return field[h>>1][3];
    case 0x0a7d: return field[h>>1][4];
  }
  return 0;
}
/* ORDER FUNCTIONS */
static void actor(unsigned h,unsigned depth) {
  field[h>>1][0]=1; field[h>>1][1]=1;
  field[h>>1][2]=1100; field[h>>1][3]=700; field[h>>1][4]=(uint16_t)depth;
}
static void native(unsigned i,unsigned h) { ram[0xab1+i*2]=(uint8_t)h; }
static int check(const uint16_t *wanted,unsigned n) {
  uint16_t order[128]={0}; memcpy(before,ram,sizeof ram);
  unsigned count=BuildActorOrder(ram,1024,200,43,order);
  if(count!=n || memcmp(order,wanted,n*2) || memcmp(ram,before,sizeof ram)) {
    printf("order:");for(unsigned i=0;i<count;i++)printf(" %02x",order[i]);puts("");return 0;
  }
  return 1;
}
#define CHECK(c,m) do { if(!(c)) { puts(m);return 1; } } while(0)
int main(void) {
  ram[0x4a]=0;ram[0x4b]=4;
  native(0,6);native(1,2);native(2,4);native(3,6); /* duplicate ignored */
  actor(6,300);actor(2,100);actor(4,100);
  actor(0x80,200);actor(0x82,100);actor(0x84,0xffff);actor(0x86,100);
  actor(0x88,0x8000);actor(0x8a,1);
  static const uint16_t sorted[]={0x84,0x88,6,0x80,2,4,0x82,0x86,0x8a};
  CHECK(check(sorted,9),"unsigned descending merge or stable ties failed");
  /* Native partial sort must never become a second complete host sort. */
  memset(field,0,sizeof field);actor(6,10);actor(2,100);actor(4,20);actor(0x80,50);
  static const uint16_t partial[]={0x80,6,2,4};
  CHECK(check(partial,4),"native subsequence changed");
  /* Highest-depth dead, invisible, and missing-pose actors must not consume
   * priority positions. A visible physical actor missing from AB1 still joins. */
  actor(8,0xffff);actor(0x82,0xffff);actor(0x84,0xffff);actor(0x86,0xffff);
  field[8>>1][2]=5000;field[0x82>>1][0]=0;field[0x84>>1][1]=0;field[0x86>>1][3]=0;
  actor(10,200);
  static const uint16_t filtered[]={10,0x80,6,2,4};
  CHECK(check(filtered,5),"extra actor visibility/liveness filter failed");
  /* First opaque OAM wins in the private accumulator: an overlapping extra
   * actor with larger depth must precede the native actor it used to trail. */
  memset(field,0,sizeof field);memset(ram+0xab1,0,0x34);
  native(0,2);actor(2,1);actor(0x80,2);
  static const uint16_t overlap[]={0x80,2};
  CHECK(check(overlap,2),"high-priority extra still draws behind native actor");
  /* All 89 handles fit exactly once; original sentinel/effect slots do not. */
  memset(field,0,sizeof field);memset(ram+0xab1,0,0x34);
  for(uint16_t h=2;Dkc1CoopEntityIsHandle(h);h=Dkc1CoopEntityNextHandle(h))actor(h,7);
  uint16_t all[128];unsigned n=BuildActorOrder(ram,1024,200,43,all);
  CHECK(n==89,"expanded capacity or handle traversal changed");
  for(unsigned i=1;i<n;i++)CHECK(all[i]>all[i-1],"equal-depth expanded handles are unstable");
  return 0;
}
'''
