#ifndef DKC1_COOP_RELATION_H
#define DKC1_COOP_RELATION_H
#include "dkc1_coop_actor.h"
/* Validate object identity before following a cartridge or host relation.
 * A link's protocol (reciprocity, state, shared passengers) remains at its
 * caller; none of these checks writes or repairs an old save state. */
enum Dkc1CoopRelationKind { kCoopAnimal, kCoopRope, kCoopSwing, kCoopCannon, kCoopCart };
static inline int Dkc1CoopRelationObject(const uint8_t *ram, uint16_t handle,
                                       enum Dkc1CoopRelationKind kind) {
  if (!ram || !Dkc1CoopEntityIsHandle(handle)) return 0;
  unsigned id = Dkc1Actor_SpriteIDLo(ram, handle);
  switch (kind) {
    case kCoopAnimal: return handle >= 6 && id >= 9 && id <= 12;
    case kCoopRope: return (id & 0xffu) == 0x30;
    case kCoopSwing: return (id & 0xffu) == 0x31;
    case kCoopCannon: return id == 0x38;
    case kCoopCart: return handle >= 6 && id == 0x51;
    default: return 0;
  }
}
static inline uint16_t Dkc1CoopRelationRider(const uint8_t *ram, uint16_t handle) {
  if (!ram || (handle != 2 && handle != 4)) return 0;
  unsigned id = Dkc1Actor_SpriteIDLo(ram, handle);
  return id == 1 || id == 2 ? handle : 0;
}

/* A seat link remains valid during the native mount/dismount handshake,
 * before or after the animal publishes its reciprocal rider. */
static inline uint16_t Dkc1CoopAnimalForSeat(const uint8_t *ram, unsigned seat) {
  if (!ram || seat >= 2u) return 0;
  uint16_t animal = Dkc1WramU16(ram, 0x0512u + seat * 2u);
  return Dkc1CoopRelationObject(ram, animal, kCoopAnimal) ? animal : 0;
}

/* Palette roles and room carry require an established reciprocal mount,
 * unlike the weaker seat link used by native entry/dismount continuations. */
static inline uint16_t Dkc1CoopReciprocalAnimalForSeat(const uint8_t *ram,
                                                   unsigned seat,
                                                   uint16_t rider) {
  if (!rider) return 0;
  uint16_t animal = Dkc1CoopAnimalForSeat(ram, seat);
  return animal && Dkc1Actor_RAMTable1375Lo(ram, animal) == rider ? animal : 0;
}

static inline uint16_t Dkc1CoopCartDriver(const uint8_t *ram, uint16_t cart) {
  if (!Dkc1CoopRelationObject(ram, cart, kCoopCart)) return 0;
  uint16_t rider = Dkc1Actor_RAMTable1375Lo(ram, cart);
  return Dkc1CoopRelationRider(ram, rider) &&
      Dkc1Actor_RAMTable1029Lo(ram, rider) == 0x2eu ? rider : 0;
}

/* The rear passenger is native $0C, not a second driver. */
static inline int Dkc1CoopCartHasPassenger(const uint8_t *ram, uint16_t cart,
                                        uint16_t kong) {
  uint16_t driver = Dkc1CoopCartDriver(ram, cart);
  if (!driver) return 0;
  if (kong == driver) return 1;
  return Dkc1CoopRelationRider(ram, kong) && kong == (driver ^ 6u) &&
      Dkc1Actor_RAMTable1029Lo(ram, kong) == 0x0cu &&
      (Dkc1WramU16(ram, 0x0579u) & 1u) &&
      Dkc1Actor_RAMTable13E9Lo(ram, cart) == kong;
}

/* Valid only during this cannon's dispatch/completion scope. A one-use
 * cannon can already be removed, so identity belongs to its saved rider. */
static inline uint16_t Dkc1CoopRegisteredCannonRider(const uint8_t *ram,
                                                  uint16_t cannon) {
  return Dkc1CoopRelationRider(ram, Dkc1Actor_RAMTable152DLo(ram, cannon));
}
#endif
