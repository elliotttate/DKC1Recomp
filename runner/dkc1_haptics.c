#include "dkc1_haptics.h"

#include <stddef.h>

enum {
  kPlayerSlot = 0x0082,
  kActorId = 0x0D45,
  kVerticalVelocity = 0x0EF1,
  kActorState = 0x1029,
  kMaximumActorSlot = 0x004E,
  kDkStompVelocity = 0x0720,
  kDiddyStompVelocity = 0x0880,
};

static uint16_t Read16(const uint8_t *wram, size_t address) {
  return (uint16_t)(wram[address] | ((uint16_t)wram[address + 1] << 8));
}

static bool IsPlayerActor(uint16_t actor_id) {
  return actor_id == 1 || actor_id == 2;
}

void Dkc1StompProbeCapture(Dkc1StompProbe *probe, const uint8_t *wram) {
  if (!probe)
    return;
  *probe = (Dkc1StompProbe){0};
  if (!wram)
    return;

  const uint8_t slot = wram[kPlayerSlot];
  if ((slot & 1) || slot < 2 || slot > kMaximumActorSlot)
    return;
  const uint16_t actor_id = Read16(wram, kActorId + slot);
  if (!IsPlayerActor(actor_id))
    return;

  probe->player_slot = slot;
  probe->actor_id = actor_id;
  probe->actor_state = Read16(wram, kActorState + slot);
  probe->vertical_velocity = (int16_t)Read16(
      wram, kVerticalVelocity + slot);
  probe->valid = true;
}

bool Dkc1StompProbeAccepted(const Dkc1StompProbe *probe,
                            const uint8_t *wram) {
  if (!probe || !probe->valid || !wram || probe->actor_state != 1 ||
      probe->vertical_velocity >= 0)
    return false;

  const uint8_t slot = wram[kPlayerSlot];
  if (slot != probe->player_slot ||
      Read16(wram, kActorId + slot) != probe->actor_id ||
      Read16(wram, kActorState + slot) != 1)
    return false;

  const uint16_t rebound = Read16(wram, kVerticalVelocity + slot);
  const uint16_t expected =
      slot == 2 ? kDkStompVelocity : kDiddyStompVelocity;
  return rebound == expected;
}
