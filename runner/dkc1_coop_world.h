#ifndef DKC1_COOP_WORLD_H
#define DKC1_COOP_WORLD_H

#include <stddef.h>
#include <stdint.h>

struct CpuState;

/* The cartridge camera remains view zero. View one runs its original
 * admitted dry/water controller privately for Diddy, including fractional motion,
 * deadzones and authored bounds. */
/* Advance history once at the completed guest-frame boundary. Reads and redraws
 * must never advance it. */
void Dkc1CoopWorldNoteFrame(const uint8_t *wram);
/* Called at completed cartridge camera boundaries: $80:9C4E/C27C/C4A3. */
void Dkc1CoopWorldNoteNativeCamera(const uint8_t *wram);
/* Capture this Kong's transient native camera requests before mux restore. */
void Dkc1CoopWorldNoteKongCamera(const uint8_t *wram, uint16_t slot);
/* Native dry-camera focus operand; retain the request for the private view. */
uint16_t Dkc1CoopWorldCameraFocus(struct CpuState *cpu,uint16_t native);
int Dkc1CoopWorldGetCamera(const uint8_t *wram, int view,
                          uint16_t *x, uint16_t *y);

/* Native scanner cursors are view-local; source bookmarks and live actors are
 * shared. This POD is part of the host save state, never cartridge WRAM. */
typedef struct Dkc1CoopWorldSnapshot {
  uint16_t version, initialized, entrance, mode, height, lower, upper;
  uint16_t cursor[5];             /* $A0/$A2/$A4/$EF/$F1 */
  uint8_t vertical[12];           /* $1E03..$1E0E */
  uint16_t scan_pending, reserved; /* v5: bits8/80 + valid100; v7: focus handle in high byte */
  /* v2 extension; the first 40 bytes retain the v1 wire layout. */
  struct {
    int32_t ready, forward, column8, previous_framed;
    int32_t row, reference;
  } camera;
  /* v3 wire extension, v4 supports the native dry controllers too.
   * Keep the field name and byte layout for old snapshot compatibility. */
  struct {
    uint16_t ready, entrance, mode, height, slot, x, y, frame;
    uint8_t motion[32];          /* $1A4B..$1A6A, including fractional speeds */
    uint8_t region[4];           /* wet: $1E0F..12; dry: $1AF9 and $1B1B */
  } water;
  /* v6 retains this layout; pre-v6 mode5 camera history is incompatible. */
  /* v4 extension: dry-mode B8 region cursor; $1E11 is refreshed input. */
  uint8_t land_region[4];

} Dkc1CoopWorldSnapshot;

void Dkc1CoopWorldReset(void);
/* Frozen prefix length selected by the enclosing DWS8 version. Missing
 * extension bytes are zeroed by the caller before the existing load codec. */
size_t Dkc1CoopWorldStateSize(uint32_t host_version);
void Dkc1CoopWorldSaveState(Dkc1CoopWorldSnapshot *state);
void Dkc1CoopWorldLoadState(const Dkc1CoopWorldSnapshot *state);

/* Generated FE67 marks a completed native scanner pass. RunScanner is called
 * once at the simulation boundary, after clearing the interpreter deadline.
 * Its new actors start their first update on the following simulation frame.
 * Return 1 on commit, 0 when idle/disabled, -1 after a complete rollback. */
void Dkc1CoopWorldNoteScan(struct CpuState *cpu);
void Dkc1CoopWorldNoteInitialize(void);
int Dkc1CoopWorldRunScanner(struct CpuState *cpu);
int Dkc1CoopWorldScannerActive(void);
unsigned Dkc1CoopWorldFreeActorSlots(const uint8_t *wram);

/* Called only at proven off-camera failure labels; death, pickup, and scripted
 * removal paths never call these helpers. routine is the original 16-bit PC. */
int Dkc1CoopWorldKeepActor(struct CpuState *cpu, uint16_t routine);
int Dkc1CoopWorldKeepSource(struct CpuState *cpu, int group);

#endif
