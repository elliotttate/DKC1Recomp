#include "dkc1_game.h"
#include "dkc1_video.h"
#include "dkc1_ws_trace.h"

#include "common_cpu_infra.h"
#include "common_rtl.h"
#include "cpu_state.h"
#include "snes/dma.h"
#include "snes/interp_bridge.h"
#include "snes/ppu.h"
#include "snes/saveload.h"
#include "snes/snes.h"
#include "snes/ws_shadow.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
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

static void Dkc1SetInterpreterA16(CpuState *cpu, uint16_t value) {
  cpu_write_a_m(cpu, value);
  cpu->_flag_Z = value == 0;
  cpu->_flag_N = (value & 0x8000u) != 0;
  cpu->P = (uint8_t)((cpu->P & (uint8_t)~0x82u) |
                     (cpu->_flag_Z ? 0x02u : 0u) |
                     (cpu->_flag_N ? 0x80u : 0u));
}

static void Dkc1InterpreterInitialBackstep(CpuState *cpu, uint32_t pc24) {
  const uint16_t native = (pc24 & 0xffffu) == 0x9ec4u ? 0x0100u : 0x0108u;
  const uint16_t widened = Dkc1VideoInitialBackstep(cpu, native);
  if (widened != native)
    cpu_write_a_m(cpu, (uint16_t)(cpu_read_a16(cpu) -
                                  (uint16_t)(widened - native)));
}

static void Dkc1InterpreterInitialColumnCount(CpuState *cpu,
                                              uint32_t pc24) {
  const uint16_t native = (pc24 & 0xffffu) == 0x9ed6u ? 0x0020u : 0x0021u;
  const uint16_t count = Dkc1VideoInitialColumnCount(cpu, native);
  if (count == native)
    return;
  Dkc1SetInterpreterA16(cpu, count);
  /* Skip the cartridge's LDA #native. The following PHA/loop body remains
   * byte-for-byte cartridge execution; only its initial loop count changes. */
  interp_bridge_pre_opcode_redirect((pc24 + 3u) & 0xffffffu);
}

static void Dkc1Initialize(void) {
  /* The main engine can reach both initializers while executing through the
   * bank-$00 HiROM interpreter mirror. Generated-C adapters alone therefore
   * miss those entries. Mirror the same two constant substitutions at the
   * exact clean-ROM opcodes; compiled paths continue to use the generated
   * helper calls and never see these hooks. */
  interp_bridge_set_pre_opcode_hook(0x809ec4u,
                                    Dkc1InterpreterInitialBackstep);
  interp_bridge_set_pre_opcode_hook(0x809ed6u,
                                    Dkc1InterpreterInitialColumnCount);
  interp_bridge_set_pre_opcode_hook(0x80c56eu,
                                    Dkc1InterpreterInitialBackstep);
  interp_bridge_set_pre_opcode_hook(0x80c57du,
                                    Dkc1InterpreterInitialColumnCount);
}

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

static void Dkc1SaveWidescreenSnapshot(SaveLoadInfo *sli);
static bool Dkc1LoadWidescreenSnapshot(SaveLoadInfo *sli);
static bool s_ws_snapshot_restore_valid;

enum {
  /* NTSC master clocks per non-short host frame. */
  kDkc1NtscFrameMasterClocks = 1364 * 262,
};

static void Dkc1RunOneFrame(void) {
  bool first_frame = !s_cpu_initialized;
  /* This must precede the cartridge scanner/allocation pass. It records free
   * actor slots left by the completed prior frame so same-ID/source reuse is
   * recognized as a new placed-object generation. */
  Dkc1VideoObserveActorPool(g_ram);
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
  Dkc1SaveWidescreenSnapshot(sli);
}

static void Dkc1LoadExtra(SaveLoadInfo *sli, uint32_t version) {
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
  s_ws_snapshot_restore_valid =
      version >= 8 && Dkc1LoadWidescreenSnapshot(sli);
}

static void Dkc1ResetWidescreenShadow(void);

static void Dkc1OnStateLoaded(uint32_t version) {
  (void)version;
  g_cpu.ram = g_ram;
  g_apu_last_sync_master = g_cpu.master_cycles;
  g_snes->beamMasterLast = g_cpu.master_cycles;
  interp_bridge_set_master_deadline(0);
  const bool restored_host_widescreen = s_ws_snapshot_restore_valid;
  const char *cold_load = getenv("DKC1_WS_COLD_STATE_LOAD");
  const bool force_cold_widescreen =
      cold_load && *cold_load && *cold_load != '0';
  /* Diagnostic oracle: keep the loaded SNES machine state byte-exact while
   * deliberately discarding only the serialized host presentation history.
   * This makes retained-margin contamination directly comparable against a
   * cold reconstruction from the same v8 snapshot. It is default-off and
   * never changes ordinary quickload semantics. */
  if (!restored_host_widescreen || force_cold_widescreen) {
    Dkc1ResetWidescreenShadow();
    Dkc1VideoResetPlacedActorPhases();
  }
  s_ws_snapshot_restore_valid = false;
}

static const RtlGameInfo kDkc1GameInfo = {
  .title = "dkc1",
  .initialize = &Dkc1Initialize,
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
  PpuBeginDrawing(g_ppu, pixels, pitch,
                  kPpuRenderFlags_NewRenderer |
                      kPpuRenderFlags_WidescreenSpriteBudget);
  const char *provenance = getenv("DKC1_WS_PROVENANCE");
  WsShadowDebugSetProvenanceEnabled(
      provenance && *provenance && *provenance != '0');
}

void Dkc1DebugSetLayerMask(uint8_t mask) {
  g_snes_ppu_dbg_layer_mask = mask;
}

uint8_t Dkc1DebugLayerMask(void) {
  return g_snes_ppu_dbg_layer_mask;
}

void Dkc1DebugSetProvenanceOverlay(int enabled) {
  WsShadowDebugSetProvenanceEnabled(enabled != 0);
}

int Dkc1DebugProvenanceOverlay(void) {
  return WsShadowDebugProvenanceEnabled() ? 1 : 0;
}

/* ---- SuperZSNES v0.230 portable-state bridge --------------------------
 *
 * BinaryFormatter is kept out of the native runtime.  The companion exporter
 * turns the source state into exact-size raw memories plus small scalar JSON
 * objects.  This reader accepts only the exporter format, only a frame-boundary
 * state with inactive general DMA, and only a PC present in the recomp dispatch
 * table.  A rejected bundle never silently falls back to a partial import. */

static int Dkc1ExternalError(char *error, size_t size, const char *message) {
  if (error && size) snprintf(error, size, "%s", message ? message : "error");
  return 0;
}

static int Dkc1ReadExternalFile(const char *directory, const char *name,
                                uint8_t *data, size_t size) {
  char path[1024];
  if (!directory || !name ||
      snprintf(path, sizeof path, "%s\\%s", directory, name) < 0)
    return 0;
  FILE *stream = fopen(path, "rb");
  if (!stream) return 0;
  int ok = fread(data, 1, size, stream) == size && fgetc(stream) == EOF;
  fclose(stream);
  return ok;
}

static char *Dkc1ReadExternalText(const char *directory, const char *name,
                                  size_t maximum) {
  char path[1024];
  if (!directory || !name ||
      snprintf(path, sizeof path, "%s\\%s", directory, name) < 0)
    return NULL;
  FILE *stream = fopen(path, "rb");
  if (!stream) return NULL;
  if (fseek(stream, 0, SEEK_END) != 0) { fclose(stream); return NULL; }
  long length = ftell(stream);
  if (length < 0 || (size_t)length > maximum ||
      fseek(stream, 0, SEEK_SET) != 0) {
    fclose(stream);
    return NULL;
  }
  char *text = (char *)malloc((size_t)length + 1);
  if (!text) { fclose(stream); return NULL; }
  int ok = fread(text, 1, (size_t)length, stream) == (size_t)length &&
           fgetc(stream) == EOF;
  fclose(stream);
  if (!ok) { free(text); return NULL; }
  text[length] = '\0';
  return text;
}

static int Dkc1JsonI64(const char *json, const char *field, int64_t *value) {
  char pattern[128];
  if (!json || !field || !value ||
      snprintf(pattern, sizeof pattern, "\"%s\":", field) < 0)
    return 0;
  const char *at = strstr(json, pattern);
  if (!at || strstr(at + strlen(pattern), pattern)) return 0;
  at += strlen(pattern);
  char *end = NULL;
  long long parsed = strtoll(at, &end, 10);
  if (end == at || (*end != ',' && *end != '}')) return 0;
  *value = (int64_t)parsed;
  return 1;
}

static int Dkc1JsonBool(const char *json, const char *field, int *value) {
  char pattern[128];
  if (!json || !field || !value ||
      snprintf(pattern, sizeof pattern, "\"%s\":", field) < 0)
    return 0;
  const char *at = strstr(json, pattern);
  if (!at || strstr(at + strlen(pattern), pattern)) return 0;
  at += strlen(pattern);
  if (!strncmp(at, "true", 4)) { *value = 1; return 1; }
  if (!strncmp(at, "false", 5)) { *value = 0; return 1; }
  return 0;
}

static int Dkc1JsonByteArray(const char *json, const char *field,
                             uint8_t *values, size_t count) {
  char pattern[128];
  if (!json || !field || !values ||
      snprintf(pattern, sizeof pattern, "\"%s\":[", field) < 0)
    return 0;
  const char *at = strstr(json, pattern);
  if (!at || strstr(at + strlen(pattern), pattern)) return 0;
  at += strlen(pattern);
  for (size_t i = 0; i < count; i++) {
    char *end = NULL;
    unsigned long parsed = strtoul(at, &end, 10);
    if (end == at || parsed > 255) return 0;
    values[i] = (uint8_t)parsed;
    if (i + 1 < count) {
      if (*end != ',') return 0;
      at = end + 1;
    } else if (*end != ']') {
      return 0;
    }
  }
  return 1;
}

static uint16_t Dkc1ExternalLe16(const uint8_t *p) {
  return (uint16_t)(p[0] | ((uint16_t)p[1] << 8));
}

static int Dkc1ExternalGetI64(const char *json, const char *field,
                              int64_t *value, char *error, size_t size) {
  if (Dkc1JsonI64(json, field, value)) return 1;
  char message[256];
  snprintf(message, sizeof message, "missing or ambiguous JSON integer: %s",
           field);
  return Dkc1ExternalError(error, size, message);
}

static uint8_t Dkc1ExternalCpuFlags(const char *cpu_json, int *ok) {
  static const char *names[8] = {
    "flagC", "flagZ", "flagI", "flagD", "flagX", "flagM", "flagV", "flagN"
  };
  uint8_t p = 0;
  *ok = 1;
  for (int bit = 0; bit < 8; bit++) {
    int value = 0;
    if (!Dkc1JsonBool(cpu_json, names[bit], &value)) { *ok = 0; return 0; }
    if (value) p |= (uint8_t)(1u << bit);
  }
  return p;
}

static int Dkc1ApplyExternalPpu(const uint8_t *io, const uint8_t *cgram,
                                const uint8_t *oam, const uint8_t *vram,
                                const char *ppu_json, int frame,
                                char *error, size_t error_size) {
  Ppu *ppu = g_snes->ppu;
  int64_t value = 0;
  int64_t dma_active = 0;
  if (!Dkc1ExternalGetI64(ppu_json, "_dmaActive", &dma_active,
                          error, error_size)) return 0;
  if (dma_active != 0)
    return Dkc1ExternalError(error, error_size,
                             "active general DMA is not importable safely");

  ppu_reset(ppu);
  ppu->inidisp = io[0x100];
  ppu->obsel = io[0x101];
  ppu->oamaddl = io[0x102];
  ppu->oamaddh = io[0x103];
  ppu->bgmode = io[0x105];
  ppu->mosaic = io[0x106];
  memcpy(ppu->bgXsc, io + 0x107, 4);
  ppu->bgTileAdr = Dkc1ExternalLe16(io + 0x10b);
  ppu->m7sel = io[0x11a];
  ppu->setini = io[0x133];
  ppu->windowsel = (uint32_t)io[0x123] |
                   ((uint32_t)io[0x124] << 8) |
                   ((uint32_t)io[0x125] << 16);
  ppu->window1left = io[0x126];
  ppu->window1right = io[0x127];
  ppu->window2left = io[0x128];
  ppu->window2right = io[0x129];
  ppu->wbgobjlog = Dkc1ExternalLe16(io + 0x12a);
  ppu->screenEnabled[0] = io[0x12c];
  ppu->screenEnabled[1] = io[0x12d];
  ppu->screenWindowed[0] = io[0x12e];
  ppu->screenWindowed[1] = io[0x12f];
  ppu->cgwsel = io[0x130];
  ppu->cgadsub = io[0x131];

#define DKC1_IMPORT_PPU_I64(name, target)                                    \
  do {                                                                        \
    if (!Dkc1ExternalGetI64(ppu_json, name, &value, error, error_size))       \
      return 0;                                                               \
    target = value;                                                           \
  } while (0)
  DKC1_IMPORT_PPU_I64("_scroll1X", ppu->hScroll[0]);
  DKC1_IMPORT_PPU_I64("_scroll1Y", ppu->vScroll[0]);
  DKC1_IMPORT_PPU_I64("_scroll2X", ppu->hScroll[1]);
  DKC1_IMPORT_PPU_I64("_scroll2Y", ppu->vScroll[1]);
  DKC1_IMPORT_PPU_I64("_scroll3X", ppu->hScroll[2]);
  DKC1_IMPORT_PPU_I64("_scroll3Y", ppu->vScroll[2]);
  DKC1_IMPORT_PPU_I64("_scroll4X", ppu->hScroll[3]);
  DKC1_IMPORT_PPU_I64("_scroll4Y", ppu->vScroll[3]);
  DKC1_IMPORT_PPU_I64("_m7A", ppu->m7matrix[0]);
  DKC1_IMPORT_PPU_I64("_m7B", ppu->m7matrix[1]);
  DKC1_IMPORT_PPU_I64("_m7C", ppu->m7matrix[2]);
  DKC1_IMPORT_PPU_I64("_m7D", ppu->m7matrix[3]);
  DKC1_IMPORT_PPU_I64("_m7X", ppu->m7matrix[4]);
  DKC1_IMPORT_PPU_I64("_m7Y", ppu->m7matrix[5]);
  DKC1_IMPORT_PPU_I64("_fixedColor", ppu->fixedColor);
  DKC1_IMPORT_PPU_I64("_vramReadLatch", ppu->vramReadBuffer);
  DKC1_IMPORT_PPU_I64("_ophct", ppu->hCount);
  DKC1_IMPORT_PPU_I64("_opvct", ppu->vCount);
  DKC1_IMPORT_PPU_I64("_bgofs_latch", ppu->scrollPrev);
  DKC1_IMPORT_PPU_I64("_bghofs_latch", ppu->scrollPrev2);
#undef DKC1_IMPORT_PPU_I64

  ppu->vramPointer = Dkc1ExternalLe16(io + 0x116);
  ppu->vramIncrementOnHigh = (io[0x115] & 0x80) != 0;
  ppu->vramRemapMode = (io[0x115] >> 2) & 3;
  ppu->vramIncrement = (io[0x115] & 3) == 0 ? 1 :
                       (io[0x115] & 3) == 1 ? 32 : 128;
  ppu->cgramPointer = io[0x121];
  ppu->cgramSecondWrite = false;
  ppu->oamAdr = ppu->oamaddl;
  ppu->oamInHigh = (ppu->oamaddh & 1) != 0;
  ppu->oamSecondWrite = false;
  ppu->m7prev = ppu->scrollPrev;
  ppu->evenFrame = (frame & 1) == 0;
  ppu->frameOverscan = (ppu->setini & 4) != 0;
  ppu->frameInterlace = (ppu->setini & 1) != 0;
  memcpy(ppu->cgram, cgram, 512);
  memcpy(ppu->oam, oam, 512);
  memcpy(ppu->highOam, oam + 512, 32);
  memcpy(ppu->vram, vram, 65536);
  return 1;
}

static int Dkc1ApplyExternalApu(const uint8_t *spc_ram,
                                const char *cpu_json,
                                const char *spc_json,
                                const char *dsp_json,
                                char *error, size_t error_size) {
  Apu *apu = g_snes->apu;
  int64_t value = 0;
  memcpy(apu->ram, spc_ram, 65536);

#define DKC1_IMPORT_SPC_I64(name, target)                                    \
  do {                                                                        \
    if (!Dkc1ExternalGetI64(spc_json, name, &value, error, error_size))       \
      return 0;                                                               \
    target = value;                                                           \
  } while (0)
  DKC1_IMPORT_SPC_I64("regA", apu->spc->a);
  DKC1_IMPORT_SPC_I64("regX", apu->spc->x);
  DKC1_IMPORT_SPC_I64("regY", apu->spc->y);
  DKC1_IMPORT_SPC_I64("regS", apu->spc->sp);
  DKC1_IMPORT_SPC_I64("regPC", apu->spc->pc);
  DKC1_IMPORT_SPC_I64("dspRegister", apu->dspAdr);
  DKC1_IMPORT_SPC_I64("spc700TotalCycleCounter", value);
  uint64_t spc_cycles = (uint64_t)value;
  int64_t control = 0;
  DKC1_IMPORT_SPC_I64("controlPort", control);
  apu->romReadable = (control & 0x80) != 0;
  for (int i = 0; i < 3; i++) {
    char target_name[32], divider_name[32], counter_name[32];
    snprintf(target_name, sizeof target_name, "timer%dtarget", i);
    snprintf(divider_name, sizeof divider_name, "timer%dstage2", i);
    snprintf(counter_name, sizeof counter_name, "timer%dstage3", i);
    DKC1_IMPORT_SPC_I64(target_name, value);
    apu->timer[i].target = value == 256 ? 0 : (uint8_t)value;
    DKC1_IMPORT_SPC_I64(divider_name, value);
    apu->timer[i].divider = (uint8_t)value;
    DKC1_IMPORT_SPC_I64(counter_name, value);
    apu->timer[i].counter = (uint8_t)value & 0x0f;
    apu->timer[i].enabled = (control & (1 << i)) != 0;
    apu->timer[i].cycles = (uint8_t)((i == 2 ? 15 : 127) -
        (spc_cycles & (uint64_t)(i == 2 ? 15 : 127)));
  }
#undef DKC1_IMPORT_SPC_I64

  static const char *spc_flags[8] = {
    "flagC", "flagZ", "flagI", "flagH", "flagB", "flagP", "flagV", "flagN"
  };
  bool *spc_targets[8] = {
    &apu->spc->c, &apu->spc->z, &apu->spc->i, &apu->spc->h,
    &apu->spc->b, &apu->spc->p, &apu->spc->v, &apu->spc->n
  };
  for (int i = 0; i < 8; i++) {
    int flag = 0;
    if (!Dkc1JsonBool(spc_json, spc_flags[i], &flag))
      return Dkc1ExternalError(error, error_size, "missing SPC flag");
    *spc_targets[i] = flag != 0;
  }
  apu->spc->stopped = false;
  apu->cpuCyclesLeft = 0;
  apu->cycles = (uint32_t)spc_cycles;
  apu->portClock = spc_cycles;
  apu_clearPortQueue(apu);

  static const char *in_names[4] = {
    "spcPort2140", "spcPort2141", "spcPort2142", "spcPort2143"
  };
  static const char *out_names[4] = {
    "spcPortf4", "spcPortf5", "spcPortf6", "spcPortf7"
  };
  for (int i = 0; i < 4; i++) {
    if (!Dkc1ExternalGetI64(cpu_json, in_names[i], &value, error, error_size))
      return 0;
    apu->inPorts[i] = (uint8_t)value;
    if (!Dkc1ExternalGetI64(cpu_json, out_names[i], &value, error, error_size))
      return 0;
    apu->outPorts[i] = (uint8_t)value;
  }

  uint8_t dsp_values[128];
  if (!Dkc1JsonByteArray(dsp_json, "dspValues", dsp_values,
                         sizeof dsp_values))
    return Dkc1ExternalError(error, error_size,
                             "invalid DSP register array");
  dsp_reset(apu->dsp);
  for (int i = 0; i < 128; i++) dsp_write(apu->dsp, (uint8_t)i, dsp_values[i]);
  return 1;
}

int Dkc1ImportSuperZsnesState(const char *directory,
                              char *error, size_t error_size) {
  enum { kIoSize = 16384, kWramSize = 131072 };
  uint8_t *wram = (uint8_t *)malloc(kWramSize);
  uint8_t *spc = (uint8_t *)malloc(65536);
  uint8_t *vram = (uint8_t *)malloc(65536);
  uint8_t *io = (uint8_t *)malloc(kIoSize);
  uint8_t cgram[512], oam[544];
  char *manifest = NULL, *master = NULL, *cpu = NULL, *spc_json = NULL;
  char *ppu = NULL, *dsp = NULL;
  int ok = 0;
  if (!wram || !spc || !vram || !io) {
    Dkc1ExternalError(error, error_size, "out of memory importing state");
    goto done;
  }
  manifest = Dkc1ReadExternalText(directory, "manifest.json", 65536);
  master = Dkc1ReadExternalText(directory, "master.json", 65536);
  cpu = Dkc1ReadExternalText(directory, "cpu65816.json", 65536);
  spc_json = Dkc1ReadExternalText(directory, "spc700.json", 65536);
  ppu = Dkc1ReadExternalText(directory, "ppu.json", 65536);
  dsp = Dkc1ReadExternalText(directory, "dsp.json", 1024 * 1024);
  if (!manifest || !master || !cpu || !spc_json || !ppu || !dsp ||
      !strstr(manifest, "\"format\":\"superzsnes-v0230-portable-state\"") ||
      !strstr(manifest, "\"version\":1") ||
      !Dkc1ReadExternalFile(directory, "wram.bin", wram, kWramSize) ||
      !Dkc1ReadExternalFile(directory, "spc-ram.bin", spc, 65536) ||
      !Dkc1ReadExternalFile(directory, "vram.bin", vram, 65536) ||
      !Dkc1ReadExternalFile(directory, "io-registers.bin", io, kIoSize) ||
      !Dkc1ReadExternalFile(directory, "cgram.bin", cgram, sizeof cgram) ||
      !Dkc1ReadExternalFile(directory, "oam.bin", oam, sizeof oam)) {
    Dkc1ExternalError(error, error_size,
                      "bundle is incomplete or has the wrong format");
    goto done;
  }

  int64_t frame = 0, reg_a = 0, reg_x = 0, reg_y = 0, reg_s = 0;
  int64_t reg_d = 0, reg_db = 0, reg_pb = 0, reg_pc = 0, total_cycles = 0;
  if (!Dkc1ExternalGetI64(master, "_curFrameNo", &frame, error, error_size) ||
      !Dkc1ExternalGetI64(cpu, "regA", &reg_a, error, error_size) ||
      !Dkc1ExternalGetI64(cpu, "regX", &reg_x, error, error_size) ||
      !Dkc1ExternalGetI64(cpu, "regY", &reg_y, error, error_size) ||
      !Dkc1ExternalGetI64(cpu, "regS", &reg_s, error, error_size) ||
      !Dkc1ExternalGetI64(cpu, "regD", &reg_d, error, error_size) ||
      !Dkc1ExternalGetI64(cpu, "regDB", &reg_db, error, error_size) ||
      !Dkc1ExternalGetI64(cpu, "regPB", &reg_pb, error, error_size) ||
      !Dkc1ExternalGetI64(cpu, "regPC", &reg_pc, error, error_size) ||
      !Dkc1ExternalGetI64(cpu, "totalCycles", &total_cycles,
                          error, error_size)) goto done;
  if (frame < 0 || reg_pc < 0 || reg_pc > 0xffff || total_cycles < 0) {
    Dkc1ExternalError(error, error_size, "state contains invalid execution values");
    goto done;
  }
  int flag_ok = 0;
  uint8_t p = Dkc1ExternalCpuFlags(cpu, &flag_ok);
  int e_flag = 0;
  if (!flag_ok || !Dkc1JsonBool(cpu, "flagE", &e_flag)) {
    Dkc1ExternalError(error, error_size, "state contains invalid CPU flags");
    goto done;
  }
  uint32_t pc24 = (((uint32_t)reg_pb >> 16) & 0x7fu) << 16 |
                  (uint16_t)reg_pc;

  memcpy(g_ram, wram, kWramSize);
  memset(&g_cpu, 0, sizeof g_cpu);
  g_cpu.A = (uint16_t)reg_a;
  g_cpu.X = (uint16_t)reg_x;
  g_cpu.Y = (uint16_t)reg_y;
  g_cpu.S = (uint16_t)reg_s;
  g_cpu.D = (uint16_t)reg_d;
  g_cpu.DB = (uint8_t)(reg_db >> 16);
  g_cpu.PB = (uint8_t)(reg_pb >> 16);
  g_cpu.P = p;
  g_cpu.m_flag = (p >> 5) & 1;
  g_cpu.x_flag = (p >> 4) & 1;
  g_cpu.emulation = e_flag != 0;
  cpu_p_to_mirrors(&g_cpu);
  g_cpu.ram = g_ram;
  g_cpu.master_cycles = (uint64_t)total_cycles;
  g_cpu.cycles = g_cpu.master_cycles / 6;
  g_cpu.coprocessor_master_cycles = g_cpu.master_cycles;
  if (!cpu_dispatch_has_entry(&g_cpu, pc24)) {
    Dkc1ExternalError(error, error_size,
                      "saved CPU PC is absent from the recomp dispatch table");
    goto done;
  }

  Cpu *mirror = g_snes->cpu;
  mirror->a = g_cpu.A; mirror->x = g_cpu.X; mirror->y = g_cpu.Y;
  mirror->sp = g_cpu.S; mirror->pc = (uint16_t)reg_pc;
  mirror->dp = g_cpu.D; mirror->k = g_cpu.PB; mirror->db = g_cpu.DB;
  mirror->c = (p & 1) != 0; mirror->z = (p & 2) != 0;
  mirror->i = (p & 4) != 0; mirror->d = (p & 8) != 0;
  mirror->xf = (p & 0x10) != 0; mirror->mf = (p & 0x20) != 0;
  mirror->v = (p & 0x40) != 0; mirror->n = (p & 0x80) != 0;
  mirror->e = e_flag != 0;

  if (!Dkc1ApplyExternalApu(spc, cpu, spc_json, dsp, error, error_size) ||
      !Dkc1ApplyExternalPpu(io, cgram, oam, vram, ppu, (int)frame,
                            error, error_size)) goto done;

  /* SuperZSNES stores the $2000-$5FFF I/O window at index
   * (cpu_address - $2000).  PPU registers therefore begin at $0100,
   * CPU control registers at $2200, and DMA registers at $2300.  Do not
   * confuse this with the SNES register's low 12 bits: doing so silently
   * reads the unused $2200 mirror for $4200 and leaves a WAI snapshot
   * permanently parked because NMI appears disabled. */
  enum {
    kIoNmitimen = 0x2200,
    kIoHtimeLo = 0x2207,
    kIoHtimeHi = 0x2208,
    kIoVtimeLo = 0x2209,
    kIoVtimeHi = 0x220a,
    kIoHdmaen = 0x220c,
    kIoMemsel = 0x220d,
    kIoDmaBase = 0x2300,
  };

  dma_reset(g_snes->dma);
  for (int channel = 0; channel < 8; channel++) {
    for (int reg = 0; reg < 16; reg++)
      dma_write(g_snes->dma, (uint16_t)(channel * 16 + reg),
                io[kIoDmaBase + channel * 16 + reg]);
  }
  int64_t hdma_transfer = 0, hdma_terminated = 0;
  if (!Dkc1ExternalGetI64(ppu, "_doTransferHDMA", &hdma_transfer,
                          error, error_size) ||
      !Dkc1ExternalGetI64(ppu, "_terminatedHDMA", &hdma_terminated,
                          error, error_size)) goto done;
  for (int channel = 0; channel < 8; channel++) {
    uint8_t bit = (uint8_t)(1u << channel);
    g_snes->dma->channel[channel].hdmaActive =
        (io[kIoHdmaen] & bit) != 0;
    g_snes->dma->channel[channel].doTransfer = (hdma_transfer & bit) != 0;
    g_snes->dma->channel[channel].terminated = (hdma_terminated & bit) != 0;
  }
  g_snesrecomp_last_hdmaen = io[kIoHdmaen];
  g_memsel = io[kIoMemsel] & 1;

  g_snes->hPos = 0;
  g_snes->vPos = 0;
  g_snes->nmiEnabled = (io[kIoNmitimen] & 0x80) != 0;
  g_snes->vIrqEnabled = (io[kIoNmitimen] & 0x20) != 0;
  g_snes->hIrqEnabled = (io[kIoNmitimen] & 0x10) != 0;
  g_snes->autoJoyRead = (io[kIoNmitimen] & 1) != 0;
  g_snes->hTimer =
      (uint16_t)(io[kIoHtimeLo] | ((io[kIoHtimeHi] & 1) << 8));
  g_snes->vTimer =
      (uint16_t)(io[kIoVtimeLo] | ((io[kIoVtimeHi] & 1) << 8));
  int64_t wram_address = 0;
  if (!Dkc1ExternalGetI64(ppu, "_wramAddress", &wram_address,
                          error, error_size)) goto done;
  g_snes->ramAdr = (uint32_t)wram_address & 0x1ffffu;
  g_snes->inVblank = true;
  g_snes->inNmi = false;
  g_snes->inIrq = false;

  s_resume_pc = pc24;
  s_cpu_initialized = true;
  s_last_lle_result = 1;
  s_next_frame_master = g_cpu.master_cycles + kDkc1NtscFrameMasterClocks;
  snes_frame_counter = (int)frame;
  g_apu_last_sync_master = g_cpu.master_cycles;
  g_snes->beamMasterLast = g_cpu.master_cycles;
  interp_bridge_set_master_deadline(0);
  Dkc1ResetWidescreenShadow();
  Dkc1VideoResetPlacedActorPhases();
  ok = 1;

done:
  free(wram); free(spc); free(vram); free(io);
  free(manifest); free(master); free(cpu); free(spc_json); free(ppu); free(dsp);
  return ok;
}

/* ---- presentation-camera widescreen ------------------------------------
 * The stock logical camera keeps driving collision, exits, movement clamps,
 * and tile streaming. Generated visibility adapters may activate objects
 * that are genuinely visible in the host margins. The host renders those
 * margin columns in world space (WsShadow) and prefills terrain by decoding
 * the level map straight from ROM. */

static uint16_t Dkc1ReadWram16(uint16_t address) {
  return (uint16_t)g_ram[address] |
         ((uint16_t)g_ram[(uint16_t)(address + 1u)] << 8);
}

static bool s_ws_shadow_active;
static bool s_ws_origin_valid[2];
static uint32_t s_ws_world_x[2];
static uint32_t s_ws_world_y[2];
/* WsShadow is a bounded cache, while DKC uses full 16-bit authored world
 * coordinates (bonus rooms can be near X=$9AF9 and vertical rooms above
 * Y=$5600). Indexing that cache with absolute tile coordinates silently
 * discarded every high-world margin cell. Keep decoder coordinates absolute
 * and project only cache keys into one stable scene-local window.
 *
 * X stays 512-pixel aligned to preserve the two 32-column tilemap-screen
 * parity. Y stays 256-pixel aligned to preserve the 32-row map wrap. */
static bool s_ws_shadow_origin_valid[2];
static uint32_t s_ws_shadow_origin_x[2];
static uint32_t s_ws_shadow_origin_y[2];
static Dkc1LevelLayout s_ws_layout;
static int s_ws_layout_grace;  /* bounded transient calibration misses */
static bool s_ws_trace_reset_pending;

typedef struct Dkc1WsIdentity {
  uint16_t mode;
  uint16_t level;
  uint16_t entrance;
  uint64_t source_signature;
  uint8_t bgmode;
  uint8_t bgsc[4];
  uint8_t main_mask;
  uint8_t sub_mask;
  uint8_t wide_layer_mask;
  int8_t terrain_layer;
} Dkc1WsIdentity;

enum Dkc1WsIdentityChange {
  kDkc1WsIdentityMode = 1u << 0,
  kDkc1WsIdentityLevel = 1u << 1,
  kDkc1WsIdentityEntrance = 1u << 2,
  kDkc1WsIdentitySource = 1u << 3,
  kDkc1WsIdentityBgMode = 1u << 4,
  kDkc1WsIdentityBgSc = 1u << 5,
  kDkc1WsIdentityScreenMasks = 1u << 6,
  kDkc1WsIdentityWideMask = 1u << 7,
  kDkc1WsIdentityTerrainLayer = 1u << 8,
};

static bool s_ws_identity_valid;
static Dkc1WsIdentity s_ws_identity;

enum {
  kDkc1WsSaveMagic = 0x38535744u, /* "DWS8" */
  kDkc1WsSaveVersion = 1,
  kDkc1WsSaveMaximumBytes = 64 * 1024 * 1024,
};

typedef struct Dkc1WidescreenSnapshot {
  uint32_t magic;
  uint32_t version;
  uint32_t shadowSize;
  uint32_t videoSize;
  uint8_t shadowActive;
  uint8_t originValid[2];
  uint8_t shadowOriginValid[2];
  uint8_t identityValid;
  uint8_t traceResetPending;
  uint8_t terrainReady;
  uint8_t reserved;
  int32_t presentationBias;
  uint32_t worldX[2];
  uint32_t worldY[2];
  uint32_t shadowOriginX[2];
  uint32_t shadowOriginY[2];
  int32_t layout;
  int32_t layoutGrace;
  Dkc1WsIdentity identity;
} Dkc1WidescreenSnapshot;

static void Dkc1SaveWidescreenSnapshot(SaveLoadInfo *sli) {
  Dkc1WidescreenSnapshot snapshot;
  memset(&snapshot, 0, sizeof snapshot);
  snapshot.magic = kDkc1WsSaveMagic;
  snapshot.version = kDkc1WsSaveVersion;
  snapshot.shadowActive = s_ws_shadow_active ? 1u : 0u;
  snapshot.originValid[0] = s_ws_origin_valid[0] ? 1u : 0u;
  snapshot.originValid[1] = s_ws_origin_valid[1] ? 1u : 0u;
  snapshot.shadowOriginValid[0] =
      s_ws_shadow_origin_valid[0] ? 1u : 0u;
  snapshot.shadowOriginValid[1] =
      s_ws_shadow_origin_valid[1] ? 1u : 0u;
  snapshot.identityValid = s_ws_identity_valid ? 1u : 0u;
  snapshot.traceResetPending = s_ws_trace_reset_pending ? 1u : 0u;
  snapshot.terrainReady = Dkc1VideoTerrainReady() ? 1u : 0u;
  snapshot.presentationBias = Dkc1VideoPresentationBias();
  memcpy(snapshot.worldX, s_ws_world_x, sizeof snapshot.worldX);
  memcpy(snapshot.worldY, s_ws_world_y, sizeof snapshot.worldY);
  memcpy(snapshot.shadowOriginX, s_ws_shadow_origin_x,
         sizeof snapshot.shadowOriginX);
  memcpy(snapshot.shadowOriginY, s_ws_shadow_origin_y,
         sizeof snapshot.shadowOriginY);
  snapshot.layout = (int32_t)s_ws_layout;
  snapshot.layoutGrace = s_ws_layout_grace;
  snapshot.identity = s_ws_identity;

  const size_t shadow_size = WsShadowSnapshotSize();
  uint8_t *shadow = NULL;
  uint8_t *video = NULL;
  if (shadow_size && shadow_size <= kDkc1WsSaveMaximumBytes &&
      shadow_size <= UINT32_MAX) {
    shadow = (uint8_t *)malloc(shadow_size);
    if (shadow && WsShadowSnapshotSave(shadow, shadow_size))
      snapshot.shadowSize = (uint32_t)shadow_size;
    else {
      free(shadow);
      shadow = NULL;
    }
  }
  const size_t video_size = Dkc1VideoSnapshotSize();
  if (video_size && video_size <= UINT32_MAX) {
    video = (uint8_t *)malloc(video_size);
    if (video && Dkc1VideoSnapshotSave(video, video_size))
      snapshot.videoSize = (uint32_t)video_size;
    else {
      free(video);
      video = NULL;
    }
  }
  sli->func(sli, &snapshot, sizeof snapshot);
  if (snapshot.shadowSize)
    sli->func(sli, shadow, snapshot.shadowSize);
  if (snapshot.videoSize)
    sli->func(sli, video, snapshot.videoSize);
  free(shadow);
  free(video);
}

static bool Dkc1LoadWidescreenSnapshot(SaveLoadInfo *sli) {
  Dkc1WidescreenSnapshot snapshot;
  memset(&snapshot, 0, sizeof snapshot);
  sli->func(sli, &snapshot, sizeof snapshot);
  if (snapshot.magic != kDkc1WsSaveMagic ||
      snapshot.version != kDkc1WsSaveVersion || !snapshot.shadowSize ||
      snapshot.shadowSize > kDkc1WsSaveMaximumBytes || !snapshot.videoSize ||
      snapshot.videoSize > 1024u * 1024u ||
      snapshot.layout < kDkc1LayoutUnknown ||
      snapshot.layout > kDkc1LayoutVertical ||
      snapshot.layoutGrace < 0 || snapshot.layoutGrace > 2 ||
      snapshot.identity.terrain_layer < -1 ||
      snapshot.identity.terrain_layer > 3)
    return false;
  uint8_t *shadow = (uint8_t *)malloc(snapshot.shadowSize);
  uint8_t *video = (uint8_t *)malloc(snapshot.videoSize);
  if (!shadow || !video) {
    free(shadow);
    free(video);
    return false;
  }
  sli->func(sli, shadow, snapshot.shadowSize);
  sli->func(sli, video, snapshot.videoSize);
  const bool restored = WsShadowSnapshotLoad(shadow, snapshot.shadowSize) &&
                        Dkc1VideoSnapshotLoad(video, snapshot.videoSize);
  free(shadow);
  free(video);
  if (!restored)
    return false;

  s_ws_shadow_active = snapshot.shadowActive != 0;
  s_ws_origin_valid[0] = snapshot.originValid[0] != 0;
  s_ws_origin_valid[1] = snapshot.originValid[1] != 0;
  s_ws_shadow_origin_valid[0] = snapshot.shadowOriginValid[0] != 0;
  s_ws_shadow_origin_valid[1] = snapshot.shadowOriginValid[1] != 0;
  s_ws_identity_valid = snapshot.identityValid != 0;
  s_ws_trace_reset_pending = snapshot.traceResetPending != 0;
  memcpy(s_ws_world_x, snapshot.worldX, sizeof s_ws_world_x);
  memcpy(s_ws_world_y, snapshot.worldY, sizeof s_ws_world_y);
  memcpy(s_ws_shadow_origin_x, snapshot.shadowOriginX,
         sizeof s_ws_shadow_origin_x);
  memcpy(s_ws_shadow_origin_y, snapshot.shadowOriginY,
         sizeof s_ws_shadow_origin_y);
  s_ws_layout = (Dkc1LevelLayout)snapshot.layout;
  s_ws_layout_grace = snapshot.layoutGrace;
  s_ws_identity = snapshot.identity;
  Dkc1VideoSetPresentationBias(snapshot.presentationBias);
  Dkc1VideoSetTerrainReady(snapshot.terrainReady != 0);
  return true;
}

static uint32_t Dkc1BlendDebugColor(uint32_t pixel, uint32_t color) {
  /* Keep the rendered image legible beneath a 50% false-color wash. */
  uint32_t rb = ((pixel & 0x00ff00ffu) + (color & 0x00ff00ffu)) >> 1;
  uint32_t g = ((pixel & 0x0000ff00u) + (color & 0x0000ff00u)) >> 1;
  return (pixel & 0xff000000u) | (rb & 0x00ff00ffu) |
         (g & 0x0000ff00u);
}

static void Dkc1ApplyProvenanceOverlay(uint8_t wide_layer_mask) {
  if (!WsShadowDebugProvenanceEnabled() || !g_ppu->renderBuffer ||
      !Dkc1VideoIsWidescreen() || !wide_layer_mask)
    return;

  int layer = Dkc1VideoTerrainLayer(
      wide_layer_mask, g_ppu->bgXsc, Dkc1ReadWram16(0x1b13));
  const uint8_t selected_bg = (uint8_t)(g_snes_ppu_dbg_layer_mask & 0x0fu);
  if (selected_bg && !(selected_bg & (uint8_t)(selected_bg - 1u))) {
    for (int candidate = 0; candidate < 4; candidate++)
      if (selected_bg & (1u << candidate)) layer = candidate;
  }
  if (layer < 0 || layer >= 4)
    return;

  const int extra = Dkc1VideoExtra();
  const int width = Dkc1VideoWidth();
  const bool repeated = (g_ppu->wsLayerRepeat & (1u << layer)) != 0;
  static const uint32_t colors[] = {
      0x00000000u, /* none */
      0x0000d040u, /* captured: green */
      0x0000d8ffu, /* ROM prefill: cyan */
      0x00e000d0u, /* periodic fold: magenta */
      0x00707070u, /* verified blank: gray */
      0x00ff8020u, /* valid 64-column raw continuation: blue */
      0x00ff2020u, /* raw circular-VRAM fallback: red */
      0x00ffd020u, /* native edge repeat: yellow */
  };
  for (int y = 0; y < kDkc1VideoHeight; y++) {
    uint32_t *row =
        (uint32_t *)(g_ppu->renderBuffer + (size_t)y * g_ppu->renderPitch);
    for (int out_x = 0; out_x < width; out_x++) {
      const int screen_x = out_x - extra;
      if (screen_x >= 0 && screen_x < kDkc1VideoNativeWidth)
        continue;
      uint8_t source = repeated ? 6u :
          WsShadowDebugProvenanceAt(layer, screen_x, y);
      if (source < sizeof colors / sizeof colors[0] && source != 0)
        row[out_x] = Dkc1BlendDebugColor(row[out_x], colors[source]);
    }
  }
}

static uint64_t Dkc1LevelSourceSignature(void) {
  const uint64_t bank = g_ram[0x00d5];
  const uint64_t metatile_bank = g_ram[0x00d6];
  const uint64_t map = Dkc1ReadWram16(0x00d3);
  const uint64_t metatiles = Dkc1ReadWram16(0x1b11);
  const uint64_t vram = Dkc1ReadWram16(0x1b13);
  return bank | (map << 8) | (metatiles << 24) | (vram << 40) |
         (metatile_bank << 56);
}

static Dkc1WsIdentity Dkc1BuildWidescreenIdentity(uint8_t wide_layer_mask,
                                                  int terrain_layer) {
  Dkc1WsIdentity identity;
  memset(&identity, 0, sizeof identity);
  identity.mode = Dkc1ReadWram16(0x0032);
  identity.level = Dkc1ReadWram16(0x0030);
  identity.entrance = Dkc1ReadWram16(0x003e);
  identity.source_signature = Dkc1LevelSourceSignature();
  identity.bgmode = g_ppu->bgmode;
  memcpy(identity.bgsc, g_ppu->bgXsc, sizeof identity.bgsc);
  identity.main_mask = g_ppu->screenEnabled[0];
  identity.sub_mask = g_ppu->screenEnabled[1];
  identity.wide_layer_mask = wide_layer_mask;
  identity.terrain_layer = (int8_t)terrain_layer;
  return identity;
}

static uint32_t Dkc1WidescreenIdentityDiff(const Dkc1WsIdentity *old,
                                           const Dkc1WsIdentity *current) {
  if (!s_ws_identity_valid)
    return UINT32_MAX;
  uint32_t changed = 0;
  if (old->mode != current->mode) changed |= kDkc1WsIdentityMode;
  if (old->level != current->level) changed |= kDkc1WsIdentityLevel;
  if (old->entrance != current->entrance)
    changed |= kDkc1WsIdentityEntrance;
  if (old->source_signature != current->source_signature)
    changed |= kDkc1WsIdentitySource;
  if (old->bgmode != current->bgmode) changed |= kDkc1WsIdentityBgMode;
  if (memcmp(old->bgsc, current->bgsc, sizeof old->bgsc) != 0)
    changed |= kDkc1WsIdentityBgSc;
  if (old->main_mask != current->main_mask ||
      old->sub_mask != current->sub_mask)
    changed |= kDkc1WsIdentityScreenMasks;
  if (old->wide_layer_mask != current->wide_layer_mask)
    changed |= kDkc1WsIdentityWideMask;
  if (old->terrain_layer != current->terrain_layer)
    changed |= kDkc1WsIdentityTerrainLayer;
  return changed;
}

static uint64_t Dkc1WidescreenIdentityHash(const Dkc1WsIdentity *identity) {
  uint64_t hash = UINT64_C(1469598103934665603);
#define DKC1_IDENTITY_MIX(value)                                          \
  do {                                                                   \
    hash ^= (uint64_t)(value);                                           \
    hash *= UINT64_C(1099511628211);                                     \
  } while (0)
  DKC1_IDENTITY_MIX(identity->mode);
  DKC1_IDENTITY_MIX(identity->level);
  DKC1_IDENTITY_MIX(identity->entrance);
  DKC1_IDENTITY_MIX(identity->source_signature);
  DKC1_IDENTITY_MIX(identity->bgmode);
  for (int i = 0; i < 4; i++) DKC1_IDENTITY_MIX(identity->bgsc[i]);
  DKC1_IDENTITY_MIX(identity->main_mask);
  DKC1_IDENTITY_MIX(identity->sub_mask);
  DKC1_IDENTITY_MIX(identity->wide_layer_mask);
  DKC1_IDENTITY_MIX((uint8_t)identity->terrain_layer);
#undef DKC1_IDENTITY_MIX
  return hash;
}

static void Dkc1ClearWidescreenShadow(bool clear_identity) {
  const bool had_state = s_ws_shadow_active ||
                         s_ws_layout != kDkc1LayoutUnknown;
  if (s_ws_shadow_active)
    WsShadowReset();
  s_ws_shadow_active = false;
  memset(s_ws_origin_valid, 0, sizeof s_ws_origin_valid);
  memset(s_ws_shadow_origin_valid, 0, sizeof s_ws_shadow_origin_valid);
  memset(s_ws_shadow_origin_x, 0, sizeof s_ws_shadow_origin_x);
  memset(s_ws_shadow_origin_y, 0, sizeof s_ws_shadow_origin_y);
  s_ws_layout = kDkc1LayoutUnknown;
  s_ws_layout_grace = 0;
  if (clear_identity) {
    s_ws_identity_valid = false;
    memset(&s_ws_identity, 0, sizeof s_ws_identity);
  }
  Dkc1VideoSetPresentationBias(0);
  Dkc1VideoSetTerrainReady(false);
  if (had_state)
    s_ws_trace_reset_pending = true;
}

static void Dkc1ResetWidescreenShadow(void) {
  Dkc1ClearWidescreenShadow(true);
}

/* A rejected frame must discard retained pixels/layout confidence but retain
 * the observed hard identity. This prevents repeated provisional cold starts
 * while still requiring a fresh calibration before any later commit. */
static void Dkc1RejectWidescreenShadow(void) {
  Dkc1ClearWidescreenShadow(false);
}

/* Clamp only the host presentation camera near level ends. A symmetric wide
 * viewport centered on camera X=lower asks for negative world columns, which
 * is why the left side stayed black until the player first scrolled. Moving
 * the presentation center inward exposes real level art immediately while
 * leaving collision, exits, camera bounds, and simulation untouched. */
static int Dkc1WidescreenPresentationBias(void) {
  const uint32_t camera = Dkc1ReadWram16(0x088b);
  const uint32_t lower = Dkc1ReadWram16(0x1b23);
  const uint32_t upper = Dkc1ReadWram16(0x1b25);
  const uint32_t extra = (uint32_t)Dkc1VideoExtra();
  if (upper < lower || upper - lower < extra * 2u)
    return 0;
  uint32_t target = camera;
  if (target < lower + extra)
    target = lower + extra;
  if (target > upper - extra)
    target = upper - extra;
  return (int32_t)target - (int32_t)camera;
}

/* Word address of a tile in a 64x32 SNES tilemap (world-keyed rolling map:
 * the column streamer at $81:883F keys VRAM columns by cameraX>>3 mod 64). */
static uint16_t Dkc1RollingMapWord(uint16_t map_base, uint32_t tile_x,
                                   uint32_t tile_y) {
  uint16_t word = (uint16_t)(map_base + ((tile_y & 0x1fu) << 5) +
                             (tile_x & 0x1fu));
  if (tile_x & 0x20u)
    word = (uint16_t)(word + 0x400u);
  return word;
}

/* Score a candidate layout by decoding the native viewport from ROM and
 * comparing with the live rolling tilemap. Dynamic tiles (animation, item
 * pickups) legitimately mismatch, so the gate is a ratio, not equality. */
static int Dkc1CalibrateLayout(Dkc1LevelLayout layout, uint16_t ppu_map_base,
                               uint8_t map_bank, uint8_t metatile_bank,
                               uint16_t map_base,
                               uint16_t metatile_base, uint32_t world_x,
                               uint32_t world_y, int *decodable_out) {
  int matches = 0, decodable = 0;
  for (int row = 0; row < 28; row += 2) {
    for (int col = 0; col < 32; col += 2) {
      const uint32_t wtx = (world_x >> 3) + (uint32_t)col;
      const uint32_t wty = (world_y >> 3) + (uint32_t)row;
      uint16_t decoded;
      if (!Dkc1VideoDecodeLevelTile(layout, map_bank, metatile_bank,
                                    map_base, metatile_base, wtx, wty,
                                    &decoded))
        continue;
      decodable++;
      const uint16_t live =
          g_ppu->vram[Dkc1RollingMapWord(ppu_map_base, wtx, wty) & 0x7fffu];
      if (live == decoded)
        matches++;
    }
  }
  if (decodable_out)
    *decodable_out = decodable;
  return matches;
}

static bool Dkc1PrepareWidescreenShadow(uint8_t layer_mask,
                                        int presentation_bias,
                                        bool cartridge_stream_ready,
                                        bool *stream_bootstrap_rejected,
                                        Dkc1WsTraceFrame *trace) {
  if (stream_bootstrap_rejected)
    *stream_bootstrap_rejected = false;
  const uint32_t camera_x = Dkc1ReadWram16(0x088b);
  const uint32_t camera_y = Dkc1ReadWram16(0x0895);
  const uint16_t stream_vram = Dkc1ReadWram16(0x1b13);
  const uint8_t map_bank = g_ram[0x00d5];
  const uint8_t metatile_bank = g_ram[0x00d6];
  const uint16_t map_base = Dkc1ReadWram16(0x00d3);
  const uint16_t metatile_base = Dkc1ReadWram16(0x1b11);
  const int terrain_layer =
      Dkc1VideoTerrainLayer(layer_mask, g_ppu->bgXsc, stream_vram);
  const Dkc1WsIdentity identity =
      Dkc1BuildWidescreenIdentity(layer_mask, terrain_layer);
  const bool identity_was_valid = s_ws_identity_valid;
  const uint32_t identity_change =
      Dkc1WidescreenIdentityDiff(&s_ws_identity, &identity);
  if (trace) {
    trace->identity_hash = Dkc1WidescreenIdentityHash(&identity);
    trace->identity_change_mask = identity_change;
  }
  if (identity_change != 0) {
    if (trace) {
      trace->identity_reset = true;
      trace->source_reset = !identity_was_valid ||
          (identity_change & kDkc1WsIdentitySource) != 0;
    }
    /* Hard scene changes are authoritative. Discard retained pixels before
     * looking at soft tile agreement, then remember this identity so repeated
     * unsupported frames do not manufacture cold-start history. */
    Dkc1ClearWidescreenShadow(false);
    s_ws_identity = identity;
    s_ws_identity_valid = true;
  }

  /* Source pointers and a Mode-1 shape become visible several frames before
   * DKC publishes usable logical camera bounds at level entry. During that
   * interval the same bytes can strongly resemble the wrong map layout. A
   * viewport cannot expose both margins until the camera range itself spans
   * the requested extension, so fail closed without touching shadow history. */
  const uint32_t lower_bound = Dkc1ReadWram16(0x1b23);
  const uint32_t upper_bound = Dkc1ReadWram16(0x1b25);
  const uint32_t minimum_span = (uint32_t)Dkc1VideoExtra() * 2u;
  const bool bounds_ready = upper_bound >= lower_bound &&
                            upper_bound - lower_bound >= minimum_span;
  if (trace) trace->bounds_ready = bounds_ready;
  if (!bounds_ready)
    return false;

  const int keep_tiles = Dkc1VideoExtra() / 8 + 2;
  bool candidate_valid[2] = {false, false};
  uint32_t candidate_world_x[2] = {0, 0};
  uint32_t candidate_world_y[2] = {0, 0};
  for (int layer = 0; layer < 2; layer++) {
    const uint8_t bit = (uint8_t)(1u << layer);
    if (!(layer_mask & bit))
      continue;
    if (layer == terrain_layer) {
      candidate_world_x[layer] = Dkc1VideoUnwrapPpuScroll(
          (uint16_t)(g_ppu->hScroll[layer] + presentation_bias), camera_x);
      candidate_world_y[layer] = Dkc1VideoUnwrapPpuScroll(
          (uint16_t)g_ppu->vScroll[layer], camera_y);
    } else {
      const uint32_t anchor_x = s_ws_origin_valid[layer]
                                    ? s_ws_world_x[layer] : camera_x;
      const uint32_t anchor_y = s_ws_origin_valid[layer]
                                    ? s_ws_world_y[layer] : camera_y;
      candidate_world_x[layer] = Dkc1VideoUnwrapPpuScroll(
          (uint16_t)(g_ppu->hScroll[layer] + presentation_bias), anchor_x);
      candidate_world_y[layer] = Dkc1VideoUnwrapPpuScroll(
          (uint16_t)g_ppu->vScroll[layer], anchor_y);
    }
    candidate_valid[layer] = true;

    if (trace) {
      trace->world_valid[layer] = true;
      trace->world_x[layer] = candidate_world_x[layer];
      trace->world_y[layer] = candidate_world_y[layer];
    }
  }

  if (terrain_layer < 0 || terrain_layer >= 2 ||
      !candidate_valid[terrain_layer] || PPU_bigTiles(g_ppu, terrain_layer))
    return false;

  /* Phase 1 is read-only: score the native rolling tilemap before any call
   * into WsShadow or mutation of retained world origins. */
  const uint16_t ppu_map_base =
      (uint16_t)PPU_bgTilemapAdr(g_ppu, terrain_layer);
  const uint32_t wx = candidate_world_x[terrain_layer];
  const uint32_t wy = candidate_world_y[terrain_layer];
  Dkc1LevelLayout best = kDkc1LayoutUnknown;
  int best_matches = 0, best_decodable = 0;
  for (int candidate = kDkc1LayoutHorizontal;
       candidate <= kDkc1LayoutVertical; candidate++) {
    int decodable = 0;
    int matches = Dkc1CalibrateLayout(
        (Dkc1LevelLayout)candidate, ppu_map_base, map_bank, metatile_bank,
        map_base, metatile_base, wx, wy, &decodable);
    if (trace) {
      const int index = candidate - kDkc1LayoutHorizontal;
      trace->calibration_matches[index] = matches;
      trace->calibration_decodable[index] = decodable;
    }
    if (matches > best_matches) {
      best_matches = matches;
      best_decodable = decodable;
      best = (Dkc1LevelLayout)candidate;
    }
  }
  const bool calibrated =
      best_decodable >= 64 && best_matches * 10 >= best_decodable * 7;
  Dkc1LevelLayout accepted_layout = kDkc1LayoutUnknown;
  int next_grace = 0;
  bool stream_revalidated = false;
  if (calibrated) {
    accepted_layout = best;
    next_grace = 2;
    if (trace) trace->calibration_accepted = true;
  } else if (cartridge_stream_ready &&
             s_ws_layout != kDkc1LayoutUnknown) {
    /* The checksum-locked cartridge adapters observed a complete widened
     * rolling-map fill for this exact mode/level/entrance. Some levels (Snow
     * Barrel Blast is the concrete oracle) cannot remain ROM-decode
     * calibrated after forced blank lifts, even though the live 64-column
     * PPU map is complete. Keep capturing that authoritative streamed map
     * instead of widening with an inactive shadow or pillarboxing gameplay.
     * Do not ROM-prefill this path: the rejected decoder is not an oracle. */
    accepted_layout = s_ws_layout;
    stream_revalidated = true;
    if (trace) trace->stream_revalidated = true;
  } else if (s_ws_layout != kDkc1LayoutUnknown && s_ws_layout_grace > 0) {
    /* Soft misses are tolerated only inside an unchanged hard identity and
     * only before the cartridge has proved a complete widened stream. Once
     * that proof exists, the live PPU map is stronger than a rejected ROM
     * decoder and must take precedence over this grace budget. */
    accepted_layout = s_ws_layout;
    next_grace = s_ws_layout_grace - 1;
    if (trace) trace->grace_accepted = true;
  } else {
    if (cartridge_stream_ready &&
        s_ws_layout == kDkc1LayoutUnknown) {
      /* A complete column count is not enough to bootstrap a scene whose
       * ROM decoder still rejects the live ring. Bonus exits can execute the
       * widened initializer while VRAM contains a mixture of the outgoing
       * room and the returning level. Treat that proof as transitional and
       * let the first calibrated frame take the ROM-prefill cold path. The
       * stream-only escape hatch remains available after this identity has
       * established a real layout (Snow Barrel Blast is the oracle). */
      Dkc1VideoInvalidateStreamCoverage();
      if (stream_bootstrap_rejected)
        *stream_bootstrap_rejected = true;
    }
    return false;
  }

  uint32_t shadow_world_x[2] = {0, 0};
  uint32_t shadow_world_y[2] = {0, 0};
  /* Select a stable cache window per layer only after calibration accepts
   * this frame. Terrain must cover the complete camera range; an independent
   * parallax plane is centered around its own coordinates so a low-Y sky can
   * coexist with high-Y terrain in vertical levels. */
  for (int layer = 0; layer < 2; layer++) {
    if (!candidate_valid[layer])
      continue;
    if (!s_ws_shadow_origin_valid[layer]) {
      const uint64_t x_left_pad = (uint64_t)Dkc1VideoExtra() + 8u;
      const uint64_t x_right_pad =
          (uint64_t)kDkc1VideoNativeWidth + Dkc1VideoExtra() + 8u;
      uint64_t min_x = candidate_world_x[layer];
      uint64_t max_x = candidate_world_x[layer];
      if (layer == terrain_layer) {
        if (lower_bound < min_x) min_x = lower_bound;
        if (upper_bound > max_x) max_x = upper_bound;
      }
      const uint64_t wanted_lo =
          min_x > x_left_pad ? min_x - x_left_pad : 0;
      const uint64_t wanted_hi = max_x + x_right_pad;
      const uint64_t capacity_x = (uint64_t)kWsShadowXTiles * 8u;
      if (wanted_hi - wanted_lo >= capacity_x)
        return false;
      uint64_t base_x = wanted_lo & ~UINT64_C(0x1ff);
      if (wanted_hi - base_x > capacity_x) {
        const uint64_t minimum_base = wanted_hi - capacity_x;
        base_x = (minimum_base + 0x1ffu) & ~UINT64_C(0x1ff);
        if (base_x > wanted_lo)
          return false;
      }
      const uint64_t wanted_y =
          candidate_world_y[layer] > 8u
              ? candidate_world_y[layer] - 8u : 0;
      s_ws_shadow_origin_x[layer] = (uint32_t)base_x;
      s_ws_shadow_origin_y[layer] =
          (uint32_t)(wanted_y & ~UINT64_C(0xff));
      s_ws_shadow_origin_valid[layer] = true;
    }
    if (candidate_world_x[layer] < s_ws_shadow_origin_x[layer] ||
        candidate_world_y[layer] < s_ws_shadow_origin_y[layer])
      return false;
    shadow_world_x[layer] =
        candidate_world_x[layer] - s_ws_shadow_origin_x[layer];
    shadow_world_y[layer] =
        candidate_world_y[layer] - s_ws_shadow_origin_y[layer];
    const uint64_t last_x = (uint64_t)shadow_world_x[layer] +
                            kDkc1VideoNativeWidth + Dkc1VideoExtra() + 8u;
    const uint64_t last_y = (uint64_t)shadow_world_y[layer] +
                            kDkc1VideoHeight + 8u;
    if (last_x >= (uint64_t)kWsShadowXTiles * 8u ||
        last_y >= (uint64_t)kWsShadowYTiles * 8u)
      return false;
  }
  if (trace) {
    memcpy(trace->shadow_origin_valid, s_ws_shadow_origin_valid,
           sizeof trace->shadow_origin_valid);
    memcpy(trace->shadow_origin_x, s_ws_shadow_origin_x,
           sizeof trace->shadow_origin_x);
    memcpy(trace->shadow_origin_y, s_ws_shadow_origin_y,
           sizeof trace->shadow_origin_y);
    memcpy(trace->shadow_local_x, shadow_world_x,
           sizeof trace->shadow_local_x);
    memcpy(trace->shadow_local_y, shadow_world_y,
           sizeof trace->shadow_local_y);
  }

  /* Phase 2 commits only an accepted frame. A rejected candidate cannot
   * capture tiles, move origins, or seed data that a later scene observes. */
  if (!s_ws_shadow_active) {
    if (trace) trace->cold_start = true;
    WsShadowReset();
    memset(s_ws_origin_valid, 0, sizeof s_ws_origin_valid);
    s_ws_shadow_active = true;
  }
  s_ws_layout = accepted_layout;
  s_ws_layout_grace = next_grace;
  for (int layer = 0; layer < 2; layer++) {
    const uint8_t bit = (uint8_t)(1u << layer);
    if (!(layer_mask & bit)) {
      s_ws_origin_valid[layer] = false;
      continue;
    }
    s_ws_world_x[layer] = candidate_world_x[layer];
    s_ws_world_y[layer] = candidate_world_y[layer];
    s_ws_origin_valid[layer] = candidate_valid[layer];

    WsShadowSetWorld(layer, shadow_world_x[layer], shadow_world_y[layer]);
    WsShadowSetScroll(layer,
                      (uint16_t)(g_ppu->hScroll[layer] + presentation_bias),
                      g_ppu->vScroll[layer]);
    WsShadowSetCaptureCols(layer, 0);
    WsShadowSetWestKeep(layer, keep_tiles);
    WsShadowSetEastKeep(layer, keep_tiles);
    /* Keep the default world-relative Y key (projected through the stable
     * per-layer origin above). RetainHistory switches the shared shadow to
     * viewport-relative rows; DKC1's ROM decoder fills authored world rows,
     * so enabling it made every margin lookup miss and exposed the
     * transparent fallback as a hard vertical cutoff. */
    WsShadowSetRespectGameWrites(layer, layer == terrain_layer ? 1 : 0);
    uint16_t blank_entry = 0;
    if (!PPU_bigTiles(g_ppu, layer))
      Dkc1VideoFindTransparent4bppTile(
          g_ppu->vram, 0x8000u,
          (uint16_t)PPU_bgTileAdr(g_ppu, layer), &blank_entry);
    const bool parallax_continuation =
        layer == 1 && layer != terrain_layer &&
        PPU_bgTilemapWider(g_ppu, layer) != 0;
    /* BG2's second 32-column screen is authored parallax data. Keeping the
     * terrain-plane blank fallback here discarded that valid continuation;
     * the later edge-repeat policy then visibly copied the native left edge
     * into the right gutter. Give this known-wide parallax plane its own
     * explicit source class while terrain misses remain fail-visible. */
    WsShadowSetRawContinuation(layer, parallax_continuation);
    WsShadowSetBlankTile(layer,
                         parallax_continuation ? -1 : (int)blank_entry);
    /* Parallax backdrops are horizontally periodic; fold their margins to
     * the congruent native column instead of exposing unwritten map. */
    if (layer == 1 && layer != terrain_layer)
      WsShadowSetPeriodicFold(layer);
  }

  WsShadowFrame(g_ppu);
  if (trace) {
    trace->shadow_commit = true;
    trace->shadow_frame = true;
    trace->terrain_layer = terrain_layer;
  }

  if (cartridge_stream_ready) {
    /* The checksum-locked DKC adapters prove the rolling terrain map covers
     * the widened viewport, including columns west of the native scroll
     * origin. The generic viewport sweep can only grow eastward, so ingest
     * this proven live range explicitly by the same world-coordinate ring
     * mapping used by DKC's column streamer. This is captured cartridge data,
     * not a ROM-decoder prediction. */
    const uint32_t guard = 8u;
    const uint32_t left_px =
        wx > (uint32_t)Dkc1VideoExtra() + guard
            ? wx - (uint32_t)Dkc1VideoExtra() - guard : 0u;
    const uint32_t right_px =
        wx + kDkc1VideoNativeWidth + (uint32_t)Dkc1VideoExtra() + guard;
    const uint32_t top_px = wy > guard ? wy - guard : 0u;
    const uint32_t bottom_px = wy + kDkc1VideoHeight + guard;
    const uint32_t origin_tx = s_ws_shadow_origin_x[terrain_layer] >> 3;
    const uint32_t origin_ty = s_ws_shadow_origin_y[terrain_layer] >> 3;
    for (uint32_t wtx = left_px >> 3; wtx <= right_px >> 3; wtx++) {
      for (uint32_t wty = top_px >> 3; wty <= bottom_px >> 3; wty++) {
        if (wtx < origin_tx || wty < origin_ty)
          continue;
        const uint16_t entry =
            g_ppu->vram[Dkc1RollingMapWord(ppu_map_base, wtx, wty) & 0x7fffu];
        WsShadowCaptureTile(terrain_layer, wtx - origin_tx,
                            wty - origin_ty, entry);
      }
    }
  }

  /* Once the cartridge has proved and populated the complete widened
   * rolling map, the symmetric live capture above is authoritative on every
   * frame. Re-running the ROM margin refill after that point overwrote live
   * cells as their one-frame game-write cooldowns expired. Because DKC may
   * upload a row over several frames, that appeared as an 8px-at-a-time
   * wipe down a stationary margin even though WRAM/VRAM/OAM were unchanged.
   * Publish the live range and leave it intact; the decoder is only the
   * bootstrap source before stream coverage is proven. */
  if (cartridge_stream_ready || accepted_layout == kDkc1LayoutUnknown)
    return true;

  /* Prefill the margin columns (plus one guard tile each side) from ROM. */
  uint16_t blank_entry = 0;
  Dkc1VideoFindTransparent4bppTile(
      g_ppu->vram, 0x8000u,
      (uint16_t)PPU_bgTileAdr(g_ppu, terrain_layer), &blank_entry);
  /* Round the partial 43-pixel side margin up, then keep one complete guard
   * tile for fine scroll.  The old floor division seeded only six columns;
   * a nonzero scroll phase could sample the unseeded seventh column as the
   * thin black strip at the far-left edge. */
  const int margin_tiles = (Dkc1VideoExtra() + 7) / 8 + 1;
  if (trace) {
    trace->prefill = true;
    trace->margin_tiles = margin_tiles;
  }
  const int visible_rows = (kDkc1VideoHeight >> 3) + 2;
  /* Croctopus Chase's vertical room map ends its authored lower-right wall
   * at the native 256px boundary: the next 32x32 map cells are fully
   * transparent because the cartridge could never display them. Continue
   * only that proven boundary into the presentation-only right margin. The
   * target metatile must be wholly transparent and the nearest native-edge
   * metatile must have pixels in all 16 characters; partial openings and
   * decorative edges therefore remain untouched. This does not alter VRAM,
   * WRAM, collision, streaming, or any pixel in the native viewport. */
  const bool allow_underwater_right_boundary =
      s_ws_layout == kDkc1LayoutVertical &&
      Dkc1ReadWram16(0x0032) == 0x0003u &&
      Dkc1ReadWram16(0x0030) == 0x0061u &&
      map_bank == 0xe9u && metatile_bank == 0xd0u;
  const uint32_t native_edge_metatile_x =
      ((wx + kDkc1VideoNativeWidth - 1u) >> 3) >> 2;
  const uint16_t character_base =
      (uint16_t)PPU_bgTileAdr(g_ppu, terrain_layer);
  for (int side = 0; side < 2; side++) {
    for (int i = 0; i < margin_tiles; i++) {
      const int64_t signed_wtx =
          side == 0 ? (int64_t)(wx >> 3) - 1 - i
                    : (int64_t)(wx >> 3) + 32 + i;
      if (signed_wtx < 0)
        continue;
      const uint32_t wtx = (uint32_t)signed_wtx;
      for (int row = -1; row < visible_rows; row++) {
        const int64_t signed_wty = (int64_t)(wy >> 3) + row;
        if (signed_wty < 0)
          continue;
        const uint32_t wty = (uint32_t)signed_wty;
        uint16_t entry;
        if (!Dkc1VideoDecodeLevelTile(s_ws_layout, map_bank, metatile_bank,
                                      map_base, metatile_base, wtx, wty,
                                      &entry))
          entry = blank_entry;
        if (allow_underwater_right_boundary && side == 1 &&
            (wtx >> 2) > native_edge_metatile_x) {
          bool target_empty = false, target_full = false;
          bool edge_empty = false, edge_full = false;
          const uint32_t metatile_y = wty >> 2;
          if (Dkc1VideoClassifyLevelMetatile(
                  s_ws_layout, map_bank, metatile_bank, map_base,
                  metatile_base, wtx >> 2, metatile_y, g_ppu->vram,
                  0x8000u, character_base, &target_empty, &target_full) &&
              target_empty &&
              Dkc1VideoClassifyLevelMetatile(
                  s_ws_layout, map_bank, metatile_bank, map_base,
                  metatile_base, native_edge_metatile_x, metatile_y,
                  g_ppu->vram, 0x8000u, character_base, &edge_empty,
                  &edge_full) && edge_full) {
            const uint32_t source_wtx =
                native_edge_metatile_x * 4u + (wtx & 3u);
            if (Dkc1VideoDecodeLevelTile(
                    s_ws_layout, map_bank, metatile_bank, map_base,
                    metatile_base, source_wtx, wty, &entry) && trace)
              trace->boundary_continuation_tiles++;
          }
        }
        const uint32_t origin_tx =
            s_ws_shadow_origin_x[terrain_layer] >> 3;
        const uint32_t origin_ty =
            s_ws_shadow_origin_y[terrain_layer] >> 3;
        if (wtx >= origin_tx && wty >= origin_ty)
          WsShadowForceTile(terrain_layer, wtx - origin_tx,
                            wty - origin_ty, entry);
      }
    }
  }
  return true;
}

static bool Dkc1DebugForceWidescreenFallback(void) {
  /* Deterministic regression injector. Unset by default and presentation-
   * only: the exact absolute SNES frame is centered over black as though
   * calibration rejected it. This exercises actor lifecycle continuity
   * across a soft fallback without modifying cartridge state. */
  const char *setting = getenv("DKC1_WS_FORCE_FALLBACK_FRAME");
  if (!setting || !*setting)
    return false;
  char *end = NULL;
  const long requested = strtol(setting, &end, 0);
  return end && *end == '\0' && requested >= 0 &&
         requested == snes_frame_counter;
}

void Dkc1DrawPpuFrame(void) {
  SimpleHdma channels[8];
  bool active[8] = {false};
  Dkc1WsTraceFrame trace;
  memset(&trace, 0, sizeof trace);
  trace.frame = snes_frame_counter;
  trace.terrain_layer = -1;
  trace.prepare_bgmode = g_ppu->bgmode;
  trace.prepare_inidisp = g_ppu->inidisp;
  trace.prepare_main_layers = g_ppu->screenEnabled[0];
  trace.prepare_sub_layers = g_ppu->screenEnabled[1];
  memcpy(trace.prepare_bgsc, g_ppu->bgXsc, sizeof trace.prepare_bgsc);
  memcpy(trace.prepare_hscroll, g_ppu->hScroll, sizeof trace.prepare_hscroll);
  memcpy(trace.prepare_vscroll, g_ppu->vScroll, sizeof trace.prepare_vscroll);
  const bool trace_enabled = Dkc1WsTraceEnabled();
  if (trace_enabled) {
    WsShadowGetMarginStats(0, &trace.shadow_before[0]);
    WsShadowGetMarginStats(1, &trace.shadow_before[1]);
  }

  /* Widescreen is host-only presentation policy, reapplied every frame. */
  uint8_t wide_layer_mask =
      Dkc1VideoIsWidescreen()
          ? Dkc1VideoPpuWideLayerMask(g_ppu->bgmode, g_ppu->bgXsc,
                                      g_ppu->screenEnabled[0],
                                      g_ppu->screenEnabled[1])
          : 0;
  /* A Mode-1/64-column register shape is necessary but not sufficient:
   * logos and fixed screens can temporarily retain the same PPU shape. Build
   * and calibrate the world-keyed shadow first, then widen only a proven
   * level layout. This prevents stale gameplay/logo data in the margins. */
  const int presentation_bias =
      wide_layer_mask != 0 ? Dkc1WidescreenPresentationBias() : 0;
  trace.wide_layer_mask = wide_layer_mask;
  trace.presentation_bias = presentation_bias;
  const bool debug_forced_fallback =
      wide_layer_mask != 0 && Dkc1DebugForceWidescreenFallback();
  trace.debug_forced_fallback = debug_forced_fallback;
  const bool cartridge_stream_ready =
      wide_layer_mask != 0 && !debug_forced_fallback &&
      Dkc1VideoCartridgeTerrainReady(g_ram);
  bool stream_bootstrap_rejected = false;
  const bool shadow_world_ready =
      wide_layer_mask != 0 && !debug_forced_fallback &&
      Dkc1PrepareWidescreenShadow(wide_layer_mask, presentation_bias,
                                  cartridge_stream_ready,
                                  &stream_bootstrap_rejected,
                                  trace_enabled ? &trace : NULL);
  Dkc1VideoGetStreamCoverageStats(&trace.stream_coverage);
  const bool extend_world = shadow_world_ready;
  trace.cartridge_stream_ready = cartridge_stream_ready;
  if (trace_enabled) {
    trace.selected_layout = s_ws_layout;
    trace.layout_grace = s_ws_layout_grace;
  }
  /* These PPU policies are host presentation state, not cartridge state.
   * Reset them every frame so a prior 64-column BG3 scene cannot leak into a
   * bounded BG3 HUD/logo scene. */
  PpuSetWidescreenLayerMask(g_ppu, 0);
  PpuSetWidescreenBg3Widen(g_ppu, 0);
  if (extend_world) {
    Dkc1VideoSetPresentationBias(presentation_bias);
    PpuSetExtraSpace(g_ppu, (uint8_t)Dkc1VideoExtra());
    PpuSetWidescreenPresentationXBias(g_ppu, presentation_bias);
    /* Repeat only enabled background planes that cannot address a second
     * tilemap screen.  The terrain-shadow mask intentionally covers only
     * BG1/BG2, so using its inverse here incorrectly repeated a physically
     * 64-column BG3 in Jungle Hijinxs Bonus 1.  Size each enabled plane from
     * its own BGxSC register: bounded 32-column overlays still repeat, while
     * 64-column BG2/BG3 expose their authored continuation. */
    uint8_t enabled = (uint8_t)((g_ppu->screenEnabled[0] |
                                 g_ppu->screenEnabled[1]) & 0x07u);
    uint8_t repeat_mask = 0;
    uint8_t physical_wide_mask = 0;
    for (int layer = 0; layer < 3; layer++) {
      const uint8_t bit = (uint8_t)(1u << layer);
      if (!(enabled & bit))
        continue;
      if (PPU_bgTilemapWider(g_ppu, layer) != 0)
        physical_wide_mask = (uint8_t)(physical_wide_mask | bit);
      else
        repeat_mask = (uint8_t)(repeat_mask | bit);
    }
    const uint8_t render_mask =
        (uint8_t)(wide_layer_mask | physical_wide_mask);
    PpuSetWidescreenLayerMask(g_ppu, render_mask);
    /* The shared renderer normally clamps BG3 as a 256-pixel HUD plane.
     * DKC1's cave foreground uses a real 64-column BG3 tilemap instead. A
     * nonzero first line enables the PPU's existing whole-visible-frame BG3
     * path without changing bounded 32-column BG3 scenes. */
    PpuSetWidescreenBg3Widen(
        g_ppu, (physical_wide_mask & 0x04u) != 0 ? 1u : 0u);
    PpuSetWidescreenLayerRepeat(g_ppu, repeat_mask);
    trace.render_layer_mask = render_mask;
    trace.repeat_layer_mask = repeat_mask;
    trace.edge_extension = true;
    Dkc1VideoSetTerrainReady(true);
  } else if (Dkc1VideoIsWidescreen()) {
    trace.centered_fallback = true;
    Dkc1RejectWidescreenShadow();
    /* Pillarbox fixed screens (logos, map, title): clear the host row and
     * center the authentic 256 columns. */
    size_t row_bytes = (size_t)Dkc1VideoWidth() * kDkc1VideoBytesPerPixel;
    for (int y = 0; y < kDkc1VideoHeight; y++)
      memset(g_ppu->renderBuffer + (size_t)y * g_ppu->renderPitch,
             0, row_bytes);
    PpuSetExtraSpaceCentered(g_ppu, (uint8_t)Dkc1VideoExtra());
    PpuSetWidescreenPresentationXBias(g_ppu, 0);
    if (stream_bootstrap_rejected) {
      /* The old renderer enabled widened gameplay culls after observing the
       * completed initializer on this boundary. Keep that next-frame
       * gameplay decision byte-identical while refusing only its unproven
       * pixels. The following calibrated frame establishes the clean shadow
       * and ordinary terrain-ready ownership resumes. */
      Dkc1VideoSetTerrainReady(true);
    }
  } else {
    Dkc1ResetWidescreenShadow();
    PpuSetExtraSpace(g_ppu, 0);
    PpuSetWidescreenPresentationXBias(g_ppu, 0);
  }

  dma_startDma(g_dma, g_snesrecomp_last_hdmaen, true);
  WsShadowDebugBeginFrame();
  for (int channel = 0; channel < 8; channel++) {
    active[channel] = g_dma->channel[channel].hdmaActive;
    if (active[channel])
      SimpleHdma_Init(&channels[channel], &g_dma->channel[channel]);
  }

  for (int line = 0; line <= 224; line++) {
    if (extend_world && presentation_bias != 0) {
      for (int layer = 0; layer < 4; layer++)
        g_ppu->hScroll[layer] =
            (uint16_t)(g_ppu->hScroll[layer] + presentation_bias);
    }
    ppu_runLine(g_ppu, line);
    if (extend_world && presentation_bias != 0) {
      for (int layer = 0; layer < 4; layer++)
        g_ppu->hScroll[layer] =
            (uint16_t)(g_ppu->hScroll[layer] - presentation_bias);
    }
    for (int channel = 0; channel < 8; channel++) {
      if (active[channel]) SimpleHdma_DoLine(&channels[channel]);
    }
  }

  /* Model the VBlank boundary after the visible lines so the PPU reloads its
   * internal OAM port from OAMADD before the next frame's OAM DMA. */
  (void)ppu_checkOverscan(g_ppu);
  ppu_handleVblank(g_ppu);

  if (trace_enabled) {
    trace.reset = s_ws_trace_reset_pending;
    s_ws_trace_reset_pending = false;
    WsShadowGetMarginStats(0, &trace.shadow_after[0]);
    WsShadowGetMarginStats(1, &trace.shadow_after[1]);
    Dkc1WsTraceEmit(&trace);
  }
  Dkc1ApplyProvenanceOverlay(wide_layer_mask);
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
