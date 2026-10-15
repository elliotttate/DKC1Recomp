#ifndef DKC1_COOP_MACHINE_H
#define DKC1_COOP_MACHINE_H
#include "common_rtl.h"
#include "cpu_state.h"
#include "common_cpu_infra.h"
#include "dkc1_coop_clock.h"
#include "snes/dma.h"
#include "snes/ppu.h"
#include <string.h>
/* Execution envelope only. Each caller owns its WRAM, host metadata and
 * commit policy; rendering and allocation do not share a gameplay policy. */
typedef struct Dkc1CoopMachineSnapshot {
  Ppu ppu;
  Dma dma;
  Dkc1CoopClockSnapshot clocks;
  uint32_t cart_bus;
  uint8_t memsel;
} Dkc1CoopMachineSnapshot;
enum { kCoopRestoreClocks = 1, kCoopRestoreDevices = 2,
       kCoopRestoreMemsel = 4, kCoopRestoreCartBus = 8 };
static inline void Dkc1CoopMachineCapture(Dkc1CoopMachineSnapshot *s) {
  if (g_ppu) s->ppu = *g_ppu;
  if (g_dma) s->dma = *g_dma;
  Dkc1CoopClockCapture(&s->clocks);
  s->memsel = g_memsel;
  s->cart_bus = g_snes && g_snes->cart ? g_snes->cart->cpuBusAddress : 0;
}
static inline int Dkc1CoopMachineDevicesUnchanged(const Dkc1CoopMachineSnapshot *s) {
  return (!g_ppu || !memcmp(g_ppu, &s->ppu, sizeof *g_ppu)) &&
         (!g_dma || !memcmp(g_dma, &s->dma, sizeof *g_dma));
}
static inline void Dkc1CoopMachineRestore(const Dkc1CoopMachineSnapshot *s, unsigned flags) {
  if (flags & kCoopRestoreDevices) {
    if (g_ppu) *g_ppu = s->ppu;
    if (g_dma) *g_dma = s->dma;
  }
  if (flags & kCoopRestoreClocks) Dkc1CoopClockRestore(&s->clocks);
  if (flags & kCoopRestoreMemsel) g_memsel = s->memsel;
  if ((flags & kCoopRestoreCartBus) && g_snes && g_snes->cart)
    g_snes->cart->cpuBusAddress = s->cart_bus;
}
#endif
