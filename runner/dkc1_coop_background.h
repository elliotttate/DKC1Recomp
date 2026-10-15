#ifndef DKC1_COOP_BACKGROUND_H
#define DKC1_COOP_BACKGROUND_H

#include <stdint.h>

enum Dkc1CoopBackgroundOwner {
  kCoopOrdinaryParallax,
  kCoopCeilingLight,
  kCoopFog,
  kCoopShip
};
enum Dkc1CoopEffectResult {
  kCoopEffectRejected = 0,
  kCoopEffectNotApplicable = 1,
  kCoopEffectOwnsTables = 2
};
typedef struct Dkc1CoopBackgroundProtocol {
  unsigned entrance;
  uint16_t nmi;
  enum Dkc1CoopBackgroundOwner owner;
  int native;
} Dkc1CoopBackgroundProtocol;

/* These are the existing producer exclusions, not a new scene whitelist.
 * The light consumes two descriptor bytes; the ship needs the full pair.
 * Channel shapes and per-view history remain each producer's responsibility. */
static inline Dkc1CoopBackgroundProtocol Dkc1CoopBackgroundDecode(
    unsigned entrance, const uint8_t *descriptor, int have_update) {
  Dkc1CoopBackgroundProtocol p = {0};
  p.entrance = entrance;
  p.native = descriptor != 0;
  if (!descriptor) return p;
  p.nmi = (uint16_t)(descriptor[0] | ((uint16_t)descriptor[1] << 8));
  if (p.nmi == 0x8f9c || p.nmi == 0x9137 || p.nmi == 0x9196)
    p.owner = kCoopCeilingLight;
  else if (entrance < 0xe6u && p.nmi == 0x8ffe)
    p.owner = kCoopFog;
  else if (have_update && p.nmi == 0x9604 &&
           descriptor[2] == 0x02 && descriptor[3] == 0x9a)
    p.owner = kCoopShip;
  return p;
}

#endif
