#ifndef DKC1_COOP_MOD_H
#define DKC1_COOP_MOD_H

#include <stddef.h>
#include <stdint.h>
#include "dkc1_coop_palette.h"

struct Ppu;
struct CpuState;

/* Frame-boundary host state. Fixed-width fields, no pointers or actor scratch.
 * Enabling the mod remains a user setting, never an effect of loading a save. */
typedef struct Dkc1CoopSnapshot {
  uint16_t version, enabled;
  uint16_t previous[2];
  uint16_t leader_lock, barrel_hold;
  /* Wire v1/v2: X/Y, count, observed actor (v2). Wire v3: per-seat Y,
   * packed byte counts and active mask. The serialized size stays 20 bytes. */
  uint16_t pose_x, pose_y, pose_stuck;
  /* v4: bits0/1 pose mask; bits2/3 Squawks creator, bits4..11 actor handle.
   * v5: bits12/13 animal-bonus collector; bit14 marks entry into the bonus. */
  uint16_t reserved;
} Dkc1CoopSnapshot;

/* A deadline may stop inside a stock actor routine. At host boundaries WRAM
 * is canonical; this separate payload preserves the suspended substitution.
 * Keep the legacy 20-byte history payload compatible with existing saves. */
typedef struct Dkc1CoopActorSnapshot {
  uint32_t resume_pc;
  uint16_t version, pending, stack, scene[2];
  uint16_t words[16]; /* six pads, four follow words, current Kong, five camera */
  uint16_t camera, fake_slot, fake_id, fake_state, fake_collision;
  /* Actor v4: bit0 marks substituted coordinates; bits3/7 retain the
   * pending $0579 vertical/horizontal request. v3 kept only bit3;
   * v1/v2 camera stays boolean. All versions retain this 64-byte layout. */
  /* Actor payload v2 uses the former reserved words for the pending cannon
   * handle and partner (0, Kong handle 2/4, or 6 for a shared-flight catch).
   * Version 1 remains readable; the serialized size is unchanged. */
  uint16_t mux_kong, mux_id, reserved[2];
} Dkc1CoopActorSnapshot;

/* DWS8 v8 adds this payload after the unchanged v1 history and actor blocks.
 * It includes unfinished rope substitutions at a guest deadline, not only
 * the per-seat ownership surviving between complete actor calls. */
typedef struct Dkc1CoopRopeState {
  int32_t rope_hold;
  int32_t rope_owner;
  uint32_t rope_slot;
  uint16_t rope_words[3];
  uint8_t rope_release[2];
  uint16_t release_hook[2];
  uint16_t rope_latched[2];
  uint16_t hook_pending;
  uint16_t frame_aff, frame_b01;
  /* Even: owned swing31. v5 odd: pending native vertical30 contact,
   * encoded as handle|1 until that player's next airborne tick. */
  uint16_t rider_hook[2];
  uint16_t release_xv, release_yv;
  int32_t release_vel;
  uint16_t swing_x[2], swing_y[2];
  uint16_t swing_aim_kong;
  int32_t swing_aim;
  uint16_t swing_latch;
  int32_t swing_latch_on;
  uint16_t keep_aff;
  int32_t keep_aff_on;
  uint16_t fallen_rope[3];
  int32_t fallen_rope_on;
  uint8_t swing_seen[2];
} Dkc1CoopRopeState;
typedef struct Dkc1CoopContinuation {
  uint16_t version, actor_wait_age, kritter_cam, kritter_cam_on;
  Dkc1CoopRopeState rope;
  /* v4/v5 pack current/carried animal intents in the low/high bytes of
   * weather.reserved: (type << 2)|(rider / 2). v3 held one full intent.
   * Live weather history retains zero. Older payloads remain readable. */
  Dkc1CoopWeatherState weather;
} Dkc1CoopContinuation;

/* Simultaneous co-op: each Kong sprite runs the stock player controller
 * with that Kong's pad on $0500 (DK=0, Diddy=1). No host-invented physics. */

#ifdef __cplusplus
extern "C" {
#endif

int Dkc1CoopSavedEnabled(void);
void Dkc1CoopSetEnabled(int enabled);
int Dkc1CoopEnabled(void);
int Dkc1CoopRopeCanDepart(const uint8_t *wram, uint16_t rope);
int Dkc1CoopBlockRopeDeparture(struct CpuState *cpu);
int Dkc1CoopRopeWaitCamera(struct CpuState *cpu);
void Dkc1CoopSelectMapAvatar(struct CpuState *cpu);
void Dkc1CoopPublishMapPads(struct CpuState *cpu);
uint16_t Dkc1CoopNativePadOwner(struct CpuState *cpu, uint16_t native);
void Dkc1CoopRememberAnimalBonus(struct CpuState *cpu);
unsigned Dkc1CoopAnimalBonusOwner(const uint8_t *ram);
void Dkc1CoopRememberRopeContact(struct CpuState *cpu);
int Dkc1CoopBlockWheelDeparture(struct CpuState *cpu);
int Dkc1CoopBlockCroctopusDeparture(struct CpuState *cpu);
/* Presentation may share a camera only for two verified cannon passengers. */
int Dkc1CoopSharedCannonCamera(const uint8_t *wram);
/* Only a verified cart passenger owns its cart controller. */
uint16_t Dkc1CoopMinecartForKong(const uint8_t *wram, uint16_t kong);
/* First rider retains the presentation view during a native cart jump. */
uint16_t Dkc1CoopMinecartViewOwner(const uint8_t *wram);
int Dkc1CoopPrepareMountedPartner(struct CpuState *cpu);
void Dkc1CoopRememberRoomMount(struct CpuState *cpu);
void Dkc1CoopSaveCarriedMount(struct CpuState *cpu);
void Dkc1CoopRestoreCarriedMount(struct CpuState *cpu);
void Dkc1CoopRestoreRoomMount(struct CpuState *cpu);
void Dkc1CoopFinishMountedPartner(uint8_t *wram);
int Dkc1CoopRestoreMountedExit(struct CpuState *cpu);
int Dkc1CoopBlockMountedSwap(struct CpuState *cpu);
int Dkc1CoopBlockTeamSwap(struct CpuState *cpu);
int Dkc1CoopAnimalMountOccupied(struct CpuState *cpu,
                               uint16_t rider, uint16_t animal);
void Dkc1CoopInitialize(void);
void Dkc1CoopBeginFrame(uint8_t *wram, uint32_t packed24);
void Dkc1CoopEndFrame(uint8_t *wram);
void Dkc1CoopSuspendActor(uint8_t *wram, uint32_t resume_pc, uint16_t stack);
void Dkc1CoopResumeActor(uint8_t *wram, uint32_t pc, uint16_t stack);
int Dkc1CoopActorPending(void);
/* Atomic extra-actor attempts must also roll back host-owned controller,
 * camera and survivor state. This scope never changes the WRAM itself. */
int Dkc1CoopBeginTransaction(void);
void Dkc1CoopEndTransaction(int commit);
/* At $BFA1D4: preserve an independently controlled survivor when its partner
 * is hit, while committing the cartridge's one-Kong health bookkeeping. */
int Dkc1CoopIndependentDamage(struct CpuState *cpu);
int Dkc1CoopIndependentFall(struct CpuState *cpu);
void Dkc1CoopSaveActorState(Dkc1CoopActorSnapshot *state);
int Dkc1CoopLoadActorState(const Dkc1CoopActorSnapshot *state, const uint8_t *wram);
void Dkc1CoopSaveContinuation(Dkc1CoopContinuation *state);
void Dkc1CoopLoadContinuation(const Dkc1CoopContinuation *state);
void Dkc1CoopSaveState(Dkc1CoopSnapshot *state);
void Dkc1CoopLoadState(const Dkc1CoopSnapshot *state, const uint8_t *wram);
/* Birth/reuse binding participates in entity speculative rollback. */
uint16_t Dkc1CoopCompanionBinding(void);
void Dkc1CoopRestoreCompanionBinding(uint16_t binding);
void Dkc1CoopActorAllocated(uint16_t handle);
void Dkc1CoopActorCreated(struct CpuState *cpu,uint16_t handle);
void Dkc1CoopCrateHit(const uint8_t *ram,uint16_t handle);
void Dkc1CoopBeforeActor(uint8_t *wram, uint32_t slot, uint16_t direct_page);
void Dkc1CoopAfterActor(uint8_t *wram);
void Dkc1CoopPublishPickupBounds(struct CpuState *cpu);
void Dkc1CoopFinishBarrelRescue(struct CpuState *cpu);
void Dkc1CoopPrepareFrame(struct Ppu *ppu, uint8_t *wram);
/* Riding exchanges the displayed Kong/animal poses without exchanging IDs.
 * Resolve graphics ownership for the private renderer, leaving WRAM intact. */
uint16_t Dkc1CoopActorPaletteOwner(const uint8_t *wram, uint16_t slot);
/* Zero preserves the resolved entity-owned palette. */
uint16_t Dkc1CoopActorPaletteSource(const uint8_t *wram, uint16_t slot);
/* The primary camera remains stable while the second actor's tick uses its
 * own projection for screen-relative controller and pickup operations. */
void Dkc1CoopPrimaryCamera(const uint8_t *wram, uint16_t *x, uint16_t *y);
uint16_t Dkc1CoopRopeClimbCameraY(const uint8_t *wram, uint16_t native);
const char *Dkc1CoopStatus(void);

#ifdef __cplusplus
}
#endif

#endif
