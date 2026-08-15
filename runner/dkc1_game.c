#include "dkc1_game.h"
#include "dkc1_video.h"

#include "common_cpu_infra.h"
#include "common_rtl.h"
#include "cpu_state.h"
#include "snes/dma.h"
#include "snes/interp_bridge.h"
#include "snes/ppu.h"
#include "snes/saveload.h"
#include "snes/snes.h"

#include <stdbool.h>
#include <string.h>

enum {
  /* DKC1 USA v1.0 vectors: reset $80:8000, native NMI $80:A968 (the runtime's
   * pc24 convention folds the system banks to bank 00). */
  kDkc1ResetPc = 0x008000,
  kDkc1NmiPc = 0x00A968,
};

static bool s_cpu_initialized;
static uint32_t s_resume_pc = kDkc1ResetPc;
static int s_last_lle_result = 1;
static uint64_t s_next_frame_master;

typedef struct Dkc1HostSnapshot {
  CpuState cpu;
  uint32_t resume_pc;
  uint64_t next_frame_master;
  uint64_t main_cpu_cycles_estimate;
  uint64_t apu_pace_cycles_estimate;
  uint64_t apu_last_sync_cycles;
  uint64_t apu_last_sync_master;
  int last_lle_result;
  int frame_counter;
  uint8_t cpu_initialized;
  uint8_t last_hdmaen;
  uint8_t memsel;
} Dkc1HostSnapshot;

enum {
  /* NTSC master clocks per non-short host frame. */
  kDkc1NtscFrameMasterClocks = 1364 * 262,
};

static void Dkc1RunOneFrame(void) {
  bool first_frame = !s_cpu_initialized;
  if (s_next_frame_master == 0) {
    s_next_frame_master =
        g_cpu.master_cycles + kDkc1NtscFrameMasterClocks;
  }
  while (s_next_frame_master <= g_cpu.master_cycles)
    s_next_frame_master += kDkc1NtscFrameMasterClocks;
  interp_bridge_set_master_deadline(s_next_frame_master);

  if (first_frame) {
    cpu_state_init(&g_cpu, g_ram);
    s_cpu_initialized = true;
  }
  if (!first_frame && g_snes->nmiEnabled) {
    /* Rare's engine parks the main loop at WAI; the NMI performs the frame's
     * VBlank work. Push the interrupt frame at the parked PC and run handler
     * plus continuation to the next quiescent wait (works for both an RTI
     * handler and a DKC2-style non-returning frame dispatcher). */
    g_snes->inNmi = true;
    cpu_push_interrupt_frame_at(&g_cpu, s_resume_pc);
    s_last_lle_result =
        interp_bridge_run_until_quiescent(&g_cpu, kDkc1NmiPc);
  } else {
    s_last_lle_result =
        interp_bridge_run_until_quiescent(&g_cpu, s_resume_pc);
  }

  interp_bridge_set_master_deadline(0);
  s_resume_pc = interp_bridge_lle_resume_pc();
  if (g_cpu.master_cycles < s_next_frame_master) {
    g_cpu.master_cycles = s_next_frame_master;
    snes_sync_master_clock(g_snes, g_cpu.master_cycles);
  }
  s_next_frame_master += kDkc1NtscFrameMasterClocks;
}

static void Dkc1SaveExtra(SaveLoadInfo *sli) {
  Dkc1HostSnapshot snapshot;
  memset(&snapshot, 0, sizeof snapshot);
  snapshot.cpu = g_cpu;
  snapshot.cpu.ram = NULL;
  snapshot.resume_pc = s_resume_pc;
  snapshot.next_frame_master = s_next_frame_master;
  snapshot.main_cpu_cycles_estimate = g_main_cpu_cycles_estimate;
  snapshot.apu_pace_cycles_estimate = g_apu_pace_cycles_estimate;
  snapshot.apu_last_sync_cycles = g_apu_last_sync_cycles;
  snapshot.apu_last_sync_master = g_apu_last_sync_master;
  snapshot.last_lle_result = s_last_lle_result;
  snapshot.frame_counter = snes_frame_counter;
  snapshot.cpu_initialized = s_cpu_initialized ? 1u : 0u;
  snapshot.last_hdmaen = g_snesrecomp_last_hdmaen;
  snapshot.memsel = g_memsel;
  sli->func(sli, &snapshot, sizeof snapshot);
}

static void Dkc1LoadExtra(SaveLoadInfo *sli, uint32_t version) {
  (void)version;
  Dkc1HostSnapshot snapshot;
  sli->func(sli, &snapshot, sizeof snapshot);
  g_cpu = snapshot.cpu;
  g_cpu.ram = g_ram;
  s_resume_pc = snapshot.resume_pc;
  s_next_frame_master = snapshot.next_frame_master;
  g_main_cpu_cycles_estimate = snapshot.main_cpu_cycles_estimate;
  g_apu_pace_cycles_estimate = snapshot.apu_pace_cycles_estimate;
  g_apu_last_sync_cycles = snapshot.apu_last_sync_cycles;
  g_apu_last_sync_master = snapshot.apu_last_sync_master;
  s_last_lle_result = snapshot.last_lle_result;
  snes_frame_counter = snapshot.frame_counter;
  s_cpu_initialized = snapshot.cpu_initialized != 0;
  g_snesrecomp_last_hdmaen = snapshot.last_hdmaen;
  g_memsel = snapshot.memsel;
}

static void Dkc1OnStateLoaded(uint32_t version) {
  (void)version;
  g_cpu.ram = g_ram;
  g_apu_last_sync_master = g_cpu.master_cycles;
  g_snes->beamMasterLast = g_cpu.master_cycles;
  interp_bridge_set_master_deadline(0);
  Dkc1VideoSetTerrainReady(false);
}

static const RtlGameInfo kDkc1GameInfo = {
  .title = "dkc1",
  .initialize = NULL,
  .run_frame = &Dkc1RunOneFrame,
  .draw_ppu_frame = &Dkc1DrawPpuFrame,
  .save_name_prefix = "dkc1s",
  .state_save_extra = &Dkc1SaveExtra,
  .state_load_extra = &Dkc1LoadExtra,
  .on_state_loaded = &Dkc1OnStateLoaded,
};

const RtlGameInfo *Dkc1GameInfo(void) {
  return &kDkc1GameInfo;
}

void Dkc1BeginDrawing(uint8_t *pixels, size_t pitch) {
  PpuBeginDrawing(g_ppu, pixels, pitch, kPpuRenderFlags_NewRenderer);
}

void Dkc1DrawPpuFrame(void) {
  SimpleHdma channels[8];
  bool active[8] = {false};

  /* Native 4:3 presentation for bring-up. Widescreen reconstruction (DKC1's
   * rolling column streamers at $81:8705/$81:883F feed it directly) comes
   * after the boot/attract differential gates pass. */
  PpuSetExtraSpace(g_ppu, 0);

  dma_startDma(g_dma, g_snesrecomp_last_hdmaen, true);
  for (int channel = 0; channel < 8; channel++) {
    active[channel] = g_dma->channel[channel].hdmaActive;
    if (active[channel])
      SimpleHdma_Init(&channels[channel], &g_dma->channel[channel]);
  }

  for (int line = 0; line <= 224; line++) {
    ppu_runLine(g_ppu, line);
    for (int channel = 0; channel < 8; channel++) {
      if (active[channel]) SimpleHdma_DoLine(&channels[channel]);
    }
  }

  /* Model the VBlank boundary after the visible lines so the PPU reloads its
   * internal OAM port from OAMADD before the next frame's OAM DMA. */
  (void)ppu_checkOverscan(g_ppu);
  ppu_handleVblank(g_ppu);
}

uint32_t Dkc1ResumePc(void) {
  return s_resume_pc;
}

int Dkc1LastLleResult(void) {
  return s_last_lle_result;
}

/* Required neutral hooks declared by generated funcs.h. */
void RunOneFrameOfGame_Internal(void) {
  Dkc1RunOneFrame();
}

void ResetSpritesFunc(int first) {
  (void)first;
}
