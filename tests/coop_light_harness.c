#include <assert.h>
#include "dkc1_coop_mod.c"
#include "dkc1_coop_light.h"
#include "coop_test_support.h"
#include "snes/interp816.h"

static const uint8_t native_gate[] = { /* ROM_B1BA */ };
static const uint8_t native_stop_go[] = { /* ROM_B206 */ };
static const uint8_t native_loopy[] = { /* ROM_B38B */ };
static const uint8_t native_clip[] = { /* ROM_B3D7 */ };
static const uint8_t native_phase[] = { /* ROM_B90B */ };
static uint8_t ram[0x20000], regs[0x4400];
static uint16_t native_y;
static unsigned y_writes;
int interp816_opcode_hook(uint32_t pc) { return 0; }
int Dkc1VideoLoadCgramFromRom(struct Ppu *p, unsigned dst, uint8_t bank,
                            uint16_t addr, unsigned words) { return 1; }
void Dkc1VideoCopyCgram(struct Ppu *p, unsigned dst, unsigned src, unsigned words) {}
static uint8_t Read(void *context, uint32_t address) {
  unsigned bank = address >> 16, lo = address & 0xffff;
  if (bank == 0x7e || bank == 0x7f) return ram[(bank & 1) * 65536 + lo];
  if (lo < 0x2000) return ram[lo];
  if (address >= 0x80b1ba && address < 0x80b1ba + sizeof native_gate)
    return native_gate[address - 0x80b1ba];
  if (address >= 0x80b206 && address < 0x80b206 + sizeof native_stop_go)
    return native_stop_go[address - 0x80b206];
  if (address >= 0x80b38b && address < 0x80b38b + sizeof native_loopy)
    return native_loopy[address - 0x80b38b];
  if (address >= 0x80b3d7 && address < 0x80b3d7 + sizeof native_clip)
    return native_clip[address - 0x80b3d7];
  if (address >= 0x80b90b && address < 0x80b90b + sizeof native_phase)
    return native_phase[address - 0x80b90b];
  assert(!"ceiling-light oracle escaped verified ROM spans"); return 0;
}
static void Write(void *context, uint32_t address, uint8_t value) {
  unsigned bank = address >> 16, lo = address & 0xffff;
  if (bank == 0x7e || bank == 0x7f) ram[(bank & 1) * 65536 + lo] = value;
  else if (lo < 0x2000) ram[lo] = value;
  else {
    assert(lo < sizeof regs); regs[lo] = value;
    if (lo == 0x2112) {
      if (!y_writes++) native_y = value;
      else native_y |= (uint16_t)value << 8;
    }
  }
}
static uint16_t Reg(unsigned a) { return regs[a] | (uint16_t)regs[a+1] << 8; }
int main(void) {
  const uint16_t positions[] = {0, 0x100, 0x8000, 0xcf07, 0xfff0};
  const int offsets[] = {-65,-64,-1,0,169,319,320};
  const int heights[] = {-240,-193,-192,-1,0,1,39,192,224};
  for (unsigned protocol=0;protocol<3;protocol++)
  for (unsigned a=0;a<5;a++) for (unsigned b=0;b<7;b++)
  for (unsigned c=0;c<9;c++) for (unsigned phase=0;phase<2;phase++)
  for (unsigned flag=0;flag<2;flag++) {
    uint16_t x=positions[a], lx=(uint16_t)(x+offsets[b]), ly=0xd0;
    uint16_t y=(uint16_t)(0x1f8-ly-heights[c]), flags=flag?0x200:0;
    memset(ram,0,sizeof ram);memset(regs,0,sizeof regs);y_writes=0;native_y=0;
    /* Native positive-Y/even-frame setup retains the initialized color
     * pointers. The private camera starts from that same template. */
    regs[0x4323]=0x20;regs[0x4332]=2;regs[0x4333]=0x24;
    unsigned color_offset=protocol==1 && flag?0x808u:0;
    regs[0x4322]=color_offset;regs[0x4323]=(0x2000+color_offset)>>8;
    regs[0x4332]=2+color_offset;regs[0x4333]=(0x2402+color_offset)>>8;
    Dkc1WramSetU16(ram,0xdd,lx);Dkc1WramSetU16(ram,0xdf,ly);
    Dkc1WramSetU16(ram,0x88b,x);Dkc1WramSetU16(ram,0x895,y);
    Dkc1WramSetU16(ram,0x28,phase);Dkc1WramSetU16(ram,0x579,flags);
    Dkc1WramSetU16(ram,0x1f1,0x7fff);
    Interp816 in={0};in.read=Read;in.write=Write;in.k=0x80;
    in.pc=protocol==2?0xb38b:protocol?0xb206:0xb1ba;in.sp=0x1f0;
    unsigned count=0;while(in.pc!=0x8000 && count++<160)interp816_runOpcode(&in);
    assert(in.pc==0x8000);
    Dkc1CoopLightPlan p=protocol==2?Dkc1CoopPlanLoopyLight(x,y,lx,ly,phase,flags)
        :protocol?Dkc1CoopPlanStopGoLight(x,y,lx,ly,phase,flags)
        :Dkc1CoopPlanLight(x,y,lx,ly,phase,flags);
    assert(Reg(0x212c)==p.screens);
    if(protocol==2)assert(p.fixed_color==(regs[0x2132]&31u)*0x421u);
    if (!p.visible) {assert(!y_writes);continue;}
    /* Native update builds the opposite buffer. Its preceding frame must
     * build exactly the buffer selected by this NMI, including wraparound. */
    Dkc1WramSetU16(ram,0x28,(uint16_t)(phase-1u));
    in.pc=0xb90b;in.k=0x80;count=0;
    while(in.pc!=0xb916 && in.pc!=0xb9a1 && count++<8)interp816_runOpcode(&in);
    assert(in.pc==0xb916 || in.pc==0xb9a1);
    assert((in.pc==0xb916 ? 0x200u:0x500u)==(phase?0x500u:0x200u));
    if (Reg(0x4322)!=p.table[1]) fprintf(stderr,
        "light offset=%d height=%d phase=%u native=%04x plan=%04x\n",
        offsets[b],heights[c],phase,Reg(0x4322),p.table[1]);
    assert(Reg(0x4312)==p.table[0]);assert(Reg(0x4322)==p.table[1]);
    assert(Reg(0x4332)==p.table[2]);assert(native_y==p.scroll_y);
    if (p.headers) {
      assert(ram[0x10200]==p.scroll_header && ram[0x10500]==p.scroll_header);
      assert(ram[0x2000]==p.color_header && ram[0x2402]==p.color_header);
    }
  }
  return 0;
}
