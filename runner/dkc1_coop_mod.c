#include "dkc1_coop_mod.h"

#include "dkc1_coop_movement.h"
#include "dkc1_coop_graphics.h"
#include "dkc1_coop_formations.h"
#include "dkc1_coop_world.h"
#include "dkc1_video.h"
#include "dkc1_coop_actor.h"
#include "dkc1_coop_relation.h"
#include "ppu.h"
#include "cpu_state.h"
#include "common_rtl.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static const uint16_t kKong[2] = {kDkc1CoopSpriteDonkey, kDkc1CoopSpriteDiddy};

typedef struct CoopPoseRecovery {
  uint16_t y;
  uint8_t ticks, active;
} CoopPoseRecovery;

/* Canonical restoration target may advance during NMI while the borrowed
 * invocation keeps its initial words in wait. None of these structs is a wire
 * layout; the existing 20/64-byte and continuation codecs remain authoritative. */
typedef struct CoopCanonicalContext {
  uint16_t held[2], pressed[2], current_held, current_pressed;
  uint16_t follow[4], scene[2];
  int animal;
} CoopCanonicalContext;
typedef struct CoopCameraSubstitution {
  uint16_t values[5], requests, scene[2];
  int active;
} CoopCameraSubstitution;
typedef struct CoopFollowerSubstitution {
  uint32_t slot;
  uint16_t state, collision;
  int active;
} CoopFollowerSubstitution;
typedef struct CoopActorContext {
  CoopCanonicalContext canonical;
  CoopCameraSubstitution camera;
  CoopFollowerSubstitution follower;
  struct { uint16_t object, partner; } cannon;
  Dkc1CoopActorSnapshot wait;
  uint8_t wait_age;
  /* mux owns the invocation even while wait exposes canonical WRAM. */
  int captured, mux, mux_kong;
  uint16_t mux_id;
} CoopActorContext;

/* Runtime ownership only: legacy snapshot layouts have explicit codecs below. */
typedef struct CoopRuntimeState {
  int enabled;
  uint32_t host;
  uint16_t prev[2];
  uint16_t pressed[2];
  struct Ppu *ppu;
  CoopActorContext actor;
  Dkc1CoopRopeState rope;
  uint16_t squawks; /* handle<<4 | creator id<<2; snapshot v4 */
  uint16_t animal_bonus; /* collector ID, bit2: entered native animal bonus */
  /* Kritter wake-up borrows the partner camera only during native dispatch. */
  uint16_t kritter_cam;
  int kritter_cam_on;
  uint16_t lock;
  uint8_t corpse[2];
  unsigned barrel_hold;
  uint32_t slot[2];
  /* Room-transition intent survives the native actor-pool reset. */
  uint16_t room_mount;
  uint16_t carried_mount;
  Dkc1CoopWeatherState weather;
  CoopPoseRecovery pose[2];
} CoopRuntimeState;
static CoopRuntimeState s_runtime;

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
static int RegReadInt(const wchar_t *value, int fallback) {
  HKEY key;
  DWORD type = 0, data = 0, size = sizeof data;
  if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\DKC1Recomp\\Mods", 0,
                    KEY_READ, &key) != ERROR_SUCCESS)
    return fallback;
  LSTATUS st = RegQueryValueExW(key, value, NULL, &type, (BYTE *)&data, &size);
  RegCloseKey(key);
  return (st == ERROR_SUCCESS && type == REG_DWORD) ? (int)data : fallback;
}

static void RegWriteInt(const wchar_t *value, int data) {
  HKEY key;
  if (RegCreateKeyExW(HKEY_CURRENT_USER, L"Software\\DKC1Recomp\\Mods", 0, NULL,
                      REG_OPTION_NON_VOLATILE, KEY_WRITE, NULL, &key,
                      NULL) != ERROR_SUCCESS)
    return;
  DWORD v = (DWORD)data;
  RegSetValueExW(key, value, 0, REG_DWORD, (const BYTE *)&v, sizeof v);
  RegCloseKey(key);
}
#endif

static int EnvForcesCoop(int *enabled) {
  const char *env = getenv("DKC1_COOP");
  if (!env || !env[0])
    return 0;
  *enabled = env[0] != '0';
  return 1;
}

static int Seat(uint16_t id) { return id == kDkc1CoopSpriteDiddy; }

static int IsKong(uint16_t id) {
  return id == kDkc1CoopSpriteDonkey || id == kDkc1CoopSpriteDiddy;
}

static uint32_t SlotOf(uint16_t id) {
  if (id == kKong[0])
    return s_runtime.slot[0];
  if (id == kKong[1])
    return s_runtime.slot[1];
  return 0;
}

static uint16_t LeaderKong(const uint8_t *wram);
static int OverlayQuiet(const uint8_t *wram);
static int KongAnimalRide(const uint8_t *wram, unsigned seat);
static int AnimalRide(const uint8_t *wram);
static int KongSeatLink(uint16_t value);

static uint16_t Held(int pad) {
  return Dkc1CoopPackedToSnes((s_runtime.host >> (12 * pad)) & 0xfffu);
}

static void LatchPads(void) {
  int pad;
  for (pad = 0; pad < 2; pad++) {
    uint16_t held = Held(pad);
    s_runtime.pressed[pad] = (uint16_t)(held & (uint16_t)~s_runtime.prev[pad]);
    s_runtime.prev[pad] = held;
  }
}

/* Native $1375 drives the cart; $13E9 is its seated rear passenger. */
uint16_t Dkc1CoopMinecartForKong(const uint8_t *wram, uint16_t kong) {
  if (!s_runtime.enabled || !wram || (kong != 2 && kong != 4) ||
      Dkc1WramU16(wram, 0x32) != 5) return 0;
  uint16_t state = Dkc1Actor_RAMTable1029Lo(wram, kong);
  if (state != 0x2e && state != 0x0c) return 0;
  for (uint16_t h = 6; Dkc1CoopEntityIsHandle(h);
       h = Dkc1CoopEntityNextHandle(h)) {
    if (Dkc1CoopCartHasPassenger(wram, h, kong)) return h;
  }
  return 0;
}

/* Native airborne cart continuations retain the cart in Kong $13E9:
 * $2F is the launch; $40 is its free-flight continuation after cart release.
 * These remain airborne gameplay, not seated MinecartForKong ownership. */
static uint16_t MinecartAirDriver(const uint8_t *wram) {
  if (!s_runtime.enabled || !wram || Dkc1WramU16(wram,0x32)!=5) return 0;
  for (uint16_t kong=2;kong<=4;kong+=2) {
    uint16_t state=Dkc1Actor_RAMTable1029Lo(wram,kong);
    if (!Dkc1CoopRelationRider(wram,kong) || (state!=0x2fu && state!=0x40u)) continue;
    uint16_t cart=Dkc1Actor_RAMTable13E9Lo(wram,kong);
    if (Dkc1CoopRelationObject(wram,cart,kCoopCart) &&
        !Dkc1Actor_RAMTable1375Lo(wram,cart)) return kong;
  }
  return 0;
}

/* Presentation trip ownership outlives the seated gameplay relation. */
uint16_t Dkc1CoopMinecartViewOwner(const uint8_t *wram) {
  if (!s_runtime.enabled || !wram || Dkc1WramU16(wram,0x32)!=5) return 0;
  for (uint16_t kong=2;kong<=4;kong+=2) {
    uint16_t cart=Dkc1CoopMinecartForKong(wram,kong);
    if (cart) return Dkc1Actor_SpriteIDLo(wram,Dkc1Actor_RAMTable1375Lo(wram,cart));
  }
  uint16_t driver=MinecartAirDriver(wram);
  return driver ? Dkc1Actor_SpriteIDLo(wram,driver) : 0;
}

/* Convert only the synthetic second cart from the retired two-cart policy.
 * Native carts, dead riders and unrelated source-owned actors are untouched. */
static void JoinMinecartPassengers(uint8_t *wram) {
  if (!s_runtime.enabled || !wram || s_runtime.actor.wait.pending ||
      !(Dkc1WramU16(wram, 0x579) & 1u)) return;
  uint16_t first = Dkc1CoopMinecartForKong(wram, 2);
  uint16_t second = Dkc1CoopMinecartForKong(wram, 4);
  if (!first || !second) return;
  if (first == second) { s_runtime.barrel_hold = 0; return; }
  uint16_t cart = Dkc1WramU16(wram, 0x1e0f);
  if (cart != first && cart != second) return;
  uint16_t extra = cart == first ? second : first;
  if (extra < 0x80 || Dkc1CoopEntityRead16(wram, 0x15fd, extra) != 0xffff ||
      Dkc1Actor_RAMTable13E9Lo(wram, cart) ||
      Dkc1Actor_RAMTable13E9Lo(wram, extra)) return;
  uint16_t follower = Dkc1Actor_RAMTable1375Lo(wram, extra);
  /* The same rider reset as B69F6F; B6A01F positions the rear passenger. */
  const uint16_t clear[] = {0x116d, 0x1105, 0x1595, 0x0e89, 0x0f25, 0x0ef1, 0x12a5};
  for (unsigned i = 0; i < sizeof clear / sizeof clear[0]; i++)
    Dkc1CoopEntityWrite16(wram, clear[i], follower, 0);
  Dkc1Actor_SetRAMTable11A1Lo(wram, follower, 1);
  Dkc1Actor_SetRAMTable1029Lo(wram, follower, 0x0c);
  Dkc1Actor_SetRAMTable13E9Lo(wram, cart, follower);
  Dkc1Actor_SetRAMTable1375Lo(wram, extra, 0);
  Dkc1Actor_SetSpriteIDLo(wram, extra, 0);
  Dkc1CoopGraphicsResetEntity(extra);
  s_runtime.barrel_hold = 0;
}

static void PublishCannonPads(uint8_t *wram) {
  int i;
  if (!wram)
    return;
  if (!s_runtime.slot[0] && !s_runtime.slot[1])
    Dkc1CoopCacheKongSlots(wram, s_runtime.slot);
  for (i = 0; i < 2; i++) {
    uint16_t press = (uint16_t)(s_runtime.pressed[i] & 0xc000u);
    if (!s_runtime.slot[i] || !press)
      continue;
    Dkc1WramSetU16(wram, 0x16e9u + s_runtime.slot[i] - 2u, press);
  }
}

static const uint16_t kOverlayWords[16] = {
  DKC1_WRAM_JoypadHeld, DKC1_WRAM_Global_HeldButtonsLoP2,
  DKC1_WRAM_JoypadPressed, DKC1_WRAM_Global_PressedButtonsLoP2,
  DKC1_WRAM_Global_HeldButtonsLo, DKC1_WRAM_Global_PressedButtonsLo,
  0x0512, 0x0514, 0x0516, 0x0518, DKC1_WRAM_Player_CurrentKongLo,
  DKC1_WRAM_CameraX, DKC1_WRAM_CameraY, DKC1_WRAM_Level_CameraXPosLo,
  DKC1_WRAM_Level_CameraYPosLo, 0x1a69
};

static void CaptureCanonicalContext(uint8_t *wram) {
  unsigned i;
  s_runtime.actor.canonical.animal = AnimalRide(wram);
  s_runtime.actor.canonical.held[0] = Dkc1WramU16(wram, DKC1_WRAM_JoypadHeld);
  s_runtime.actor.canonical.held[1] = Dkc1WramU16(wram, DKC1_WRAM_Global_HeldButtonsLoP2);
  s_runtime.actor.canonical.pressed[0] = Dkc1WramU16(wram, DKC1_WRAM_JoypadPressed);
  s_runtime.actor.canonical.pressed[1] = Dkc1WramU16(wram, DKC1_WRAM_Global_PressedButtonsLoP2);
  s_runtime.actor.canonical.current_held = Dkc1WramU16(wram, DKC1_WRAM_Global_HeldButtonsLo);
  s_runtime.actor.canonical.current_pressed = Dkc1WramU16(wram, DKC1_WRAM_Global_PressedButtonsLo);
  for (i = 0; i < 4; i++)
    s_runtime.actor.canonical.follow[i] = Dkc1WramU16(wram, (uint32_t)(0x0512u + i * 2u));
  s_runtime.actor.canonical.scene[0] = Dkc1WramU16(wram, 0x32u);
  s_runtime.actor.canonical.scene[1] = Dkc1WramU16(wram, 0x3eu);
}

static void CaptureActorCamera(const uint8_t *wram) {
  CoopCameraSubstitution *camera = &s_runtime.actor.camera;
  for (unsigned i = 0; i < 5; ++i)
    camera->values[i] = Dkc1WramU16(wram, kOverlayWords[11 + i]);
  camera->requests = Dkc1WramU16(wram, 0x0579u) & 0x88u;
  camera->scene[0] = Dkc1WramU16(wram, 0x32u);
  camera->scene[1] = Dkc1WramU16(wram, 0x3eu);
  camera->active = 1;
}

static int HeldCameraMatchesScene(const uint8_t *wram) {
  return s_runtime.actor.camera.active &&
      s_runtime.actor.camera.scene[0] == Dkc1WramU16(wram, 0x32u) &&
      s_runtime.actor.camera.scene[1] == Dkc1WramU16(wram, 0x3eu);
}

static int ControlScene(const uint8_t *wram) {
  return (Dkc1CoopSceneCapabilities(wram) & kDkc1CoopSceneControl) != 0;
}

static void RestoreCanonicalContext(uint8_t *wram) {
  unsigned i;
  if (s_runtime.actor.mux_kong)
    Dkc1CoopWorldNoteKongCamera(wram,(uint16_t)SlotOf(s_runtime.actor.mux_id));
  Dkc1WramSetU16(wram, DKC1_WRAM_JoypadHeld, s_runtime.actor.canonical.held[0]);
  Dkc1WramSetU16(wram, DKC1_WRAM_Global_HeldButtonsLoP2, s_runtime.actor.canonical.held[1]);
  Dkc1WramSetU16(wram, DKC1_WRAM_JoypadPressed, s_runtime.actor.canonical.pressed[0]);
  Dkc1WramSetU16(wram, DKC1_WRAM_Global_PressedButtonsLoP2, s_runtime.actor.canonical.pressed[1]);
  Dkc1WramSetU16(wram, DKC1_WRAM_Global_HeldButtonsLo, s_runtime.actor.canonical.current_held);
  Dkc1WramSetU16(wram, DKC1_WRAM_Global_PressedButtonsLo, s_runtime.actor.canonical.current_pressed);
  const int same_canonical_scene =
      s_runtime.actor.canonical.scene[0] == Dkc1WramU16(wram, 0x32u) &&
      s_runtime.actor.canonical.scene[1] == Dkc1WramU16(wram, 0x3eu);
  /* A native dismount may clear the animal link during this tick. Do not
   * resurrect the frame's old link after AnimalRide has become false.
   * UseKong only hides a partner-follow handle. A mount id with no live
   * animal, and a rope handle the tick wrote, stay as the tick left them. */
  for (i = 0; i < 2; i++) {
    uint32_t addr = 0x0512u + (uint32_t)i * 2u;
    if (same_canonical_scene && !OverlayQuiet(wram) &&
        !s_runtime.actor.canonical.animal && !AnimalRide(wram) &&
        Dkc1WramU16(wram, addr) == 0 && KongSeatLink(s_runtime.actor.canonical.follow[i]))
      Dkc1WramSetU16(wram, addr, s_runtime.actor.canonical.follow[i]);
  }
  if (same_canonical_scene && ControlScene(wram)) {
    /* Scene initialization owns the next Kong selector. In particular, a
     * final-death actor can yield while the map clears the player pool;
     * its entry-time snapshot must not replace the last death owner. */
    uint16_t id = LeaderKong(wram);
    if (id)
      Dkc1WramSetU16(wram, DKC1_WRAM_Player_CurrentKongLo, id);
  }
  if (s_runtime.actor.camera.active) {
    /* The death map keeps mode and entrance, so a partner tick would
     * put the level camera back under icons that are already in
     * screen space. */
    if (HeldCameraMatchesScene(wram) && ControlScene(wram)) {
    Dkc1WramSetU16(wram, DKC1_WRAM_CameraX, s_runtime.actor.camera.values[0]);
    Dkc1WramSetU16(wram, DKC1_WRAM_CameraY, s_runtime.actor.camera.values[1]);
    Dkc1WramSetU16(wram, DKC1_WRAM_Level_CameraXPosLo, s_runtime.actor.camera.values[2]);
    Dkc1WramSetU16(wram, DKC1_WRAM_Level_CameraYPosLo, s_runtime.actor.camera.values[3]);
    Dkc1WramSetU16(wram, 0x1a69u, s_runtime.actor.camera.values[4]);
    /* A05E consumes vertical fast-follow8;9C07 consumes horizontal lead80.
     * Requests belong to each Kong; shared gameplay bits remain committed. */
    Dkc1WramSetU16(wram, 0x0579u,
        (Dkc1WramU16(wram, 0x0579u) & ~0x88u) | s_runtime.actor.camera.requests);
    }
    s_runtime.actor.camera.active = 0;
  }
}

enum { kCccMask = 0x0E00u };

static unsigned KongCcc(const uint8_t *wram, uint32_t slot) {
  return (Dkc1Actor_SprFlags(wram, slot) & kCccMask) >> 9;
}

static const uint16_t kKongLeaderPalOff[2] = {0x849Au, 0x8422u};

static void ApplyKongLeaderCgram(const uint8_t *wram, int seat) {
  uint32_t slot;
  unsigned ccc;
  if (!s_runtime.ppu || seat < 0 || seat > 1)
    return;
  slot = SlotOf(kKong[seat]);
  if (!slot || !Dkc1CoopKongCam(wram, slot))
    return;
  ccc = KongCcc(wram, slot);
  if (ccc > 7u)
    return;
  const unsigned first = 0x81u + ccc * 16u;
  Dkc1VideoLoadCgramFromRom(s_runtime.ppu, first, 0xBCu,
                            kKongLeaderPalOff[seat], 15u);
  uint16_t colors[15];
  if (Dkc1CoopWeatherPalette(wram, kKongLeaderPalOff[seat],
                            s_runtime.ppu->cgram + first, &s_runtime.weather, colors))
    memcpy(s_runtime.ppu->cgram + first, colors, sizeof colors);
}

static void CopyPartnerCgram(const uint8_t *wram, uint32_t slot, uint16_t id) {
  unsigned live, ccc;
  if (!slot || !s_runtime.ppu || !IsKong(id))
    return;
  ccc = KongCcc(wram, slot);
  if (ccc < 6u || ccc > 7u)
    return;
  live = 4u + (unsigned)Seat(id);
  Dkc1VideoCopyCgram(s_runtime.ppu, 0x80u + ccc * 16u, 0x80u + live * 16u, 16u);
}

static void PresentKongPalettes(uint8_t *wram) {
  int i;
  unsigned animal_palettes = 0, refresh = 0;
  for (i = 0; i < 2; i++) {
    if (KongAnimalRide(wram, (unsigned)i))
      animal_palettes |= 1u << KongCcc(wram, s_runtime.slot[i]);
  }
  for (i = 0; i < 2; i++) {
    uint32_t slot = SlotOf(kKong[i]);
    /* Only the rider owns animal colors. The on-foot partner still needs
     * the co-op palette refresh after a cave loads stock follower colors.
     * If a layout shares that palette, preserve the animal's ownership. */
    uint16_t cart = Dkc1CoopMinecartForKong(wram, (uint16_t)slot);
    if (cart && Dkc1CoopEntityRead16(wram, 0x1375u, cart) != slot) continue;
    if (slot && Dkc1CoopKongCam(wram, slot) &&
        !(animal_palettes & (1u << KongCcc(wram, slot)))) {
      CopyPartnerCgram(wram, slot, kKong[i]);
      refresh |= 1u << i;
    }
  }
  for (i = 0; i < 2; i++) {
    if (refresh & (1u << i))
      ApplyKongLeaderCgram(wram, i);
  }
}

static int SeatOfSlot(uint32_t slot) {
  if (slot && slot == s_runtime.slot[0]) return 0;
  if (slot && slot == s_runtime.slot[1]) return 1;
  if (slot == 2) return 0;
  if (slot == 4) return 1;
  return -1;
}

static int KongDeathRun(const uint8_t *wram, uint32_t slot) {
  if (!wram || !slot) return 0;
  uint16_t state=Dkc1Actor_RAMTable1029Lo(wram,slot);
  if (Dkc1CoopKongDeathPose(state,(int16_t)Dkc1Actor_SprXVelocity(wram,slot))) return 1;
  int seat=SeatOfSlot(slot);
  return state==0x0cu && seat>=0 && s_runtime.corpse[seat];
}

static void NoteCorpse(const uint8_t *wram) {
  int i;
  if (!wram)
    return;
  for (i = 0; i < 2; i++) {
    uint32_t slot = SlotOf(kKong[i]);
    uint16_t state, id;
    if (!slot)
      continue;
    id = Dkc1Actor_SpriteIDLo(wram, slot);
    state = Dkc1Actor_RAMTable1029Lo(wram, slot);
    if (!id || state == 0x23u || state == 0x53u || state == 0x56u)
      s_runtime.corpse[i] = 0;
    else if (KongDeathRun(wram, slot))
      s_runtime.corpse[i] = 1;
  }
}

static int InBarrelSeq(const uint8_t *wram) {
  int i;
  for (i = 0; i < 2; i++) {
    uint32_t s = SlotOf(kKong[i]);
    if (s && Dkc1CoopKongInBarrel(wram, s) && !s_runtime.corpse[i])
      return 1;
  }
  return 0;
}

static int InTeamSwap(const uint8_t *wram) {
  int i;
  for (i = 0; i < 2; i++) {
    uint32_t s = SlotOf(kKong[i]);
    uint16_t st;
    if (!s)
      continue;
    st = Dkc1Actor_RAMTable1029Lo(wram, s);
    /* $11 is the on-screen Kong swap. Stock keeps $0512 and both IDs. */
    if (st == 0x11u)
      return 1;
  }
  return 0;
}

static int OverlayQuiet(const uint8_t *wram) {
  return InBarrelSeq(wram) || s_runtime.barrel_hold != 0 || InTeamSwap(wram);
}

static int MountSprite(uint16_t id) {
  return id >= 0x09u && id <= 0x0cu;
}

/* BFCC4D stores the animal slot in $0512/$0514 and its ID in $0516;
 * BF9EC4 carries that association into a bonus. It outlives the mounting
 * poses: jump $15 and cave entry $1F/$20/$24/$46 still own animal graphics.
 * Validate the live link instead of extending a list of rider states.
 * Keep the existing $14/$16 guard for the mount/dismount handshake. */
int Dkc1CoopAnimalMountOccupied(struct CpuState *cpu,
                               uint16_t rider, uint16_t animal) {
  if (!s_runtime.enabled || !cpu || !cpu->ram || cpu->D || cpu->m_flag || cpu->x_flag ||
      (rider != 2 && rider != 4) || !Dkc1CoopEntityIsHandle(animal) ||
      !MountSprite(Dkc1Actor_SpriteIDLo(cpu->ram, animal))) return 0;
  uint16_t other = rider ^ 6u;
  return IsKong(Dkc1Actor_SpriteIDLo(cpu->ram, other)) &&
      Dkc1CoopAnimalForSeat(cpu->ram, other / 2u - 1u) == animal;
}

static int KongAnimalRide(const uint8_t *wram, unsigned seat) {
  uint32_t slot;
  uint16_t state;
  if (!wram || seat >= 2u || !(slot = s_runtime.slot[seat]))
    return 0;
  if (Dkc1CoopAnimalForSeat(wram, seat)) return 1;
  state = Dkc1Actor_RAMTable1029Lo(wram, slot);
  return state == 0x14u || state == 0x16u;
}

static int AnimalRide(const uint8_t *wram) {
  return KongAnimalRide(wram, 0) || KongAnimalRide(wram, 1);
}

int Dkc1CoopPrepareMountedPartner(struct CpuState *cpu) {
  if (!s_runtime.enabled || !cpu || !cpu->ram || cpu->D || cpu->m_flag || cpu->x_flag)
    return 0;
  const uint8_t *wram = cpu->ram;
  uint16_t slot = Dkc1WramU16(wram, 0x82);
  if ((slot != 2 && slot != 4) || !(Dkc1WramU16(wram, 0x0579) & 1u) ||
      !MountSprite(Dkc1WramU16(wram, 0x0516))) return 0;
  uint16_t animal = Dkc1WramU16(wram, 0x0512 + slot - 2);
  /* 80876D/8083A8 use 1/2 as pre-allocation mount intent. */
  if (animal != 1 && animal != 2 &&
      (animal < 6 || !Dkc1CoopEntityIsHandle(animal))) return 0;
  /* $80855D initializes a stock follower without recreating its mount.
   * At room entry the saved link is intent, not a live actor: the pool has
   * already been reset. Use $8085B7's native rider path for this Kong too. */
  cpu->X = slot;
  cpu->Y = (uint16_t)(slot - 2);
  return 1;
}

int Dkc1CoopBlockMountedSwap(struct CpuState *cpu) {
  if (!s_runtime.enabled || !cpu || !cpu->ram || cpu->D || cpu->m_flag || cpu->x_flag)
    return 0;
  const uint8_t *ram = cpu->ram;
  uint16_t slot = Dkc1WramU16(ram, 0x82);
  if ((slot != 2 && slot != 4) || !MountSprite(Dkc1WramU16(ram, 0x516)) ||
      Dkc1WramU16(ram, 0x512 + slot - 2)) return 0;
  uint16_t animal = Dkc1WramU16(ram, 0x512 + (slot ^ 6u) - 2);
  if (animal < 6 || !Dkc1CoopEntityIsHandle(animal) ||
      !MountSprite(Dkc1Actor_SpriteIDLo(ram, animal))) return 0;
  /* $BFBFAD checks only the initiating Kong for a mount. Its on-foot
   * exchange cannot transfer a second player's rider/animal association.
   * Consume this exchange request; a rider's own dismount stays native. */
  return 1;
}

static int ActorContact(const uint8_t *ram, uint16_t first, uint16_t second);

int Dkc1CoopBlockTeamSwap(struct CpuState *cpu) {
  if (!s_runtime.enabled || !cpu || !cpu->ram || cpu->D || cpu->m_flag || cpu->x_flag)
    return 0;
  const uint8_t *ram = cpu->ram;
  uint16_t slot = Dkc1WramU16(ram, 0x82);
  if (slot != 2 && slot != 4) return 0;
  if (Dkc1CoopBlockMountedSwap(cpu)) return 1;
  /* The cartridge rejects an initiating rider through $0512. Keep its native
   * dismount handling separate from the on-foot exchange. */
  if (Dkc1WramU16(ram, 0x512 + slot - 2)) return 0;
  uint16_t other = slot ^ 6u;
  if (!IsKong(Dkc1Actor_SpriteIDLo(ram, slot)) ||
      !IsKong(Dkc1Actor_SpriteIDLo(ram, other)) ||
      !Dkc1CoopKongCam(ram, other)) return 1;
  return !ActorContact(ram, slot, other);
}

int Dkc1CoopRestoreMountedExit(struct CpuState *cpu) {
  if (!s_runtime.enabled || !cpu || !cpu->ram || cpu->D || cpu->m_flag || cpu->x_flag)
    return 0;
  uint8_t *wram = cpu->ram;
  if (!MountSprite(Dkc1WramU16(wram, 0x0516)) || (cpu->Y != 2 && cpu->Y != 4)) return 0;
  uint16_t partner = cpu->Y ^ 6u;
  uint16_t animal = Dkc1WramU16(wram, 0x0512 + partner - 2);
  if (animal < 6 || !Dkc1CoopEntityIsHandle(animal) ||
      !MountSprite(Dkc1Actor_SpriteIDLo(wram, animal))) return 0;
  /* $B6F456 starts the follower's on-foot animation $17 after the room
   * intro. The leader has a mounted branch at $B6F449; co-op needs the
   * equivalent rider state for its independently mounted partner. */
  Dkc1Actor_SetRAMTable1029Lo(wram, partner, 0x14);
  Dkc1CoopEntityWrite16(wram, 0x12a5, partner, 0);
  return 1;
}

void Dkc1CoopFinishMountedPartner(uint8_t *wram) {
  if (!s_runtime.enabled || !wram || !MountSprite(Dkc1WramU16(wram, 0x0516))) return;
  uint16_t leader = Dkc1WramU16(wram, DKC1_WRAM_Player_CurrentKongLo);
  if (leader != 1 && leader != 2) return;
  uint16_t lead_slot = (uint16_t)(leader * 2), partner = lead_slot ^ 6u;
  uint16_t animal = Dkc1WramU16(wram, 0x0512 + partner - 2);
  if (animal < 6 || !Dkc1CoopEntityIsHandle(animal) ||
      !MountSprite(Dkc1Actor_SpriteIDLo(wram, animal)) ||
      Dkc1Actor_RAMTable1029Lo(wram, partner) != 0x46) return;
  /* $808787 positions both Kongs, but writes the walk-in destination $1375
   * only for the leader. Translate it by the same final partner offset so
   * the newly mounted partner completes entry before accepting control. */
  Dkc1CoopEntityWrite16(wram, 0x1375, partner, (uint16_t)(
      Dkc1CoopEntityRead16(wram, 0x1375, lead_slot) +
      Dkc1Actor_SprXPos(wram, partner) - Dkc1Actor_SprXPos(wram, lead_slot)));
}

static uint16_t RoomMountIntent(const uint8_t *ram) {
  uint16_t intent = 0;
  for (uint16_t rider = 2; rider <= 4; rider += 2) {
    uint16_t animal = Dkc1CoopReciprocalAnimalForSeat(ram, rider / 2u - 1u, rider);
    if (!IsKong(Dkc1Actor_SpriteIDLo(ram, rider)) || !animal ||
        Dkc1Actor_SpriteIDLo(ram, animal) != Dkc1WramU16(ram, 0x516)) continue;
    /* The cartridge carries one animal. Do not guess between conflicting
     * claims, including a historical duplicate link to this same animal. */
    if (intent || Dkc1WramU16(ram, 0x512 + (rider ^ 6u) - 2) == animal) return 0;
    intent = (uint16_t)((Dkc1Actor_SpriteIDLo(ram, animal) << 8) | rider);
  }
  return intent;
}

void Dkc1CoopRememberRoomMount(struct CpuState *cpu) {
  if (!s_runtime.enabled || !cpu || !cpu->ram || cpu->D || cpu->m_flag || cpu->x_flag)
    return;
  uint16_t intent = RoomMountIntent(cpu->ram);
  s_runtime.room_mount = (intent & 0xffu) == cpu->Y ? intent : 0;
}

void Dkc1CoopSaveCarriedMount(struct CpuState *cpu) {
  if (!s_runtime.enabled || !cpu || !cpu->ram || cpu->D || cpu->m_flag || cpu->x_flag ||
      (cpu->Y != 2 && cpu->Y != 4)) return;
  /* BF9EC2 saves the current leader's link in055F. Carry the actual rider,
   * even when the on-foot partner initiated entry. Keep this separate from
   * an animal subsequently mounted inside the bonus. */
  s_runtime.carried_mount = RoomMountIntent(cpu->ram);
  if (s_runtime.carried_mount) cpu->Y = s_runtime.carried_mount & 0xffu;
}

void Dkc1CoopRestoreCarriedMount(struct CpuState *cpu) {
  if (!s_runtime.enabled || !cpu || !cpu->ram || cpu->D || cpu->m_flag || cpu->x_flag ||
      (cpu->Y != 2 && cpu->Y != 4)) return;
  uint16_t intent = s_runtime.carried_mount;
  s_runtime.carried_mount = 0;
  if (intent && Dkc1WramU16(cpu->ram, 0x55f) &&
      Dkc1WramU16(cpu->ram, 0x561) == (intent >> 8)) {
    cpu->Y = intent & 0xffu;
    s_runtime.room_mount = intent; /* The subsequent native initializer uses83A8. */
  } else {
    s_runtime.room_mount = 0;
  }
}

void Dkc1CoopRestoreRoomMount(struct CpuState *cpu) {
  if (!s_runtime.enabled || !cpu || !cpu->ram || cpu->D || cpu->m_flag || cpu->x_flag)
    return;
  /* 8083A8 restores bonus carry as link2 after selecting the native leader.
   * Restore the verified rider's link before either Kong is initialized. */
  uint16_t intent = s_runtime.room_mount;
  s_runtime.room_mount = 0;
  if (intent && Dkc1WramU16(cpu->ram, 0x516) == (intent >> 8) &&
      (cpu->Y == 2 || cpu->Y == 4))
    cpu->Y = intent & 0xffu;
}

static void NoteBarrel(const uint8_t *wram) {
  if (InBarrelSeq(wram)) {
    s_runtime.barrel_hold = 60u;
    s_runtime.lock = 0;
  } else if (s_runtime.barrel_hold) {
    s_runtime.barrel_hold--;
  }
}

void Dkc1CoopPrimaryCamera(const uint8_t *wram, uint16_t *x, uint16_t *y) {
  int held = HeldCameraMatchesScene(wram);
  *x = held ? s_runtime.actor.camera.values[0] : Dkc1WramU16(wram, DKC1_WRAM_CameraX);
  *y = held ? s_runtime.actor.camera.values[1] : Dkc1WramU16(wram, DKC1_WRAM_CameraY);
}

uint16_t Dkc1CoopRopeClimbCameraY(const uint8_t *ram, uint16_t native) {
  /* $BF:A0A9 bounds climbing by the gameplay viewport. Generic private
   * following cameras must not lift the stock horizontal ceiling.
   * Substitute only this operand; retain the cartridge's arithmetic/flags. */
  /* Mode 9 uses the native vertical controller independently for each view.
   * Its climbing ceiling follows that viewport; borrowing the primary ceiling
   * blocks an ascending rider whenever the other Kong's camera is lower.
   * Horizontal layouts retain their shared stock climbing ceiling. */
  if (ram && Dkc1WramU16(ram,0x32u)==9u) return native;
  return s_runtime.enabled && HeldCameraMatchesScene(ram) ? s_runtime.actor.camera.values[1] : native;
}

static int KongSeatLink(uint16_t value) {
  return value == 2u || value == 4u;
}

static int ClimbState(uint16_t state) {
  return state == 0x25u || state == 0x26u;
}

/* $1AFD/$1AFF/$1B01 are one shared current-player slot. Both Kong
 * ticks run the player routine. A tick publishes the slot when that
 * Kong is climbing. The partner's clear is rolled back while the
 * climb is live and while that same Kong is still in the airborne
 * release ($0001) that left the rope, so the rope does not pull him
 * straight back. A later jump starts from the ground with no such
 * release, and a leftover rope latch would compare equal to the new
 * overlap and refuse the grab. That latch is dropped first. */
static int RopeSprite(const uint8_t *wram, uint16_t slot) {
  return Dkc1CoopRelationObject(wram, slot, kCoopRope);
}

/* BF:EF32..4E: native vertical-rope overlap, including the lower edge. */
static int RopeGrabReach(const uint8_t *ram,uint32_t kong,uint16_t rope) {
  if (!RopeSprite(ram,rope)) return 0;
  int dx=(int)Dkc1Actor_SprXPos(ram,kong)-(int)Dkc1Actor_SprXPos(ram,rope);
  int dy=(int16_t)(uint16_t)(Dkc1Actor_SprYPos(ram,kong)-
      Dkc1Actor_SprYPos(ram,rope)+0x18u);
  if (dx<0) dx=-dx;
  return dx<0x0c && dy>=0;
}

typedef int (*CoopParticipantReady)(const uint8_t *ram, uint16_t object,
                                    uint32_t kong, unsigned seat);
enum CoopParticipantSet { kCachedLiveSeats, kRegisteredLiveSeats };

/* Read membership at this native operation, never cache it for a whole frame.
 * The wheel also requires the cached slot to retain its exact Kong identity. */
static int AllParticipantsReady(const uint8_t *ram, uint16_t object,
    enum CoopParticipantSet participants, CoopParticipantReady ready, int require_live) {
  int any_live=0;
  for (unsigned seat = 0; seat < 2; ++seat) {
    uint32_t kong = s_runtime.slot[seat];
    if (!kong || (participants == kRegisteredLiveSeats &&
        Dkc1Actor_SpriteIDLo(ram, kong) != kKong[seat]) ||
        !Dkc1CoopKongCanParticipate(ram, kong)) continue;
    any_live=1;
    if (!ready(ram, object, kong, seat)) return 0;
  }
  return any_live || !require_live;
}

static int RopeParticipantReady(const uint8_t *ram, uint16_t rope,
                                uint32_t kong, unsigned seat) {
  return Dkc1Actor_RAMTable1029Lo(ram, kong) == 0x25u &&
      s_runtime.rope.rope_latched[seat] == rope;
}

/* Only the native waiting-rope 4 -> 5 transition asks this question.
 * Once departed, a rope continues normally even if a rider jumps off. */
int Dkc1CoopRopeCanDepart(const uint8_t *wram, uint16_t rope) {
  if (!s_runtime.enabled || !wram || !RopeSprite(wram, rope)) return 1;
  return AllParticipantsReady(wram, rope, kCachedLiveSeats, RopeParticipantReady,0);
}

/* B6F553..F580: unsigned CMP carry, ROL/EOR/LSR; bit15 selects Y,
 * bit0 reverses the boundary. A zero second boundary skips its comparison. */
static int PlayerCrossesAuthoredTrigger(const uint8_t *ram, uint16_t source,
                                      uint16_t kong) {
  const uint16_t limits[] = {0x13e9, 0x1491};
  const uint16_t flags[] = {0x145d, 0x14c5};
  for (unsigned i=0; i<2; ++i) {
    uint16_t limit=Dkc1CoopEntityRead16(ram,limits[i],source);
    if (i && !limit) break;
    uint16_t flag=Dkc1CoopEntityRead16(ram,flags[i],source);
    uint16_t coord=Dkc1CoopEntityRead16(ram,(flag & 0x8000u)?0x0bc1:0x0b19,kong);
    if (!((coord>=limit) ^ (flag & 1u))) return 0;
  }
  return 1;
}

static int AuthoredTriggerReady(const uint8_t *ram, uint16_t wheel,
                                 uint32_t kong, unsigned seat) {
  (void)seat;
  return PlayerCrossesAuthoredTrigger(ram, wheel, (uint16_t)kong);
}

/* Native wheel and Croctopus share the CMP/ROL/EOR trigger encoding.
 * Gate only the authored waiting -> running transition, never later motion. */
static int BlockAuthoredDeparture(struct CpuState *cpu,uint16_t id,uint16_t state) {
  if (!s_runtime.enabled || !cpu || !cpu->ram || cpu->m_flag || cpu->x_flag ||
      !Dkc1CoopEntityIsHandle(cpu->X) ||
      Dkc1Actor_SpriteIDLo(cpu->ram,cpu->X)!=id ||
      Dkc1Actor_RAMTable1029Lo(cpu->ram,cpu->X)!=state) return 0;
  if (!AllParticipantsReady(cpu->ram,cpu->X,kRegisteredLiveSeats,AuthoredTriggerReady,id==0x57u)) {
    cpu->_flag_C=0; cpu->P=(uint8_t)(cpu->P & ~1u); return 1;
  }
  return 0;
}

int Dkc1CoopBlockWheelDeparture(struct CpuState *cpu) {
  return BlockAuthoredDeparture(cpu,0x71u,1u);
}

int Dkc1CoopBlockCroctopusDeparture(struct CpuState *cpu) {
  return BlockAuthoredDeparture(cpu,0x57u,0u);
}

int Dkc1CoopBlockRopeDeparture(struct CpuState *cpu) {
  if (!s_runtime.enabled || !cpu || !cpu->ram) return 0;
  uint16_t rope = Dkc1WramU16(cpu->ram, (uint16_t)(cpu->D + 0x82u));
  return RopeSprite(cpu->ram, rope) &&
      Dkc1Actor_RAMTable1029Lo(cpu->ram, rope) == 4u &&
      !Dkc1CoopRopeCanDepart(cpu->ram, rope);
}

/* BF951B selects an 80-pixel lead from the rope's velocity sign. Waiting4
 * oscillates around its anchor; keep the preceding native zero lead until
 * departure instead of treating each sway reversal as player travel. */
int Dkc1CoopRopeWaitCamera(struct CpuState *cpu) {
  if (!s_runtime.enabled || !cpu || !cpu->ram || !RopeSprite(cpu->ram, cpu->Y) ||
      Dkc1Actor_RAMTable1029Lo(cpu->ram, cpu->Y) != 4u) return 0;
  uint16_t kong = Dkc1WramU16(cpu->ram, (uint16_t)(cpu->D + 0x82u));
  for (int seat = 0; seat < 2; ++seat)
    if (s_runtime.slot[seat] == kong &&
        Dkc1Actor_RAMTable1029Lo(cpu->ram, kong) == 0x25u &&
        s_runtime.rope.rope_latched[seat] == cpu->Y) return 1;
  return 0;
}

static int HookSprite(const uint8_t *wram, uint16_t slot) {
  return Dkc1CoopRelationObject(wram, slot, kCoopSwing);
}

static int HookDistance(const uint8_t *wram, uint32_t slot, uint16_t hook) {
  int dx = (int)Dkc1Actor_SprXPos(wram, slot) - (int)Dkc1Actor_SprXPos(wram, hook);
  int dy = (int)Dkc1Actor_SprYPos(wram, slot) - (int)Dkc1Actor_SprYPos(wram, hook);
  if (dx < 0)
    dx = -dx;
  if (dy < 0)
    dy = -dy;
  return dx + dy;
}

/* The rider already owns $1B01. Stock treats an equal $1AFF as "this
 * player is already holding it" and drops the grab. A partner within
 * this box of that rider is on the same chain. */
static int NearHookRider(const uint8_t *wram, uint32_t slot) {
  uint32_t rider;
  for (rider = DKC1_ACTOR_SLOT_FIRST; rider <= 4u; rider += 2u) {
    int dx, dy;
    if (rider == slot ||
        Dkc1Actor_RAMTable1029Lo(wram, rider) != 0x0026u)
      continue;
    dx = (int)Dkc1Actor_SprXPos(wram, slot) -
         (int)Dkc1Actor_SprXPos(wram, rider);
    dy = (int)Dkc1Actor_SprYPos(wram, slot) -
         (int)Dkc1Actor_SprYPos(wram, rider);
    if (dx < 0)
      dx = -dx;
    if (dy < 0)
      dy = -dy;
    if (dx <= 0x18 && dy <= 0x20)
      return 1;
  }
  return 0;
}

static int PartnerClimbing(const uint8_t *wram, uint16_t id) {
  uint16_t other = id == kKong[0] ? kKong[1] : kKong[0];
  uint32_t slot = SlotOf(other);
  return slot && ClimbState(Dkc1Actor_RAMTable1029Lo(wram, slot));
}

static void DropStaleRopeLatch(uint8_t *wram) {
  uint16_t latched = Dkc1WramU16(wram, 0x1B01u);
  if (RopeSprite(wram, latched))
    Dkc1WramSetU16(wram, 0x1B01u, 0);
}

static int ReleaseSeat(uint16_t id) {
  return id == kKong[1] ? 1 : 0;
}

/* BFEF51 has passed the cartridge's shaft/lower-edge and width tests.
 * Y is the tested Kong, X this rope. Retain that actual contact privately:
 * another rider may publish his own rope before this Kong's next tick. */
void Dkc1CoopRememberRopeContact(struct CpuState *cpu) {
  if (!s_runtime.enabled || !cpu || !cpu->ram || cpu->m_flag || cpu->x_flag ||
      !RopeSprite(cpu->ram, cpu->X)) return;
  for (int seat = 0; seat < 2; ++seat)
    if (s_runtime.slot[seat] == cpu->Y &&
        Dkc1Actor_SpriteIDLo(cpu->ram, cpu->Y) == kKong[seat] &&
        Dkc1Actor_RAMTable1029Lo(cpu->ram, cpu->Y) == 1u &&
        (!s_runtime.rope.rope_release[seat] ||
         s_runtime.rope.rope_latched[seat] != cpu->X))
      s_runtime.rope.rider_hook[seat] = (uint16_t)(cpu->X | 1u);
}

static void SnapshotSharedLatch(uint8_t *wram, uint32_t slot) {
  uint16_t state, id;
  int seat;
  s_runtime.rope.rope_hold = 0;
  if (!wram || !slot)
    return;
  state = Dkc1Actor_RAMTable1029Lo(wram, slot);
  id = (uint16_t)(Dkc1Actor_SpriteIDLo(wram, slot) & 0xFFu);
  seat = ReleaseSeat(id);
  /* Each Kong remembers his own jump off the rope. The other climber
   * must not replace that mark, or the rope is offered again and the
   * jump never leaves. */
  if (state != 0x0001u) {
    s_runtime.rope.rope_release[seat] = 0;
    s_runtime.rope.release_hook[seat] = 0;
  }
  /* Each rider keeps the $31 he grabbed. A loaded state has no memory
   * of that, and $1B01 names only one of the two ropes. Give him the
   * nearer of $1AFF and $1B01 so he is not pulled onto the other. */
  if (state == 0x0026u) {
    if (!HookSprite(wram, s_runtime.rope.rider_hook[seat])) {
      uint16_t hooks[2];
      int n = 0, pick;
      uint16_t a = s_runtime.rope.frame_aff;
      uint16_t b = s_runtime.rope.frame_b01;
      if (HookSprite(wram, a))
        hooks[n++] = a;
      if (HookSprite(wram, b) && b != a)
        hooks[n++] = b;
      if (!n && HookSprite(wram, s_runtime.rope.hook_pending))
        hooks[n++] = s_runtime.rope.hook_pending;
      pick = 0;
      if (n == 2 && HookDistance(wram, slot, hooks[1]) <
                        HookDistance(wram, slot, hooks[0]))
        pick = 1;
      if (n)
        s_runtime.rope.rider_hook[seat] = hooks[pick];
    }
  } else if (state != 1u || !(s_runtime.rope.rider_hook[seat] & 1u)) {
    s_runtime.rope.rider_hook[seat] = 0;
  }
  /* A fall off the bottom leaves state $000D and clears the one
   * latch on a later frame. The Kong still climbing would take the
   * $1AFD==0 path and leave with him. Put his rope back first. */
  if (state == 0x25u) {
    /* $1B01 is the last rider's latch, not this rider's ownership.
     * A partner grabbing another rope must not replace a live binding. */
    uint16_t latched = s_runtime.rope.rope_latched[seat];
    if (!RopeSprite(wram, latched)) {
      latched = Dkc1WramU16(wram, 0x1B01u);
      if (RopeSprite(wram, latched))
        s_runtime.rope.rope_latched[seat] = latched;
    }
    if (!s_runtime.rope.rope_release[seat] && RopeSprite(wram, latched)) {
      Dkc1WramSetU16(wram, 0x1AFDu, latched);
      Dkc1WramSetU16(wram, 0x1B01u, latched);
    }
  } else if (!s_runtime.rope.rope_release[seat]) {
    s_runtime.rope.rope_latched[seat] = 0;
  }
  /* Equal $1AFD/$1B01 is "already holding" and skips the grab.
   * The partner's own jump must not clear it while the other Kong
   * is still the climber: that is what drops him off the rope. */
  if (state == 0x0001u && !s_runtime.rope.rope_release[seat] && !PartnerClimbing(wram, id))
    DropStaleRopeLatch(wram);
  s_runtime.rope.rope_hold = 1;
  s_runtime.rope.rope_slot = slot;
  s_runtime.rope.rope_owner = ClimbState(state);
  s_runtime.rope.rope_words[0] = Dkc1WramU16(wram, 0x1AFDu);
  s_runtime.rope.rope_words[1] = Dkc1WramU16(wram, 0x1AFFu);
  s_runtime.rope.rope_words[2] = Dkc1WramU16(wram, 0x1B01u);
  if (state == 1u && (s_runtime.rope.rider_hook[seat] & 1u)) {
    uint16_t contact = (uint16_t)(s_runtime.rope.rider_hook[seat] & ~1u);
    s_runtime.rope.rider_hook[seat] = 0; /* An offer is consumed once. */
    if (RopeGrabReach(wram, slot, contact) &&
        (!s_runtime.rope.rope_release[seat] ||
         contact != s_runtime.rope.rope_latched[seat])) {
      Dkc1WramSetU16(wram, 0x1AFDu, contact);
      Dkc1WramSetU16(wram, 0x1B01u, 0);
    }
  }
  /* The holder consumes $1AFD before the partner's tick. BFEF2B
   * accepts the grab only inside 12 pixels of X and when this Y is
   * no more than $18 below the rope. A jump from the ground under
   * the rope is outside that Y test; forcing it enters $25 for one
   * frame and cancels the jump. */
  if (state == 0x0001u && PartnerClimbing(wram, id)) {
    uint16_t rope = s_runtime.rope.rope_words[2];
    if (!RopeSprite(wram, rope))
      rope = s_runtime.rope.rope_words[0];
    if (RopeGrabReach(wram,slot,rope) && (!s_runtime.rope.rope_release[seat] ||
        rope != s_runtime.rope.rope_latched[seat])) {
      Dkc1WramSetU16(wram,0x1AFDu,rope);
      Dkc1WramSetU16(wram,0x1B01u,0);
    }
  }
  /* The other Kong's tick clears $1B01. Put this rider's own rope
   * back. The frame's pending slot may belong to the other rope. */
  if (state == 0x0026u) {
    uint16_t mine = s_runtime.rope.rider_hook[seat];
    if (!HookSprite(wram, mine))
      mine = s_runtime.rope.hook_pending;
    if (HookSprite(wram, mine) && Dkc1WramU16(wram, 0x1B01u) != mine) {
      Dkc1WramSetU16(wram, 0x1B01u, mine);
      s_runtime.rope.rope_words[2] = mine;
    }
  }
  /* Sprite $31 writes its slot to $1AFF after this Kong has already
   * ticked, and the other Kong's routine clears that word before the
   * next air tick. BeginFrame kept the slot. BF8870 turns it into
   * state $0026 while $1AFD is empty. A vertical overlap keeps $0025.
   * The nearer airborne Kong takes the one slot. */
  if (state == 0x0001u && Dkc1WramU16(wram, 0x1AFDu) == 0) {
    uint16_t hook = Dkc1WramU16(wram, 0x1AFFu);
    uint16_t other = id == kKong[0] ? kKong[1] : kKong[0];
    uint32_t partner = SlotOf(other);
    int closer, occupied;
    if (!HookSprite(wram, hook))
      hook = s_runtime.rope.hook_pending;
    /* The release hides the rope he just left. A different rope that
     * already wrote $1AFF is the next one he is jumping at. */
    if (s_runtime.rope.rope_release[seat] && hook == s_runtime.rope.release_hook[seat])
      hook = 0;
    closer = !partner ||
        Dkc1Actor_RAMTable1029Lo(wram, partner) != 0x0001u ||
        HookDistance(wram, slot, hook) <= HookDistance(wram, partner, hook);
    occupied = HookSprite(wram, hook) &&
        hook == Dkc1WramU16(wram, 0x1B01u);
    if (HookSprite(wram, hook) && closer &&
        (!occupied || (PartnerClimbing(wram, id) && NearHookRider(wram, slot)))) {
      Dkc1WramSetU16(wram, 0x1B01u, hook);
      if (!occupied) {
        Dkc1WramSetU16(wram, 0x1AFFu, 0);
        s_runtime.rope.hook_pending = 0;
      }
      Dkc1Actor_SetRAMTable1029Lo(wram, slot, 0x0026u);
      s_runtime.rope.rider_hook[seat] = hook;
      s_runtime.rope.rope_release[seat] = 0;
      s_runtime.rope.release_hook[seat] = 0;
      Dkc1Actor_SetSprXVelocity(wram, slot, 0);
      Dkc1Actor_SetSprYVelocity(wram, slot, 0);
      Dkc1Actor_SetRAMTable0F25Lo(wram, slot, 0);
      Dkc1Actor_SetRAMTable0F8DLo(wram, slot, 0);
      s_runtime.rope.rope_owner = 1;
      s_runtime.rope.hook_pending = 0;
      s_runtime.rope.rope_words[0] = 0;
      s_runtime.rope.rope_words[1] = 0;
      s_runtime.rope.rope_words[2] = hook;
    }
  }
  /* $0700 is the jump impulse. The next tick sees $1AFF still naming
   * this rope, enters $0026 again, and that entry zeroes the impulse.
   * Hide the rope for this airborne release and put the impulse back
   * if the cartridge grabs anyway. */
  if (state == 0x0001u && s_runtime.rope.rope_release[seat]) {
    uint16_t aff = Dkc1WramU16(wram, 0x1AFFu);
    uint16_t left = s_runtime.rope.release_hook[seat];
    s_runtime.rope.release_xv = (uint16_t)Dkc1Actor_SprXVelocity(wram, slot);
    s_runtime.rope.release_yv = (uint16_t)Dkc1Actor_SprYVelocity(wram, slot);
    s_runtime.rope.release_vel = 1;
    /* A release excludes the rope just left, not every vertical rope.
     * Preserve a new native overlap so BF8821 can perform the real grab. */
    uint16_t overlap=Dkc1WramU16(wram,0x1AFDu);
    if (!RopeGrabReach(wram,slot,overlap) || overlap==s_runtime.rope.rope_latched[seat])
      Dkc1WramSetU16(wram,0x1AFDu,0);
    else if (Dkc1WramU16(wram,0x1B01u)==overlap)
      Dkc1WramSetU16(wram,0x1B01u,0);
    if (!HookSprite(wram, aff) || aff == left) {
      Dkc1WramSetU16(wram, 0x1AFFu, 0);
      if (!HookSprite(wram, s_runtime.rope.hook_pending) || s_runtime.rope.hook_pending == left)
        s_runtime.rope.hook_pending = 0;
    }
  } else {
    s_runtime.rope.release_vel = 0;
  }
}

static void PublishSharedLatch(uint8_t *wram) {
  uint16_t state, id;
  int seat, other;
  if (!s_runtime.rope.rope_hold || !wram)
    return;
  s_runtime.rope.rope_hold = 0;
  state = Dkc1Actor_RAMTable1029Lo(wram, s_runtime.rope.rope_slot);
  id = (uint16_t)(Dkc1Actor_SpriteIDLo(wram, s_runtime.rope.rope_slot) & 0xFFu);
  seat = ReleaseSeat(id);
  other = seat ^ 1;
  /* The jump already left the rope. Stock can enter $0026 again from
   * the shared latch and the swing copy then carries him to the rider
   * who is still on the other rope. Stay in the air. */
  if (s_runtime.rope.rope_release[seat] && state == 0x0026u &&
      !HookSprite(wram, s_runtime.rope.rider_hook[seat])) {
    Dkc1Actor_SetRAMTable1029Lo(wram, s_runtime.rope.rope_slot, 0x0001u);
    if (s_runtime.rope.release_vel) {
      Dkc1Actor_SetSprXVelocity(wram, s_runtime.rope.rope_slot, s_runtime.rope.release_xv);
      Dkc1Actor_SetSprYVelocity(wram, s_runtime.rope.rope_slot, s_runtime.rope.release_yv);
    }
    state = 0x0001u;
  }
  /* Acquire only at this Kong's own native grab, before another Kong
   * publishes a different rope. Re-grabbing after a release replaces it. */
  if (state == 0x25u && !s_runtime.rope.rope_owner &&
      RopeSprite(wram,Dkc1WramU16(wram,0x1B01u))) {
    s_runtime.rope.rope_latched[seat]=Dkc1WramU16(wram,0x1B01u);
    s_runtime.rope.rope_release[seat]=0;
    s_runtime.rope.release_hook[seat]=0;
  }
  if (s_runtime.rope.rope_owner && !ClimbState(state)) {
    if (!s_runtime.rope.rope_release[seat] && HookSprite(wram, s_runtime.rope.rider_hook[seat]))
      s_runtime.rope.release_hook[seat] = s_runtime.rope.rider_hook[seat];
    s_runtime.rope.rope_release[seat] = 1;
  }
  if (s_runtime.rope.rope_owner || ClimbState(state)) {
    /* The second climber sees $1AFD already consumed and would clear
     * the latch. Put it back when someone is still on the rope, or
     * just left and must not be grabbed again. A lone climber's own
     * clear is left alone. */
    if (!RopeSprite(wram, Dkc1WramU16(wram, 0x1B01u)) &&
        RopeSprite(wram, s_runtime.rope.rope_words[2]) &&
        (PartnerClimbing(wram, id) || s_runtime.rope.rope_release[other]))
      Dkc1WramSetU16(wram, 0x1B01u, s_runtime.rope.rope_words[2]);
    return;
  }
  if (s_runtime.rope.rope_release[seat]) {
    if (state == 0x0001u)
      return;
    s_runtime.rope.rope_release[seat] = 0;
    s_runtime.rope.release_hook[seat] = 0;
  }
  if (state == 0x0001u && !PartnerClimbing(wram, id)) {
    DropStaleRopeLatch(wram);
    return;
  }
  Dkc1WramSetU16(wram, 0x1AFDu, s_runtime.rope.rope_words[0]);
  Dkc1WramSetU16(wram, 0x1AFFu, s_runtime.rope.rope_words[1]);
  Dkc1WramSetU16(wram, 0x1B01u, s_runtime.rope.rope_words[2]);
}

typedef struct CoopPadSample { uint16_t held, pressed; } CoopPadSample;
/* Keep paired levels/edges at the native rider-cache ABI. Admission and
 * button composition remain at the existing call boundaries. */
static void PublishRiderPad(uint8_t *wram,uint16_t rider,CoopPadSample sample) {
  Dkc1WramSetU16(wram,0x16e9u+rider-2u,sample.pressed&0xc000u);
  Dkc1WramSetU16(wram,0x16ebu+rider,sample.held);
}

static void UseKong(uint8_t *wram, uint16_t kong_id) {
  int seat;
  uint16_t off;
  if (!IsKong(kong_id))
    return;
  seat = Seat(kong_id);
  unsigned bonus_owner = Dkc1CoopAnimalBonusOwner(wram);
  if (bonus_owner) seat = Seat((uint16_t)bonus_owner);
  CoopPadSample sample = {Held(seat),s_runtime.pressed[seat]};
  uint16_t rider = (uint16_t)SlotOf(kong_id);
  if (bonus_owner && rider) {
    /* Native animal challenges keep their one cartridge rider. Its cached
     * jump edges and pad levels belong to the collector's physical pad. */
    PublishRiderPad(wram,rider,sample);
  }
  uint16_t cart = Dkc1CoopMinecartForKong(wram, rider);
  if (cart && Dkc1Actor_RAMTable1375Lo(wram, cart) == rider &&
      Dkc1CoopMinecartForKong(wram, rider ^ 6u) == cart) {
    /* Merge each pad's own B edge: a held B on one controller must not
     * suppress a fresh press on the other. Other controls remain rider-owned. */
    sample.held |= Held(seat ^ 1) & 0x8000u;
    sample.pressed |= s_runtime.pressed[seat ^ 1] & 0x8000u;
    PublishRiderPad(wram,rider,sample);
  }
  if (!bonus_owner && MinecartAirDriver(wram)==rider &&
      (Dkc1WramU16(wram,0x579)&1u) &&
      Dkc1CoopKongCanParticipate(wram,rider^6u)) {
    /* The shared jump must keep reading both pads after the seat is released.
     * Preserve the driver's direction on conflict; otherwise either passenger
     * can steer the native free-flight continuation back onto a cart. */
    uint16_t other=Held(seat^1);
    sample.held |= other & 0xc000u;
    if (!(sample.held&0x0300u)) sample.held |= other & 0x0300u;
    sample.pressed |= s_runtime.pressed[seat^1] & 0xc000u;
    PublishRiderPad(wram,rider,sample);
  }
  /* Camera ownership belongs to the viewport, not $056F. A handoff can
   * make P2 the leader before the native camera leaves P1. P2 still needs
   * his own camera for gameplay bounds; restore the native context afterward. */
  uint16_t leader = LeaderKong(wram);
  int primary_role = Dkc1CoopKongRole(wram, SlotOf(kKong[0]));
  int secondary_survivor = seat == 1 &&
      (primary_role == kDkc1CoopRoleDying || primary_role == kDkc1CoopRoleGone ||
       primary_role == kDkc1CoopRoleLost);
  if (leader && !s_runtime.actor.camera.active &&
      (kong_id != leader ||
       (secondary_survivor && Dkc1CoopKongOwnCamera(wram, rider) &&
        (Dkc1CoopSceneCapabilities(wram) & kDkc1CoopSceneCamera)))) {
    CaptureActorCamera(wram);
    Dkc1WramSetU16(wram,0x579,Dkc1WramU16(wram,0x579)&~0x88u);
    uint16_t x, y;
    uint32_t slot = SlotOf(kong_id);
    if (seat == 1 && Dkc1CoopKongOwnCamera(wram, slot) &&
        Dkc1CoopWorldGetCamera(wram, 1, &x, &y)) {
      Dkc1WramSetU16(wram, DKC1_WRAM_CameraX, x);
      Dkc1WramSetU16(wram, DKC1_WRAM_CameraY, y);
    }
  }
  Dkc1WramSetU16(wram, DKC1_WRAM_Player_CurrentKongLo, kong_id);
  Dkc1WramSetU16(wram, DKC1_WRAM_JoypadHeld, sample.held);
  Dkc1WramSetU16(wram, DKC1_WRAM_Global_HeldButtonsLoP2, sample.held);
  Dkc1WramSetU16(wram, DKC1_WRAM_Global_HeldButtonsLo, sample.held);
  Dkc1WramSetU16(wram, DKC1_WRAM_JoypadPressed, sample.pressed);
  Dkc1WramSetU16(wram, DKC1_WRAM_Global_PressedButtonsLoP2, sample.pressed);
  Dkc1WramSetU16(wram, DKC1_WRAM_Global_PressedButtonsLo, sample.pressed);
  /* Partner-follow handles sit next to the current pad. A rope slot
   * is not one of those handles. A mount id with no live animal is the
   * stale Rambi value and must not survive into this tick. */
  if (!OverlayQuiet(wram) && !AnimalRide(wram)) {
    for (off = 0x0512u; off <= 0x0514u; off += 2u) {
      if (KongSeatLink(Dkc1WramU16(wram, off)))
        Dkc1WramSetU16(wram, off, 0);
    }
    for (off = 0x0516u; off <= 0x0518u; off += 2u) {
      if (MountSprite(Dkc1WramU16(wram, off)))
        Dkc1WramSetU16(wram, off, 0);
    }
  }
}


/* Semantic host state is separate from the WRAM transaction. In particular,
 * a successful hit can change s_runtime.lock before a later actor operation rejects
 * the private execution. Preserve an already-running/suspended overlay too.
 */
static CoopRuntimeState s_saved_runtime;
static int s_transaction_active;
int Dkc1CoopBeginTransaction(void) {
  if (s_transaction_active) return 0;
  s_saved_runtime=s_runtime;
  s_transaction_active=1;
  return 1;
}
void Dkc1CoopEndTransaction(int commit) {
  if (!s_transaction_active) return;
  if (!commit) s_runtime=s_saved_runtime;
  s_transaction_active=0;
}

static void Unfollow(uint8_t *wram, uint32_t slot, uint16_t dp) {
  uint16_t off, st, col;
  if (!slot)
    return;
  st = Dkc1Actor_RAMTable1029Lo(wram, slot);
  col = Dkc1Actor_SprCollisionFlags(wram, slot);
  /* $0005 is follow-AI. State $0002 can remain after a native landing/exit.
   * Allow its airborne phase and 45 stationary-height ticks to finish before
   * releasing control. Horizontal jitter and its looping fall animation do
   * not mean the Kong is still falling. */
  int seat = SeatOfSlot(slot);
  if (seat < 0) return;
  /* BF99AC's $34 follows the other $25 Kong and copies his coordinates.
   * Co-op needs the same verified rope with this rider's own native pad. */
  if (st == 0x34u) {
    uint32_t partner = s_runtime.slot[seat ^ 1];
    uint16_t rope = Dkc1WramU16(wram, 0x1B01u);
    if (partner && Dkc1Actor_RAMTable1029Lo(wram, partner) == 0x25u &&
        RopeGrabReach(wram, slot, rope)) {
      Dkc1Actor_SetRAMTable1029Lo(wram, slot, 0x25u);
      s_runtime.rope.rope_latched[seat] = rope;
      st = 0x25u;
    }
  }
  CoopPoseRecovery *pose = &s_runtime.pose[seat];
  /* Native cart exits can leave the rear Kong in passive fall $02/$0001.
   * It follows the leader's Y, so a moving platform prevents the old stationary
   * landing timeout forever. Release only after the shared cart flight ends. */
  uint16_t partner=(uint16_t)s_runtime.slot[seat^1];
  uint16_t partner_state=partner?Dkc1Actor_RAMTable1029Lo(wram,partner):0;
  if (st==2u && Dkc1WramU16(wram,0x32)==5u &&
      Dkc1Actor_RAMTable11A1Lo(wram,slot)==1u &&
      !Dkc1WramU16(wram,0x1929) && !s_runtime.barrel_hold &&
      !Dkc1CoopMinecartViewOwner(wram) &&
      partner_state!=0x2fu && partner_state!=0x40u) {
    Dkc1Actor_SetRAMTable1029Lo(wram,slot,1);
    Dkc1Actor_SetRAMTable11A1Lo(wram,slot,0xc1);
    memset(pose,0,sizeof *pose);
    st=1;
  }
  if (st == 5u) {
    Dkc1Actor_SetRAMTable1029Lo(wram, slot, 0);
    memset(pose, 0, sizeof *pose);
  } else if (st == 2u && (col & 0x0100u) == 0) {
    uint16_t y = Dkc1Actor_SprYPos(wram, slot);
    if (pose->active && y == pose->y) {
      if (pose->ticks < 45) pose->ticks++;
    } else pose->ticks = 0;
    pose->active = 1;
    pose->y = y;
    if (pose->ticks >= 45)
      Dkc1Actor_SetRAMTable1029Lo(wram, slot, 0);
  } else {
    memset(pose, 0, sizeof *pose);
  }
  /* Bit $10 is also the half-tire contact the bounce reads on this tick.
   * Clearing it for a Kong who is not in the follow pose turns the
   * held-jump launch into the short hop. */
  if (st == 5u || st == 2u)
    Dkc1Actor_SetSprCollisionFlags(
        wram, slot,
        (uint16_t)(Dkc1Actor_SprCollisionFlags(wram, slot) & (uint16_t)~0x10u));
  if (!dp)
    return;
  off = (uint16_t)(dp + 0x009Cu);
  Dkc1WramSetU16(wram, off,
                 (uint16_t)(Dkc1WramU16(wram, off) & (uint16_t)~0x40u));
}


/* A script tick may temporarily need the native follower role. The actor
 * transaction always restores it before the next actor; both IDs stay live. */
static void ClearFakeFollow(uint8_t *wram) {
  if (!s_runtime.actor.follower.active || !wram || !s_runtime.actor.follower.slot)
    return;
  Dkc1Actor_SetRAMTable1029Lo(wram, s_runtime.actor.follower.slot, s_runtime.actor.follower.state);
  Dkc1Actor_SetSprCollisionFlags(wram, s_runtime.actor.follower.slot, s_runtime.actor.follower.collision);
  s_runtime.actor.follower.active = 0;
  s_runtime.actor.follower.slot = 0;
}

static void FakePartnerFollow(uint8_t *wram, uint16_t active_id) {
  uint32_t slot = SlotOf(kKong[1]);
  ClearFakeFollow(wram);
  /* Never tag Donkey: the native draw path drops him with collision $10. */
  if (!wram || active_id != kKong[0] || !slot || OverlayQuiet(wram) ||
      !Dkc1CoopKongCam(wram, slot))
    return;
  s_runtime.actor.follower.slot = slot;
  s_runtime.actor.follower.state = Dkc1Actor_RAMTable1029Lo(wram, slot);
  s_runtime.actor.follower.collision = Dkc1Actor_SprCollisionFlags(wram, slot);
  Dkc1Actor_SetRAMTable1029Lo(wram, slot, 5u);
  Dkc1Actor_SetSprCollisionFlags(wram, slot, (uint16_t)(s_runtime.actor.follower.collision | 0x10u));
  s_runtime.actor.follower.active = 1;
}

static int KeepBothIds(uint16_t id) {
  return id == 0x5Du || id == 0x36u;
}

static int KongCanLead(const uint8_t *wram, int i) {
  uint32_t slot = SlotOf(kKong[i]);
  int role;
  if (!slot || Dkc1Actor_SpriteIDLo(wram, slot) != kKong[i])
    return 0;
  if (s_runtime.lock && kKong[i] != s_runtime.lock)
    return 0;
  uint16_t cart = Dkc1CoopMinecartForKong(wram, (uint16_t)slot);
  if (cart && Dkc1Actor_RAMTable1375Lo(wram, cart) != slot) return 0;
  role = Dkc1CoopKongRole(wram, slot);
  if (role == kDkc1CoopRoleGone || role == kDkc1CoopRoleLost)
    return 0;
  if (role == kDkc1CoopRoleDying) {
    int o = i ^ 1;
    uint32_t os = SlotOf(kKong[o]);
    /* $1A is the held-barrel toss as well as a death state. A partner
     * parked in the DK barrel ($0018) must not own $056F or the camera
     * runs back to that sentinel. */
    if (os && Dkc1CoopKongInBarrel(wram, os))
      return 1;
    return 0;
  }
  if (role == kDkc1CoopRoleBarrel) {
    int o = i ^ 1;
    uint32_t os = SlotOf(kKong[o]);
    /* A playable partner must lead so a P2 smash can finish the pop. */
    if (os && Dkc1CoopKongRole(wram, os) == kDkc1CoopRolePlayable)
      return 0;
    /* Native entry briefly parks both actors in $0C. Keep its selected
     * entrant instead of letting the Donkey-first preference replace him. */
    if (os && Dkc1Actor_SpriteIDLo(wram, os) == kKong[o] &&
        Dkc1CoopKongInBarrel(wram, os)) {
      uint16_t current = Dkc1WramU16(wram, DKC1_WRAM_Player_CurrentKongLo);
      if (IsKong(current)) return current == kKong[i];
    }
    return 1;
  }
  return 1;
}

static uint16_t LeaderKong(const uint8_t *wram) {
  if (s_runtime.lock)
    return s_runtime.lock;
  if (KongCanLead(wram, 0))
    return kKong[0];
  if (KongCanLead(wram, 1))
    return kKong[1];
  return 0;
}

static void StreamAsAhead(uint8_t *wram) {
  uint16_t id;
  if (!wram)
    return;
  id = LeaderKong(wram);
  if (id)
    Dkc1WramSetU16(wram, DKC1_WRAM_Player_CurrentKongLo, id);
}



/* Formation records at $7EC000 describe a bitmap, not a single banana.
 * $B8B5D8 starts the bitmap at $D000; +$04 is its byte length, +$0A
 * points at its columns. Each word has 16 rows, bit 15 first, at 8px
 * spacing ($B8BD9C/$B8BEDE/$B8BFDE). +$0C/+ $0E are traversal links.
 * Never rewrite a record's origin or links to collect one of its pieces. */

static uint16_t ByteSwap16(uint16_t v) {
  return (uint16_t)((v << 8) | (v >> 8));
}

static uint16_t PopDelta(uint16_t swapped) {
  return (uint16_t)(~(swapped >> 5) + 1u);
}

static uint32_t FreePopSlot(const uint8_t *wram) {
  uint32_t slot;
  for (slot = 0x34u; slot < 0x74u; slot += 2u) {
    if ((Dkc1Actor_SpriteIDLo(wram, slot) & 0xFFu) == 0)
      return slot;
  }
  return 0;
}

/* BC68/BC94 store the selected piece's screen position in 8.8 form.
 * BFD466 performs the first motion step and awards the banana at the end.
 * Leave unsupported/offscreen pieces to the cartridge path. In particular,
 * do not clamp an offscreen pickup onto another visible banana. */
static int SpawnStockBanana(uint8_t *wram, unsigned world_x, unsigned world_y,
                            uint16_t kong_id) {
  uint16_t camera_x = Dkc1WramU16(wram, DKC1_WRAM_CameraX);
  uint16_t camera_y = Dkc1WramU16(wram, DKC1_WRAM_CameraY);
  /* A sole Diddy is also the native leader: UseKong does not temporarily
   * install his second camera in that case. Pick the effect's view directly. */
  if (kong_id == kKong[1])
    Dkc1CoopWorldGetCamera(wram, 1, &camera_x, &camera_y);
  int sx = (int)world_x + Dkc1WramU16(wram, 0x1B23u) -
           camera_x;
  int sy = (int)world_y - (camera_y & 0x3FFFu);
  uint32_t slot;
  uint16_t x, y;
  if (sx < 0 || sx >= 0xF0 || sy < 0 || sy >= 224)
    return 0;
  slot = FreePopSlot(wram);
  if (!slot)
    return 0;
  x = ByteSwap16((uint16_t)sx);
  y = ByteSwap16((uint16_t)sy);
  Dkc1Actor_SetSprXPos(wram, slot, x);
  Dkc1Actor_SetSprYPos(wram, slot, y);
  Dkc1Actor_SetRAMTable1375Lo(wram, slot, PopDelta(x));
  Dkc1Actor_SetRAMTable13E9Lo(wram, slot, PopDelta(y));
  Dkc1WramSetU16(wram, 0x0C69u + slot, 0x31E0u);
  Dkc1Actor_SetRAMTable1029Lo(wram, slot, 0);
  /* The $34..$72 pool has no independent pose/animation tables. */
  Dkc1WramSetU16(wram, 0x12A5u + slot, 0);
  Dkc1Actor_SetSpriteIDLo(wram, slot, 0x42u);
  Dkc1WramSetU16(wram, DKC1_WRAM_HUD_CounterShowTimer, 0x3Cu);
  Dkc1WramSetU16(wram, 0x00F5u, 0x0021u);
  return 1;
}

/* $BF870D/$BF8711 publish the current Kong's pose/facing-aware bounds
 * at $C5/$C7/$C9/$CB. Consume them at the publishing routine's return,
 * before the other playable Kong replaces this single cartridge rectangle.
 * The later stock collector sees the same bitmap with these bits removed.
 * A sole Diddy still uses his view's camera for the flight; the later
 * cartridge scan otherwise spawns it in the primary camera's pixels.
 * A sole Donkey also collects here: a dying partner still replaces the
 * cartridge rectangle before the later scan. Unsupported bounds stay stock. */
static void CollectKongFormations(uint8_t *wram, uint16_t kong_id) {
  unsigned off, left, top, width, height, right, bottom;
  uint32_t slot = SlotOf(kong_id);
  int cart = 0;
  if (!slot)
    return;
  cart = Dkc1CoopMinecartForKong(wram, (uint16_t)slot) != 0;
  if ((OverlayQuiet(wram) && !cart) ||
      Dkc1CoopKongRole(wram, slot) != kDkc1CoopRolePlayable)
    return;
  left = Dkc1WramU16(wram, 0xC5u);
  top = Dkc1WramU16(wram, 0xC7u) & 0x3FFFu;
  width = Dkc1WramU16(wram, 0xC9u);
  height = Dkc1WramU16(wram, 0xCBu);
  if (!width || width > 256u || !height || height > 256u)
    return;
  right = left + width;
  bottom = top + height;
  for (off = 0; off < 0x1000u; off += 0x10u) {
    Dkc1CoopFormationRecord record;
    int valid=Dkc1CoopFormationRead(wram,0xc000u+off,&record);
    if (valid<0) break;
    if (!valid || record.y>=0x4000u || !(record.flags&wram[0xc4u]&3u)) continue;
    unsigned fx=record.x, fy=record.y, bytes=record.bytes, ptr=record.bitmap, col;
    for (col = 0; col < bytes; col += 2u) {
      unsigned bx = fx + col * 4u;
      uint16_t bits;
      /* BB4B/BBB4 subtract 8, then floor to the 8px bitmap grid.
       * A leading gap of 8..15 still selects that column/row. */
      if (bx + 15u < left || bx >= right)
        continue;
      bits = Dkc1WramU16(wram, ptr + col);
      for (unsigned row = 0; row < 16u && bits; row++) {
        uint16_t bit = (uint16_t)(0x8000u >> row);
        unsigned by = fy + row * 8u;
        if (!(bits & bit) || by + 15u < top || by >= bottom)
          continue;
        if (SpawnStockBanana(wram, bx, by, kong_id)) {
          bits = (uint16_t)(bits & (uint16_t)~bit);
          Dkc1WramSetU16(wram, ptr + col, bits);
        }
      }
    }
  }
}

/* BF8749 is the RTS after all four rectangle stores. No pose allowlist:
 * a stale shared box cannot be collected by a later actor or host frame. */
void Dkc1CoopPublishPickupBounds(CpuState *cpu) {
  if (!cpu || !cpu->ram || cpu->D > 0x1f7c || !s_runtime.enabled ||
      RtlSpeculativeExecutionActive() || !s_runtime.actor.mux || !s_runtime.actor.mux_kong ||
      Dkc1WramU16(cpu->ram, cpu->D + 0x82u) != SlotOf(s_runtime.actor.mux_id))
    return;
  CollectKongFormations(cpu->ram, s_runtime.actor.mux_id);
}







static void RefreshLock(const uint8_t *wram) {
  int i;
  uint32_t ls;
  uint16_t living = 0, corpse = 0;
  if (!wram)
    return;
  /* Barrel wait/pop is not a death-run. Do not pin $056F away from Donkey. */
  if (OverlayQuiet(wram))
    return;
  for (i = 0; i < 2; i++) {
    uint32_t s = SlotOf(kKong[i]);
    int role = Dkc1CoopKongRole(wram, s);
    if (role == kDkc1CoopRoleDying)
      corpse = kKong[i];
    else if (Dkc1CoopKongCam(wram, s))
      living = kKong[i];
  }
  if (corpse && living)
    s_runtime.lock = living;
  ls = s_runtime.lock ? SlotOf(s_runtime.lock) : 0;
  if (s_runtime.lock && !Dkc1CoopKongCam(wram, ls))
    s_runtime.lock = 0;
  /* The pin lasts while the other Kong is off camera. A returned Donkey
   * leads again; leaving the lock on Diddy keeps the upper camera there. */
  if (s_runtime.lock) {
    int other = s_runtime.lock == kKong[0] ? 1 : 0;
    uint32_t os = SlotOf(kKong[other]);
    if (os && Dkc1CoopKongCam(wram, os))
      s_runtime.lock = 0;
  }
}

static int LevelExitPose(uint16_t state) {
  return state == 0x1bu || state == 0x1du || state == 0x1eu;
}

static void DropLostCorpse(uint8_t *wram) {
  int i;
  if (!wram || Dkc1WramU16(wram, 0x1929u))
    return;
  for (i = 0; i < 2; i++) {
    uint32_t slot = SlotOf(kKong[i]);
    uint16_t state;
    if (!slot || !s_runtime.corpse[i])
      continue;
    state = Dkc1Actor_RAMTable1029Lo(wram, slot);
    if (state != 0x0cu && state != 0x0du && state != 0x09u &&
        state != 0x19u && state != 0x39u && state != 0x3au)
      continue;
    Dkc1Actor_SetSpriteIDLo(wram, slot, 0);
    Dkc1Actor_SetRAMTable1029Lo(wram, slot, 0);
    s_runtime.corpse[i] = 0;
  }
}

static void SyncKongState(const uint8_t *wram) {
  Dkc1CoopCacheKongSlots(wram, s_runtime.slot);
  NoteCorpse(wram);
  RefreshLock(wram);
}

/* Shared health/leader commit; damage and fatal fall keep their own native
 * entry predicates and animal continuations. */
static void CommitSurvivorHandoff(uint8_t *wram, uint16_t fallen,
                                  uint16_t survivor, uint16_t id) {
  Dkc1WramSetU16(wram, 0x0579u, Dkc1WramU16(wram, 0x0579u) & ~1u);
  Dkc1WramSetU16(wram, 0x1929u, 0);
  uint16_t flags = (uint16_t)(0x1699u + survivor - 2u);
  Dkc1WramSetU16(wram, flags, Dkc1WramU16(wram, flags) & ~0x88u);
  Dkc1WramSetU16(wram, DKC1_WRAM_Player_CurrentKongLo, id);
  s_runtime.lock = id;
  int seat = SeatOfSlot(fallen);
  if (seat >= 0) s_runtime.corpse[seat] = 1;
}

/* Survival is gameplay ownership, not ownership of a private camera.
 * A native seated cart driver has one shared camera and can still continue. */
static int SurvivorCanContinue(const uint8_t *wram,uint16_t survivor) {
  return Dkc1CoopKongRole(wram,survivor)==kDkc1CoopRolePlayable &&
      (Dkc1CoopKongOwnCamera(wram,survivor) ||
       Dkc1CoopMinecartForKong(wram,survivor));
}

int Dkc1CoopIndependentDamage(struct CpuState *cpu) {
  if (!s_runtime.enabled || !cpu || !cpu->ram || cpu->m_flag || cpu->x_flag)
    return 0;
  uint8_t *wram = cpu->ram;
  uint16_t hit = cpu->X;
  if ((hit != 2 && hit != 4) || !(Dkc1CoopInLevel(wram) || Dkc1CoopFixedBossRoom(wram)))
    return 0;
  uint16_t survivor = hit ^ 6u;
  uint16_t id = Dkc1Actor_SpriteIDLo(wram, survivor);
  uint16_t hit_id = Dkc1Actor_SpriteIDLo(wram, hit);
  if (!IsKong(id) || !IsKong(hit_id) || id == hit_id ||
      (Dkc1Actor_RAMTable1029Lo(wram, hit) != 0x0du &&
       Dkc1Actor_RAMTable1029Lo(wram, hit) != 0x3au) ||
      !SurvivorCanContinue(wram,survivor) ||
      OverlayQuiet(wram) || Dkc1CoopAnimalForSeat(wram, hit / 2u - 1u) ||
      (KongAnimalRide(wram, survivor / 2u - 1u) &&
       !Dkc1CoopAnimalForSeat(wram, survivor / 2u - 1u)))
    return 0;
  /* A mounted survivor keeps its native rider/animal association. Only
   * the hit Kong needs the cartridge animal-damage continuation. */
  /* $BFA191 / $BFA3BE have already applied the hit and death animation to X.
   * Water enters here at $A3F9, after PLX, preserving its native sink. Native
   * $A1D4 would overwrite the survivor's state/timer, then $8FC6 -> $8DEA
   * pulls that Kong to the hit partner. Co-op retains the survivor's actual
   * controller/pose, including a carried object. Commit only the handoff
   * bookkeeping proven at $BF8E77/$8EAD/$8EB5; without it a second hit would
   * still believe two Kongs are alive and DK-barrel revival could not close. */
  CommitSurvivorHandoff(wram, hit, survivor, id);
  return 1;
}

/* $BFA6D1 is reached only after the cartridge has declared a fatal fall.
 * Keep the cartridge's last-Kong path when nobody can continue. */
int Dkc1CoopIndependentFall(struct CpuState *cpu) {
  if (!s_runtime.enabled || !cpu || !cpu->ram || cpu->D || cpu->m_flag || cpu->x_flag)
    return 0;
  uint8_t *wram = cpu->ram;
  uint16_t fallen = Dkc1WramU16(wram, 0x82u);
  if ((fallen != 2 && fallen != 4) || !(Dkc1CoopInLevel(wram) || Dkc1CoopFixedBossRoom(wram)))
    return 0;
  uint16_t survivor = fallen ^ 6u;
  uint16_t id = Dkc1Actor_SpriteIDLo(wram, survivor);
  if (!IsKong(id) || !IsKong(Dkc1Actor_SpriteIDLo(wram, fallen)) ||
      id == Dkc1Actor_SpriteIDLo(wram, fallen) ||
      !SurvivorCanContinue(wram,survivor) ||
      Dkc1Actor_RAMTable1029Lo(wram, survivor) == 0x3bu || OverlayQuiet(wram))
    return 0;
  /* The body is already below the fatal boundary. Use the normal death
   * continuation, without the native $3B level-exit timer or life debit. */
  uint16_t link = (uint16_t)(0x0512u + fallen - 2u);
  if (Dkc1WramU16(wram, link)) {
    uint16_t animal = Dkc1CoopAnimalForSeat(wram, fallen / 2u - 1u);
    if (animal) {
      /* $BFC927 consumes event $08 on the mounted animal and returns it
       * to its free pose. Clearing only the link leaves its rider state
       * alive; the next mount then consumes $08 as a dismount instead. */
      Dkc1Actor_SetSprEventFlags(wram, animal, 8u);
    }
    Dkc1WramSetU16(wram, link, 0);
    if (!Dkc1WramU16(wram, 0x0512u + survivor - 2u)) {
      Dkc1WramSetU16(wram, 0x0516u, 0);
      Dkc1WramSetU16(wram, 0x0518u, 0);
    }
  }
  Dkc1Actor_SetRAMTable1029Lo(wram, fallen, 0x0du);
  CommitSurvivorHandoff(wram, fallen, survivor, id);
  return 1;
}

static void ResetOverlayState(void) {
  int enabled=s_runtime.enabled;
  struct Ppu *ppu=s_runtime.ppu;
  memset(&s_runtime,0,sizeof s_runtime);
  s_runtime.enabled=enabled;
  s_runtime.ppu=ppu;
}

typedef char Dkc1CoopSnapshotSizeCheck[sizeof(Dkc1CoopSnapshot) == 20 ? 1 : -1];
typedef char Dkc1CoopActorSnapshotSizeCheck[
    sizeof(Dkc1CoopActorSnapshot) == 64 ? 1 : -1];



static void ClearSuspendedActor(void) {
  memset(&s_runtime.actor.wait, 0, sizeof s_runtime.actor.wait);
  s_runtime.actor.wait_age = 0;
}

static void CaptureSuspendedActor(uint8_t *wram, uint32_t resume_pc, uint16_t stack) {
  ClearSuspendedActor();
  s_runtime.actor.wait.version = 4;
  s_runtime.actor.wait.reserved[0] = s_runtime.actor.cannon.object;
  s_runtime.actor.wait.reserved[1] = s_runtime.actor.cannon.partner;
  s_runtime.actor.wait.pending = 1;
  s_runtime.actor.wait.resume_pc = resume_pc & 0xffffffu;
  s_runtime.actor.wait.stack = stack;
  s_runtime.actor.wait.scene[0] = Dkc1WramU16(wram, 0x32u);
  s_runtime.actor.wait.scene[1] = Dkc1WramU16(wram, 0x3eu);
  for (unsigned i = 0; i < 16; ++i)
    s_runtime.actor.wait.words[i] = Dkc1WramU16(wram, kOverlayWords[i]);
  s_runtime.actor.wait.camera = HeldCameraMatchesScene(wram);
  if (s_runtime.actor.wait.camera)
    s_runtime.actor.wait.camera |= Dkc1WramU16(wram, 0x0579u) & 0x88u;
  s_runtime.actor.wait.mux_kong = (uint16_t)s_runtime.actor.mux_kong;
  s_runtime.actor.wait.mux_id = s_runtime.actor.mux_id;
  if (s_runtime.actor.follower.active) {
    s_runtime.actor.wait.fake_slot = (uint16_t)s_runtime.actor.follower.slot;
    s_runtime.actor.wait.fake_id = Dkc1Actor_SpriteIDLo(wram, s_runtime.actor.follower.slot);
    s_runtime.actor.wait.fake_state = Dkc1Actor_RAMTable1029Lo(wram, s_runtime.actor.follower.slot);
    s_runtime.actor.wait.fake_collision = Dkc1Actor_SprCollisionFlags(wram, s_runtime.actor.follower.slot);
  }
}

void Dkc1CoopSuspendActor(uint8_t *wram, uint32_t resume_pc, uint16_t stack) {
  if (!wram || !s_runtime.actor.mux || s_runtime.actor.wait.pending) return;
  CaptureSuspendedActor(wram, resume_pc, stack);
  ClearFakeFollow(wram);
  RestoreCanonicalContext(wram);
  /* This is not actor completion: no formation collection or second tick.
   * Rendering, source scans, and the next NMI now see canonical state. */
}

/* A level exit abandons the suspended routine. Waiting for its PC leaves
 * both pads unmuxed. Drop the wait when the scene changes, and when the
 * next frame of the same scene never returns to that PC. */
static void DropStaleActorWait(const uint8_t *wram) {
  if (!wram || !s_runtime.actor.wait.pending) {
    s_runtime.actor.wait_age = 0;
    return;
  }
  if (s_runtime.actor.wait.scene[0] != Dkc1WramU16(wram, 0x32u) ||
      s_runtime.actor.wait.scene[1] != Dkc1WramU16(wram, 0x3eu) ||
      s_runtime.actor.wait_age >= 1) {
    ClearSuspendedActor();
    s_runtime.actor.captured = s_runtime.actor.mux = s_runtime.actor.mux_kong = 0;
    s_runtime.actor.mux_id = 0;
    s_runtime.actor.cannon.object = s_runtime.actor.cannon.partner = 0;
    return;
  }
  s_runtime.actor.wait_age = 1;
}

static void InstallSuspendedActor(uint8_t *wram) {
  if (s_runtime.actor.wait.camera) {
    CaptureActorCamera(wram);
  }
  for (unsigned i = 0; i < (s_runtime.actor.wait.camera ? 16u : 11u); ++i)
    Dkc1WramSetU16(wram, kOverlayWords[i], s_runtime.actor.wait.words[i]);
  if (s_runtime.actor.wait.camera && s_runtime.actor.wait.version >= 3)
    Dkc1WramSetU16(wram, 0x0579u,
        (Dkc1WramU16(wram, 0x0579u) & ~0x88u) | (s_runtime.actor.wait.camera & (s_runtime.actor.wait.version>=4?0x88u:8u)));
  if (s_runtime.actor.wait.fake_slot && Dkc1Actor_SpriteIDLo(wram,
      s_runtime.actor.wait.fake_slot) == s_runtime.actor.wait.fake_id) {
    s_runtime.actor.follower.slot = s_runtime.actor.wait.fake_slot;
    s_runtime.actor.follower.state = Dkc1Actor_RAMTable1029Lo(wram, s_runtime.actor.follower.slot);
    s_runtime.actor.follower.collision = Dkc1Actor_SprCollisionFlags(wram, s_runtime.actor.follower.slot);
    Dkc1Actor_SetRAMTable1029Lo(wram, s_runtime.actor.follower.slot, s_runtime.actor.wait.fake_state);
    Dkc1Actor_SetSprCollisionFlags(wram, s_runtime.actor.follower.slot, s_runtime.actor.wait.fake_collision);
    s_runtime.actor.follower.active = 1;
  }
  s_runtime.actor.mux = 1;
  s_runtime.actor.mux_kong = s_runtime.actor.wait.mux_kong;
  s_runtime.actor.mux_id = s_runtime.actor.wait.mux_id;
  s_runtime.actor.cannon.object = s_runtime.actor.wait.reserved[0];
  s_runtime.actor.cannon.partner = s_runtime.actor.wait.reserved[1];
  ClearSuspendedActor();
}

void Dkc1CoopResumeActor(uint8_t *wram, uint32_t pc, uint16_t stack) {
  if (!wram || !s_runtime.actor.wait.pending ||
      (pc & 0xffffffu) != s_runtime.actor.wait.resume_pc || stack != s_runtime.actor.wait.stack)
    return;
  if (s_runtime.actor.wait.scene[0] != Dkc1WramU16(wram, 0x32u) ||
      s_runtime.actor.wait.scene[1] != Dkc1WramU16(wram, 0x3eu)) {
    DropStaleActorWait(wram);
    return;
  }
  /* NMI can update canonical pad levels between deadline and continuation.
   * Restore these new values on completion, while the unfinished routine
   * keeps the exact input and camera with which it began. */
  CaptureCanonicalContext(wram);
  s_runtime.actor.captured = 1;
  InstallSuspendedActor(wram);
}

void Dkc1CoopSaveActorState(Dkc1CoopActorSnapshot *state) {
  *state = s_runtime.actor.wait;
  state->version = 4;
}

int Dkc1CoopActorPending(void) { return s_runtime.actor.wait.pending != 0; }

int Dkc1CoopLoadActorState(const Dkc1CoopActorSnapshot *state,
                         const uint8_t *wram) {
  ClearSuspendedActor();
  if (!state || !wram || state->version < 1 || state->version > 4 || state->pending > 1 ||
      (state->version == 1 && (state->reserved[0] || state->reserved[1])) ||
      (state->reserved[0] && !Dkc1CoopEntityIsHandle(state->reserved[0])) ||
      (state->reserved[1] && state->reserved[1] != 2 && state->reserved[1] != 4 && state->reserved[1] != 6))
    return 0;
  if (!state->pending)
    return 1;
  if (!state->resume_pc || state->resume_pc > 0xffffffu ||
      (state->version < 3 ? state->camera > 1 :
       (state->version == 3 ? (state->camera != 0 && state->camera != 1 && state->camera != 9) :
        ((state->camera & ~0x89u) || (state->camera && !(state->camera&1u))))) ||
      state->mux_kong > 1 || (state->mux_kong && !IsKong(state->mux_id)) ||
      (state->fake_slot && (!Dkc1CoopEntityIsHandle(state->fake_slot) ||
                           state->fake_id != kKong[1])) ||
      state->scene[0] != Dkc1WramU16(wram, 0x32u) ||
      state->scene[1] != Dkc1WramU16(wram, 0x3eu))
    return 0;
  s_runtime.actor.wait = *state;
  s_runtime.actor.mux = 1;
  s_runtime.actor.mux_kong = state->mux_kong;
  s_runtime.actor.mux_id = state->mux_id;
  s_runtime.actor.cannon.object = state->reserved[0];
  s_runtime.actor.cannon.partner = state->reserved[1];
  return 1;
}

static uint16_t PackMountIntent(uint16_t intent) {
  return intent ? (uint16_t)(((intent >> 8) << 2) | ((intent & 0xffu) / 2u)) : 0;
}

static uint16_t UnpackMountIntent(uint16_t packed) {
  return packed ? (uint16_t)(((packed >> 2) << 8) | ((packed & 3u) * 2u)) : 0;
}

static int ValidMountIntent(uint16_t intent) {
  return !intent || (MountSprite(intent >> 8) &&
                    ((intent & 0xffu) == 2 || (intent & 0xffu) == 4));
}

void Dkc1CoopSaveContinuation(Dkc1CoopContinuation *state) {
  memset(state, 0, sizeof *state);
  state->version = 5;
  state->rope = s_runtime.rope;
  state->weather = s_runtime.weather;
  state->weather.reserved = (uint16_t)(PackMountIntent(s_runtime.room_mount) |
                                     (PackMountIntent(s_runtime.carried_mount) << 8));
  state->actor_wait_age = s_runtime.actor.wait_age;
  state->kritter_cam = s_runtime.kritter_cam;
  state->kritter_cam_on = (uint16_t)s_runtime.kritter_cam_on;
}

void Dkc1CoopLoadContinuation(const Dkc1CoopContinuation *state) {
  if (!state || state->version < 1 || state->version > 5 || !s_runtime.enabled ||
      state->actor_wait_age > 1 || state->kritter_cam_on > 1 ||
      state->weather.clearing > 1 ||
      (state->version < 3 && state->weather.reserved > 1) ||
      (state->version == 3 && !ValidMountIntent(state->weather.reserved)) ||
      (state->version >= 4 &&
       (!ValidMountIntent(UnpackMountIntent(state->weather.reserved & 0xffu)) ||
        !ValidMountIntent(UnpackMountIntent(state->weather.reserved >> 8)))) ||
      (state->version == 1 && state->weather.reserved)) return;
  s_runtime.rope = state->rope;
  if (state->version < 5)
    for (int seat = 0; seat < 2; ++seat)
      if (s_runtime.rope.rider_hook[seat] & 1u) s_runtime.rope.rider_hook[seat] = 0;
  s_runtime.weather = state->weather;
  s_runtime.weather.reserved = 0; /* Ignore the withdrawn v2 bonus-lighting marker. */
  s_runtime.room_mount = state->version >= 4 ? UnpackMountIntent(state->weather.reserved & 0xffu) :
                 state->version == 3 ? state->weather.reserved : 0;
  s_runtime.carried_mount = state->version >= 4 ? UnpackMountIntent(state->weather.reserved >> 8) : 0;
  s_runtime.actor.wait_age = (uint8_t)state->actor_wait_age;
  s_runtime.kritter_cam = state->kritter_cam;
  s_runtime.kritter_cam_on = state->kritter_cam_on != 0;
}

void Dkc1CoopSaveState(Dkc1CoopSnapshot *state) {
  memset(state, 0, sizeof *state);
  state->version = 5;
  state->enabled = s_runtime.enabled != 0;
  state->previous[0] = s_runtime.prev[0];
  state->previous[1] = s_runtime.prev[1];
  state->leader_lock = s_runtime.lock;
  state->barrel_hold = (uint16_t)s_runtime.barrel_hold;
  /* v5 keeps the 20-byte wire; bits12..14 retain the animal-bonus owner.
   * Old versions carry no unrecorded transition owner. */
  state->pose_x = s_runtime.pose[0].y;
  state->pose_y = s_runtime.pose[1].y;
  state->pose_stuck = (uint16_t)(s_runtime.pose[0].ticks | (s_runtime.pose[1].ticks << 8));
  state->reserved = (uint16_t)(s_runtime.pose[0].active | (s_runtime.pose[1].active << 1) |
                             s_runtime.squawks | (s_runtime.animal_bonus << 12));
}

void Dkc1CoopLoadState(const Dkc1CoopSnapshot *state, const uint8_t *wram) {
  ResetOverlayState();
  Dkc1CoopCacheKongSlots(wram, s_runtime.slot);
  /* Old saves lack co-op history. Seed button levels from serialized WRAM;
   * Snes.input*_currentState is host input and is NOT part of the save.
   * Timers and death handoff start cold rather than inherit a later branch. */
  if (wram) {
    s_runtime.prev[0] = Dkc1WramU16(wram, DKC1_WRAM_JoypadHeld) & 0xFFF0u;
    s_runtime.prev[1] = Dkc1WramU16(wram, DKC1_WRAM_Global_HeldButtonsLoP2) & 0xFFF0u;
  }
  if (!state || !s_runtime.enabled || state->enabled != 1 ||
      (state->version < 1 || state->version > 5) ||
      (state->version == 1 ? state->reserved != 0 : state->version == 2 ?
       state->reserved != 0 && state->reserved != 2 && state->reserved != 4 :
       state->version == 3 ? state->reserved > 3 :
       (state->version == 4 ? (state->reserved & 0xf000u) :
        ((state->reserved & 0x8000u) || ((state->reserved >> 12) & 3u) == 3u ||
         ((state->reserved & 0x4000u) && !(state->reserved & 0x3000u)))) ||
       ((state->reserved & 0x0ffcu) &&
        (!Dkc1CoopEntityIsHandle((state->reserved & 0x0ffcu) >> 4) ||
         !IsKong((state->reserved >> 2)&3u)))) ||
      state->leader_lock > 2 || state->barrel_hold > 60 ||
      (state->version < 3 ? state->pose_stuck > 45 :
       (state->pose_stuck & 255u) > 45 || (state->pose_stuck >> 8) > 45) ||
      (state->previous[0] & 0xFu) ||
      (state->previous[1] & 0xFu))
    return;
  memcpy(s_runtime.prev, state->previous, sizeof s_runtime.prev);
  s_runtime.lock = state->leader_lock;
  s_runtime.barrel_hold = state->barrel_hold;
  if (state->version >= 3) {
    s_runtime.squawks=state->version >= 4 ? state->reserved & 0x0ffcu : 0;
    s_runtime.animal_bonus=state->version >= 5 ? state->reserved >> 12 : 0;
    s_runtime.pose[0] = (CoopPoseRecovery){state->pose_x, state->pose_stuck & 255u, state->reserved & 1u};
    s_runtime.pose[1] = (CoopPoseRecovery){state->pose_y, state->pose_stuck >> 8, (state->reserved >> 1) & 1u};
  } else if (state->version == 2 && state->reserved) {
    unsigned seat = state->reserved / 2u - 1u;
    s_runtime.pose[seat] = (CoopPoseRecovery){state->pose_y, state->pose_stuck, 1};
  }
}

int Dkc1CoopSavedEnabled(void) {
#if defined(_WIN32)
  return RegReadInt(L"Coop", 0) != 0;
#else
  int enabled = 0;
  return EnvForcesCoop(&enabled) ? enabled : 0;
#endif
}

void Dkc1CoopSetEnabled(int enabled) {
  if (s_runtime.enabled != (enabled != 0))
    Dkc1CoopWorldReset();
  s_runtime.enabled = enabled != 0;
#if defined(_WIN32)
  RegWriteInt(L"Coop", s_runtime.enabled);
#endif
  /* A paused routine still owns its substitutions until its return. The
   * option affects future actors; losing this context would leak its camera. */
  if (!s_runtime.actor.mux && !s_runtime.actor.wait.pending)
    ResetOverlayState();
}

void Dkc1CoopInitialize(void) {
  int forced = 0;
  ResetOverlayState();
  if (EnvForcesCoop(&forced)) {
    s_runtime.enabled = forced;
    return;
  }
#if defined(_WIN32)
  s_runtime.enabled = Dkc1CoopSavedEnabled();
#endif
}

int Dkc1CoopEnabled(void) { return s_runtime.enabled; }


const char *Dkc1CoopStatus(void) {
  return s_runtime.enabled ? "simultaneous co-op on (experimental)"
                   : "simultaneous co-op off";
}

/* $80:E565 stores each map head as Y = $0100 | screen_row. $80:A4E1
 * then subtracts $088B/$0895. After a fall those words are still the
 * level camera, and both heads are parked at the hide point. */
static int LevelMap(const uint8_t *wram) {
  uint32_t slot;
  if (!wram || Dkc1WramU16(wram, 0x32u) != 0 ||
      Dkc1WramU16(wram, 0x1b25u) != 0 || Dkc1WramU16(wram, 0x4au) != 0)
    return 0;
  for (slot = DKC1_ACTOR_SLOT_FIRST; slot <= DKC1_ACTOR_SLOT_LAST; slot += 2u) {
    if ((Dkc1Actor_SpriteIDLo(wram, slot) & 0xffu) != kDkc1CoopSpriteDiddy)
      continue;
    if (Dkc1Actor_DisplayedPoseLo(wram, slot) != 0x0088u)
      continue;
    if ((Dkc1Actor_SprYPos(wram, slot) & 0xff00u) == 0x0100u)
      return 1;
  }
  return 0;
}

static unsigned MapSeats(const uint8_t *ram) {
  if (!s_runtime.enabled || !ram || Dkc1CoopInLevel(ram) ||
      Dkc1Actor_SpriteIDLo(ram,0)!=0x10u) return 0;
  uint16_t id=Dkc1WramU16(ram,DKC1_WRAM_Player_CurrentKongLo);
  if (!IsKong(id)) return 0;
  /* E2E4 draws the partner from this cartridge flag, not pooled gameplay IDs. */
  return (Dkc1WramU16(ram,0x0579u) & 1u)?3u:1u << (id-1u);
}

uint16_t Dkc1CoopNativePadOwner(struct CpuState *cpu, uint16_t native) {
  if (!s_runtime.enabled || !cpu || !cpu->ram || cpu->D || cpu->m_flag || cpu->x_flag)
    return native;
  const uint8_t *ram=cpu->ram;
  if (!(Dkc1WramU16(ram,0x1e15) & 0x20u)) return native;
  unsigned caps=Dkc1CoopSceneCapabilities(ram);
  unsigned fixed=kDkc1CoopScenePresentation | kDkc1CoopSceneFixed;
  if ((caps & fixed)!=fixed || (caps & kDkc1CoopSceneControl)) return native;
  uint16_t id=Dkc1WramU16(ram,DKC1_WRAM_Player_CurrentKongLo);
  if (!IsKong(id) || Dkc1WramU16(ram,0x84)!=id*2u-2u ||
      Dkc1Actor_SpriteIDLo(ram,id*2u)!=id) return native;
  /* BFB2C7 chooses a physical pad by cartridge player turn. A native bonus
   * keeps one selected Kong; co-op instead binds that Kong to his own pad. */
  return (uint16_t)Seat(id);
}

static uint16_t MapPad(const uint8_t *ram, unsigned seats, unsigned address) {
  uint16_t result=0;
  for (unsigned seat=0; seat<2; ++seat)
    if (seats & (1u << seat)) result |= Dkc1WramU16(ram,address+seat*2u);
  return result;
}

/* Run at C1C8, after physical pad levels/edges have been polled and before
 * map transitions E75E/E822 or avatar navigation D199 consume effective pads. */
void Dkc1CoopPublishMapPads(struct CpuState *cpu) {
  if (!cpu || !cpu->ram || cpu->D || cpu->m_flag || cpu->x_flag) return;
  unsigned seats=MapSeats(cpu->ram);
  if (!seats) return;
  Dkc1WramSetU16(cpu->ram, DKC1_WRAM_Global_HeldButtonsLo,
      MapPad(cpu->ram,seats,DKC1_WRAM_JoypadHeld));
  Dkc1WramSetU16(cpu->ram, DKC1_WRAM_Global_PressedButtonsLo,
      MapPad(cpu->ram,seats,DKC1_WRAM_JoypadPressed));
}

/* Native E2FD..E321 selects avatar0 animation $1E for Donkey, $1C for
 * Diddy. Bit1 distinguishes their paired map animations. The native map
 * controller remains sprite $10; only its ordinary animation follows the
 * selected Kong, including after an independent co-op death. */
void Dkc1CoopSelectMapAvatar(struct CpuState *cpu) {
  if (!s_runtime.enabled || !cpu || !cpu->ram || cpu->D || cpu->m_flag || cpu->x_flag ||
      Dkc1CoopInLevel(cpu->ram) || Dkc1Actor_SpriteIDLo(cpu->ram, 0) != 0x10u)
    return;
  uint16_t id = Dkc1WramU16(cpu->ram, DKC1_WRAM_Player_CurrentKongLo);
  if (!IsKong(id)) return;
  uint16_t animation = Dkc1WramU16(cpu->ram, 0x1105u);
  if ((animation & 1u) || animation > 0x22u) return;
  Dkc1WramSetU16(cpu->ram, 0x1105u,
      (uint16_t)((animation & ~2u) | (id == kKong[0] ? 2u : 0u)));
}

static void ReleaseMapCamera(uint8_t *wram) {
  if (!LevelMap(wram))
    return;
  Dkc1WramSetU16(wram, DKC1_WRAM_CameraX, 0);
  Dkc1WramSetU16(wram, DKC1_WRAM_CameraY, 0);
}

/* Called once per guest frame, after the runtime has selected and normalized
 * both controllers. Presentation and platform polling do not own this clock. */
void Dkc1CoopBeginFrame(uint8_t *wram, uint32_t packed24) {
  s_runtime.host = packed24;
  DropStaleActorWait(wram);
  if (!s_runtime.actor.mux)
    s_runtime.actor.captured = 0;
  if (!s_runtime.enabled || !wram)
    return;
  if (Dkc1WramU16(wram, 0x1e15) & 0x400u) {
    if (s_runtime.animal_bonus & 3u) s_runtime.animal_bonus |= 4u;
  } else if (s_runtime.animal_bonus & 4u) {
    s_runtime.animal_bonus = 0;
  }
  ReleaseMapCamera(wram);
  LatchPads();
  JoinMinecartPassengers(wram);
  PublishCannonPads(wram);
  {
    s_runtime.rope.frame_aff = Dkc1WramU16(wram, 0x1AFFu);
    s_runtime.rope.frame_b01 = Dkc1WramU16(wram, 0x1B01u);
    uint16_t hook = s_runtime.rope.frame_aff;
    if (!HookSprite(wram, hook))
      hook = s_runtime.rope.frame_b01;
    s_runtime.rope.hook_pending = HookSprite(wram, hook) ? hook : 0;
  }
}

static void ShareSwingPose(uint8_t *wram, const uint32_t slot[2],
                           const int swing[2]);

/* Sprite $31 has one swing point and writes it to one Kong. The partner
 * who joined keeps state $0026 but stays where the grab happened. */
static void ShareSwingPoint(uint8_t *wram) {
  uint32_t slot[2];
  uint16_t x[2], y[2];
  int swing[2], moved[2], i;
  if (!s_runtime.slot[0] || !s_runtime.slot[1])
    return;
  for (i = 0; i < 2; i++) {
    slot[i] = s_runtime.slot[i];
    swing[i] = Dkc1Actor_RAMTable1029Lo(wram, slot[i]) == 0x0026u;
    x[i] = (uint16_t)Dkc1Actor_SprXPos(wram, slot[i]);
    y[i] = (uint16_t)Dkc1Actor_SprYPos(wram, slot[i]);
    moved[i] = s_runtime.rope.swing_seen[i] && swing[i] &&
        (x[i] != s_runtime.rope.swing_x[i] || y[i] != s_runtime.rope.swing_y[i]);
  }
  if (HookSprite(wram, s_runtime.rope.rider_hook[0]) && HookSprite(wram, s_runtime.rope.rider_hook[1]) &&
      s_runtime.rope.rider_hook[0] == s_runtime.rope.rider_hook[1] &&
      swing[0] && swing[1] && moved[0] != moved[1]) {
    int source = moved[0] ? 0 : 1, destination = source ^ 1;
    Dkc1Actor_SetSprXPos(wram, slot[destination], x[source]);
    Dkc1Actor_SetSprYPos(wram, slot[destination], y[source]);
    x[destination] = x[source];
    y[destination] = y[source];
  }
  for (i = 0; i < 2; i++) {
    s_runtime.rope.swing_seen[i] = (uint8_t)swing[i];
    s_runtime.rope.swing_x[i] = x[i];
    s_runtime.rope.swing_y[i] = y[i];
  }
  ShareSwingPose(wram, slot, swing);
}

/* BFED33 writes the swing pose to the one Kong in Y. The frame is the
 * rope pose relative to $2670, added to $063C for handle 2 and $0E38
 * for the partner. Bit $4000 of the rope flags uses $78 minus that
 * distance, the other side of the swing. A nonzero rope $1491 selects
 * the single frame at +$78. */
static void ShareSwingPose(uint8_t *wram, const uint32_t slot[2],
                           const int swing[2]) {
  uint16_t hook, raw, delta, pose;
  int i;
  if (!swing[0] || !swing[1])
    return;
  const int separate_hooks =
      HookSprite(wram, s_runtime.rope.rider_hook[0]) &&
      HookSprite(wram, s_runtime.rope.rider_hook[1]) &&
      s_runtime.rope.rider_hook[0] != s_runtime.rope.rider_hook[1];
  if (!separate_hooks) {
    hook = Dkc1WramU16(wram, 0x1B01u);
    if (!HookSprite(wram, hook)) return;
  }
  for (i = 0; i < 2; i++) {
    /* A shared hook is sampled once, before either rider's pose is written. */
    if (separate_hooks || i == 0) {
      if (separate_hooks) hook = s_runtime.rope.rider_hook[i];
      raw = (uint16_t)(Dkc1Actor_SprAnimFrame(wram, hook) - 0x2670u);
      delta = (Dkc1Actor_SprFlags(wram, hook) & 0x4000u)
                  ? (uint16_t)(0x0078u - raw) : raw;
      if (Dkc1Actor_RAMTable1491Lo(wram, hook) != 0) delta = 0x0078u;
    }
    pose = (uint16_t)((slot[i] == 2u ? 0x063Cu : 0x0E38u) + delta);
    Dkc1Actor_SetSprAnimFrame(wram, slot[i], pose);
    Dkc1Actor_SetDisplayedPoseLo(wram, slot[i], pose);
  }
}

static int CannonShot(uint16_t st) {
  return st == 0x28u; /* $2A owns a separate native level-transition path. */
}

/* Plain rider branch of $BFE82F -> Player_SetAnimation($17). The locked
 * ROM selects $17/$7F through $84 and immediately consumes the first frame.
 * Each Kong must own that animation; a copied $29 still has a hidden pose. */
static void StartCannonSpin(uint8_t *wram, uint32_t slot) {
  const int diddy = Dkc1Actor_SpriteIDLo(wram, slot) == kDkc1CoopSpriteDiddy;
  const uint16_t animation = diddy ? 0x7fu : 0x17u;
  Dkc1WramSetU16(wram, 0x16adu + slot - 2u, 0x17u);
  Dkc1Actor_SetSprAnimID(wram, slot, animation);
  Dkc1Actor_SetSprAnimCallbackBank(wram, slot, 0);
  Dkc1Actor_SetSprAnimScriptPtr(wram, slot, diddy ? 0xb6a1u : 0x95f9u);
  Dkc1Actor_SetSprAnimFrame(wram, slot, diddy ? 0x0b54u : 0x0478u);
  Dkc1Actor_SetSprAnimSpeed(wram, slot, 0x0200u);
  Dkc1Actor_SetSprAnimTimer(wram, slot, 0);
}

static void CopyCannonFlight(uint8_t *wram, uint32_t dst, uint32_t src) {
  uint16_t shot = Dkc1Actor_RAMTable1029Lo(wram, src);
  if (CannonShot(shot)) {
    const uint16_t expected = Dkc1Actor_SpriteIDLo(wram, dst) == kDkc1CoopSpriteDiddy ? 0x7fu : 0x17u;
    if (!CannonShot(Dkc1Actor_RAMTable1029Lo(wram, dst)) ||
        Dkc1Actor_SprAnimID(wram, dst) != expected)
      StartCannonSpin(wram, dst);
    Dkc1Actor_SetSprFlags(wram, dst, (Dkc1Actor_SprFlags(wram, dst) & ~0x4000u) |
        (Dkc1Actor_SprFlags(wram, src) & 0x4000u));
    Dkc1Actor_SetRAMTable1375Lo(wram, dst, Dkc1Actor_RAMTable1375Lo(wram, src));
    Dkc1Actor_SetRAMTable13E9Lo(wram, dst, Dkc1Actor_RAMTable13E9Lo(wram, src));
  }
  Dkc1Actor_SetRAMTable1029Lo(wram, dst, shot);
  Dkc1Actor_SetRAMTable11A1Lo(wram, dst, Dkc1Actor_RAMTable11A1Lo(wram, src));
  Dkc1Actor_SetSprCollisionFlags(wram, dst,
                                 Dkc1Actor_SprCollisionFlags(wram, src));
  /* Do not write $0C69 (YXPPCCCT) or remap $0D11/$0AE5. Rope bases
   * $063C/$0E38 are not the barrel-spin table; those writes steal CCC
   * and paint Donkey frames with Diddy's row. */
  Dkc1Actor_SetSprXPos(wram, dst, Dkc1Actor_SprXPos(wram, src));
  Dkc1Actor_SetSprYPos(wram, dst, Dkc1Actor_SprYPos(wram, src));
  Dkc1Actor_SetSprXSubpixel(wram, dst, Dkc1Actor_SprXSubpixel(wram, src));
  Dkc1Actor_SetSprYSubpixel(wram, dst, Dkc1Actor_SprYSubpixel(wram, src));
  Dkc1Actor_SetSprXVelocity(wram, dst, Dkc1Actor_SprXVelocity(wram, src));
  Dkc1Actor_SetSprYVelocity(wram, dst, Dkc1Actor_SprYVelocity(wram, src));
  Dkc1Actor_SetRAMTable0E55Lo(wram, dst, Dkc1Actor_RAMTable0E55Lo(wram, src));
  Dkc1Actor_SetRAMTable0EBDLo(wram, dst, Dkc1Actor_RAMTable0EBDLo(wram, src));
  Dkc1Actor_SetRAMTable0F25Lo(wram, dst, Dkc1Actor_RAMTable0F25Lo(wram, src));
}

/* BF93DE is the dry DK-barrel pop's final JML to Player_SetAnimation.
 * Native BF936E has finished health/position restoration and cleared Y
 * velocity; DP84 is the rescued Kong's zero-based animation index. Preserve that native
 * pause/pop, then resume its cannon flight once, without coupling free play. */
void Dkc1CoopFinishBarrelRescue(struct CpuState *cpu) {
  if (!s_runtime.enabled || !cpu || !cpu->ram || cpu->D || cpu->m_flag || cpu->x_flag ||
      RtlSpeculativeExecutionActive()) return;
  uint8_t *ram = cpu->ram;
  uint16_t rescued = Dkc1WramU16(ram, 0x82);
  uint16_t rescuer = rescued ^ 6u;
  if (!Dkc1CoopRelationRider(ram, rescued) || Dkc1WramU16(ram, 0x84) != rescued - 2u ||
      !Dkc1CoopRelationRider(ram, rescuer) ||
      Dkc1Actor_RAMTable1029Lo(ram, rescued) != 2u ||
      !CannonShot(Dkc1Actor_RAMTable1029Lo(ram, rescuer)) ||
      !(Dkc1WramU16(ram, 0x0579u) & 1u)) return;
  CopyCannonFlight(ram, rescued, rescuer);
  /* The original JML still runs with its normal ABI and cycle accounting.
   * Select the cartridge's spin animation instead of the falling animation. */
  cpu->A = 0x17u;
}

/* $BBA4D5/$A574 rectangles, with modulo-16-bit coordinates and inclusive
 * overlap. These are the cartridge hitboxes, not a capture radius. */
typedef struct ActorBox { uint16_t left, right, bottom, top; } ActorBox;
static int ActorHitbox(const uint8_t *ram, uint32_t slot, ActorBox *box) {
  if (!g_rom) return 0;
  unsigned pose = Dkc1Actor_SprAnimFrame(ram, slot);
  unsigned at = 0x8000u + (pose >> 1);
  if (at + 1u > 0xffffu) return 0;
  const uint8_t *bank = g_rom + 0x3b0000u;
  unsigned record = bank[at] | (unsigned)bank[at + 1] << 8;
  if (record < 0x8000u || record > 0xfff8u) return 0;
  uint16_t dx = bank[record] | (uint16_t)bank[record + 1] << 8;
  uint16_t dy = bank[record + 2] | (uint16_t)bank[record + 3] << 8;
  uint16_t width = bank[record + 4] | (uint16_t)bank[record + 5] << 8;
  uint16_t height = bank[record + 6] | (uint16_t)bank[record + 7] << 8;
  uint16_t x = Dkc1Actor_SprXPos(ram, slot), y = Dkc1Actor_SprYPos(ram, slot);
  uint16_t flags = Dkc1Actor_SprFlags(ram, slot);
  if (flags & 0x4000u) { box->right = x-dx; box->left = box->right-width; }
  else { box->left = x+dx; box->right = box->left+width; }
  if (flags & 0x8000u) { box->bottom = y+dy; box->top = box->bottom+height; }
  else { box->top = y-dy; box->bottom = box->top-height; }
  return 1;
}
static int ActorContact(const uint8_t *ram, uint16_t first, uint16_t second) {
  ActorBox a, b;
  if (!ActorHitbox(ram, first, &a) || !ActorHitbox(ram, second, &b)) return 0;
  return (int16_t)(b.right-a.left) >= 0 && (int16_t)(a.right-b.left) >= 0 &&
         (int16_t)(b.top-a.bottom) >= 0 && (int16_t)(a.top-b.bottom) >= 0;
}
static int CannonContact(const uint8_t *ram, uint16_t barrel, uint16_t kong) {
  uint16_t state = Dkc1Actor_RAMTable1029Lo(ram, kong);
  uint16_t flags = Dkc1Actor_RAMTable145DLo(ram, barrel);
  if (!IsKong(Dkc1Actor_SpriteIDLo(ram, kong)) || !Dkc1CoopKongCam(ram, kong) ||
      !(Dkc1Actor_RAMTable11A1Lo(ram, kong) & 0x41u) ||
      state == 0x53u || state == 0x56u || state == 0x0au || state == 0x0eu ||
      (flags & 0x1000u ? kong == 2 : (flags & 0x2000u) && kong == 4)) return 0;
  return ActorContact(ram, barrel, kong);
}
static int CannonOccupied(const uint8_t *ram, uint16_t barrel) {
  uint16_t st = Dkc1Actor_RAMTable1029Lo(ram, barrel);
  return st == 2 || st == 3 || st == 4 || st == 8 || st == 9 || st == 10 || st == 11;
}
int Dkc1CoopSharedCannonCamera(const uint8_t *ram) {
  if (!s_runtime.enabled || !ram || !Dkc1CoopInLevel(ram) ||
      Dkc1Actor_SpriteIDLo(ram, 2) != kDkc1CoopSpriteDonkey ||
      Dkc1Actor_SpriteIDLo(ram, 4) != kDkc1CoopSpriteDiddy ||
      Dkc1Actor_RAMTable1029Lo(ram, 2) != 0x29u ||
      Dkc1Actor_RAMTable1029Lo(ram, 4) != 0x29u ||
      Dkc1Actor_SprXPos(ram, 2) != Dkc1Actor_SprXPos(ram, 4) ||
      Dkc1Actor_SprYPos(ram, 2) != Dkc1Actor_SprYPos(ram, 4) ||
      Dkc1WramU16(ram, 0x1929u)) return 0;
  /* Coincident hidden poses alone may be an orphaned old save. Require
   * the native cannon's occupied phase and its actual registered rider. */
  for (uint16_t h = 2; Dkc1CoopEntityIsHandle(h);
       h = Dkc1CoopEntityNextHandle(h)) {
    if (Dkc1CoopRelationObject(ram, h, kCoopCannon) && CannonOccupied(ram, h) &&
        Dkc1CoopRegisteredCannonRider(ram, h) &&
        Dkc1Actor_SprXPos(ram, h) == Dkc1Actor_SprXPos(ram, 2) &&
        Dkc1Actor_SprYPos(ram, h) == Dkc1Actor_SprYPos(ram, 2)) return 1;
  }
  return 0;
}
static void PrepareCannon(uint8_t *ram, uint16_t barrel) {
  s_runtime.actor.cannon.object = barrel; s_runtime.actor.cannon.partner = 0;
  PublishCannonPads(ram);
  /* A shared native launch starts coincident flight states. Preserve its
   * accepted catch as a pair even when the two character hitboxes differ.
   * Nothing is coupled while flying; this is observed before this barrel's
   * single collision/update, with no distance threshold. */
  if (Dkc1Actor_SpriteIDLo(ram, 2) == 1 && Dkc1Actor_SpriteIDLo(ram, 4) == 2 &&
      Dkc1Actor_RAMTable1029Lo(ram, 2) == 0x28 &&
      Dkc1Actor_RAMTable1029Lo(ram, 4) == 0x28) {
    static const uint16_t fields[] = {0x0b19,0x0bc1,0x0db9,0x0e21,0x0e89,0x0ef1};
    int same = 1;
    for (unsigned i=0; i<sizeof fields/sizeof fields[0]; ++i)
      same &= Dkc1CoopEntityRead16(ram, fields[i], 2) ==
              Dkc1CoopEntityRead16(ram, fields[i], 4);
    if (same) s_runtime.actor.cannon.partner = 6; /* resolve the peer after native entry */
  }
  uint16_t rider = Dkc1CoopRegisteredCannonRider(ram, barrel);
  if (!rider || !CannonOccupied(ram, barrel) ||
      Dkc1Actor_RAMTable1029Lo(ram, rider) != 0x29u) return;
  uint16_t other = rider ^ 6u;
  /* A joined rider is hidden at exactly the registered rider's position.
   * Capture this relationship BEFORE the moving barrel advances. */
  if (IsKong(Dkc1Actor_SpriteIDLo(ram, other)) &&
      Dkc1Actor_RAMTable1029Lo(ram, other) == 0x29u &&
      Dkc1Actor_SprXPos(ram, other) == Dkc1Actor_SprXPos(ram, rider) &&
      Dkc1Actor_SprYPos(ram, other) == Dkc1Actor_SprYPos(ram, rider)) {
    s_runtime.actor.cannon.partner = other;
    /* E818 reads the registered rider's pressed word, not $056F. Either
     * occupant may fire, without borrowing the other player's input. */
    if (!(Dkc1WramU16(ram, 0x16e9u + rider - 2u) & 0xc000u) &&
        (Dkc1WramU16(ram, 0x16e9u + other - 2u) & 0xc000u)) {
      Dkc1Actor_SetRAMTable152DLo(ram, barrel, other);
      s_runtime.actor.cannon.partner = rider;
    }
  }
}
static void FinishCannon(uint8_t *ram) {
  uint16_t barrel = s_runtime.actor.cannon.object, other = s_runtime.actor.cannon.partner;
  s_runtime.actor.cannon.object = s_runtime.actor.cannon.partner = 0;
  if (!barrel) return;
  /* A one-use cannon may already have removed itself after E622. Its
   * rider field survives until allocation, within this same actor call. */
  uint16_t rider = Dkc1CoopRegisteredCannonRider(ram, barrel);
  if (!rider) return;
  uint16_t state = Dkc1Actor_RAMTable1029Lo(ram, rider);
  /* $BFE754 sets the cartridge-wide camera lock for flag $0200 cannons.
   * Split-screen needs the native primary controller to keep following its
   * Kong, including while the barrel moves. Release only this lone cannon
   * lock; shared scene-script locks ($0003, $8000, ...) retain stock handling. */
  if (s_runtime.enabled && state == 0x29u &&
      Dkc1Actor_SpriteIDLo(ram, barrel) == 0x38u &&
      CannonOccupied(ram, barrel) &&
      (Dkc1Actor_RAMTable145DLo(ram, barrel) & 0x0200u) &&
      Dkc1WramU16(ram, 0x1929u) == 1u)
    Dkc1WramSetU16(ram, 0x1929u, 0);
  if (other == 6) other = state == 0x29u ? (rider ^ 6u) : 0;
  if (other && CannonShot(state) &&
      Dkc1Actor_RAMTable1029Lo(ram, other) == 0x29u) {
    CopyCannonFlight(ram, other, rider);
    return;
  }
  if (Dkc1Actor_SpriteIDLo(ram, barrel) != 0x38u ||
      !CannonOccupied(ram, barrel) || state != 0x29u) return;
  if (!other) {
    other = rider ^ 6u;
    if (!CannonContact(ram, barrel, other) || AnimalRide(ram)) return;
  }
  /* Secondary entry retains the cannon's phase, passenger and motion.
   * Native $E424..E43C clears the rider's motion and interaction mask. */
  CopyCannonFlight(ram, other, rider);
  Dkc1CoopEntityWrite16(ram, 0x1595u, other, 0);
  Dkc1Actor_SetSprCollisionFlags(ram, other, 0);
  Dkc1Actor_SetRAMTable11A1Lo(ram, other, 0);
  Dkc1Actor_SetSprAnimFrame(ram, other, 0);
  Dkc1Actor_SetDisplayedPoseLo(ram, other, 0);
}

void Dkc1CoopEndFrame(uint8_t *wram) {
  if (!s_runtime.enabled || !wram)
    return;
  /* Native state9 pauses the ordinary actor/pad dispatch while the new
   * Kong follows the leader in $23. Release this one shared cutscene into
   * each player's normal airborne controller, retaining its native launch impulse. */
  if (ControlScene(wram) && Dkc1WramU16(wram, 0x1929u) == 9u &&
      Dkc1WramU16(wram, 0x32u) != 3u && Dkc1WramU16(wram, 0x32u) != 5u) {
    for (uint32_t slot = 2; slot <= 4; slot += 2) {
      if (!IsKong(Dkc1Actor_SpriteIDLo(wram, slot)) ||
          Dkc1Actor_RAMTable1029Lo(wram, slot) != 0x23u) continue;
      Dkc1Actor_SetSprYVelocity(wram, slot, Dkc1Actor_RAMTable145DLo(wram, slot));
      Dkc1Actor_SetRAMTable13E9Lo(wram, slot, 0);
      Dkc1Actor_SetRAMTable145DLo(wram, slot, 0);
      Dkc1Actor_SetRAMTable1029Lo(wram, slot, 1);
      s_runtime.barrel_hold = 0;
      int revived = SeatOfSlot(slot);
      if (revived >= 0) {
        s_runtime.corpse[revived] = 0;
        memset(&s_runtime.pose[revived], 0, sizeof s_runtime.pose[revived]);
      }
      Dkc1WramSetU16(wram, 0x1929u, 0);
    }
  }
  Dkc1CoopWeatherObserve(wram, &s_runtime.weather);
  ShareSwingPoint(wram);
  ClearFakeFollow(wram);
  JoinMinecartPassengers(wram);
  SyncKongState(wram);
  NoteBarrel(wram);
  /* All admitted gameplay scenes share this ownership observation. Scene
   * transitions retain the last live intent until native restoration. */
  if (ControlScene(wram)) s_runtime.room_mount = RoomMountIntent(wram);
  if (Dkc1CoopInLevel(wram)) {
    int i;
    StreamAsAhead(wram);
    for (i = 0; i < 2; i++) {
      uint32_t slot = SlotOf(kKong[i]);
      if (slot && Dkc1CoopKongCam(wram, slot) &&
          LevelExitPose(Dkc1Actor_RAMTable1029Lo(wram, slot))) {
        DropLostCorpse(wram);
        break;
      }
    }
  } else if (!s_runtime.lock || !SlotOf(s_runtime.lock) ||
             !Dkc1CoopKongCam(wram, SlotOf(s_runtime.lock))) {
    s_runtime.lock = 0;
  }
}

void Dkc1CoopPrepareFrame(struct Ppu *ppu, uint8_t *wram) {
  s_runtime.ppu = ppu;
  if (s_runtime.enabled && wram &&
      (Dkc1CoopInLevel(wram) || Dkc1CoopFixedBossRoom(wram)))
    PresentKongPalettes(wram);
}

uint16_t Dkc1CoopActorPaletteOwner(const uint8_t *wram, uint16_t slot) {
  if (!s_runtime.enabled || !wram) return slot;
  uint32_t kongs[2];
  Dkc1CoopCacheKongSlots(wram, kongs);
  for (unsigned seat = 0; seat < 2; seat++) {
    uint16_t animal = Dkc1CoopReciprocalAnimalForSeat(wram, seat, (uint16_t)kongs[seat]);
    if (!kongs[seat] || !animal) continue;
    /* BF88B4 establishes the link; BF8903..892C transfers palette bits.
     * The Kong slot draws the animal and the animal slot draws its rider.
     * Enguarde ($0C) uses the same exchange as Rambi ($09). */
    if (slot == kongs[seat]) return animal;
    if (slot == animal) return (uint16_t)kongs[seat];
  }
  return slot;
}

uint16_t Dkc1CoopActorPaletteSource(const uint8_t *wram, uint16_t slot) {
  if (!s_runtime.enabled || !wram) return 0;
  uint16_t owner = Dkc1CoopActorPaletteOwner(wram, slot);
  uint16_t id = Dkc1Actor_SpriteIDLo(wram, owner);
  /* The native cart owns its rear passenger palette. */
  uint16_t cart = Dkc1CoopMinecartForKong(wram, owner);
  if (cart && Dkc1CoopEntityRead16(wram, 0x1375u, cart) != owner) return 0;
  if (!IsKong(id) || !Dkc1CoopKongCam(wram, owner) ||
      (owner == slot && KongAnimalRide(wram, (unsigned)Seat(id)))) return 0;
  return kKongLeaderPalOff[Seat(id)];
}

void Dkc1CoopAfterActor(uint8_t *wram) {
  if (s_runtime.rope.swing_aim && wram) {
    Dkc1WramSetU16(wram, DKC1_WRAM_Player_CurrentKongLo, s_runtime.rope.swing_aim_kong);
    s_runtime.rope.swing_aim = 0;
  }
  if (s_runtime.rope.swing_latch_on && wram) {
    Dkc1WramSetU16(wram, 0x1B01u, s_runtime.rope.swing_latch);
    s_runtime.rope.swing_latch_on = 0;
  }
  /* The rope someone is already riding writes its own slot to $1AFF.
   * A free rope earlier in the pool had just named itself there for the
   * Kong who is jumping at it. Put that name back. */
  if (s_runtime.rope.keep_aff_on && wram) {
    if (Dkc1WramU16(wram, 0x1AFFu) != s_runtime.rope.keep_aff)
      Dkc1WramSetU16(wram, 0x1AFFu, s_runtime.rope.keep_aff);
    s_runtime.rope.keep_aff_on = 0;
  } else {
    s_runtime.rope.keep_aff_on = 0;
  }
  /* Put back the overlap a fallen Kong's grounded tail just cleared.
   * His tick is skipped by the pad mux, so nothing else restores it,
   * and the partner's jump reads $1AFD before the rope can write it again. */
  if (s_runtime.rope.fallen_rope_on && wram) {
    Dkc1WramSetU16(wram, 0x1AFDu, s_runtime.rope.fallen_rope[0]);
    Dkc1WramSetU16(wram, 0x1AFFu, s_runtime.rope.fallen_rope[1]);
    Dkc1WramSetU16(wram, 0x1B01u, s_runtime.rope.fallen_rope[2]);
    s_runtime.rope.fallen_rope_on = 0;
  }
  if (s_runtime.kritter_cam_on && wram) {
    Dkc1WramSetU16(wram, DKC1_WRAM_CameraX, s_runtime.kritter_cam);
    s_runtime.kritter_cam_on = 0;
  }
  if (!wram || s_runtime.actor.wait.pending)
    return;
  FinishCannon(wram);
  ClearFakeFollow(wram);
  if (!s_runtime.actor.mux)
    return;
  if (s_runtime.actor.mux_kong) {
    PublishSharedLatch(wram);
  }
  RestoreCanonicalContext(wram);
  s_runtime.actor.mux = 0;
  s_runtime.actor.mux_kong = 0;
  s_runtime.actor.mux_id = 0;
  s_runtime.actor.captured = 0;
}

static void AimSwingRider(uint8_t *wram, uint32_t slot);

/* The sprite-pair pass records a hit on the one Kong in $056F. Between
 * actor ticks that word is the leader, so a Necky nut on the partner sets
 * nobody's $1595. $BF874A consumes $1595 == 1 through $BFA0F7/$BFA17E,
 * which still requires bit 0 of $0579. Mark that pair before this Kong's
 * own tick; the cartridge applies the death and the survivor handoff. */
static void NotePartnerNut(uint8_t *wram, uint32_t slot) {
  uint16_t handle, kx, ky;
  if (!(Dkc1Actor_SprCollisionFlags(wram, slot) & 0x0100u))
    return;
  if (Dkc1Actor_SprEventFlags(wram, slot) == 1u)
    return;
  kx = Dkc1Actor_SprXPos(wram, slot);
  ky = Dkc1Actor_SprYPos(wram, slot);
  for (handle = 2u; handle <= Dkc1CoopEntityLastHandle();
       handle = Dkc1CoopEntityNextHandle(handle)) {
    int dx, dy;
    if (handle == (uint16_t)slot)
      continue;
    if ((Dkc1Actor_SpriteIDLo(wram, handle) & 0xFFu) != 0x10u)
      continue;
    if (!(Dkc1Actor_SprCollisionFlags(wram, handle) & 1u))
      continue;
    dx = (int)Dkc1Actor_SprXPos(wram, handle) - (int)kx;
    dy = (int)Dkc1Actor_SprYPos(wram, handle) - (int)ky;
    if (dx < 0)
      dx = -dx;
    if (dy < 0)
      dy = -dy;
    if (dx > 0x10 || dy > 0x14)
      continue;
    Dkc1Actor_SetSprEventFlags(wram, slot, 1u);
    Dkc1WramSetU16(wram, (uint16_t)(0x16F5u + slot - 2u), handle);
    Dkc1WramSetU16(wram, 0x0579u,
                   (uint16_t)(Dkc1WramU16(wram, 0x0579u) | 1u));
    return;
  }
}

/* $BF:F141 stays asleep while actor X is left of $088B or $110 past it.
 * The partner picture can show that head anyway. Point the test at his
 * camera for this tick, including a head a few pixels past the left edge. */
static void AimKritterWake(uint8_t *wram, uint32_t slot) {
  uint16_t view_x, view_y, reach, actor, cam;
  int dx, view_dx, sep;
  if (!wram || s_runtime.kritter_cam_on)
    return;
  if ((Dkc1Actor_SpriteIDLo(wram, slot) & 0xffu) != 5u)
    return;
  if (Dkc1Actor_RAMTable1029Lo(wram, slot) != 2u)
    return;
  if (!Dkc1CoopWorldGetCamera(wram, 1, &view_x, &view_y))
    return;
  actor = (uint16_t)Dkc1Actor_SprXPos(wram, slot);
  cam = Dkc1WramU16(wram, DKC1_WRAM_CameraX);
  reach = Dkc1CoopEntityRead16(wram, 0x14c5u, (uint16_t)slot);
  dx = (int)actor - (int)cam;
  if (dx >= 0 && dx < (int)reach)
    return;
  sep = (int)view_x - (int)cam;
  if (sep < 0)
    sep = -sep;
  if (sep < 0x20)
    return;
  view_dx = (int)actor - (int)view_x;
  if (view_dx < -0x20 || view_dx >= (int)reach)
    return;
  if (view_dx < 0)
    view_x = actor;
  s_runtime.kritter_cam = cam;
  s_runtime.kritter_cam_on = 1;
  Dkc1WramSetU16(wram, DKC1_WRAM_CameraX, view_x);
}

/* Scripted actor birth is the one place that knows the opener. Keep the
 * companion owner in host state, never an unused cartridge actor field. */
uint16_t Dkc1CoopCompanionBinding(void) { return s_runtime.squawks; }
void Dkc1CoopRestoreCompanionBinding(uint16_t binding) { s_runtime.squawks=binding; }
void Dkc1CoopActorAllocated(uint16_t handle) {
  if ((s_runtime.squawks >> 4)==handle) s_runtime.squawks=0;
}
void Dkc1CoopCrateHit(const uint8_t *ram,uint16_t handle) {
  if (!s_runtime.enabled || !ram || !Dkc1CoopEntityIsHandle(handle) ||
      Dkc1Actor_SpriteIDLo(ram,handle)!=0x18u ||
      Dkc1Actor_RAMTable1029Lo(ram,handle)!=0) return;
  uint16_t companion=s_runtime.squawks >> 4;
  if (Dkc1CoopEntityIsHandle(companion) &&
      Dkc1Actor_SpriteIDLo(ram,companion)==0x0du) return;
  uint16_t id=Dkc1WramU16(ram,DKC1_WRAM_Player_CurrentKongLo);
  if (IsKong(id) && (s_runtime.squawks >> 4)!=handle)
    s_runtime.squawks=(uint16_t)((handle<<4)|(id<<2));
}
void Dkc1CoopActorCreated(struct CpuState *cpu,uint16_t handle) {
  if (!s_runtime.enabled || !cpu || !cpu->ram) return;
  /* B5833B borrows D as the destination field base. The spawn parent
   * remains in the canonical $82 slot, not D+$82. */
  uint16_t parent=Dkc1WramU16(cpu->ram,0x82u);
  uint16_t id=Dkc1WramU16(cpu->ram,DKC1_WRAM_Player_CurrentKongLo);
  if (Dkc1CoopEntityIsHandle(parent) &&
      Dkc1Actor_SpriteIDLo(cpu->ram,parent)==0x18u &&
      (s_runtime.squawks >> 4)==parent && IsKong((s_runtime.squawks >> 2)&3u))
    id=(s_runtime.squawks >> 2)&3u;
  s_runtime.squawks=IsKong(id)?(uint16_t)((handle<<4)|(id<<2)):0;
}
void Dkc1CoopRememberAnimalBonus(struct CpuState *cpu) {
  if (!s_runtime.enabled || s_runtime.animal_bonus || !cpu || !cpu->ram ||
      cpu->D || cpu->m_flag || cpu->x_flag)
    return;
  uint16_t handle = Dkc1WramU16(cpu->ram, 0x82);
  uint16_t id = Dkc1WramU16(cpu->ram, 0x56f);
  /* B68421 follows the native CMP #3/BCC after an accepted token. The first
   * transition owns the challenge even if another token is touched this tick. */
  if (IsKong(id) && Dkc1CoopEntityIsHandle(handle) &&
      Dkc1CoopEntityRead16(cpu->ram, 0x0d45, handle) == 0x45u &&
      Dkc1CoopEntityRead16(cpu->ram, 0x14f9, handle) >= 3u)
    s_runtime.animal_bonus = id;
}

unsigned Dkc1CoopAnimalBonusOwner(const uint8_t *ram) {
  if (!s_runtime.enabled || !ram || !(Dkc1WramU16(ram, 0x1e15) & 0x400u))
    return 0;
  return s_runtime.animal_bonus & 3u;
}

static uint16_t SquawksOwner(const uint8_t *ram,uint16_t handle) {
  if ((s_runtime.squawks >> 4)!=handle) return 0;
  uint16_t id=(s_runtime.squawks >> 2)&3u;
  if (IsKong(id) && Dkc1CoopKongCanParticipate(ram,SlotOf(id))) return id;
  uint16_t other=id ^ 3u;
  if (IsKong(other) && Dkc1CoopKongCanParticipate(ram,SlotOf(other))) return other;
  return 0;
}

void Dkc1CoopBeforeActor(uint8_t *wram, uint32_t slot, uint16_t direct_page) {
  uint16_t id, owner;
  int role;
  if (!s_runtime.enabled || !wram || s_runtime.actor.mux || s_runtime.actor.wait.pending)
    return;
  if (!Dkc1CoopEntityIsHandle((uint16_t)slot))
    return;
  if (!ControlScene(wram))
    return;
  if (!s_runtime.actor.captured) {
    SyncKongState(wram);
    CaptureCanonicalContext(wram);
    s_runtime.actor.captured = 1;
  }

  id = (uint16_t)(Dkc1Actor_SpriteIDLo(wram, slot) & 0xFFu);
  if (!id)
    return;
  if (Dkc1CoopWorldSkip(id)) {
    uint16_t lead = LeaderKong(wram);
    if (lead) {
      if (KeepBothIds(id) || id == 0x3Cu)
        ClearFakeFollow(wram);
      else
        FakePartnerFollow(wram, lead);
      UseKong(wram, lead);
      s_runtime.actor.mux = 1;
    }
    return;
  }

  if (IsKong(id)) {
    /* $44 -> BF9CB5 consumes the shared bonus return once. Park the
     * other Kong in native follow during this tick, then AfterActor restores
     * his pose. The room transition belongs to the selected Kong. */
    unsigned caps = Dkc1CoopSceneCapabilities(wram);
    if ((caps & (kDkc1CoopSceneFixed | kDkc1CoopSceneNativeView)) ==
        (kDkc1CoopSceneFixed | kDkc1CoopSceneNativeView) &&
        Dkc1Actor_RAMTable1029Lo(wram, slot) == 0x44u) {
      if (id != LeaderKong(wram)) {
        ClearFakeFollow(wram);
        s_runtime.actor.follower.slot = slot;
        s_runtime.actor.follower.state = 0x44u;
        s_runtime.actor.follower.collision = Dkc1Actor_SprCollisionFlags(wram, slot);
        s_runtime.actor.follower.active = 1;
        Dkc1Actor_SetRAMTable1029Lo(wram, slot, 5);
      }
      return;
    }
    role = Dkc1CoopKongRole(wram, slot);
    uint16_t cart = Dkc1CoopMinecartForKong(wram, (uint16_t)slot);
    if ((cart && Dkc1Actor_RAMTable1375Lo(wram, cart) != slot) ||
        role == kDkc1CoopRoleBarrel || role == kDkc1CoopRoleLost ||
        (role == kDkc1CoopRoleDying && !OverlayQuiet(wram)) ||
        (s_runtime.lock && id != s_runtime.lock)) {
      /* $BF8FBA jumps into the grounded tail. Bit 0 of $12A5, still set
       * after the fall, clears the shared rope slot. Keep the words the
       * rope already published for the partner. */
      if (role == kDkc1CoopRoleDying && !OverlayQuiet(wram)) {
        s_runtime.rope.fallen_rope[0] = Dkc1WramU16(wram, 0x1AFDu);
        s_runtime.rope.fallen_rope[1] = Dkc1WramU16(wram, 0x1AFFu);
        s_runtime.rope.fallen_rope[2] = Dkc1WramU16(wram, 0x1B01u);
        s_runtime.rope.fallen_rope_on = 1;
      }
      return;
    }
    if (!OverlayQuiet(wram))
      Unfollow(wram, slot, direct_page);
    /* Mode 3 parks the other Kong in $0036. That pose never reads his
     * pad. $00C1 and $002B are the pair $BF9AA9 stores for the swimmer.
     * $1595 = 1 or $20 is the tag token: $BF:A38F then sinks him in
     * $003A and pulls the partner through $000E. */
    if (Dkc1WramU16(wram, 0x0032u) == 3u &&
        Dkc1Actor_RAMTable1029Lo(wram, slot) == 0x0036u) {
      uint16_t script = Dkc1Actor_RAMTable11A1Lo(wram, slot);
      uint16_t tag = Dkc1CoopEntityRead16(wram, 0x1595u, (uint16_t)slot) & 0x7fffu;
      if (!script)
        Dkc1Actor_SetRAMTable11A1Lo(wram, slot, 0x00c1u);
      if (!script || (script == 0x00c1u && tag != 1u && tag != 0x20u))
        Dkc1Actor_SetRAMTable1029Lo(wram, slot, 0x002bu);
    }
    if (id != LeaderKong(wram) && role == kDkc1CoopRolePlayable)
      NotePartnerNut(wram, slot);
    SnapshotSharedLatch(wram, slot);
    UseKong(wram, id);
    s_runtime.actor.mux = 1;
    s_runtime.actor.mux_kong = 1;
    s_runtime.actor.mux_id = id;
    return;
  }
  if (id == 0x38u) PrepareCannon(wram, (uint16_t)slot);
  uint16_t passenger = id == 0x51u ? Dkc1Actor_RAMTable1375Lo(wram, slot) : 0;
  uint16_t companion = id==0x0du ? SquawksOwner(wram,(uint16_t)slot) : 0;
  owner = companion ? companion : passenger && Dkc1CoopMinecartForKong(wram, passenger) == slot ?
      Dkc1Actor_SpriteIDLo(wram, passenger) :
      Dkc1CoopKongForActorTick(wram, slot, s_runtime.slot, s_runtime.lock);
  if (owner) {
    UseKong(wram, owner);
    s_runtime.actor.mux = 1;
  }
  /* BFED03 places the swing point on the Kong in $056F, and only while
   * that Kong is in $0026. After he jumps, the partner is still the
   * rider but the point keeps aiming at the one who left. A free rope
   * tests the airborne Kong inside its chain, including when the
   * leader is standing. */
  if (id == 0x31u)
    AimSwingRider(wram, slot);
  AimKritterWake(wram, slot);
}

/* $056F 0 or >=3 selects handle 2. Any other value is the handle index
 * shifted once: 1 is Donkey, 2 is Diddy. */
static uint32_t SwingAimSlot(uint16_t current) {
  if (current == 0 || current >= 3u)
    return 2u;
  return (uint32_t)current << 1;
}

static void AimSwingRider(uint8_t *wram, uint32_t slot) {
  uint16_t current = Dkc1WramU16(wram, DKC1_WRAM_Player_CurrentKongLo);
  uint32_t aimed = SwingAimSlot(current);
  uint32_t rider = 0;
  uint16_t kong = 0;
  int i;
  s_runtime.rope.swing_aim = 0;
  for (i = 0; i < 2; i++) {
    if (s_runtime.rope.rider_hook[i] == (uint16_t)slot && s_runtime.slot[i] &&
        Dkc1Actor_RAMTable1029Lo(wram, s_runtime.slot[i]) == 0x0026u) {
      rider = s_runtime.slot[i];
      kong = kKong[i];
    }
  }
  if (!rider && Dkc1WramU16(wram, 0x1B01u) == (uint16_t)slot) {
    for (i = 0; i < 2; i++) {
      if (!s_runtime.slot[i] || s_runtime.slot[i] == aimed)
        continue;
      if (Dkc1Actor_RAMTable1029Lo(wram, s_runtime.slot[i]) != 0x0026u ||
          (s_runtime.rope.rider_hook[i] &&
           s_runtime.rope.rider_hook[i] != (uint16_t)slot))
        continue;
      rider = s_runtime.slot[i];
      kong = (uint16_t)(Dkc1Actor_SpriteIDLo(wram, rider) & 0xFFu);
      break;
    }
  }
  /* B680AE stores this rope's handle in $1AFF when the tested Kong is
   * inside the chain. The rider's rope does that every frame. A free
   * rope that already stored its handle must still be there next frame,
   * or the jump never enters $0026. */
  if (rider && kong) {
    uint16_t aff = Dkc1WramU16(wram, 0x1AFFu);
    if (!s_runtime.rope.keep_aff_on && HookSprite(wram, aff) && aff != (uint16_t)slot) {
      s_runtime.rope.keep_aff = aff;
      s_runtime.rope.keep_aff_on = 1;
    }
  }
  /* This rope has no rider. $056F was put back to the leader after his
   * tick, so the rope only tests him. The chain sits within $58 of the
   * hook and the grab is $10 wide, so an airborne partner inside $70
   * is the one the rope must test. A release still hides his old rope. */
  if (!rider || !kong) {
    /* BFED42 tests only the shared latch and current Kong's swing state.
     * A released rope must not carry a Kong bound to another rope. Hide
     * this stale latch for its tick; AfterActor restores the caller context. */
    if (Dkc1WramU16(wram,0x1B01u)==(uint16_t)slot) {
      s_runtime.rope.swing_latch=Dkc1WramU16(wram,0x1B01u);
      s_runtime.rope.swing_latch_on=1;
      Dkc1WramSetU16(wram,0x1B01u,0);
    }
    int best = -1;
    int best_dx = 0;
    uint16_t rope_x = Dkc1Actor_SprXPos(wram, slot);
    uint16_t rope_y = Dkc1Actor_SprYPos(wram, slot);
    for (i = 0; i < 2; i++) {
      int dx, dy;
      if (!s_runtime.slot[i] ||
          Dkc1Actor_RAMTable1029Lo(wram, s_runtime.slot[i]) != 0x0001u)
        continue;
      if (s_runtime.rope.rope_release[i] && s_runtime.rope.release_hook[i] == (uint16_t)slot)
        continue;
      dy = (int)rope_y + 0x80 - (int)Dkc1Actor_SprYPos(wram, s_runtime.slot[i]);
      if (dy < 0 || dy >= 0xE0)
        continue;
      dx = (int)Dkc1Actor_SprXPos(wram, s_runtime.slot[i]) - (int)rope_x;
      if (dx < 0)
        dx = -dx;
      if (dx >= 0x70)
        continue;
      if (best < 0 || dx < best_dx) {
        best = i;
        best_dx = dx;
      }
    }
    if (best < 0 || s_runtime.slot[best] == aimed)
      return;
    s_runtime.rope.swing_aim_kong = current;
    s_runtime.rope.swing_aim = 1;
    Dkc1WramSetU16(wram, DKC1_WRAM_Player_CurrentKongLo, kKong[best]);
    return;
  }
  /* BFED03 places the rider only while $1B01 names this rope. The other
   * rider's tick has already put his own rope there. */
  if (!s_runtime.rope.swing_latch_on) {
    s_runtime.rope.swing_latch = Dkc1WramU16(wram, 0x1B01u);
    s_runtime.rope.swing_latch_on = 1;
  }
  Dkc1WramSetU16(wram, 0x1B01u, (uint16_t)slot);
  if (rider == aimed)
    return;
  /* Leave $056F alone when that Kong is already swinging on this rope.
   * $1B01 flips between the two ropes, so it is not that test by itself. */
  if (Dkc1Actor_RAMTable1029Lo(wram, aimed) == 0x0026u) {
    int aseat = aimed == s_runtime.slot[0] ? 0 : 1;
    if (s_runtime.slot[aseat] == aimed &&
        (s_runtime.rope.rider_hook[aseat] == (uint16_t)slot ||
         (!s_runtime.rope.rider_hook[aseat] &&
          Dkc1WramU16(wram, 0x1B01u) == (uint16_t)slot)))
      return;
  }
  s_runtime.rope.swing_aim_kong = current;
  s_runtime.rope.swing_aim = 1;
  Dkc1WramSetU16(wram, DKC1_WRAM_Player_CurrentKongLo, kong);
}
