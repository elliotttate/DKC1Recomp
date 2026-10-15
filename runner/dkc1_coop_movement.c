#include "dkc1_coop_movement.h"

#include "dkc1_coop_actor.h"
#include "dkc1_coop_mod.h"

#include <stdint.h>

uint16_t Dkc1CoopPackedToSnes(uint32_t packed) {
  static const uint16_t snes[12] = {
      0x8000u, 0x4000u, 0x2000u, 0x1000u, 0x0800u, 0x0400u,
      0x0200u, 0x0100u, 0x0080u, 0x0040u, 0x0020u, 0x0010u};
  uint16_t out = 0;
  int bit;
  for (bit = 0; bit < 12; bit++) {
    if (packed & (UINT32_C(1) << bit))
      out = (uint16_t)(out | snes[bit]);
  }
  return out;
}

void Dkc1CoopCacheKongSlots(const uint8_t *wram, uint32_t slot[2]) {
  uint32_t i;
  if (!slot)
    return;
  slot[0] = slot[1] = 0;
  if (!wram)
    return;
  for (i = DKC1_ACTOR_SLOT_FIRST; Dkc1CoopEntityIsHandle((uint16_t)i);
       i = Dkc1CoopEntityNextHandle((uint16_t)i)) {
    uint16_t id = (uint16_t)(Dkc1Actor_SpriteIDLo(wram, i) & 0xFFu);
    if (id == kDkc1CoopSpriteDonkey && !slot[0])
      slot[0] = i;
    else if (id == kDkc1CoopSpriteDiddy && !slot[1])
      slot[1] = i;
    if (slot[0] && slot[1])
      return;
  }
}

int Dkc1CoopInLevel(const uint8_t *wram) {
  uint16_t lo, hi;
  if (!wram)
    return 0;
  lo = Dkc1WramU16(wram, 0x1B23u);
  hi = Dkc1WramU16(wram, 0x1B25u);
  return hi > lo && hi >= 0x100u;
}

/* Boss arenas can lock camera limits during combat and victory. This is
 * gameplay admission only: equal bounds must not enable extra streaming. */
int Dkc1CoopFixedBossRoom(const uint8_t *wram) {
  if (!wram) return 0;
  uint16_t entrance = Dkc1WramU16(wram, 0x3eu);
  uint16_t lower = Dkc1WramU16(wram, 0x1b23u);
  uint16_t mode = Dkc1WramU16(wram, 0x32u);
  /* K. Rool's ship uses mode0D and the same equal-limit arena. Its
   * reward releases1929, so scene admission uses the live actors below,
   * rather than that transient camera/control flag. */
  int ship = mode == 0x0du && entrance == 0x68u;
  if (!(((mode == 0x0cu && entrance >= 0xe0u && entrance < 0xe6u) || ship) &&
      lower == Dkc1WramU16(wram, 0x1b25u) &&
      Dkc1WramU16(wram, 0x4au) >= 224u &&
      (Dkc1Actor_SpriteIDLo(wram, 2) == 1 ||
       Dkc1Actor_SpriteIDLo(wram, 4) == 2))) return 0;
  if (lower && !ship) return 1;
  /* B6FC74 locks the arena to the current camera, including X=0.
   * At zero (and for the ship), require a live boss scene instead of an empty map
   * with retained mode/entrance words. The map avatar owns slot zero. */
  if (Dkc1Actor_SpriteIDLo(wram, 0) == 0x10u) return 0;
  for (uint16_t h = 2; Dkc1CoopEntityIsHandle(h); h = Dkc1CoopEntityNextHandle(h)) {
    uint16_t id = Dkc1Actor_SpriteIDLo(wram, h);
    if (id == 0x70u || id == 0x74u || id == 0x6cu) return 1;
  }
  return 0;
}

unsigned Dkc1CoopSceneCapabilities(const uint8_t *ram) {
  if (!ram) return 0;
  if (Dkc1CoopInLevel(ram))
    return kDkc1CoopSceneControl | kDkc1CoopScenePresentation |
           kDkc1CoopSceneScanner | kDkc1CoopSceneCamera;
  if (Dkc1CoopFixedBossRoom(ram))
    return kDkc1CoopSceneControl | kDkc1CoopScenePresentation |
           kDkc1CoopSceneCamera | kDkc1CoopSceneFixed;
  unsigned mode = Dkc1WramU16(ram, 0x32);
  unsigned lo = Dkc1WramU16(ram, 0x1b23);
  /* Sprite77 runs the native cast program (BF835F -> 81D307). Its Kongs
   * are parked, while the displayed enemies still own virtual palettes.
   * This is a scripted picture, not a playable bonus or widened scanner. */
  if ((mode == 1 || mode == 0x0d) && (Dkc1WramU16(ram,0x1e15) & 0x20u) &&
      lo >= 0x100u && lo == Dkc1WramU16(ram,0x1b25) &&
      Dkc1WramU16(ram,0x4a) >= 224u && Dkc1Actor_SpriteIDLo(ram,0) != 0x10u) {
    for (uint16_t h=2;Dkc1CoopEntityIsHandle(h);h=Dkc1CoopEntityNextHandle(h))
      if (Dkc1Actor_SpriteIDLo(ram,h) == 0x77u)
        return kDkc1CoopScenePresentation | kDkc1CoopSceneFixed |
               kDkc1CoopSceneSingleView;
  }
  int native_bonus = mode == 4 || mode == 9 || mode == 0x0a;
  int scripted_bonus = (mode == 1 || native_bonus || mode == 0x0b) &&
                  (Dkc1WramU16(ram, 0x1e15) & 0x20);
  if ((mode != 0 && !scripted_bonus) || Dkc1WramU16(ram, 0x3e) >= 0xe6 ||
      lo < 0x100 || lo != Dkc1WramU16(ram, 0x1b25) ||
      Dkc1WramU16(ram, 0x4a) < 224 ||
      (Dkc1Actor_SpriteIDLo(ram, 2) != 1 && Dkc1Actor_SpriteIDLo(ram, 4) != 2))
    return 0;
  /* Free-moving fixed bonuses share the native room view and scanner.
   * Other scripted minigames retain their cartridge input sequence. */
  return kDkc1CoopScenePresentation | kDkc1CoopSceneFixed |
      (((mode == 1 || mode == 0x0a) && scripted_bonus) ? kDkc1CoopSceneControl : 0u) |
      (native_bonus ? kDkc1CoopSceneNativeView : 0u);
}

int Dkc1CoopKongDeathPose(uint16_t state, int16_t velocity) {
  int speed=velocity;
  if (speed<0) speed=-speed;
  return state==0x09u || state==0x0du || state==0x19u ||
         state==0x39u || state==0x3au || (state==0x0cu && speed>=0x80);
}

int Dkc1CoopKongRole(const uint8_t *wram, uint32_t slot) {
  uint16_t state;
  int vx;
  if (!wram || !slot)
    return kDkc1CoopRoleGone;
  state = Dkc1Actor_RAMTable1029Lo(wram, slot);
  /* A live $13E9 cart passenger uses stock $0C, not DK-barrel waiting. */
  if (state == 0x0c && Dkc1CoopMinecartForKong(wram, (uint16_t)slot))
    return kDkc1CoopRoleFollow;
  vx = (int16_t)Dkc1Actor_SprXVelocity(wram, slot);
  if (vx < 0)
    vx = -vx;
  if (state == 0x53u || state == 0x56u)
    return kDkc1CoopRoleLost;
  /* $0C is DK-barrel wait when slow and the death-run when fast.
   * $23 is the pop. $18 is Donkey lifting a barrel at his own X, not
   * a Kong parked in the DK barrel, so it stays playable. */
  if (state == 0x23u || (state == 0x0Cu && vx < 0x80))
    return kDkc1CoopRoleBarrel;
  /* $1A is the barrel toss. Treating it as a death hands $056F to the
   * partner and the camera leaves the Kong who is still holding it.
   * Real handoffs stay on $09/$0D/$19, water sink $39/$3A, and a fast $0C. */
  if (Dkc1CoopKongDeathPose(state,(int16_t)vx))
    return kDkc1CoopRoleDying;
  if (state == 0x0Au || state == 0x0Eu)
    return kDkc1CoopRoleTagPull;
  if (state == 5u)
    return kDkc1CoopRoleFollow;
  return kDkc1CoopRolePlayable;
}

int Dkc1CoopKongLost(const uint8_t *wram, uint32_t slot) {
  int role = Dkc1CoopKongRole(wram, slot);
  return role == kDkc1CoopRoleGone || role == kDkc1CoopRoleLost;
}

int Dkc1CoopKongInBarrel(const uint8_t *wram, uint32_t slot) {
  return Dkc1CoopKongRole(wram, slot) == kDkc1CoopRoleBarrel;
}

int Dkc1CoopKongCanParticipate(const uint8_t *wram, uint32_t slot) {
  /* BF:A6E6 starts the native final-fall timer in $3B. That role remains
   * native-owned and is excluded from these co-op operations. */
  if (wram && slot && Dkc1Actor_RAMTable1029Lo(wram, slot) == 0x3Bu)
    return 0;
  int role = Dkc1CoopKongRole(wram, slot);
  return role == kDkc1CoopRolePlayable || role == kDkc1CoopRoleFollow ||
         role == kDkc1CoopRoleTagPull;
}

int Dkc1CoopKongCam(const uint8_t *wram, uint32_t slot) {
  /* Camera targets share live admission; the final-fall view stays frozen
   * throughout the fade instead of following off-level corpse coordinates. */
  return Dkc1CoopKongCanParticipate(wram, slot);
}

int Dkc1CoopKongOwnCamera(const uint8_t *wram, uint32_t slot) {
  if (!Dkc1CoopKongCam(wram, slot)) return 0;
  if (Dkc1CoopMinecartForKong(wram, (uint16_t)slot)) return 0;
  /* These cartridge entry/exit poses complete by crossing a fixed screen
   * edge. Moving that edge with the actor makes the script non-terminating. */
  switch (Dkc1Actor_RAMTable1029Lo(wram, slot)) {
    case 0x1d: case 0x2a: case 0x31: case 0x3d: case 0x41: case 0x4f:
      return 0;
    default: return 1;
  }
}

int Dkc1CoopCoordsInView(const uint8_t *wram, unsigned x, unsigned y) {
  unsigned cx, cy, dx, dy;
  if (!wram)
    return 0;
  cx = Dkc1WramU16(wram, DKC1_WRAM_CameraX);
  cy = Dkc1WramU16(wram, DKC1_WRAM_CameraY);
  if (x >= cx)
    dx = x - cx;
  else
    dx = cx - x;
  if (y >= cy)
    dy = y - cy;
  else
    dy = cy - y;
  if (x < cx && dx > 64u)
    return 0;
  if (x >= cx && dx > 256u + 64u)
    return 0;
  if (y < cy && dy > 64u)
    return 0;
  if (y >= cy && dy > 224u + 64u)
    return 0;
  return 1;
}

int Dkc1CoopKongInView(const uint8_t *wram, uint32_t slot) {
  if (!Dkc1CoopKongCam(wram, slot))
    return 0;
  return Dkc1CoopCoordsInView(wram, Dkc1Actor_SprXPos(wram, slot),
                              Dkc1Actor_SprYPos(wram, slot));
}

int Dkc1CoopWorldSkip(uint16_t sprite_id) {
  /* Camera / HUD / script-only sprites must not steal $056F. Indexed so a
   * new id is one byte, not another linear scan. The $70 world boss is an
   * enemy, and  is a prize barrel: their native collision scans must
   * see the nearest live Kong. The  bonus controller remains global. */
  static const uint8_t skip[256] = {
      [0x07] = 1, [0x17] = 1, [0x32] = 1, [0x35] = 1, [0x36] = 1, [0x3B] = 1,
      [0x3C] = 1, [0x43] = 1, [0x48] = 1, [0x4C] = 1, [0x52] = 1, [0x5D] = 1,
      [0x64] = 1, [0x65] = 1, [0x66] = 1, [0x67] = 1, [0x69] = 1, [0x6C] = 1,
      [0x6E] = 1, [0x72] = 1, [0x73] = 1, [0x77] = 1};
  return sprite_id < 256u && skip[sprite_id];
}

static unsigned Dkc1CoopAxisDistance(uint16_t a, uint16_t b) {
  return a >= b ? (unsigned)(a - b) : (unsigned)(b - a);
}

static int Dkc1CoopAnimalTickDistance(unsigned dx) {
  /* BFCADA keeps a free animal while the selected Kong is within
   * $200 horizontally. The crate bounce can already lift its opener
   * beyond the contact radius before the animal's first tick. */
  return dx < 0x200u ? (int)dx : -1;
}

static int Dkc1CoopRopeTickDistance(const uint8_t *wram, uint32_t obj,
                                  uint32_t kong, unsigned dx) {
  /* BF:EF32..3D accepts the vertical shaft above its base. A square
   * bind window would discard a reachable partner before native
   * BF:EF3F..4E gets to test its 12-pixel horizontal grab width. */
  if ((int16_t)(uint16_t)(Dkc1Actor_SprYPos(wram, kong) -
      Dkc1Actor_SprYPos(wram, obj) + 0x18u) < 0)
    return -1;
  return dx < kDkc1CoopBindReach ? (int)dx : -1;
}

static int Dkc1CoopContactTickDistance(unsigned dx, unsigned dy) {
  unsigned distance = dx > dy ? dx : dy;
  return distance < kDkc1CoopBindReach ? (int)distance : -1;
}

uint16_t Dkc1CoopKongForActorTick(const uint8_t *wram, uint32_t obj,
                                const uint32_t slot[2], uint16_t lock) {
  static const uint16_t kKong[2] = {kDkc1CoopSpriteDonkey,
                                    kDkc1CoopSpriteDiddy};
  uint16_t best = 0;
  int best_d = 0, i;
  if (!wram || !slot)
    return 0;
  for (i = 0; i < 2; i++) {
    uint32_t ks = slot[i];
    unsigned dx;
    int d;
    if (!Dkc1CoopKongCanParticipate(wram, ks))
      continue;
    if (lock && kKong[i] != lock)
      continue;
    dx = Dkc1CoopAxisDistance(Dkc1Actor_SprXPos(wram, obj),
                              Dkc1Actor_SprXPos(wram, ks));
    const uint16_t id = Dkc1Actor_SpriteIDLo(wram, obj) & 0xffu;
    if (id >= 9u && id <= 12u) {
      d = Dkc1CoopAnimalTickDistance(dx);
    } else if (id == 0x30u) {
      d = Dkc1CoopRopeTickDistance(wram, obj, ks, dx);
    } else {
      unsigned dy = Dkc1CoopAxisDistance(Dkc1Actor_SprYPos(wram, obj),
                                       Dkc1Actor_SprYPos(wram, ks));
      d = Dkc1CoopContactTickDistance(dx, dy);
    }
    if (d < 0)
      continue;
    if (!best || d < best_d) {
      best = kKong[i];
      best_d = d;
    }
  }
  return best;
}
