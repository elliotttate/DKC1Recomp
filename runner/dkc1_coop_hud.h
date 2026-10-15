#ifndef DKC1_COOP_HUD_H
#define DKC1_COOP_HUD_H
#include <stddef.h>
#include <stdint.h>
struct CpuState;

/* A native HUD draw owns its pose and palette even when another actor
 * reuses the same virtual OBJ tile/palette address later in the frame. */
typedef struct Dkc1CoopHudBatch {
  uint16_t first, count, handle, pose, flags, palette_valid, x, y;
  uint16_t palette[16];
} Dkc1CoopHudBatch;
typedef struct Dkc1CoopHudGraphics {
  uint16_t count, pending;
  Dkc1CoopHudBatch batches[128];
} Dkc1CoopHudGraphics;

/* Native HUD output and admitted direct sprite jobs retain their OAM and
 * resource batches. Persist with host save data so paused redraws never
 * advance cartridge HUD timers. */
typedef struct Dkc1CoopHudSnapshot {
  uint16_t version, valid, pending, first, count, entrance, mode, frame;
  uint16_t pending_entrance, pending_mode, pending_frame, reserved;
  uint8_t oam[512], high[32];
  Dkc1CoopHudGraphics graphics, capture;
} Dkc1CoopHudSnapshot;

void Dkc1CoopHudReset(void);
/* Frozen prefix length selected by the enclosing DWS8 version. */
size_t Dkc1CoopHudStateSize(uint32_t host_version);
void Dkc1CoopHudSaveState(Dkc1CoopHudSnapshot *state);
void Dkc1CoopHudLoadState(const Dkc1CoopHudSnapshot *state);
void Dkc1CoopHudBegin(struct CpuState *cpu); /* $80A1AE */
void Dkc1CoopHudEnd(struct CpuState *cpu);   /* $80A1B9 */
void Dkc1CoopHudSpriteBegin(struct CpuState *cpu); /* $BBA772 */
void Dkc1CoopHudSpriteEnd(struct CpuState *cpu);   /* $BBA844 */
const Dkc1CoopHudBatch *Dkc1CoopHudBatchForSlot(unsigned slot, unsigned total);

/* Pure screen-space presentation: native A503 effects followed by the saved
 * HUD. Banana flight42 is displayed only in its owning view. Outputs one
 * complete private OAM page; returns OBJ count or -1 before first capture. */
int Dkc1CoopHudBuildOam(const uint8_t *ram, unsigned view,
                      uint8_t *oam, uint8_t *high);
#endif
