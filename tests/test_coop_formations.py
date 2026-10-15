from pathlib import Path
import hashlib
import unittest

import test_coop_mod

ROOT = Path(__file__).resolve().parents[1]


class CoopFormationsTests(unittest.TestCase):
    def test_pure_renderer_matches_native_rom_oam(self):
        rom = ROOT / "dkc1.sfc"
        if not rom.exists():
            self.skipTest("supported local ROM is required for native draw oracle")
        self.assertEqual(hashlib.sha256(rom.read_bytes()).hexdigest(),
                         "fa8cacf5bbfc39ee6bbaa557adf89133d60d42f6cf9e1db30d5a36a469f74d15")
        test_coop_mod.CoopModTests().compile_harness(
            HARNESS.replace("ROM_PATH", rom.as_posix()),
            ["runner/dkc1_coop_formations.c", "snesrecomp/runner/src/snes/interp816.c"])


HARNESS = r'''
#include "dkc1_coop_formations.h"
#include "interp816.h"
#include <stdio.h>
#include <string.h>

static uint8_t ram[0x20000], before[0x20000], rom[0x400000];
static unsigned port, emitted;
static uint16_t out_x[4096], out_tile[4096];
static uint8_t out_y[4096];
static int fail;
static uint16_t rd(unsigned a) { return ram[a] | (uint16_t)ram[a+1] << 8; }
static void wr(unsigned a, unsigned v) { ram[a]=(uint8_t)v; ram[a+1]=(uint8_t)(v>>8); }
int interp816_opcode_hook(uint32_t pc) { (void)pc; return 0; }
static uint8_t read_bus(void *p, uint32_t address) {
  (void)p;
  unsigned bank=address>>16, a=address&65535;
  if (bank==0x7e || bank==0x7f) return ram[address-0x7e0000];
  if (a<0x2000 && (bank&0x7f)<0x40) return ram[a];
  return rom[address&0x3fffff];
}
static void write_bus(void *p, uint32_t address, uint8_t v) {
  (void)p;
  unsigned bank=address>>16, a=address&65535;
  if (bank==0x7e || bank==0x7f) { ram[address-0x7e0000]=v; return; }
  if (a<0x2000 && (bank&0x7f)<0x40) { ram[a]=v; return; }
  switch(a) {
    case 0x2180: ram[port++&0x1ffff]=v; break;
    case 0x2181: port=(port&0x1ff00)|v; break;
    case 0x2182: port=(port&0x100ff)|((unsigned)v<<8); break;
    case 0x2183: port=(port&0xffff)|((unsigned)(v&1)<<16); break;
    case 0x2184: break; /* ignored second byte of native STZ $2183 */
    default: fail=1; printf("unexpected native write %06x\n",address); break;
  }
}
static void emit(void *p, int16_t x, uint8_t y, uint16_t tile) {
  (void)p;
  if(emitted>=4096) { fail=1; return; }
  out_x[emitted]=(uint16_t)x&511; out_y[emitted]=y; out_tile[emitted]=tile;
  emitted++;
}
static unsigned random_state=17;
static unsigned rnd(void) { random_state=random_state*1664525u+1013904223u; return random_state; }
static void check_wide(unsigned extra) {
  uint16_t saved_x[4096], saved_tile[4096]; uint8_t saved_y[4096];
  unsigned saved_count=emitted;
  memcpy(saved_x,out_x,sizeof out_x); memcpy(saved_y,out_y,sizeof out_y);
  memcpy(saved_tile,out_tile,sizeof out_tile); emitted=0;
  unsigned n=Dkc1CoopFormationsDrawWide(ram,rom,1024,200,extra,extra,emit,0);
  if(n!=emitted || memcmp(ram,before,sizeof ram)) { puts("wide draw modified WRAM/count"); fail=1; }
  unsigned center=0;
  for(unsigned i=0;i<n;i++) {
    int x=out_x[i]>=256?(int)out_x[i]-512:out_x[i];
    if(x < -15) continue; /* both left margin and right 9-bit coordinates */
    if(center>=saved_count || out_x[i]!=saved_x[center] ||
       out_y[i]!=saved_y[center] || out_tile[i]!=saved_tile[center]) {
      printf("wide %u changed native center OBJ %u\n",extra,center); fail=1; break;
    }
    center++;
  }
  if(center!=saved_count) { puts("wide draw lost native center pieces"); fail=1; }
  emitted=saved_count;
  memcpy(out_x,saved_x,sizeof out_x); memcpy(out_y,saved_y,sizeof out_y);
  memcpy(out_tile,saved_tile,sizeof out_tile);
}
static void fixture(int dx, int dy, unsigned frame, unsigned width) {
  memset(ram,0,sizeof ram); port=emitted=0;
  wr(0xc000,1024+dx); wr(0xc002,200+dy);
  wr(0xc004,width*2); wr(0xc006,width*8);
  wr(0xc008,1); wr(0xc00a,0xd000); wr(0xc010,0xffff);
  for(unsigned i=0;i<width;i++) wr(0xd000+i*2,rnd()>>16);
  wr(0xc4,1); wr(0xd1,0x71e0); wr(0x28,frame);
  wr(0xbe,1024); wr(0xc0,200); wr(0x8e,0x200);
  memcpy(before,ram,sizeof ram);
  unsigned count=Dkc1CoopFormationsDraw(ram,rom,1024,200,emit,0);
  if(count!=emitted || memcmp(ram,before,sizeof ram)) { puts("host mutated WRAM/count"); fail=1; }
  check_wide(43); check_wide(71);
  Interp816 *cpu=interp816_init(0,read_bus,write_bus);
  cpu->e=cpu->mf=cpu->xf=0; cpu->sp=0x1ff; cpu->pc=0xb89f; cpu->k=0xb8;
  unsigned steps;
  for(steps=0;steps<100000;steps++) {
    if(cpu->pc==0xb918) { cpu->pc=0xb9b5; cpu->x=0; } /* replace only list/collection pass */
    if(cpu->pc==0xb90f) break;
    interp816_runOpcode(cpu);
  }
  if(steps==100000) { puts("native draw did not return"); fail=1; }
  unsigned native_count=(rd(0x8e)-0x200)/4;
  unsigned compared=emitted>128?128:emitted;
  if(native_count!=compared) {
    printf("count dx=%d dy=%d frame=%u width=%u: native=%u host=%u\n",dx,dy,frame,width,native_count,emitted); fail=1;
  }
  for(unsigned i=0;i<compared;i++) {
    unsigned high=(ram[0x400+i/4]>>(2*(i%4)))&3;
    unsigned x=ram[0x200+i*4]|((high&1)<<8);
    unsigned y=ram[0x201+i*4], tile=rd(0x202+i*4);
    if(x!=out_x[i] || y!=out_y[i] || tile!=out_tile[i] || !(high&2)) {
      printf("OBJ dx=%d dy=%d frame=%u width=%u index=%u: native=%03x,%02x,%04x host=%03x,%02x,%04x high=%u\n",dx,dy,frame,width,i,x,y,tile,out_x[i],out_y[i],out_tile[i],high); fail=1; break;
    }
  }
  interp816_free(cpu);
}
int main(void) {
  FILE *fp=fopen("ROM_PATH","rb");
  if(!fp || fread(rom,1,sizeof rom,fp)!=sizeof rom) return 1;
  fclose(fp);
  /* Execute the stock leading-edge quantizers used by collection. In both
   * axes a gap of 8..15 still selects bitmap cell zero, not cell one. */
  for(unsigned gap=8;gap<32;gap++) for(unsigned axis=0;axis<2;axis++) {
    Interp816 *cpu=interp816_init(0,read_bus,write_bus);
    cpu->e=cpu->mf=cpu->xf=0; cpu->c=1; cpu->a=gap;
    cpu->k=0xb8; cpu->pc=axis?0xbbb4:0xbb4b;
    for(unsigned op=0;op<(axis?8:6);op++) interp816_runOpcode(cpu);
    if(cpu->a!=((gap-8)>>3)*2) {
      puts("native collection grid rounding changed"); fail=1;
    }
    interp816_free(cpu);
  }
  static const int xs[]={-250,-129,-24,-16,-15,-8,-1,0,1,128,248,255};
  static const int ys[]={-135,-128,-16,-15,-8,-1,0,95,96,111,127,200,223};
  for(unsigned x=0;x<sizeof xs/sizeof xs[0];x++)
    for(unsigned y=0;y<sizeof ys/sizeof ys[0];y++)
      fixture(xs[x],ys[y],rnd()&65535,32);
  fixture(0,0,0,1); fixture(1,1,0x7fff,2); fixture(-16,-1,0x8000,32);
  if(fail) return 1;
  /* A distant source still draws with the primary traversal cursor elsewhere.
   * The source bitmap remains shared, and clearing one bit removes one OBJ. */
  memset(ram,0,sizeof ram); wr(0xc000,2048); wr(0xc002,100);
  wr(0xc004,2); wr(0xc006,8); wr(0xc008,1); wr(0xc00a,0xd000);
  wr(0xc010,0xffff); wr(0xc2,0x1234); wr(0xc4,1); wr(0xd1,0x71e0);
  wr(0xd000,0xc000); wr(0x1b23,512); emitted=0;
  if(Dkc1CoopFormationsDraw(ram,rom,2560,0,emit,0)!=2 || out_x[0]!=0) return 1;
  wr(0xd000,0x8000); emitted=0;
  if(Dkc1CoopFormationsDraw(ram,rom,2560,0,emit,0)!=1) return 1;
  wr(0xc4,2); emitted=0;
  if(Dkc1CoopFormationsDraw(ram,rom,2560,0,emit,0)!=0) return 1;
  wr(0xc4,1); wr(0xc000,2310); /* screen X=262 is in the right margin */
  emitted=0;
  if(Dkc1CoopFormationsDraw(ram,rom,2560,0,emit,0)!=0) return 1;
  if(Dkc1CoopFormationsDrawWide(ram,rom,2560,0,43,43,emit,0)!=1 ||
     out_x[0]!=262) return 1;
  wr(0xc000,1992); emitted=0; /* X=-56 still overlaps the 342px left margin */
  if(Dkc1CoopFormationsDrawWide(ram,rom,2560,0,43,43,emit,0)!=1) return 1;
  wr(0xc000,1984); emitted=0; /* X=-64 */
  if(Dkc1CoopFormationsDrawWide(ram,rom,2560,0,43,43,emit,0)!=0) return 1;
  if(Dkc1CoopFormationsDrawWide(ram,rom,2560,0,71,71,emit,0)!=1 ||
     out_x[0]!=(512-64)) return 1;
  return 0;
}
'''
