#include <assert.h>
#include "dkc1_coop_mod.c"
#include "coop_test_support.h"
#include "snes/interp816.h"

static const uint8_t input_copy[] = { /* ROM_B2C7 */ };
static uint8_t ram[0x20000];
int interp816_opcode_hook(uint32_t pc) { return 0; }
int Dkc1VideoLoadCgramFromRom(struct Ppu *p,unsigned d,uint8_t b,uint16_t a,unsigned n){return 1;}
void Dkc1VideoCopyCgram(struct Ppu *p,unsigned d,unsigned s,unsigned n){}
static uint8_t Read(void *context,uint32_t a) {
  if ((uint16_t)a<0x2000) return ram[(uint16_t)a];
  assert(a>=0xbfb2c7 && a<0xbfb2c7+sizeof input_copy);
  return input_copy[a-0xbfb2c7];
}
static void Write(void *context,uint32_t a,uint8_t v) {
  assert((uint16_t)a<0x2000);ram[(uint16_t)a]=v;
}
int main(void) {
  const unsigned modes[]={0,4,9,0x0b};
  for(unsigned mode=0;mode<sizeof modes/sizeof modes[0];++mode)
  for(unsigned id=1;id<=2;++id)
  for(unsigned turn=0;turn<=1;++turn) {
    memset(ram,0,sizeof ram);ResetOverlayState();CoopTestResetSupport();s_runtime.enabled=1;
    Dkc1WramSetU16(ram,0x32,modes[mode]);Dkc1WramSetU16(ram,0x3e,0xc5);
    Dkc1WramSetU16(ram,0x4a,0x7000);Dkc1WramSetU16(ram,0x1e15,0x22);
    Dkc1WramSetU16(ram,0x1b23,0x600);Dkc1WramSetU16(ram,0x1b25,0x600);
    Dkc1Actor_SetSpriteIDLo(ram,2,1);Dkc1Actor_SetSpriteIDLo(ram,4,2);
    Dkc1WramSetU16(ram,0x56f,id);Dkc1WramSetU16(ram,0x84,id*2-2);
    Dkc1WramSetU16(ram,0x44,turn);
    Dkc1WramSetU16(ram,0x500,0x8100);Dkc1WramSetU16(ram,0x502,0x4800);
    Dkc1WramSetU16(ram,0x504,0x8000);Dkc1WramSetU16(ram,0x506,0x0800);
    Interp816 in={0};in.read=Read;in.write=Write;in.k=0xbf;in.pc=0xb2c7;
    interp816_runOpcode(&in);assert(in.pc==0xb2c9 && in.a==turn);
    CpuState cpu={0};cpu.ram=ram;cpu.P=0x81;CpuState saved=cpu;
    in.a=Dkc1CoopNativePadOwner(&cpu,in.a);
    assert(in.a==id-1 && !memcmp(&saved,&cpu,sizeof cpu));
    while(in.pc!=0xb2e1) interp816_runOpcode(&in);
    assert(Dkc1WramU16(ram,0x16e9+id*2-2)==(id==1?0x8000:0x0800));
    assert(Dkc1WramU16(ram,0x16ed+id*2-2)==(id==1?0x8100:0x4800));
    assert(Dkc1WramU16(ram,0x44)==turn);
    assert(Dkc1WramU16(ram,0x500)==0x8100 && Dkc1WramU16(ram,0x502)==0x4800);
    s_runtime.enabled=0;assert(Dkc1CoopNativePadOwner(&cpu,turn)==turn);s_runtime.enabled=1;
    Dkc1WramSetU16(ram,0x1b25,0x700);assert(Dkc1CoopNativePadOwner(&cpu,turn)==turn);
    Dkc1WramSetU16(ram,0x1b25,0x600);Dkc1WramSetU16(ram,0x1e15,2);
    assert(Dkc1CoopNativePadOwner(&cpu,turn)==turn);
    Dkc1WramSetU16(ram,0x1e15,0x22);Dkc1WramSetU16(ram,0x32,1);
    assert(Dkc1CoopNativePadOwner(&cpu,turn)==turn);
    Dkc1WramSetU16(ram,0x32,9);Dkc1WramSetU16(ram,0x84,6);
    assert(Dkc1CoopNativePadOwner(&cpu,turn)==turn);
  }
  return 0;
}
