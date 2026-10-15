#ifndef DKC1_COOP_SCROLL_H
#define DKC1_COOP_SCROLL_H

#include "snes/interp816.h"
#include <string.h>

/* Re-evaluate the cartridge's NMI on an isolated bus. Results belong only
 * to private presentation. DMA, audio, clocks and shared devices are unreachable. */
typedef struct Dkc1CoopScrollBus {
  uint8_t ram[0x20000], dma[128];
  const uint8_t *rom;
  uint16_t x[4], y[4], dividend, quotient, product;
  uint8_t prev, prev2, written, hdma, multiplicand, failed;
  uint32_t failure_address;
  int32_t flash_edge[224];
  uint8_t flash_left[224], flash_ready;
  int32_t flash_origin;
} Dkc1CoopScrollBus;

static uint8_t Dkc1CoopScrollRead(void *context, uint32_t address) {
  Dkc1CoopScrollBus *b = context;
  unsigned bank = address >> 16, a = address & 0xffffu;
  if (bank == 0x7e || bank == 0x7f)
    return b->ram[((bank & 1u) << 16) | a];
  if ((bank & 0x7fu) < 0x40u) {
    if (a < 0x2000) return b->ram[a];
    if (a >= 0x4300 && a < 0x4380) return b->dma[a - 0x4300];
    if (a >= 0x4214 && a <= 0x4217)
      return (uint8_t)((a < 0x4216 ? b->quotient : b->product) >> ((a & 1) * 8));
  }
  if ((bank & 0x40u) || a >= 0x8000u) return b->rom[address & 0x3fffffu];
  b->failed = 1; b->failure_address = address;
  return 0;
}

static void Dkc1CoopScrollWrite(void *context, uint32_t address, uint8_t value) {
  Dkc1CoopScrollBus *b = context;
  unsigned bank = address >> 16, a = address & 0xffffu;
  if (bank == 0x7e || bank == 0x7f) {
    b->ram[((bank & 1u) << 16) | a] = value; return;
  }
  if ((bank & 0x7fu) < 0x40u) {
    if (a < 0x2000) { b->ram[a] = value; return; }
    if (a >= 0x4300 && a < 0x4380) { b->dma[a - 0x4300] = value; return; }
    if (a >= 0x210d && a <= 0x2114) {
      unsigned plane = (a - 0x210d) / 2;
      if (a & 1u) {
        b->x[plane] = ((value << 8) | (b->prev & 0xf8) | (b->prev2 & 7)) & 0x3ff;
        b->prev2 = value;
      } else b->y[plane] = ((value << 8) | b->prev) & 0x3ff;
      b->prev = value;
      b->written |= (uint8_t)(1u << (a - 0x210d));
      return;
    }
    switch (a) {
      case 0x4202: b->multiplicand = value; return;
      case 0x4203: b->product = b->multiplicand * value; return;
      case 0x4204: b->dividend = (b->dividend & 0xff00) | value; return;
      case 0x4205: b->dividend = (b->dividend & 0xff) | ((uint16_t)value << 8); return;
      case 0x4206:
        b->quotient = value ? b->dividend / value : 0xffff;
        b->product = value ? b->dividend % value : b->dividend; return;
      case 0x420c: b->hdma = value; return;
    }
    /* Transfers and non-scroll PPU writes belong to the real NMI. Never
     * repeat those effects while calculating a second camera's projection. */
    if ((a >= 0x2100 && a <= 0x2133) || a == 0x420b) return;
  }
  b->failed = 1; b->failure_address = address;
}

/* A nonzero stop ends a proven scroll-only span before its gameplay tail.
 * The balanced span retains the sentinel return frame; a whole routine
 * consumes that frame with RTS as before. */
static int Dkc1CoopScrollRunUntil(Dkc1CoopScrollBus *b, uint32_t entry,
                                uint32_t stop) {
  uint16_t routine=(uint16_t)entry;
  uint8_t bank=(uint8_t)(entry >> 16);
  if (!b || !b->rom || (bank!=0x80 && bank!=0x81) || routine < 0x8000 || routine == 0xffff) return 0;
  if (stop && ((stop >> 16) != bank || (uint16_t)stop <= routine ||
               (uint16_t)stop == 0xffff)) return 0;
  Interp816 cpu = {0};
  cpu.mem = b; cpu.read = Dkc1CoopScrollRead; cpu.write = Dkc1CoopScrollWrite;
  cpu.k = bank; cpu.db = 0x80; cpu.pc = routine; cpu.sp = 0x1fd;
  b->ram[0x1fe] = 0xfe; b->ram[0x1ff] = 0xff;
  for (unsigned i = 0; i < 65536 && !b->failed; i++) {
    if (cpu.k == bank && cpu.pc == (stop ? (uint16_t)stop : 0xffff))
      return cpu.sp == (stop ? 0x1fd : 0x1ff);
    if (stop && cpu.k == bank && cpu.pc == 0xffff) return 0;
    if (entry==0x819374u && cpu.k==0x81 && cpu.pc==0x93fcu &&
        !cpu.mf && !cpu.xf && !cpu.dp) {
      /* Native 93F2..9404: world X minus camera, then XBA truncates the
       * screen coordinate into an 8.8 accumulator. Keep its signed value
       * and the cartridge's two slopes before the 256-wide clipping loops. */
      int row=(int16_t)cpu.x / 3;
      unsigned actor=cpu.y;
      uint16_t wx=b->ram[0xb19+actor]|b->ram[0xb1a+actor]<<8;
      uint16_t cx=b->ram[0x88b]|b->ram[0x88c]<<8;
      /* The pose offset's upper byte is a native fractional/flip bias,
       * not another 256 pixels. Retain XBA's fraction for native parity. */
      int screen=(int16_t)(uint16_t)(wx-cx)+(int8_t)b->ram[0x4e];
      b->flash_origin=screen;
      int wraps=screen>=0?screen/256:-((-screen+255)/256);
      uint8_t fraction=(uint8_t)((cpu.a >> 8)-wraps);
      int32_t origin=(int32_t)screen*256+fraction;
      int16_t upper=(int16_t)(b->ram[0x52]|b->ram[0x53]<<8);
      int16_t lower=(int16_t)(b->ram[0x54]|b->ram[0x55]<<8);
      b->flash_ready=(int16_t)cpu.x % 3==0;
      for (int line=0;line<224;line++) {
        int16_t slope=line<row?upper:lower;
        int distance=line<row?row-line+1:line-row+1;
        int32_t fixed=origin+(int32_t)slope*distance;
        b->flash_edge[line]=fixed>=0?fixed/256:-((-fixed+255)/256);
        b->flash_left[line]=slope<0;
      }
      /* $029D ends the 223-record stream; the final scanline holds it. */
      b->flash_edge[223]=b->flash_edge[222];
      b->flash_left[223]=b->flash_left[222];
    }
    /* Instructions must remain in cartridge ROM, including nested calls. */
    if (cpu.k == 0x7e || cpu.k == 0x7f ||
        (!(cpu.k & 0x40u) && cpu.pc < 0x8000)) return 0;
    if ((cpu.k & 0x7fu) == 0 && cpu.pc == 0xa2cdu) {
      /* A2CD..A32A consumes a CGRAM DMA queue entry and polls the physical
       * VBlank deadline. The real NMI already owns that transfer; this bus
       * only projects scroll/HDMA and the view retains completed native CGRAM.
       * Use the routine's RTS, preserving its caller's stack. Pin the whole
       * upload-only body rather than inventing values for PPU timing reads. */
      static const uint8_t upload[] = {
        0x9c,0x43,0x1e,0xad,0xcf,0x1a,0xcd,0xd1,0x1a,0xf0,0x52,0x0a,0x0a,0xaa,
        0xad,0xcf,0x1a,0x1a,0x29,0x0f,0x00,0x8d,0xcf,0x1a,0xee,0x43,0x1e,
        0xa9,0x00,0x22,0x8d,0x00,0x43,0xa9,0x1e,0x00,0x8d,0x05,0x43,0xbd,
        0x8f,0x1a,0x8d,0x02,0x43,0xbd,0x91,0x1a,0xe2,0x20,0x8d,0x04,0x43,
        0xeb,0x8d,0x21,0x21,0xa9,0x01,0x8d,0x0b,0x42,0xc2,0x20,0xe2,0x20,
        0xad,0x37,0x21,0xad,0x3f,0x21,0xad,0x3d,0x21,0xeb,0xad,0x3d,0x21,
        0xeb,0xc2,0x20,0x29,0xff,0x01,0xc9,0xe0,0x00,0x30,0x03,0xc9,0xf8,0x00,0x60
      };
      if (cpu.mf || cpu.xf || cpu.dp ||
          memcmp(b->rom + 0xa2cd, upload, sizeof upload)) return 0;
      cpu.pc = 0xa32a;
    }
    interp816_runOpcode(&cpu);
    if (cpu.waiting || cpu.stopped) return 0;
  }
  return 0;
}

static int Dkc1CoopScrollRunAt(Dkc1CoopScrollBus *b, uint32_t entry) {
  return Dkc1CoopScrollRunUntil(b, entry, 0);
}

static int Dkc1CoopScrollRun(Dkc1CoopScrollBus *b, uint16_t routine) {
  return Dkc1CoopScrollRunAt(b,0x800000u|routine);
}

#endif
