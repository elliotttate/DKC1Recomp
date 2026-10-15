#ifndef DKC1_COOP_MOVEMENT_H
#define DKC1_COOP_MOVEMENT_H

#include <stdint.h>

enum {
  kDkc1CoopSpriteDonkey = 1u,
  kDkc1CoopSpriteDiddy = 2u,
  kDkc1CoopSpriteCamera = 0x5Du,
  kDkc1CoopBindReach = 0x40
};

enum Dkc1CoopKongRole {
  kDkc1CoopRoleGone = 0,
  kDkc1CoopRolePlayable,
  kDkc1CoopRoleFollow,
  kDkc1CoopRoleBarrel,
  kDkc1CoopRoleDying,
  kDkc1CoopRoleLost,
  kDkc1CoopRoleTagPull
};

uint16_t Dkc1CoopPackedToSnes(uint32_t packed);
void Dkc1CoopCacheKongSlots(const uint8_t *wram, uint32_t slot[2]);
enum Dkc1CoopSceneCapability {
  kDkc1CoopSceneControl = 1u,
  kDkc1CoopScenePresentation = 2u,
  kDkc1CoopSceneScanner = 4u,
  kDkc1CoopSceneCamera = 8u,
  kDkc1CoopSceneFixed = 16u,
  /* Reconstruct owned OBJ resources using the native room projection. */
  kDkc1CoopSceneNativeView = 32u,
  /* A scripted picture uses one native projection, independent of players. */
  kDkc1CoopSceneSingleView = 64u
};
unsigned Dkc1CoopSceneCapabilities(const uint8_t *wram);
int Dkc1CoopInLevel(const uint8_t *wram);
int Dkc1CoopFixedBossRoom(const uint8_t *wram);
/* Native death poses only; corpse history and final-fall presentation remain separate. */
int Dkc1CoopKongDeathPose(uint16_t state, int16_t velocity);
int Dkc1CoopKongRole(const uint8_t *wram, uint32_t slot);
int Dkc1CoopKongLost(const uint8_t *wram, uint32_t slot);
int Dkc1CoopKongInBarrel(const uint8_t *wram, uint32_t slot);
/* Live roles admitted to co-op actor/formation operations, independent of
 * whether this Kong may move its camera. */
int Dkc1CoopKongCanParticipate(const uint8_t *wram, uint32_t slot);
int Dkc1CoopKongCam(const uint8_t *wram, uint32_t slot);
int Dkc1CoopKongOwnCamera(const uint8_t *wram, uint32_t slot);
int Dkc1CoopKongInView(const uint8_t *wram, uint32_t slot);
int Dkc1CoopCoordsInView(const uint8_t *wram, unsigned x, unsigned y);
int Dkc1CoopWorldSkip(uint16_t sprite_id);
/* Select the native actor tick's Kong using its species-specific metric.
 * Returns a sprite identity (1/2), with seat zero winning equal distances. */
uint16_t Dkc1CoopKongForActorTick(const uint8_t *wram, uint32_t obj,
                                const uint32_t slot[2], uint16_t lock);

#endif
