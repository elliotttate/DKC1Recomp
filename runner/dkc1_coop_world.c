#include "dkc1_coop_world.h"
#include "dkc1_coop_wire.h"

#include "common_rtl.h"
#include "dkc1_coop_machine.h"
#include "dkc1_coop_transaction.h"
#include "cpu_state.h"
#include "dkc1_coop_mod.h"
#include "dkc1_coop_entities.h"
#include "dkc1_coop_graphics.h"
#include "dkc1_coop_movement.h"
#include "dkc1_video.h"
#include "dkc1_wram_gen.h"
#include "snes/dma.h"
#include "snes/interp816.h"
#include "snes/ppu.h"

#include <string.h>
#include <stddef.h>
/* These are historical wire layouts, independent of future runtime growth.
 * World v5/v6 reuse the v4 length and carry only nested migration changes. */
enum {
  kWorldWireV1Bytes = 40,
  kWorldWireV2Bytes = 64,
  kWorldWireV3Bytes = 116,
  kWorldWireV4Bytes = 120,
};
typedef char WorldV1WireLayout[offsetof(Dkc1CoopWorldSnapshot, camera) == kWorldWireV1Bytes ? 1 : -1];
typedef char WorldV2WireLayout[offsetof(Dkc1CoopWorldSnapshot, water) == kWorldWireV2Bytes ? 1 : -1];
typedef char WorldV3WireLayout[offsetof(Dkc1CoopWorldSnapshot, land_region) == kWorldWireV3Bytes ? 1 : -1];
typedef char WorldV4WireLayout[sizeof(Dkc1CoopWorldSnapshot) == kWorldWireV4Bytes ? 1 : -1];

extern RecompReturn CODE_BDFDBD_M0X0(CpuState *cpu);
extern RecompReturn CODE_BDFE39_M0X0(CpuState *cpu);

static const Dkc1CoopTransactionParticipant kParticipants[] = {
  {Dkc1CoopEntitiesBeginTransaction,Dkc1CoopEntitiesEndTransaction},
  {Dkc1CoopGraphicsBeginTransaction,Dkc1CoopGraphicsEndTransaction},
};

static const uint16_t kCursorAddress[5] = {0xa0, 0xa2, 0xa4, 0xef, 0xf1};
static Dkc1CoopWorldSnapshot s_world;
static int s_scanning;
static uint16_t s_primary_x, s_primary_y;
/* Diddy's screen column eases like the cartridge camera. The forward
 * lead is the framed Kong's settled column. Walking the other way eases
 * toward the center, 128. The value is 8.8 so the ease can be slower
 * than one pixel. */
static uint8_t s_before[0x20000];
static Dkc1CoopMachineSnapshot s_machine;

static uint16_t Read(const uint8_t *ram, unsigned address) {
  return Dkc1WramU16(ram, address);
}

static void Write(uint8_t *ram, unsigned address, uint16_t value) {
  Dkc1WramSetU16(ram, address, value);
}

static uint16_t Clamp(int value, unsigned low, unsigned high) {
  if (value < (int)low) return (uint16_t)low;
  if (value > (int)high) return (uint16_t)high;
  return (uint16_t)value;
}

typedef struct NativeCameraController {
  uint16_t update, initialize, signature;
  uint32_t completed;
  const char *bytes;
  unsigned byte_count, wet;
} NativeCameraController;
static const NativeCameraController kDryCamera = {
  0x9bec, 0, 0x9bec, 0x809c4e, "\x20\xb0\x9b\xad\x29\x19", 6, 0};
static const NativeCameraController kWetCamera = {
  0xc263, 0xc501, 0xc263, 0x80c27c, "\xad\x29\x19\x29\x01\x00", 6, 1};
static const NativeCameraController kVerticalCamera = {
  0xc47b, 0, 0x9bec, 0x80c4a3, "\x20\xb0\x9b\xad\x29\x19", 6, 0};
static const struct { unsigned mode; const NativeCameraController *controller; }
kCameraControllers[] = {
  /* Mode0 entrance0D: 80C5F1 -> 9728 -> dry9BEC at9735. */
  {0,&kDryCamera}, {1,&kDryCamera}, {2,&kDryCamera}, {3,&kWetCamera},
  /* Mode8 entrances42/43: 80C6C5/C9 -> 98FC -> dry9BEC. */
  /* Mode7 entrance40: 80C6BD -> 98CC -> dry9BEC at98D9. */
  /* Mode6 entrance2F: 80C679 -> 9842 -> dry9BEC at984F. */
  /* Mode5 entrances27/2E: 97EC/9821 call dry9BEC; carts share native view. */
  {5,&kDryCamera}, {6,&kDryCamera}, {7,&kDryCamera}, {8,&kDryCamera},
  /* Slip-Slide Ride 6D/C8: 80:992F uses C47B outside its fixed bonus. */
  {9,&kVerticalCamera}, {0x0a,&kDryCamera}, {0x0b,&kDryCamera}
};
static const NativeCameraController *CameraController(unsigned mode) {
  for (unsigned i=0;i<sizeof kCameraControllers/sizeof *kCameraControllers;i++)
    if (kCameraControllers[i].mode==mode) return kCameraControllers[i].controller;
  return NULL;
}
static int NativeCameraMode(unsigned mode) { return CameraController(mode)!=NULL; }

/* B6AD1E requests focus only within $40 world-X pixels of its selected
 * Kong. Reapply that same admission for each camera, not the producer's pad. */
static uint16_t CameraFocusForKong(const uint8_t *ram,uint16_t focus,uint16_t kong) {
  if (!focus || !Dkc1CoopEnabled() ||
      !(Dkc1CoopSceneCapabilities(ram)&kDkc1CoopSceneCamera) ||
      !Dkc1CoopEntityIsHandle(focus) || (kong!=2 && kong!=4)) return focus;
  uint16_t dx=(uint16_t)(Dkc1CoopEntityRead16(ram,0xb19,focus)-
                         Dkc1CoopEntityRead16(ram,0xb19,kong));
  return dx>=0x40u && dx<=0xffc0u ? 0 : focus;
}

uint16_t Dkc1CoopWorldCameraFocus(struct CpuState *cpu,uint16_t native) {
  if (!cpu || !cpu->ram || !Dkc1CoopEnabled() ||
      RtlSpeculativeExecutionActive() ||
      !(Dkc1CoopSceneCapabilities(cpu->ram)&kDkc1CoopSceneCamera)) return native;
  /* The interpreter supplies one byte at a time. Read the complete handle
   * before the native STZ consumes it; repeated byte reads are idempotent. */
  uint16_t focus=Read(cpu->ram,0x1e29);
  if (!focus || !Dkc1CoopEntityIsHandle(focus)) return native;
  s_world.reserved=(uint16_t)((s_world.reserved&0x188u)|(focus<<8));
  uint16_t mine=CameraFocusForKong(cpu->ram,focus,cpu->Y);
  /* This camera still consumes the one-shot shared request when it declines
   * it. The private view uses the captured handle after the native STZ. */
  if (!mine) Write(cpu->ram,0x1e29,0);
  return mine;
}

/* Execute the cartridge's water camera on a private bus: no clocks, devices,
 * actor hooks or shared WRAM are reachable. Bounds use authored B8 regions. */
typedef struct WaterCameraBus {
  uint8_t ram[0x2000];
  uint16_t dividend, quotient, product;
  uint8_t multiplicand, failed;
} WaterCameraBus;
static uint8_t WaterCameraRead(void *context, uint32_t address) {
  WaterCameraBus *b = context;
  unsigned bank = address >> 16, a = address & 0xffff;
  if ((bank & 0x7f) < 0x40 || bank == 0x7e) {
    if (a < sizeof b->ram) {
      if (a==0x1e29 || a==0x1e2a) {
        unsigned id=Read(b->ram,0x56f);
        uint16_t kong=id==1 || id==2 ? (uint16_t)(id*2u) : 2;
        uint16_t focus=Read(b->ram,0x1e29);
        /* Private raw RAM cannot project a virtual actor's overlapping
         * tables; retain its ordinary Kong camera for that unproven case. */
        if (focus>=0x80 && Dkc1CoopEnabled()) focus=0;
        return (uint8_t)(CameraFocusForKong(b->ram,focus,kong)>>((a-0x1e29)*8));
      }
      return b->ram[a];
    }
    if (bank != 0x7e && a >= 0x4214 && a <= 0x4217)
      return (uint8_t)((a < 0x4216 ? b->quotient : b->product) >> ((a & 1) * 8));
  }
  if (bank != 0x7e && bank != 0x7f && ((bank & 0x40) || a >= 0x8000))
    return g_rom[address & 0x3fffff];
  b->failed = 1;
  return 0;
}
static void WaterCameraWrite(void *context, uint32_t address, uint8_t value) {
  WaterCameraBus *b = context;
  unsigned bank = address >> 16, a = address & 0xffff;
  if ((bank & 0x7f) < 0x40 || bank == 0x7e) {
    if ((a >= 0x4c && a <= 0x5f) || (a >= 0x76 && a <= 0x79) ||
        (a >= 0x0579 && a <= 0x057a) || (a >= 0x82 && a <= 0x83) ||
        (a >= 0x100 && a <= 0x1ff) || (a >= 0x0889 && a <= 0x088d) ||
        (a >= 0x0895 && a <= 0x0898) || (a >= 0x0a75 && a <= 0x0a76) ||
        (a >= 0x1a4b && a <= 0x1a6a) || (a >= 0x1af3 && a <= 0x1afa) ||
        (a >= 0x1b0b && a <= 0x1b0e) || (a >= 0x1b17 && a <= 0x1b20) ||
        (a >= 0x1e29 && a <= 0x1e2a) ||
        (a >= 0x1b23 && a <= 0x1b26) || (a >= 0x1e0f && a <= 0x1e12)) {
      b->ram[a] = value;
      return;
    }
    if (bank != 0x7e) switch (a) {
      case 0x4202: b->multiplicand = value; return;
      case 0x4203: b->product = b->multiplicand * value; return;
      case 0x4204: b->dividend = (b->dividend & 0xff00) | value; return;
      case 0x4205: b->dividend = (b->dividend & 0x00ff) | (value << 8); return;
      case 0x4206:
        b->quotient = value ? b->dividend / value : 0xffff;
        b->product = value ? b->dividend % value : b->dividend;
        return;
    }
  }
  b->failed = 1;
}
typedef struct CameraRange { uint32_t first, end; } CameraRange;
static const CameraRange kWetRanges[] = {
  {0x809ba0,0x809bc0}, {0x80c263,0x80c56a},
  {0x80c246,0x80c263}, {0xb8a7e7,0xb8ab8c}
};
static const CameraRange kDryRanges[] = {
  {0x809ba0,0x809c4f}, {0x809c8f,0x809e11},
  {0x809f9a,0x80a14f}, {0x80c246,0x80c501},
  {0xbcb041,0xbcb102}, {0xb8a7e7,0xb8ab8c}
};
static int NativeCameraRun(WaterCameraBus *bus, unsigned mode, int initialize) {
  const NativeCameraController *controller = CameraController(mode);
  if (!controller || (initialize && !controller->initialize)) return 0;
  const int wet = controller->wet;
  const CameraRange *ranges = wet ? kWetRanges : kDryRanges;
  const unsigned count = wet ? sizeof kWetRanges / sizeof *ranges :
                               sizeof kDryRanges / sizeof *ranges;
  Interp816 cpu = {0};
  cpu.mem = bus; cpu.read = WaterCameraRead; cpu.write = WaterCameraWrite;
  cpu.k = cpu.db = 0x80;
  cpu.pc = initialize ? controller->initialize : controller->update;
  cpu.sp = initialize ? 0x1ff : 0x1fd;
  if (!initialize) { bus->ram[0x1fe] = 0xfe; bus->ram[0x1ff] = 0xff; }
  const uint16_t stop = initialize ? 0xc56a : 0xffff;
  for (unsigned i = 0; i < 8192 && !bus->failed; ++i) {
    if (cpu.k == 0x80 && cpu.pc == stop && cpu.sp == 0x1ff) return 1;
    uint32_t pc = ((uint32_t)cpu.k << 16) | cpu.pc;
    unsigned r;
    for (r = 0; r < count; r++)
      if (pc >= ranges[r].first && pc < ranges[r].end) break;
    if (r == count) return 0;
    interp816_runOpcode(&cpu);
    if (cpu.waiting || cpu.stopped) return 0;
  }
  return 0;
}

/* Store an already completed camera update, including its fractional motion.
 * Shared-view ownership uses this same history so releasing it has no seam. */
static void StoreCameraHistory(const uint8_t *ram,uint16_t slot,int wet,
                               uint16_t x,uint16_t y) {
  s_world.water.ready=1;
  s_world.water.entrance=Read(ram,0x3e);
  s_world.water.mode=Read(ram,0x32);
  s_world.water.height=Read(ram,0x4a);
  s_world.water.slot=slot;
  s_world.water.x=x;s_world.water.y=y;
  s_world.water.frame=Read(ram,0x28);
  memcpy(s_world.water.motion,ram+0x1a4b,sizeof s_world.water.motion);
  if(wet)memcpy(s_world.water.region,ram+0x1e0f,4);
  else {
    memcpy(s_world.water.region,ram+0x1af9,2);
    memcpy(s_world.water.region+2,ram+0x1b1b,2);
    memcpy(s_world.land_region,ram+0x1e0f,4);
  }
}

static int NativeCamera(const uint8_t *ram, uint16_t slot, int advance,
                        uint16_t *x, uint16_t *y) {
  unsigned mode = Read(ram, 0x32);
  const NativeCameraController *controller = CameraController(mode);
  if (!g_rom || !controller || (slot != 2 && slot != 4) ||
      memcmp(g_rom + controller->signature,controller->bytes,controller->byte_count)) return 0;
  int wet = controller->wet;
  int ready = s_world.water.ready && s_world.water.entrance == Read(ram, 0x3e) &&
      s_world.water.mode == mode && s_world.water.height == Read(ram, 0x4a) &&
      s_world.water.slot == slot;
  if (ready && (!advance || s_world.water.frame == Read(ram, 0x28))) {
    *x = s_world.water.x; *y = s_world.water.y;
    return 1;
  }
  int acquire_vertical = !ready && mode == 9 && s_world.initialized &&
      s_world.entrance == Read(ram,0x3e) && s_world.mode == mode &&
      s_world.height == Read(ram,0x4a) && s_world.camera.ready &&
      s_world.camera.row >= 0 && s_world.camera.row < 224;
  if (!advance && !wet && !acquire_vertical) return 0;
  if (acquire_vertical) {
    *x = Clamp((int)Dkc1Actor_SprXPos(ram,slot) - (s_world.camera.column8 >> 8),
               Read(ram,0x1b23),Read(ram,0x1b25));
    unsigned height = Read(ram,0x4a);
    *y = Clamp((int)height - Dkc1Actor_SprYPos(ram,slot) - s_world.camera.row,
               0,height > 224 ? height - 224 : 0);
    /* An old state can already place P2 far from P1. Keep that accepted
     * viewport until the first completed native update acquires its bounds. */
    if (!advance) return 1;
  }
  WaterCameraBus bus = {0};
  memcpy(bus.ram, ram, sizeof bus.ram);
  if (acquire_vertical) {
    Write(bus.ram,0x88b,*x);Write(bus.ram,0x895,*y);
    Write(bus.ram,0x1a62,*x);Write(bus.ram,0x1a4c,*y);
    bus.ram[0x1a61] = bus.ram[0x1a4b] = 0;
  }
  Write(bus.ram, 0x56f, slot / 2);
  if (s_world.reserved & 0x100u)
    Write(bus.ram,0x579,(Read(bus.ram,0x579)&~0x88u)|(s_world.reserved&0x88u));
  Write(bus.ram,0x1e29,s_world.reserved>>8 & 0xfeu);
  /* A newly admitted dry controller replaces the old row approximation.
   * Keep its valid horizontal placement on a same-scene saved continuation;
   * vertical placement is acquired by the native authored controller. */
  if (!ready && controller == &kDryCamera && s_world.initialized && s_world.entrance == Read(ram,0x3e) &&
      s_world.mode == mode && s_world.height == Read(ram,0x4a) &&
      s_world.camera.ready && s_world.camera.row >= 0 && s_world.camera.row < 224) {
    *x = Clamp((int)Dkc1Actor_SprXPos(ram,slot) - (s_world.camera.column8 >> 8),
               Read(ram,0x1b23),Read(ram,0x1b25));
    Write(bus.ram,0x88b,*x);
  }
  if (ready) {
    memcpy(bus.ram + 0x1a4b, s_world.water.motion, sizeof s_world.water.motion);
    if (!wet) {
      memcpy(bus.ram + 0x1af9, s_world.water.region, 2);
      memcpy(bus.ram + 0x1b1b, s_world.water.region + 2, 2);
    }
    const uint8_t *region = wet ? s_world.water.region : s_world.land_region;
    memcpy(bus.ram + 0x1e0f, region, 2);
    bus.ram[0x1e12] = region[3]; /* $1E11 remains gameplay input. */
    Write(bus.ram, 0x88b, s_world.water.x);
    Write(bus.ram, 0x895, s_world.water.y);
  } else if (wet && !NativeCameraRun(&bus, mode, 1)) return 0;
  uint16_t focus=Read(bus.ram,0x1e29);
  if (focus && focus<0x34u && CameraFocusForKong(ram,focus,slot)) {
    /* B6AD36 also supplies $50 look-ahead for a moving focus actor. The
     * other actor's borrowed tick has already restored the primary column. */
    uint16_t speed=Dkc1CoopEntityRead16(ram,0xe89,focus);
    if (speed&0x8000u) speed=(uint16_t)(0u-speed);
    if (speed&0xfff0u) Write(bus.ram,0x1a69,0x50);
  }
  /* Dry cameras seed at their first completed native boundary; wet cameras
   * have a separate source-backed initializer before their first tick. */
  if (advance && (ready || wet || acquire_vertical) && !NativeCameraRun(&bus, mode, 0)) {
    s_world.water.ready = 0;
    return 0;
  }
  if (ready || wet || acquire_vertical) { *x = Read(bus.ram, 0x88b); *y = Read(bus.ram, 0x895); }
  if (advance) StoreCameraHistory(bus.ram,slot,wet,*x,*y);
  return 1;
}

static int Camera(const uint8_t *ram, int view, int advance,
                          uint16_t *x, uint16_t *y) {
  uint32_t slots[2];
  if (!ram || !x || !y || view < 0 || view > 1)
    return 0;
  Dkc1CoopPrimaryCamera(ram, x, y);
  if (s_scanning) { *x = s_primary_x; *y = s_primary_y; }
  if (view == 0)
    return 1;
  if (!Dkc1CoopEnabled() || !(Dkc1CoopSceneCapabilities(ram) & kDkc1CoopSceneCamera))
    return 0;
  Dkc1CoopCacheKongSlots(ram, slots);
  if (!Dkc1CoopKongCam(ram, slots[1]))
    return 0;
  if (Dkc1CoopFixedBossRoom(ram))
    return 1; /* The arena controller owns both fixed viewports. */
  if (Dkc1CoopMinecartViewOwner(ram)) {
    /* The trip renders one completed cartridge camera. Its private successor
     * must inherit that controller's history, not resume a pre-boarding ease. */
    if (advance) StoreCameraHistory(ram,(uint16_t)slots[1],0,*x,*y);
    return 1;
  }
  if (!Dkc1CoopKongOwnCamera(ram, slots[1]))
    return 1;  /* Shared entry/exit scripts keep their fixed screen edge. */
  unsigned mode = Read(ram, 0x32);
  if (NativeCameraMode(mode)) {
    NativeCamera(ram, (uint16_t)slots[1], advance, x, y);
    return 1; /* Rejected private execution retains the stock camera. */
  }
  const unsigned lower = Read(ram, 0x1b23), upper = Read(ram, 0x1b25);
  const unsigned height = Read(ram, 0x004a);
  const uint16_t primary_x = *x;
  const uint16_t primary_y = *y;
  /* A viewport row is acquired from a visible, initialized Kong. Entry
   * scripts briefly place the returning actor above the picture; retaining
   * that negative row makes his later landing drag the camera downward. */
  const int camera_ready = s_world.camera.ready &&
      s_world.camera.row >= 0 && s_world.camera.row < 224;
  int partner_x = Dkc1CoopEntityRead16(ram, 0x0b19, (uint16_t)slots[1]);
  int partner_y = Dkc1CoopEntityRead16(ram, 0x0bc1, (uint16_t)slots[1]);
  /* The cartridge camera keeps a lead ahead of the Kong it frames.
   * Donkey is the reference while he is inside that picture. Walking
   * back eases that column toward the center. Diddy keeps his own
   * column, so his walk does this even while Donkey stands still. */
  int lead_x = 0, lead_y = 0, reference = 0;
  for (int i = 0; i < 2 && !lead_y; i++) {
    if (!Dkc1CoopKongCam(ram, slots[i])) continue;
    int ay = Dkc1CoopEntityRead16(ram, 0x0bc1, (uint16_t)slots[i]);
    if (!ay) continue;
    int ax = Dkc1CoopEntityRead16(ram, 0x0b19, (uint16_t)slots[i]);
    int sx = ax - (int)primary_x;
    if (sx < -16 || sx >= 256 + 16) continue;
    int sy = (int)height - (int)primary_y - ay;
    if (!camera_ready && (sy < 0 || sy >= 224 ||
        !(Dkc1Actor_SprCollisionFlags(ram, slots[i]) & 0x0100u))) continue;
    lead_x = ax;
    lead_y = ay;
    reference = i + 1;
  }
  /* A death handoff can leave the survivor outside the cartridge picture.
   * Keep his acquired framing while that camera catches up. The fallback
   * center is only an initializer, not a replacement for a live viewport. */
  if (!lead_y && camera_ready) {
    lead_x = partner_x;
    lead_y = partner_y;
  }
  if (!lead_y && !camera_ready &&
      !(Dkc1Actor_SprCollisionFlags(ram, slots[1]) & 0x0100u)) {
    if (advance) memset(&s_world.camera, 0, sizeof s_world.camera);
    return 1; /* Keep native entry framing until an actor can acquire it. */
  }
  if (lead_y || camera_ready) {
    int framed = reference ? lead_x - (int)primary_x
                           : s_world.camera.column8 >> 8;
    int column8 = camera_ready ? s_world.camera.column8 : framed * 256;
    /* Acquire the cartridge row once, then follow this Kong alone. A
     * reference change or the native death-camera easing must not scroll
     * an already established stationary viewport. */
    int row = camera_ready ? s_world.camera.row
                                  : (int)height - (int)primary_y - lead_y;
    if (advance) {
      s_world.camera.row = row;
      s_world.camera.reference = reference;
      if (!camera_ready) {
        s_world.camera.forward = framed;
        s_world.camera.previous_framed = framed;
        s_world.camera.ready = 1;
      }
      if ((framed <= 96 || framed >= 160) &&
          framed == s_world.camera.previous_framed)
        s_world.camera.forward = framed;
      s_world.camera.previous_framed = framed;
      int pvel = (int16_t)Dkc1Actor_SprXVelocity(ram, slots[1]);
      int target = column8 >> 8;
      if (pvel >= 0x20 || pvel <= -0x20) {
        if (s_world.camera.forward < 112)
          target = pvel > 0 ? s_world.camera.forward : 128;
        else if (s_world.camera.forward > 144)
          target = pvel < 0 ? s_world.camera.forward : 128;
      }
      column8 += (target * 256 - column8) / 48;
      s_world.camera.column8 = column8;
    }
    *x = Clamp(partner_x - (column8 >> 8), lower, upper);
    *y = Clamp((int)height - partner_y - row, 0,
               height > 224 ? height - 224 : 0);
  } else {
    *x = Clamp(partner_x - 128, lower, upper);
    *y = Clamp((int)height - partner_y - 112, 0,
               height > 224 ? height - 224 : 0);
  }
  return 1;
}

void Dkc1CoopWorldReset(void) {
  memset(&s_world, 0, sizeof s_world);
}

int Dkc1CoopWorldGetCamera(const uint8_t *ram, int view,
                          uint16_t *x, uint16_t *y) {
  return Camera(ram, view, 0, x, y);
}

void Dkc1CoopWorldNoteKongCamera(const uint8_t *ram,uint16_t slot) {
  if (ram && slot==4 && Dkc1CoopEnabled() && !RtlSpeculativeExecutionActive())
    s_world.reserved=(uint16_t)(0x100u|(Read(ram,0x579)&0x88u));
}

void Dkc1CoopWorldNoteNativeCamera(const uint8_t *ram) {
  uint16_t x, y;
  if (!ram || (!NativeCameraMode(Read(ram,0x32))) || RtlSpeculativeExecutionActive()) return;
  if (!Camera(ram, 1, 1, &x, &y))
    memset(&s_world.water, 0, sizeof s_world.water);
  s_world.reserved&=0x188u; /* Both views have consumed this frame's focus. */
}

void Dkc1CoopWorldNoteFrame(const uint8_t *ram) {
  uint16_t x, y;
  if (!Camera(ram, 1, !NativeCameraMode(Read(ram,0x32)), &x, &y)) {
    memset(&s_world.camera, 0, sizeof s_world.camera);
    memset(&s_world.water, 0, sizeof s_world.water);
  } else if (!NativeCameraMode(Read(ram,0x32))) {
    memset(&s_world.water, 0, sizeof s_world.water);
  }
}

size_t Dkc1CoopWorldStateSize(uint32_t host_version) {
  if (host_version >= kDkc1WsSaveVersionCoopDryCamera) return kWorldWireV4Bytes;
  if (host_version >= kDkc1WsSaveVersionCoopNativeCamera) return kWorldWireV3Bytes;
  if (host_version >= kDkc1WsSaveVersionCoopContinuation) return kWorldWireV2Bytes;
  return kWorldWireV1Bytes;
}

void Dkc1CoopWorldSaveState(Dkc1CoopWorldSnapshot *state) {
  if (state) {
    *state = s_world;
    state->version = 7;
  }
}

void Dkc1CoopWorldLoadState(const Dkc1CoopWorldSnapshot *state) {
  Dkc1CoopWorldReset();
  if (!state) return;
  if (state->version == 1) {
    memcpy(&s_world, state, kWorldWireV1Bytes);
  } else if ((state->version >= 2 && state->version <= 7) && state->camera.ready >= 0 &&
      state->camera.ready <= 1 && state->camera.reference >= 0 &&
      state->camera.reference <= 2 && state->camera.forward >= -16 &&
      state->camera.forward <= 271 && state->camera.previous_framed >= -16 &&
      state->camera.previous_framed <= 271 && state->camera.column8 >= -16*256 &&
      state->camera.column8 <= 271*256 && state->camera.row >= -131070 &&
      state->camera.row <= 65535) {
    memcpy(&s_world, state, kWorldWireV2Bytes);
    if ((state->version >= 3 && state->version <= 7) && state->water.ready <= 1 &&
        (!state->water.ready || ((state->water.mode == 3 || (state->version >= 4 &&
          NativeCameraMode(state->water.mode))) &&
         (state->water.slot == 2 || state->water.slot == 4))))
      s_world.water = state->water;
    if (state->version >= 4 && s_world.water.ready)
      memcpy(s_world.land_region, state->land_region, sizeof s_world.land_region);
  }
  /* Before v6, mode5 replayed C47B although its native updater calls
   * 9BEC. Those private velocities/targets belong to a different controller.
   * Preserve all guest state and scanner cursors; reacquire only that history. */
  if (state->version < 6 && s_world.water.mode == 5)
    memset(&s_world.water, 0, sizeof s_world.water);
  if (state->version<5 || (state->version<7 && (s_world.reserved&~0x188u)) ||
      (s_world.reserved&0x77u) ||
      ((s_world.reserved&0xfe00u) && !Dkc1CoopEntityIsHandle(s_world.reserved>>8 & 0xfeu)) ||
      ((s_world.reserved&0x88u) && !(s_world.reserved&0x100u))) s_world.reserved=0;
}

int Dkc1CoopWorldScannerActive(void) { return s_scanning; }

void Dkc1CoopWorldNoteScan(CpuState *cpu) {
  if (!s_scanning && cpu && Dkc1CoopEnabled())
    s_world.scan_pending = 1;
}

void Dkc1CoopWorldNoteInitialize(void) {
  if (!s_scanning) Dkc1CoopWorldReset();
}

static void CaptureCursors(const uint8_t *ram, Dkc1CoopWorldSnapshot *state) {
  for (unsigned i = 0; i < 5; i++)
    state->cursor[i] = Read(ram, kCursorAddress[i]);
  memcpy(state->vertical, ram + 0x1e03, sizeof state->vertical);
}

static void ApplyCursors(uint8_t *ram, const Dkc1CoopWorldSnapshot *state) {
  for (unsigned i = 0; i < 5; i++)
    Write(ram, kCursorAddress[i], state->cursor[i]);
  memcpy(ram + 0x1e03, state->vertical, sizeof state->vertical);
}

static int SameScene(const uint8_t *ram) {
  return s_world.initialized && s_world.entrance == Read(ram, 0x003e) &&
      s_world.mode == Read(ram, 0x0032) &&
      s_world.height == Read(ram, 0x004a) &&
      s_world.lower == Read(ram, 0x1b23) &&
      s_world.upper == Read(ram, 0x1b25);
}

int Dkc1CoopWorldRunScanner(CpuState *cpu) {
  uint16_t x, y;
  if (!cpu || cpu->ram != g_ram || s_scanning ||
      !s_world.scan_pending ||
      !(Dkc1CoopSceneCapabilities(g_ram) & kDkc1CoopSceneScanner) ||
      !Dkc1CoopWorldGetCamera(g_ram, 1, &x, &y)) {
    if (!Dkc1CoopEnabled()) Dkc1CoopWorldReset();
    return 0;
  }
  s_world.scan_pending = 0;
  if (RtlSpeculativeExecutionActive())
    return 0;
  Dkc1CoopTransaction transaction;
  if (!Dkc1CoopTransactionBegin(&transaction,kParticipants,
      sizeof kParticipants/sizeof *kParticipants)) return 0;
  const int initialize = !SameScene(g_ram);
  const Dkc1CoopWorldSnapshot old_world = s_world;
  Dkc1CoopWorldSnapshot primary;
  CaptureCursors(g_ram, &primary);
  memcpy(s_before, g_ram, sizeof s_before);
  Dkc1CoopMachineCapture(&s_machine);
  Dkc1CoopPrimaryCamera(g_ram, &s_primary_x, &s_primary_y);
  s_scanning = 1;
  if (initialize) {
    memset(&s_world, 0, offsetof(Dkc1CoopWorldSnapshot, camera));
    s_world.entrance = Read(g_ram, 0x003e);
    s_world.mode = Read(g_ram, 0x0032);
    s_world.height = Read(g_ram, 0x004a);
    s_world.lower = Read(g_ram, 0x1b23);
    s_world.upper = Read(g_ram, 0x1b25);
  }
  ApplyCursors(g_ram, &s_world);
  Write(g_ram, 0x088b, x);
  Write(g_ram, 0x0895, y);

  /* This is an allocation pass, not a second machine frame. The established
   * speculative scope rejects MMIO and prevents clock/APU synchronization;
   * successful WRAM allocations commit, all temporary CPU state is private. */
  CpuState scratch = *cpu;
  scratch.D = 0;
  scratch.S = 0x01ff;
  scratch.DB = scratch.PB = 0xbd;
  scratch.P &= (uint8_t)~0x38u;
  scratch.m_flag = scratch.x_flag = scratch._flag_D = 0;
  scratch.host_return_valid = 0;
  RtlSpeculativeExecutionBegin();
  RecompReturn result = RECOMP_RETURN_NORMAL;
  if (initialize) {
    cpu_push_jsl_return_frame(&scratch);
    result = CODE_BDFDBD_M0X0(&scratch);
  }
  if (result == RECOMP_RETURN_NORMAL) {
    /* Exact F88A window arithmetic, without decrementing F8A2's $1E13
     * gameplay timer again. Both scanners use the existing width policy. */
    uint16_t left = (uint16_t)(x - Dkc1VideoObjectScannerCullLeft(0x20));
    if (left >= 0xfc00) left = 0;
    Write(g_ram, 0xef, left);
    Write(g_ram, 0xf1,
          (uint16_t)(left + Dkc1VideoObjectScannerCullSpan(0x140)));
    cpu_push_jsl_return_frame(&scratch);
    result = CODE_BDFE39_M0X0(&scratch);
  }
  const int commit = result == RECOMP_RETURN_NORMAL &&
      !RtlSpeculativeExecutionViolation() && scratch.S == 0x01ff &&
      Dkc1CoopMachineDevicesUnchanged(&s_machine);
  RtlSpeculativeExecutionEnd();
  if (commit) {
    CaptureCursors(g_ram, &s_world);
    s_world.initialized = 1;
    /* Scratch/stack and camera are not shared allocation state. $192B,
     * actors and graphics queues deliberately remain committed. */
    memcpy(g_ram, s_before, 0x0200);
    ApplyCursors(g_ram, &primary);
    Write(g_ram, 0x088b, Read(s_before, 0x088b));
    Write(g_ram, 0x0895, Read(s_before, 0x0895));
  } else {
    memcpy(g_ram, s_before, sizeof s_before);
    s_world = old_world;
  }
  Dkc1CoopMachineRestore(&s_machine, kCoopRestoreClocks | kCoopRestoreDevices);
  Dkc1CoopTransactionEnd(&transaction,commit);
  s_scanning = 0;
  return commit ? 1 : -1;
}

unsigned Dkc1CoopWorldFreeActorSlots(const uint8_t *ram) {
  unsigned free_slots = 0;
  if (ram)
    for (uint16_t slot = 2; slot <= Dkc1CoopEntityLastHandle();
         slot = Dkc1CoopEntityNextHandle(slot))
      free_slots += Dkc1CoopEntityRead16(ram, 0x0d45, slot) == 0;
  return free_slots;
}

static int LeftAllows(uint16_t left, uint16_t actor_x) {
  /* Native BMI/FC00 special case distinguishes an underflow near world zero
   * from a valid unsigned coordinate at or beyond world $8000. */
  return left >= 0xfc00 ? left >= actor_x : left < actor_x;
}

static int VerticalAllows(uint16_t camera_y, uint16_t target, unsigned span) {
  return (int16_t)(camera_y - target) < 0 &&
      (int16_t)((uint16_t)(camera_y + span) - target) >= 0;
}

static int KeepActorInView(CpuState *cpu, uint16_t routine,
                           uint16_t x, uint16_t y) {
  const uint8_t *ram = cpu->ram;
  const unsigned slot = Read(ram, (uint16_t)(cpu->D + 0x82));
  if (!Dkc1CoopEntityIsHandle((uint16_t)slot)) return 0;
  const uint16_t ax = Dkc1CoopEntityRead16(ram, 0x0b19, (uint16_t)slot);
  const uint16_t ay = Dkc1CoopEntityRead16(ram, 0x0bc1, (uint16_t)slot);
  const uint16_t height = Read(ram, 0x004a);
  /* BCBC63/77 use the scanner's X interval, without a vertical test.
   * Moving actors can leave their authored source behind while remaining
   * visible to the other player. Rejecting them on the primary interval
   * reaches BDF570, clears $192B and permits an origin respawn. */
  if (routine == 0xbc63 || routine == 0xbc77) {
    uint16_t minimum = (uint16_t)(x - Dkc1VideoObjectScannerCullLeft(0x20));
    if (minimum >= 0xfc00u) minimum = 0;
    uint16_t maximum = (uint16_t)(minimum + Dkc1VideoObjectScannerCullSpan(0x140));
    if (routine == 0xbc77) {
      /* BCBC7C..BC84 clamps the signed subtraction result to zero.
       * This is the native predicate, not an initialization heuristic. */
      uint16_t padded = (uint16_t)(minimum - 0x20u);
      minimum = padded <= 0x7fffu ? padded : 0;
      maximum = (uint16_t)(maximum + 0x20u);
    }
    return LeftAllows(minimum, ax) && maximum >= ax;
  }
  unsigned left = 0x20, span = 0x140, pad_y = 0x20, span_y = 0x120;
  if (routine == 0xf85b) {
    const uint16_t target = (uint16_t)(height + 0x40 - ay);
    return (int16_t)Dkc1CoopEntityRead16(ram, 0x0ef1, (uint16_t)slot) < 0 ?
        (int16_t)((uint16_t)(y + 0x160) - target) >= 0 :
        (int16_t)(y - target) < 0;
  }
  if (routine == 0xf7d4) { left = 0x80; span = 0x200; }
  else if (routine == 0xf81c) { left = 0xc0; span = 0x280; }
  else if (routine == 0xf751) { pad_y = 0x80; span_y = 0x180; }
  else if (routine != 0xf6ff && routine != 0xf78c) return 0;
  if (routine == 0xf6ff || routine == 0xf751 || routine == 0xf78c) {
    left = Dkc1VideoObjectScannerCullLeft((uint16_t)left);
    span = Dkc1VideoObjectScannerCullSpan((uint16_t)span);
  }
  const uint16_t minimum = (uint16_t)(x - left);
  if (!LeftAllows(minimum, ax) ||
      (routine != 0xf751 && (uint16_t)(minimum + span) < ax))
    return 0;
  if (routine == 0xf81c) return 1;
  const uint16_t target = (uint16_t)(height + pad_y - ay);
  if (routine == 0xf6ff && ((int16_t)target < 0 || (int16_t)(y - target) >= 0))
    return ay < 0x0200;
  return VerticalAllows(y, target, span_y);
}

int Dkc1CoopWorldKeepActor(CpuState *cpu, uint16_t routine) {
  uint16_t x, y;
  if (!cpu || !Dkc1CoopWorldGetCamera(cpu->ram, 1, &x, &y)) return 0;
  if (KeepActorInView(cpu, routine, x, y)) return 1;
  Dkc1CoopWorldGetCamera(cpu->ram, 0, &x, &y);
  return KeepActorInView(cpu, routine, x, y);
}

static uint16_t RomWord(uint16_t address) {
  return (uint16_t)(g_rom[0x3d0000u + address] |
                   (uint16_t)g_rom[0x3d0000u + (uint16_t)(address + 1)] << 8);
}

static int KeepSourceInView(CpuState *cpu, int group,
                            uint16_t x, uint16_t y) {
  const uint8_t *ram = cpu->ram;
  const unsigned slot = cpu->X;
  if (!Dkc1CoopEntityIsHandle((uint16_t)slot)) return 0;
  const unsigned source = Dkc1CoopEntityRead16(ram, 0x15fd, (uint16_t)slot);
  const unsigned entrance = Read(ram, 0x003e);
  if (source >= 0x100 || entrance >= 0xe6) return 0;
  const uint16_t table = RomWord((uint16_t)(0x8000 + entrance * 2));
  const uint16_t record = (uint16_t)(table + source * 8);
  uint16_t left = (uint16_t)(x - Dkc1VideoObjectScannerCullLeft(0x20));
  if (!group) {
    if (left >= 0xfc00) left = 0;
    const uint16_t right = (uint16_t)(left + Dkc1VideoObjectScannerCullSpan(0x140));
    const uint16_t source_x = RomWord((uint16_t)(record + 2));
    if (left >= source_x || right < source_x) return 0;
    return Read(ram, 0x0032) != 3 || VerticalAllows(y,
        (uint16_t)(Read(ram, 0x004a) + 0x20 - RomWord((uint16_t)(record + 4))),
        0x120);
  }
  if (RomWord(record) != 5) return 0;
  const uint16_t first = (uint16_t)(RomWord((uint16_t)(record + 6)) + 8);
  uint16_t last = first;
  int child_visible = 0;
  for (unsigned i = 0; i < 0x100; i++) {
    const unsigned child_source = (uint16_t)(last - table) >> 3;
    if (child_source < 0x100) {
      const unsigned child = ram[0x192b + child_source];
      if (Dkc1CoopEntityIsHandle((uint16_t)child)) {
        const uint16_t child_x = Dkc1CoopEntityRead16(ram, 0x0b19, (uint16_t)child);
        /* BDF5F8..BDF60D uses the live child's X after the source group
         * leaves the view; a moving child can keep its parent alive. */
        if (left < child_x &&
            (uint16_t)(left + Dkc1VideoObjectScannerCullSpan(0x140)) >= child_x)
          child_visible = 1;
      }
    }
    if (!RomWord((uint16_t)(last + 8))) break;
    if (i == 0xff) return 0;
    last = (uint16_t)(last + 8);
  }
  return child_visible || ((uint16_t)(x + Dkc1VideoObjectScannerCullLeft(0x120)) >=
          RomWord((uint16_t)(first + 2)) &&
      LeftAllows(left, RomWord((uint16_t)(last + 2))));
}

int Dkc1CoopWorldKeepSource(CpuState *cpu, int group) {
  uint16_t x, y;
  if (!cpu || !g_rom || !Dkc1CoopWorldGetCamera(cpu->ram, 1, &x, &y)) return 0;
  if (KeepSourceInView(cpu, group, x, y)) return 1;
  Dkc1CoopWorldGetCamera(cpu->ram, 0, &x, &y);
  return KeepSourceInView(cpu, group, x, y);
}
