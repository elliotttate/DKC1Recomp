#include "dkc1_coop_movement.h"
#include "desktop_input.h"
#include "dkc1_wram_gen.h"
#include <assert.h>
#include <stdint.h>
#include <string.h>

/* This fixture covers movement outside carts; the full runtime fixture
 * exercises the source-backed shared-cart bindings. */
uint16_t Dkc1CoopMinecartForKong(const uint8_t *wram, uint16_t kong) {
  (void)wram; (void)kong; return 0;
}
static uint8_t ram[0x20000];

static void characterize_actor_tick_selection(void) {
  static const uint16_t ids[] = {0x16, 9, 10, 11, 12, 0x30};
  static const struct { uint16_t state, velocity; int admitted; } poses[] = {
    {0, 0, 1}, {5, 0, 1}, {0x0a, 0, 1}, {0x0e, 0, 1}, {0x18, 0, 1},
    {0x1d, 0, 1}, {0x2a, 0, 1}, {0x31, 0, 1}, {0x3d, 0, 1},
    {0x41, 0, 1}, {0x4f, 0, 1},
    {0x09, 0, 0}, {0x0d, 0, 0}, {0x19, 0, 0}, {0x39, 0, 0},
    {0x3a, 0, 0}, {0x3b, 0, 0}, {0x53, 0, 0}, {0x56, 0, 0},
    {0x23, 0, 0}, {0x0c, 0x7f, 0}, {0x0c, 0x80, 0},
    {0x0c, 0xff80, 0}
  };
  const uint32_t slots[2] = {2, 4};
  memset(ram, 0, sizeof ram);
  Dkc1Actor_SetSpriteIDLo(ram, 2, 1);
  Dkc1Actor_SetSpriteIDLo(ram, 4, 2);
  Dkc1Actor_SetSprXPos(ram, 6, 0x8000);
  Dkc1Actor_SetSprYPos(ram, 6, 0x8000);
  for (unsigned n = 0; n < sizeof ids / sizeof ids[0]; ++n) {
    uint16_t id = ids[n];
    unsigned reach = id >= 9 && id <= 12 ? 512 : 64;
    Dkc1Actor_SetSpriteIDLo(ram, 6, id);
    for (unsigned seat = 0; seat < 2; ++seat) {
      uint32_t kong = slots[seat], other = slots[seat ^ 1u];
      uint16_t owner = (uint16_t)(seat + 1u);
      Dkc1Actor_SetRAMTable1029Lo(ram, other, 0x3b);
      Dkc1Actor_SetSprXPos(ram, kong, 0x8000 + reach - 1);
      Dkc1Actor_SetSprYPos(ram, kong, 0x8000);
      for (unsigned p = 0; p < sizeof poses / sizeof poses[0]; ++p) {
        Dkc1Actor_SetRAMTable1029Lo(ram, kong, poses[p].state);
        Dkc1Actor_SetSprXVelocity(ram, kong, poses[p].velocity);
        assert(Dkc1CoopKongCanParticipate(ram, kong) == poses[p].admitted);
        assert(Dkc1CoopKongCam(ram, kong) == poses[p].admitted);
        assert(Dkc1CoopKongForActorTick(ram, 6, slots, 0) ==
               (poses[p].admitted ? owner : 0));
      }
      Dkc1Actor_SetRAMTable1029Lo(ram, kong, 0);
      assert(Dkc1CoopKongForActorTick(ram, 6, slots, owner) == owner);
      assert(!Dkc1CoopKongForActorTick(ram, 6, slots, owner ^ 3u));
      assert(!Dkc1CoopKongForActorTick(ram, 6, slots, 3));
      Dkc1Actor_SetSprXPos(ram, kong, 0x8000 + reach);
      assert(!Dkc1CoopKongForActorTick(ram, 6, slots, 0));
      Dkc1Actor_SetSprXPos(ram, kong, 0x8000 - reach + 1);
      assert(Dkc1CoopKongForActorTick(ram, 6, slots, 0) == owner);
      Dkc1Actor_SetSprXPos(ram, kong, 0x8000 - reach);
      assert(!Dkc1CoopKongForActorTick(ram, 6, slots, 0));
    }
    /* Both metric ties and the explicit owner lock retain seat order. */
    for (unsigned seat = 0; seat < 2; ++seat) {
      Dkc1Actor_SetRAMTable1029Lo(ram, slots[seat], 0);
      Dkc1Actor_SetSprXPos(ram, slots[seat], seat ? 0x800b : 0x7ff5);
      Dkc1Actor_SetSprYPos(ram, slots[seat], 0x8000);
    }
    assert(Dkc1CoopKongForActorTick(ram, 6, slots, 0) == 1);
    assert(Dkc1CoopKongForActorTick(ram, 6, slots, 2) == 2);
    Dkc1Actor_SetSprXPos(ram, 4, 0x800a);
    assert(Dkc1CoopKongForActorTick(ram, 6, slots, 0) == 2);
  }
  for (unsigned seat = 0; seat < 2; ++seat) {
    uint32_t kong = slots[seat];
    uint16_t owner = (uint16_t)(seat + 1u);
    Dkc1Actor_SetRAMTable1029Lo(ram, slots[seat ^ 1u], 0x3b);
    Dkc1Actor_SetRAMTable1029Lo(ram, kong, 0);
    Dkc1Actor_SetSprXPos(ram, kong, 0x803f);
    Dkc1Actor_SetSpriteIDLo(ram, 6, 0x16);
    Dkc1Actor_SetSprYPos(ram, kong, 0x803f);
    assert(Dkc1CoopKongForActorTick(ram, 6, slots, 0) == owner);
    Dkc1Actor_SetSprYPos(ram, kong, 0x8040);
    assert(!Dkc1CoopKongForActorTick(ram, 6, slots, 0));
    Dkc1Actor_SetSprYPos(ram, kong, 0x7fc0);
    assert(!Dkc1CoopKongForActorTick(ram, 6, slots, 0));
    Dkc1Actor_SetSprYPos(ram, kong, 0x7fc1);
    assert(Dkc1CoopKongForActorTick(ram, 6, slots, 0) == owner);
    /* Rope routing keeps the native signed 16-bit lower-edge expression,
     * and the 64-pixel horizontal routing window preceding native contact. */
    Dkc1Actor_SetSpriteIDLo(ram, 6, 0x130);
    Dkc1Actor_SetSprYPos(ram, kong, 0x7fe8);
    assert(Dkc1CoopKongForActorTick(ram, 6, slots, 0) == owner);
    Dkc1Actor_SetSprYPos(ram, kong, 0x7fe7);
    assert(!Dkc1CoopKongForActorTick(ram, 6, slots, 0));
    Dkc1Actor_SetSprYPos(ram, kong, 0x0000);
    assert(!Dkc1CoopKongForActorTick(ram, 6, slots, 0));
    Dkc1Actor_SetSprYPos(ram, 6, 0x0018);
    assert(Dkc1CoopKongForActorTick(ram, 6, slots, 0) == owner);
    Dkc1Actor_SetSprYPos(ram, kong, 0xffff);
    assert(!Dkc1CoopKongForActorTick(ram, 6, slots, 0));
    Dkc1Actor_SetSprYPos(ram, 6, 0x8000);
  }
  assert(!Dkc1CoopKongForActorTick(NULL, 6, slots, 0));
  assert(!Dkc1CoopKongForActorTick(ram, 6, NULL, 0));
  { const uint32_t absent[2] = {0, 0};
    assert(!Dkc1CoopKongForActorTick(ram, 6, absent, 0)); }
}

int main(void) {
  characterize_actor_tick_selection();
  memset(ram, 0, sizeof ram);
  const uint32_t dk = 0x02u;
  const uint32_t diddy = 0x04u;
  Dkc1Actor_SetSpriteIDLo(ram, dk, 1);
  Dkc1Actor_SetSpriteIDLo(ram, diddy, 2);
  Dkc1Actor_SetSprXPos(ram, dk, 0x8000);
  Dkc1Actor_SetSprYPos(ram, dk, 0x8000);
  Dkc1Actor_SetSprXPos(ram, diddy, 0x8040);
  Dkc1Actor_SetSprYPos(ram, diddy, 0x8000);
  {
    uint32_t slots[2];
    Dkc1CoopCacheKongSlots(ram, slots);
    if (slots[0] != dk) return 2;
    if (slots[1] != diddy) return 3;
    Dkc1Actor_SetSprXPos(ram, 0x06u, 0x8010);
    Dkc1Actor_SetSprYPos(ram, 0x06u, 0x8000);
    if (Dkc1CoopKongForActorTick(ram, 0x06u, slots, 0) != 1) return 4;
    Dkc1Actor_SetSprXPos(ram, 0x06u, 0x8000);
    Dkc1Actor_SetSprYPos(ram, 0x06u, 0x8030);
    if (Dkc1CoopKongForActorTick(ram, 0x06u, slots, 0) != 1) return 5;
    Dkc1Actor_SetSprYPos(ram, 0x06u, 0x8080);
    if (Dkc1CoopKongForActorTick(ram, 0x06u, slots, 0) != 0) return 6;
  }
  if (Dkc1CoopPackedToSnes(0x81) != 0x8100) return 7;
  /* Animals use the cartridge's horizontal lifetime interval, including
   * the crate opener already bouncing high above its newly spawned animal. */
  for (unsigned id = 9; id <= 12; ++id) {
    uint32_t slots[2] = {dk, diddy};
    Dkc1Actor_SetSpriteIDLo(ram, 6, id);
    for (uint32_t rider = dk; rider <= diddy; rider += 2) {
      uint32_t other = rider ^ 6u;
      Dkc1Actor_SetSprXPos(ram, 6, 0x45e1);
      Dkc1Actor_SetSprYPos(ram, 6, 0x12f);
      Dkc1Actor_SetSprXPos(ram, other, 0x3c1a);
      Dkc1Actor_SetSprXPos(ram, rider, 0x45e0);
      Dkc1Actor_SetSprYPos(ram, rider, 0x1a6);
      unsigned owner = rider / 2;
      if (Dkc1CoopKongForActorTick(ram, 6, slots, 0) != owner) return 95;
      if (Dkc1CoopKongForActorTick(ram, 6, slots, owner ^ 3u)) return 96;
      Dkc1Actor_SetSprXPos(ram, rider, 0x43e2);
      if (Dkc1CoopKongForActorTick(ram, 6, slots, 0) != owner) return 97;
      Dkc1Actor_SetSprXPos(ram, rider, 0x43e1);
      if (Dkc1CoopKongForActorTick(ram, 6, slots, 0)) return 98;
    }
  }
  /* Vertical ropes test the shaft, not a square around its base. Either
   * Kong above that base must remain eligible for the native grab test. */
  for (uint32_t rider = dk; rider <= diddy; rider += 2) {
    uint32_t other = rider == dk ? diddy : dk;
    uint32_t slots[2] = {dk, diddy};
    Dkc1Actor_SetSpriteIDLo(ram, 6, 0x30);
    Dkc1Actor_SetSprXPos(ram, 6, 0x8100);
    Dkc1Actor_SetSprYPos(ram, 6, 0x006c);
    Dkc1Actor_SetSprXPos(ram, other, 0x8400);
    Dkc1Actor_SetSprXPos(ram, rider, 0x810b);
    Dkc1Actor_SetSprYPos(ram, rider, 0x00da);
    if (Dkc1CoopKongForActorTick(ram, 6, slots, 0) != (rider == dk ? 1 : 2)) return 90;
    if (Dkc1CoopKongForActorTick(ram, 6, slots, rider == dk ? 2 : 1)) return 91;
    Dkc1Actor_SetSprYPos(ram, rider, 0x0053);
    if (Dkc1CoopKongForActorTick(ram, 6, slots, 0)) return 92;
    Dkc1Actor_SetSprYPos(ram, rider, 0xffe8);
    if (Dkc1CoopKongForActorTick(ram, 6, slots, 0)) return 93;
    Dkc1Actor_SetSprYPos(ram, rider, 0x00da);
    Dkc1Actor_SetSpriteIDLo(ram, 6, 0x16);
    if (Dkc1CoopKongForActorTick(ram, 6, slots, 0)) return 94;
  }
  if (Dkc1CoopWorldSkip(0x5D) == 0) return 15;
  if (Dkc1CoopWorldSkip(0x16) != 0) return 17;
  if (Dkc1CoopWorldSkip(0x38) != 0) return 19;
  if (Dkc1CoopWorldSkip(0x70) != 0) return 22;
  if (Dkc1CoopWorldSkip(0x6C) == 0) return 23;
  Dkc1WramSetU16(ram, 0x1B23u, 0x0100);
  Dkc1WramSetU16(ram, 0x1B25u, 0x0200);
  if (Dkc1CoopInLevel(ram) == 0) return 20;
  Dkc1WramSetU16(ram, 0x1B25u, 0x0080);
  if (Dkc1CoopInLevel(ram) != 0) return 21;
  Dkc1Actor_SetRAMTable1029Lo(ram, dk, 10);
  if (Dkc1CoopKongRole(ram, dk) != kDkc1CoopRoleTagPull) return 60;
  if (Dkc1CoopKongRole(ram, dk) == kDkc1CoopRoleDying) return 55;
  Dkc1Actor_SetRAMTable1029Lo(ram, dk, 5);
  if (Dkc1CoopKongRole(ram, dk) != kDkc1CoopRoleFollow) return 61;
  Dkc1Actor_SetRAMTable1029Lo(ram, dk, 0x56);
  if (Dkc1CoopKongLost(ram, dk) == 0) return 32;
  if (Dkc1CoopKongInBarrel(ram, dk) != 0) return 36;
  Dkc1Actor_SetRAMTable1029Lo(ram, dk, 0x19);
  if (Dkc1CoopKongCam(ram, dk) != 0) return 33;
  if (Dkc1CoopKongRole(ram, dk) != kDkc1CoopRoleDying) return 35;
  Dkc1Actor_SetRAMTable1029Lo(ram, dk, 0x09);
  if (Dkc1CoopKongRole(ram, dk) != kDkc1CoopRoleDying) return 53;
  Dkc1Actor_SetRAMTable1029Lo(ram, dk, 0x0D);
  if (Dkc1CoopKongRole(ram, dk) != kDkc1CoopRoleDying) return 54;
  Dkc1Actor_SetRAMTable1029Lo(ram, dk, 0x3A);
  if (Dkc1CoopKongCam(ram, dk) != 0) return 83;
  if (Dkc1CoopKongRole(ram, dk) != kDkc1CoopRoleDying) return 84;
  Dkc1Actor_SetRAMTable1029Lo(ram, dk, 0x39);
  if (Dkc1CoopKongCam(ram, dk) != 0) return 85;
  if (Dkc1CoopKongRole(ram, dk) != kDkc1CoopRoleDying) return 86;
  for (uint32_t dead = dk; dead <= diddy; dead += 2) {
    Dkc1Actor_SetRAMTable1029Lo(ram, dead, 0x3B);
    if (Dkc1CoopKongCam(ram, dead)) return 88;
    if (Dkc1CoopKongOwnCamera(ram, dead)) return 89;
  }
  Dkc1Actor_SetRAMTable1029Lo(ram, dk, 0x0C);
  if (Dkc1CoopKongCam(ram, dk) != 0) return 56;
  if (Dkc1CoopKongInBarrel(ram, dk) == 0) return 57;
  if (Dkc1CoopKongRole(ram, dk) != kDkc1CoopRoleBarrel) return 62;
  Dkc1Actor_SetRAMTable1029Lo(ram, dk, 0x23);
  if (Dkc1CoopKongRole(ram, dk) != kDkc1CoopRoleBarrel) return 63;
  Dkc1Actor_SetRAMTable1029Lo(ram, dk, 0x18);
  if (Dkc1CoopKongInBarrel(ram, dk) != 0) return 58;
  if (Dkc1CoopKongRole(ram, dk) != kDkc1CoopRolePlayable) return 59;
  if (!Dkc1CoopKongOwnCamera(ram, dk)) return 80;
  {
    static const uint16_t fixed_edge_states[] = {0x1d, 0x2a, 0x31, 0x3d, 0x41, 0x4f};
    for (unsigned i = 0; i < sizeof fixed_edge_states / sizeof fixed_edge_states[0]; i++) {
      Dkc1Actor_SetRAMTable1029Lo(ram, dk, fixed_edge_states[i]);
      if (!Dkc1CoopKongCam(ram, dk)) return 81;
      if (Dkc1CoopKongOwnCamera(ram, dk)) return 82;
    }
    Dkc1Actor_SetRAMTable1029Lo(ram, dk, 0x09);
    if (Dkc1CoopKongOwnCamera(ram, dk)) return 83;
  }
  Dkc1Actor_SetRAMTable1029Lo(ram, dk, 0);
  Dkc1Actor_SetRAMTable1029Lo(ram, diddy, 0);
  Dkc1WramSetU16(ram, 0x088Bu, 0x0400);
  Dkc1WramSetU16(ram, 0x0895u, 0x00A0);
  Dkc1Actor_SetSprXPos(ram, dk, 0x0480);
  Dkc1Actor_SetSprYPos(ram, dk, 0x00B0);
  Dkc1Actor_SetSprXPos(ram, diddy, 0x0100);
  Dkc1Actor_SetSprYPos(ram, diddy, 0x00B0);
  if (Dkc1CoopKongInView(ram, dk) == 0) return 70;
  if (Dkc1CoopKongInView(ram, diddy) != 0) return 71;
  Dkc1Actor_SetSprXPos(ram, diddy, 0x0450);
  if (Dkc1CoopKongInView(ram, diddy) == 0) return 72;
  if (Dkc1CoopCoordsInView(ram, 0x0100, 0x00B0) != 0) return 73;
  {
    int src[2] = {3, 2};
    Dkc1PrepareCoopSources(src, 1, 1);
    if (src[0] != 1) return 11;
    if (src[1] != 3) return 12;
  }
  return 0;
}
