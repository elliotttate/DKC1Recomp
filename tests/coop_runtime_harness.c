#include "dkc1_coop_mod.c"
#include "coop_test_support.h"
#include "snes/interp816.h"

int Dkc1VideoLoadCgramFromRom(struct Ppu *p, unsigned dst, uint8_t bank,
                            uint16_t addr, unsigned words) { return 1; }
void Dkc1VideoCopyCgram(struct Ppu *p, unsigned dst, unsigned src,
                       unsigned words) {}

static uint8_t ram[0x20000];
static unsigned failures;
#define CHECK(cond, msg) do { if (!(cond)) { puts(msg); failures++; } } while (0)

static const uint8_t trigger_code[] = { /* ROM_F553 */ 0x60 };
static uint8_t TriggerRead(void *unused, uint32_t address) {
  if (address >= 0xb6f553 && address < 0xb6f553 + sizeof trigger_code)
    return trigger_code[address - 0xb6f553];
  unsigned bank = address >> 16, at = address & 0xffff;
  if (at < 0x2000 && (bank == 0x7e || !(bank & 0x40))) return ram[at];
  return 0;
}
static void TriggerWrite(void *unused, uint32_t address, uint8_t value) {
  unsigned at = address & 0xffff;
  if (at < 0x2000) ram[at] = value;
}
static int NativeTriggerAllows(uint16_t object, uint16_t kong) {
  Interp816 cpu = {0}; cpu.read = TriggerRead; cpu.write = TriggerWrite;
  cpu.pc = 0xf553; cpu.k = 0xb6; cpu.db = 0x7e;
  cpu.x = object; cpu.y = kong; cpu.sp = 0x1fd;
  ram[0x1fe] = 0xfe; ram[0x1ff] = 0xfe;
  for (int step = 0; step < 64 && cpu.pc != 0xfeff; ++step)
    interp816_runOpcode(&cpu);
  CHECK(cpu.pc == 0xfeff, "native trigger oracle did not return");
  return cpu.c;
}

static void Setup(void) {
  memset(ram, 0, sizeof ram);
  ResetOverlayState();
  CoopTestResetSupport();
  s_runtime.enabled = 1;
  Dkc1WramSetU16(ram, 0x1B25, 0x1400);
  Dkc1Actor_SetSpriteIDLo(ram, 2, 1);
  Dkc1Actor_SetSpriteIDLo(ram, 4, 2);
  Dkc1Actor_SetSprCollisionFlags(ram, 2, 0x101);
  Dkc1Actor_SetSprCollisionFlags(ram, 4, 0x101);
  Dkc1CoopCacheKongSlots(ram, s_runtime.slot);
}

/* Exercise the same ordering as Dkc1LoadedExtra; keep immutable machine RAM
 * outside the reset paths, and compare the actual next actor completion. */
static void CheckContinuation(uint32_t next_slot) {
  Dkc1CoopSnapshot history;
  Dkc1CoopActorSnapshot actor;
  Dkc1CoopContinuation saved, expected, loaded;
  uint8_t root[sizeof ram], result[sizeof ram];
  Dkc1CoopSaveState(&history); Dkc1CoopSaveActorState(&actor);
  Dkc1CoopSaveContinuation(&saved); memcpy(root,ram,sizeof ram);
  if(actor.pending) Dkc1CoopResumeActor(ram,actor.resume_pc,actor.stack);
  else Dkc1CoopBeforeActor(ram,next_slot,0);
  Dkc1CoopAfterActor(ram);memcpy(result,ram,sizeof ram);
  Dkc1CoopSaveContinuation(&expected);
  memcpy(ram,root,sizeof ram);Dkc1CoopLoadState(&history,ram);
  CHECK(Dkc1CoopLoadActorState(&actor,ram),"actor continuation rejected");
  Dkc1CoopLoadContinuation(&saved);Dkc1CoopSaveContinuation(&loaded);
  CHECK(!memcmp(&saved,&loaded,sizeof saved),"rope continuation did not roundtrip");
  if(actor.pending) Dkc1CoopResumeActor(ram,actor.resume_pc,actor.stack);
  else Dkc1CoopBeforeActor(ram,next_slot,0);
  Dkc1CoopAfterActor(ram);Dkc1CoopSaveContinuation(&loaded);
  CHECK(!memcmp(result,ram,sizeof ram),"rope split load changed next actor WRAM");
  CHECK(!memcmp(&expected,&loaded,sizeof expected),"rope split load changed next actor history");
  /* Every semantic rope field participates, including substitutions installed
   * by non-Kong rope actors and pending release velocity. */
  CHECK(Dkc1CoopBeginTransaction(),"rope transaction did not begin");
  memset(&s_runtime.rope,0x39,sizeof s_runtime.rope);s_runtime.actor.wait_age=1;
  Dkc1CoopEndTransaction(0);Dkc1CoopSaveContinuation(&loaded);
  CHECK(!memcmp(&expected,&loaded,sizeof loaded),"rejected rope transaction leaked state");
}

static void CannonBarrelRescue(void) {
  for (uint16_t rescued=2;rescued<=4;rescued+=2) {
    Setup(); uint16_t rescuer=rescued^6u;
    CpuState cpu={0};cpu.ram=ram;cpu.X=rescued;cpu.A=1;
    Dkc1WramSetU16(ram,0x82,rescued);Dkc1WramSetU16(ram,0x84,rescued-2u);
    Dkc1WramSetU16(ram,0x579,1);
    Dkc1Actor_SetRAMTable1029Lo(ram,rescued,2);
    Dkc1Actor_SetRAMTable1029Lo(ram,rescuer,0x28);
    Dkc1Actor_SetSprXPos(ram,rescuer,0x2a4);Dkc1Actor_SetSprYPos(ram,rescuer,0xc5);
    Dkc1Actor_SetSprXVelocity(ram,rescuer,0x661);Dkc1Actor_SetSprYVelocity(ram,rescuer,0xfc10);
    Dkc1Actor_SetSprXSubpixel(ram,rescuer,0x4321);Dkc1Actor_SetSprYSubpixel(ram,rescuer,0x7654);
    Dkc1Actor_SetRAMTable1375Lo(ram,rescuer,0xffff);
    Dkc1Actor_SetRAMTable13E9Lo(ram,rescuer,0x18);
    Dkc1Actor_SetRAMTable0F25Lo(ram,rescuer,0x30);
    Dkc1Actor_SetRAMTable0E55Lo(ram,rescuer,0x50);
    Dkc1Actor_SetRAMTable0EBDLo(ram,rescuer,0x60);
    Dkc1Actor_SetSprFlags(ram,rescued,0x3c00);Dkc1Actor_SetSprFlags(ram,rescuer,0x4c00);
    const uint16_t native_fields[]={0xb19,0xbc1,0xdb9,0xe21,0xe89,0xef1,0xe55,0xebd,0xf25,0x1375,0x13e9,0x1029,0x11a1,0x12a5};
    uint8_t before[sizeof ram];memcpy(before,ram,sizeof ram);
    /* The same coordinates or an old falling pose cannot trigger this from
     * ordinary control; only the exact native pop hook calls the adapter. */
    s_runtime.enabled=0;Dkc1CoopFinishBarrelRescue(&cpu);CHECK(!memcmp(before,ram,sizeof ram)&&cpu.A==1,"disabled rescue changed state");s_runtime.enabled=1;
    Dkc1Actor_SetRAMTable1029Lo(ram,rescuer,0x29);memcpy(before,ram,sizeof ram);
    Dkc1CoopFinishBarrelRescue(&cpu);CHECK(!memcmp(before,ram,sizeof ram),"rescue copied a hidden cannon passenger");
    Dkc1Actor_SetRAMTable1029Lo(ram,rescuer,0x2a);memcpy(before,ram,sizeof ram);
    Dkc1CoopFinishBarrelRescue(&cpu);CHECK(!memcmp(before,ram,sizeof ram),"rescue copied a level-transition shot");
    Dkc1Actor_SetRAMTable1029Lo(ram,rescuer,0x28);
    Dkc1WramSetU16(ram,0x84,6);memcpy(before,ram,sizeof ram);
    Dkc1CoopFinishBarrelRescue(&cpu);CHECK(!memcmp(before,ram,sizeof ram),"rescue followed a foreign DP84");
    Dkc1WramSetU16(ram,0x84,rescued-2u);Dkc1CoopFinishBarrelRescue(&cpu);
    CHECK(cpu.A==0x17 && Dkc1Actor_SprAnimID(ram,rescued)==(rescued==2?0x17:0x7f),"rescue missed identity-specific flight animation");
    for(unsigned i=0;i<sizeof native_fields/sizeof native_fields[0];i++)
      CHECK(Dkc1CoopEntityRead16(ram,native_fields[i],rescued)==Dkc1CoopEntityRead16(ram,native_fields[i],rescuer),"rescue missed a flight controller field");
    CHECK((Dkc1Actor_SprFlags(ram,rescued)&0x3e00)==0x3c00,"rescue stole partner palette/tile flags");
  }
}

static void SharedMinecart(void) {
  for (uint16_t rider=2;rider<=4;rider+=2) {
    Setup();uint16_t follower=rider^6u;
    Dkc1WramSetU16(ram,0x32,5);Dkc1WramSetU16(ram,0x579,1);
    Dkc1Actor_SetSpriteIDLo(ram,6,0x51);
    Dkc1Actor_SetRAMTable1375Lo(ram,6,rider);
    Dkc1Actor_SetRAMTable13E9Lo(ram,6,follower);
    Dkc1Actor_SetRAMTable1029Lo(ram,rider,0x2e);
    Dkc1Actor_SetRAMTable1029Lo(ram,follower,0x0c);
    CHECK(Dkc1CoopMinecartForKong(ram,rider)==6 &&
          Dkc1CoopMinecartForKong(ram,follower)==6,"shared passenger binding missing");
    CHECK(Dkc1CoopKongCam(ram,2) && Dkc1CoopKongCam(ram,4),"parked passenger blacks a view");
    CHECK(!Dkc1CoopKongInBarrel(ram,follower),"cart follower became a DK-barrel wait");
    CHECK(!Dkc1CoopKongOwnCamera(ram,follower),"shared vehicle acquired a second camera");
    Dkc1CoopBeginFrame(ram,1);Dkc1CoopBeforeActor(ram,6,0);
    CHECK(Dkc1WramU16(ram,0x16e9+rider-2)&0x8000,"Donkey cannot jump shared cart");
    CHECK(Dkc1WramU16(ram,DKC1_WRAM_JoypadHeld)&0x8000,"Donkey held jump lost");
    Dkc1CoopAfterActor(ram);Dkc1CoopBeginFrame(ram,0);
    Dkc1CoopBeginFrame(ram,0x1000);Dkc1CoopBeforeActor(ram,6,0);
    CHECK(Dkc1WramU16(ram,0x16e9+rider-2)&0x8000,"Diddy cannot jump shared cart");
    CHECK(Dkc1WramU16(ram,DKC1_WRAM_JoypadHeld)&0x8000,"Diddy held jump lost");
    Dkc1CoopAfterActor(ram);
    CHECK(!Dkc1Actor_SpriteIDLo(ram,0x80),"common cart duplicated");
    CHECK(Dkc1CoopMinecartViewOwner(ram)==rider/2,"seated cart view lost first rider");
    Dkc1Actor_SetRAMTable1375Lo(ram,6,0);
    Dkc1Actor_SetRAMTable13E9Lo(ram,6,0);
    Dkc1Actor_SetRAMTable1029Lo(ram,rider,0x2f);
    Dkc1Actor_SetRAMTable13E9Lo(ram,rider,6);
    CHECK(!Dkc1CoopMinecartForKong(ram,rider),"jump acquired seated gameplay control");
    CHECK(Dkc1CoopMinecartViewOwner(ram)==rider/2,"native cart jump lost first rider view");
    Dkc1Actor_SetRAMTable1029Lo(ram,follower,2);
    uint32_t other_buttons=rider==2?0x083000u:0x83u;
    for(unsigned phase=0;phase<2;phase++) {
      Dkc1Actor_SetRAMTable1029Lo(ram,rider,phase?0x40:0x2f);
      Dkc1CoopBeginFrame(ram,0);Dkc1CoopBeginFrame(ram,other_buttons);
      Dkc1CoopBeforeActor(ram,rider,0);
      CHECK((Dkc1WramU16(ram,DKC1_WRAM_JoypadHeld)&0xc300u)==0xc100u,
            "airborne cart driver lost second player's jump/run/direction");
      CHECK((Dkc1WramU16(ram,0x16ebu+rider)&0xc300u)==0xc100u,
            "native cached airborne cart pad lost shared controls");
      Dkc1CoopAfterActor(ram);
    }
    Dkc1Actor_SetRAMTable1029Lo(ram,rider,0x2f);
    uint32_t driver_left=rider==2?0x40u:0x040000u;
    Dkc1CoopBeginFrame(ram,driver_left|other_buttons);
    Dkc1CoopBeforeActor(ram,rider,0);
    CHECK((Dkc1WramU16(ram,DKC1_WRAM_JoypadHeld)&0x300u)==0x200u,
          "shared air pad overrode driver's opposite direction");
    Dkc1CoopAfterActor(ram);
    Dkc1Actor_SetRAMTable1029Lo(ram,follower,0x0d);
    Dkc1CoopBeginFrame(ram,0);Dkc1CoopBeginFrame(ram,other_buttons);
    Dkc1CoopBeforeActor(ram,rider,0);
    CHECK(!(Dkc1WramU16(ram,DKC1_WRAM_JoypadHeld)&0xc300u),
          "dead partner retained shared air controls");
    Dkc1CoopAfterActor(ram);
    Dkc1Actor_SetRAMTable1029Lo(ram,follower,2);
    Dkc1Actor_SetRAMTable1029Lo(ram,rider,0x2f);
    Dkc1Actor_SetSpriteIDLo(ram,6,0x38);
    CHECK(!Dkc1CoopMinecartViewOwner(ram),"cart jump followed reused non-cart slot");
    Dkc1Actor_SetSpriteIDLo(ram,6,0x51);
    Dkc1Actor_SetRAMTable1029Lo(ram,rider,1);
    CHECK(!Dkc1CoopMinecartViewOwner(ram),"free air retained cart view");
    Dkc1Actor_SetRAMTable1029Lo(ram,rider,0x2f);
    Dkc1WramSetU16(ram,0x32,2);
    CHECK(!Dkc1CoopMinecartViewOwner(ram),"foreign level retained cart view");
    Dkc1WramSetU16(ram,0x32,5);
    Dkc1Actor_SetRAMTable1029Lo(ram,rider,0x2e);
    Dkc1Actor_SetRAMTable1029Lo(ram,follower,0x0c);
    Dkc1Actor_SetRAMTable1375Lo(ram,6,rider);
    Dkc1Actor_SetRAMTable13E9Lo(ram,6,follower);

    Dkc1CoopBeginFrame(ram,0x1001);Dkc1CoopBeforeActor(ram,6,0);
    CHECK(Dkc1WramU16(ram,0x16e9+rider-2)&0x8000,"second player's new press suppressed by held first pad");
    Dkc1CoopAfterActor(ram);
    Dkc1Actor_SetRAMTable13E9Lo(ram,6,0);
    CHECK(Dkc1CoopKongInBarrel(ram,follower),"unbound DK-barrel wait became a passenger");
  }
}
static void CartFollowerRelease(void) {
  for(uint16_t follower=2;follower<=4;follower+=2) {
    Setup();Dkc1WramSetU16(ram,0x32,5);
    Dkc1Actor_SetRAMTable1029Lo(ram,follower,2);
    Dkc1Actor_SetRAMTable11A1Lo(ram,follower,1);
    Dkc1Actor_SetSprCollisionFlags(ram,follower,0);
    Dkc1Actor_SetSprYVelocity(ram,follower,0xff00);
    uint16_t x=Dkc1Actor_SprXPos(ram,follower),y=Dkc1Actor_SprYPos(ram,follower);
    Dkc1CoopBeginFrame(ram,follower==4?0x080000:0x80);
    Dkc1CoopBeforeActor(ram,follower,0);
    CHECK(Dkc1Actor_RAMTable1029Lo(ram,follower)==1 &&
          Dkc1Actor_RAMTable11A1Lo(ram,follower)==0xc1,
          "finished cart follower never regained independent air control");
    CHECK(Dkc1Actor_SprXPos(ram,follower)==x && Dkc1Actor_SprYPos(ram,follower)==y &&
          Dkc1Actor_SprYVelocity(ram,follower)==0xff00,
          "cart follower release replaced native position or velocity");
    Dkc1CoopAfterActor(ram);
    /* The cart still owns the airborne companion while its driver jumps. */
    Dkc1Actor_SetRAMTable1029Lo(ram,follower,2);
    Dkc1Actor_SetRAMTable11A1Lo(ram,follower,1);
    uint16_t driver=follower^6u;
    Dkc1Actor_SetRAMTable1029Lo(ram,driver,0x2f);
    Dkc1Actor_SetRAMTable13E9Lo(ram,driver,6);
    Dkc1Actor_SetSpriteIDLo(ram,6,0x51);
    Dkc1CoopBeforeActor(ram,follower,0);
    CHECK(Dkc1Actor_RAMTable1029Lo(ram,follower)==2,
          "active cart jump split its companion into free play");
    Dkc1CoopAfterActor(ram);
  }
}
static void RetiredDualCartState(void) {
  Setup();Dkc1WramSetU16(ram,0x32,5);Dkc1WramSetU16(ram,0x579,1);
  Dkc1WramSetU16(ram,0x1e0f,6);
  for(uint16_t h=2;h<=4;h+=2)Dkc1Actor_SetRAMTable1029Lo(ram,h,0x2e);
  Dkc1Actor_SetSpriteIDLo(ram,6,0x51);Dkc1Actor_SetRAMTable1375Lo(ram,6,2);
  Dkc1Actor_SetSpriteIDLo(ram,0x80,0x51);Dkc1Actor_SetRAMTable1375Lo(ram,0x80,4);
  Dkc1CoopEntityWrite16(ram,0x15fd,0x80,9);JoinMinecartPassengers(ram);
  CHECK(Dkc1Actor_SpriteIDLo(ram,0x80)==0x51,"source-owned cart merged by historical-state policy");
  Dkc1CoopEntityWrite16(ram,0x15fd,0x80,0xffff);
  s_runtime.actor.wait.pending=1;JoinMinecartPassengers(ram);
  CHECK(Dkc1Actor_SpriteIDLo(ram,0x80)==0x51,"merged a suspended actor");
  s_runtime.actor.wait.pending=0;JoinMinecartPassengers(ram);
  CHECK(!Dkc1Actor_SpriteIDLo(ram,0x80) && Dkc1Actor_RAMTable13E9Lo(ram,6)==4,
        "synthetic cart retained instead of joining the common vehicle");
  CHECK(Dkc1CoopMinecartForKong(ram,2)==6 && Dkc1CoopMinecartForKong(ram,4)==6,
        "joined state lost a passenger");
  CHECK(Dkc1WramU16(ram,0x1e0f)==6,"original registered vehicle changed");
}

int main(void) {
  CannonBarrelRescue();
  SharedMinecart();
  CartFollowerRelease();
  /* Every world-boss entrance can freeze its native arena bounds. Both
   * controllers remain live, without admitting additional object scanning. */
  for (unsigned entrance = 0xe0; entrance < 0xe6; ++entrance) {
    Setup();
    Dkc1WramSetU16(ram, 0x32, 0x0c);
    Dkc1WramSetU16(ram, 0x3e, entrance);
    Dkc1WramSetU16(ram, 0x4a, 0x200);
    Dkc1WramSetU16(ram, 0x1b23, 0x100);
    Dkc1WramSetU16(ram, 0x1b25, 0x100);
    Dkc1WramSetU16(ram, 0x056f, 1);
    Dkc1Actor_SetRAMTable1029Lo(ram, 4, 5);
    Dkc1CoopBeginFrame(ram, 0x080000);
    Dkc1CoopBeforeActor(ram, 4, 0);
    CHECK(Dkc1CoopFixedBossRoom(ram) && !Dkc1CoopInLevel(ram),
          "fixed boss arena widened scanner admission");
    CHECK(s_runtime.actor.mux && Dkc1Actor_RAMTable1029Lo(ram, 4) == 0 &&
          Dkc1WramU16(ram, DKC1_WRAM_JoypadHeld) == 0x100,
          "fixed boss arena lost the second controller");
    Dkc1CoopAfterActor(ram);
    CHECK(Dkc1WramU16(ram, 0x056f) == 1,
          "fixed boss tick leaked its selected player");
    CpuState hit = {0}; hit.ram = ram; hit.X = 2;
    Dkc1Actor_SetRAMTable1029Lo(ram, 2, 0x0d);
    CHECK(Dkc1CoopIndependentDamage(&hit) &&
          Dkc1Actor_RAMTable1029Lo(ram, 4) == 0 &&
          Dkc1WramU16(ram, 0x056f) == 2,
          "fixed boss damage took control away from the survivor");
    hit.X = 4; Dkc1Actor_SetRAMTable1029Lo(ram, 4, 0x0d);
    CHECK(!Dkc1CoopIndependentDamage(&hit),
          "fixed boss bypassed the native last-Kong death");
    Dkc1WramSetU16(ram, 0x1b23, 0); Dkc1WramSetU16(ram, 0x1b25, 0);
    CHECK(!Dkc1CoopFixedBossRoom(ram), "zero-bound map admitted as a boss");
    for (unsigned actor = 0; actor < 3; ++actor) {
      static const uint16_t scene_ids[] = {0x70,0x74,0x6c};
      Dkc1Actor_SetSpriteIDLo(ram,6,scene_ids[actor]);
      CHECK(Dkc1CoopFixedBossRoom(ram) && !Dkc1CoopInLevel(ram),
            "boss X=0 lock lost scene admission or widened the scanner");
      Dkc1Actor_SetSpriteIDLo(ram,0,0x10);
      CHECK(!Dkc1CoopFixedBossRoom(ram),"map avatar retained a stale boss capability");
      Dkc1Actor_SetSpriteIDLo(ram,0,0);
    }
  }
  /* A native dry bonus has equal camera limits, but still accepts two pads.
   * Keep this control capability distinct from scrolling/scanner admission. */
  for (unsigned bonus_mode = 0; bonus_mode < 2; ++bonus_mode)
  for (unsigned invalid = 0; invalid < 6; ++invalid) {
    Setup();
    Dkc1WramSetU16(ram, 0x32, bonus_mode ? 0x0a : 1);
    Dkc1WramSetU16(ram, 0x3e, 3);
    Dkc1WramSetU16(ram, 0x1e15, 0x22);
    Dkc1WramSetU16(ram, 0x4a, 0x200);
    Dkc1WramSetU16(ram, 0x1b23, 0x6c00);
    Dkc1WramSetU16(ram, 0x1b25, 0x6c00);
    Dkc1WramSetU16(ram, 0x056f, 1);
    Dkc1Actor_SetRAMTable1029Lo(ram, 4, 5);
    if (invalid == 1) Dkc1WramSetU16(ram, 0x1e15, 2);
    if (invalid == 2) Dkc1WramSetU16(ram, 0x32, 3);
    if (invalid == 3) Dkc1WramSetU16(ram, 0x4a, 0);
    if (invalid == 4) Dkc1WramSetU16(ram, 0x3e, 0xe6);
    if (invalid == 5) Dkc1WramSetU16(ram, 0x32, 0x0b);
    Dkc1CoopBeginFrame(ram, 0x080000);
    Dkc1CoopBeforeActor(ram, 4, 0);
    CHECK(!Dkc1CoopInLevel(ram), "fixed bonus enabled scrolling admission");
    if (!invalid) {
      CHECK(s_runtime.actor.mux && Dkc1Actor_RAMTable1029Lo(ram, 4) == 0 &&
            Dkc1WramU16(ram, DKC1_WRAM_JoypadHeld) == 0x100,
            "fixed dry bonus left Diddy following without his pad");
      Dkc1WramSetU16(ram, DKC1_WRAM_CameraX, 0x777);
      Dkc1CoopAfterActor(ram);
      CHECK(Dkc1WramU16(ram, DKC1_WRAM_CameraX) == 0 &&
            Dkc1WramU16(ram, 0x056f) == 1,
            "bonus actor leaked the private camera or current Kong");
    } else {
      CHECK(!s_runtime.actor.mux && Dkc1Actor_RAMTable1029Lo(ram, 4) == 5,
            "unproven fixed scene received co-op control");
    }
  }
  /* Fixed native bonus completion consumes one shared return context,
   * whichever Kong owns the cartridge scene. The other pose is temporary. */
  for (unsigned leader = 1; leader <= 2; ++leader) {
    Setup();
    Dkc1WramSetU16(ram, 0x32, 0x0a);
    Dkc1WramSetU16(ram, 0x3e, 0xd7);
    Dkc1WramSetU16(ram, 0x1e15, 0x22);
    Dkc1WramSetU16(ram, 0x4a, 0x200);
    Dkc1WramSetU16(ram, 0x1b23, 0x2a00);
    Dkc1WramSetU16(ram, 0x1b25, 0x2a00);
    Dkc1WramSetU16(ram, 0x056f, leader);
    unsigned other = (leader ^ 3u) * 2u;
    Dkc1Actor_SetRAMTable1029Lo(ram, leader * 2u, 0x44);
    Dkc1Actor_SetRAMTable1029Lo(ram, other, 0x44);
    Dkc1CoopBeginFrame(ram, 0);
    s_runtime.lock = leader;
    CaptureCanonicalContext(ram);
    s_runtime.actor.captured = 1;
    Dkc1CoopBeforeActor(ram, other, 0);
    CHECK(!s_runtime.actor.mux && Dkc1Actor_RAMTable1029Lo(ram, other) == 5 &&
          Dkc1WramU16(ram, 0x056f) == leader,
          "bonus follower ran a second return transition");
    Dkc1CoopAfterActor(ram);
    CHECK(Dkc1Actor_RAMTable1029Lo(ram, other) == 0x44 &&
          !s_runtime.actor.follower.active,
          "bonus ending leaked the temporary follower pose");
    Dkc1CoopEndFrame(ram);
  }
  RetiredDualCartState();

  /* Third-token collector survives the pool reset and the existing 20-byte
   * snapshot. Native animal challenges use one rider with that player's pad. */
  for (unsigned animal = 9; animal <= 12; ++animal)
  for (unsigned collector = 1; collector <= 2; ++collector) {
    Setup(); CpuState token={0}; token.ram=ram;
    Dkc1WramSetU16(ram,0x82,6);Dkc1WramSetU16(ram,0x56f,collector);
    Dkc1Actor_SetSpriteIDLo(ram,6,0x45);
    Dkc1CoopEntityWrite16(ram,0x14f9,6,2);
    Dkc1CoopRememberAnimalBonus(&token);
    CHECK(!s_runtime.animal_bonus,"second token claimed the animal bonus");
    Dkc1CoopEntityWrite16(ram,0x14f9,6,3);
    CpuState untouched=token;Dkc1CoopRememberAnimalBonus(&token);
    CHECK(!memcmp(&untouched,&token,sizeof token) && s_runtime.animal_bonus==collector,
          "third token lost its collector or changed the native ABI");
    Dkc1WramSetU16(ram,0x56f,collector^3u);Dkc1CoopRememberAnimalBonus(&token);
    CHECK(s_runtime.animal_bonus==collector,"later token stole the pending challenge");
    Dkc1CoopSnapshot pending;Dkc1CoopSaveState(&pending);
    CHECK(pending.version==5 && sizeof pending==20,"animal bonus changed the wire size");
    Dkc1WramSetU16(ram,0x1e15,0x402);
    Dkc1Actor_SetSpriteIDLo(ram,6,(uint16_t)animal);
    Dkc1CoopLoadState(&pending,ram);
    CHECK(Dkc1CoopAnimalBonusOwner(ram)==collector,"pending save lost the bonus collector");
    Dkc1CoopBeginFrame(ram,0x81040u);
    UseKong(ram,1);
    CHECK(Dkc1WramU16(ram,DKC1_WRAM_JoypadHeld)==(collector==1?0x200u:0x8100u) &&
          Dkc1WramU16(ram,0x16e9)==(collector==1?0:0x8000u),
          "animal bonus used the other player's held buttons or jump edge");
    Dkc1CoopSnapshot active;Dkc1CoopSaveState(&active);Dkc1CoopLoadState(&active,ram);
    CHECK(Dkc1CoopAnimalBonusOwner(ram)==collector,"active save lost the bonus collector");
    Dkc1WramSetU16(ram,0x1e15,2);Dkc1CoopBeginFrame(ram,0);
    CHECK(!s_runtime.animal_bonus && !Dkc1CoopAnimalBonusOwner(ram),
          "bonus owner leaked into the returned level");
    active.version=4;active.reserved&=0x0fffu;Dkc1CoopLoadState(&active,ram);
    CHECK(!s_runtime.animal_bonus,"old save invented a bonus collector");
  }
  RetiredDualCartState();

  /* DK-barrel creation has already restored the party and initialized its
   * native impulse. Co-op releases only the dry on-foot state9/$23 modal. */
  for(unsigned revived=2;revived<=4;revived+=2) for(unsigned variant=0;variant<10;variant++) {
    Setup();Dkc1WramSetU16(ram,0x32,6);Dkc1WramSetU16(ram,0x1929,9);
    Dkc1Actor_SetRAMTable1029Lo(ram,revived,0x23);
    Dkc1Actor_SetRAMTable145DLo(ram,revived,0x600);
    Dkc1Actor_SetRAMTable13E9Lo(ram,revived,0x600);
    Dkc1Actor_SetSprYVelocity(ram,revived,0xffc9);
    Dkc1Actor_SetSprXVelocity(ram,revived,0x80);
    Dkc1Actor_SetSprXPos(ram,revived,0x7000);
    unsigned other=revived^6u;
    Dkc1Actor_SetSprXVelocity(ram,other,variant==6?0xfe80:0x180);
    Dkc1Actor_SetSprYVelocity(ram,other,variant==6?0xff00:0x200);
    s_runtime.barrel_hold=60;s_runtime.corpse[revived/2-1]=1;
    if(variant==1)Dkc1WramSetU16(ram,0x1929,1);
    if(variant==2)Dkc1Actor_SetRAMTable1029Lo(ram,revived,2);
    if(variant==3)Dkc1WramSetU16(ram,0x32,3);
    if(variant==4)Dkc1WramSetU16(ram,0x32,5);
    if(variant==5)s_runtime.enabled=0;
    if(variant==7){Dkc1Actor_SetRAMTable1029Lo(ram,other,0x0c);s_runtime.corpse[other/2-1]=1;}
    if(variant==8)Dkc1Actor_SetSpriteIDLo(ram,other,0);
    if(variant==9){Dkc1Actor_SetSprXVelocity(ram,other,0);Dkc1Actor_SetSprYVelocity(ram,other,0);}
    Dkc1CoopEndFrame(ram);
    if(!variant || variant>=6) {
      CHECK(Dkc1Actor_RAMTable1029Lo(ram,revived)==1 &&
            Dkc1Actor_SprYVelocity(ram,revived)==0x600 &&
            Dkc1Actor_SprXVelocity(ram,revived)==0x80 &&
            Dkc1Actor_SprXPos(ram,revived)==0x7000 &&
            !Dkc1WramU16(ram,0x1929) && !s_runtime.barrel_hold &&
            !s_runtime.corpse[revived/2-1],
            "barrel pop inherited partner speed or lost native impulse/control");
    } else CHECK(Dkc1Actor_RAMTable1029Lo(ram,revived)==(variant==2?2:0x23),
                 "barrel release changed an unrelated/native-owned state");
  }
  RetiredDualCartState();

  /* The world boss uses the native current-Kong collision scan. Its tick
   * must select the nearby live Kong, while the arena controller stays global. */
  Setup();
  Dkc1Actor_SetSpriteIDLo(ram, 6, 0x70);
  Dkc1Actor_SetSprXPos(ram, 2, 0x200);
  Dkc1Actor_SetSprXPos(ram, 4, 0x150);
  Dkc1Actor_SetSprXPos(ram, 6, 0x150);
  Dkc1WramSetU16(ram, 0x056F, 1);
  Dkc1CoopBeginFrame(ram, 0);
  Dkc1CoopBeforeActor(ram, 6, 0);
  CHECK(Dkc1WramU16(ram, 0x056F) == 2,
        "boss collision ignored nearby Diddy while Donkey was alive");
  CHECK(Dkc1Actor_RAMTable1029Lo(ram, 4) == 0,
        "boss collision hid Diddy in the follower pose");
  Dkc1CoopAfterActor(ram);
  CHECK(Dkc1WramU16(ram, 0x056F) == 1,
        "boss tick leaked its collision target into the leader");
  Dkc1Actor_SetSprXPos(ram, 6, 0x200);
  Dkc1CoopBeforeActor(ram, 6, 0);
  CHECK(Dkc1WramU16(ram, 0x056F) == 1,
        "boss collision ignored nearby Donkey");
  Dkc1CoopAfterActor(ram);
  Dkc1Actor_SetSpriteIDLo(ram, 6, 0x6D);
  Dkc1Actor_SetSprXPos(ram, 6, 0x150);
  Dkc1CoopBeforeActor(ram, 6, 0);
  CHECK(Dkc1WramU16(ram, 0x056F) == 2,
        "prize barrel ignored nearby Diddy");
  Dkc1CoopAfterActor(ram);
  Dkc1Actor_SetSpriteIDLo(ram, 6, 0x6C);
  Dkc1Actor_SetSprXPos(ram, 6, 0x150);
  Dkc1CoopBeforeActor(ram, 6, 0);
  CHECK(Dkc1WramU16(ram, 0x056F) == 1,
        "arena controller borrowed the boss collision target");
  Dkc1CoopAfterActor(ram);
  Dkc1CoopEndFrame(ram);

  /* The ship reward uses the same native current-Kong contact path.
   * Both a nearby partner and a sole Diddy must be selectable at1929=0. */
  for(unsigned sole=0;sole<=1;sole++){
    Setup();
    Dkc1WramSetU16(ram,0x32,0x0d);Dkc1WramSetU16(ram,0x3e,0x68);
    Dkc1WramSetU16(ram,0x4a,0x200);Dkc1WramSetU16(ram,0x1b23,0);
    Dkc1WramSetU16(ram,0x1b25,0);Dkc1WramSetU16(ram,0x1929,0);
    Dkc1Actor_SetSpriteIDLo(ram,6,0x74);
    Dkc1Actor_SetSprXPos(ram,2,0x200);Dkc1Actor_SetSprYPos(ram,2,0x127);
    Dkc1Actor_SetSprXPos(ram,4,0x71);Dkc1Actor_SetSprYPos(ram,4,0x134);
    Dkc1Actor_SetSprXPos(ram,6,0x71);Dkc1Actor_SetSprYPos(ram,6,0x134);
    if(sole)Dkc1Actor_SetRAMTable1029Lo(ram,2,0x53);
    Dkc1WramSetU16(ram,0x056f,1);
    Dkc1CoopBeginFrame(ram,0x80000);
    Dkc1CoopBeforeActor(ram,6,0);
    CHECK(Dkc1WramU16(ram,0x056f)==2,
          "ship giant banana did not select the living Diddy");
    Dkc1CoopAfterActor(ram);Dkc1CoopEndFrame(ram);
  }

  /* A native-registered passenger owns the moving barrel's phase. The
   * secondary passenger enters only through the ROM rectangle, not radius. */
  static uint8_t cannon_rom[0x400000];
  g_rom = cannon_rom;
  cannon_rom[0x3b8008] = 0x00; cannon_rom[0x3b8009] = 0x90;
  cannon_rom[0x3b9000] = 0xf8; cannon_rom[0x3b9001] = 0xff; /* dx=-8 */
  cannon_rom[0x3b9002] = 0xf8; cannon_rom[0x3b9003] = 0xff; /* dy=-8 */
  cannon_rom[0x3b9004] = cannon_rom[0x3b9006] = 16;
  Setup();
  CpuState swap_cpu = {0}; swap_cpu.ram = ram;
  for (uint16_t h=2;h<=4;h+=2) {
    Dkc1Actor_SetSprAnimFrame(ram,h,16);
    Dkc1Actor_SetSprXPos(ram,h,0x8100);
    Dkc1Actor_SetSprYPos(ram,h,0x100);
  }
  for (uint16_t h=2;h<=4;h+=2) {
    Dkc1WramSetU16(ram,0x82,h);
    CHECK(!Dkc1CoopBlockTeamSwap(&swap_cpu),"intersecting Kongs could not swap");
    Dkc1Actor_SetSprXPos(ram,4,0x8110);
    CHECK(!Dkc1CoopBlockTeamSwap(&swap_cpu),"native edge contact could not swap");
    Dkc1Actor_SetSprXPos(ram,4,0x8111);
    CHECK(Dkc1CoopBlockTeamSwap(&swap_cpu),"separated Kongs could swap");
    Dkc1Actor_SetSprXPos(ram,4,0x8100);
    Dkc1Actor_SetSprYPos(ram,4,0x121);
    CHECK(Dkc1CoopBlockTeamSwap(&swap_cpu),"vertically separated Kongs could swap");
    Dkc1Actor_SetSprYPos(ram,4,0x100);
  }
  const uint8_t *saved_rom=g_rom;g_rom=NULL;
  CHECK(Dkc1CoopBlockTeamSwap(&swap_cpu),"unavailable hitbox did not fail closed");
  g_rom=saved_rom;s_runtime.enabled=0;
  CHECK(!Dkc1CoopBlockTeamSwap(&swap_cpu),"co-op-off cartridge swap changed");
  s_runtime.enabled=1;
  Setup();
  for (unsigned h=2; h<=6; h+=2) {
    Dkc1Actor_SetSprXPos(ram,h,0x100); Dkc1Actor_SetSprYPos(ram,h,0x100);
    Dkc1Actor_SetSprAnimFrame(ram,h,16);
  }
  Dkc1Actor_SetSpriteIDLo(ram,6,0x38);
  Dkc1Actor_SetRAMTable1029Lo(ram,6,4);
  Dkc1Actor_SetRAMTable152DLo(ram,6,2);
  Dkc1Actor_SetRAMTable1029Lo(ram,2,0x29);
  Dkc1Actor_SetRAMTable11A1Lo(ram,4,0xc1);
  Dkc1Actor_SetSprXPos(ram,4,0x121);
  PrepareCannon(ram,6); FinishCannon(ram);
  CHECK(Dkc1Actor_RAMTable1029Lo(ram,4)==0,"radius caught Kong outside native hitbox");
  Dkc1Actor_SetSprXPos(ram,4,0x110); /* inclusive touching edges */
  PrepareCannon(ram,6); FinishCannon(ram);
  CHECK(Dkc1Actor_RAMTable1029Lo(ram,4)==0x29 &&
        Dkc1Actor_RAMTable1029Lo(ram,6)==4 &&
        Dkc1Actor_RAMTable152DLo(ram,6)==2,"join reset the barrel's phase or owner");
  CHECK(Dkc1Actor_SprXPos(ram,4)==0x100,"joined passenger not attached to rider");
  CHECK(Dkc1CoopSharedCannonCamera(ram),"verified shared cannon did not share its presentation camera");
  Dkc1Actor_SetRAMTable152DLo(ram,6,4);
  CHECK(Dkc1CoopSharedCannonCamera(ram),"Diddy-owned cannon lost shared camera");
  Dkc1Actor_SetRAMTable152DLo(ram,6,2);
  Dkc1Actor_SetRAMTable1029Lo(ram,6,1);
  CHECK(!Dkc1CoopSharedCannonCamera(ram),"orphaned hidden poses acquired shared camera");
  Dkc1Actor_SetRAMTable1029Lo(ram,6,4);
  Dkc1Actor_SetSprYPos(ram,4,0x101);
  CHECK(!Dkc1CoopSharedCannonCamera(ram),"different passengers acquired shared camera");
  Dkc1Actor_SetSprYPos(ram,4,0x100);
  Dkc1WramSetU16(ram,0x1929,3);
  CHECK(!Dkc1CoopSharedCannonCamera(ram),"shared camera overrode scene lock");
  Dkc1WramSetU16(ram,0x1929,0);s_runtime.enabled=0;
  CHECK(!Dkc1CoopSharedCannonCamera(ram),"stock mode acquired shared camera");
  s_runtime.enabled=1;
  /* $BFE754's cannon lock is global on cartridge. It must not freeze the
   * native primary follower while co-op seats occupy different views. */
  Dkc1Actor_SetRAMTable145DLo(ram,6,0x200);
  Dkc1WramSetU16(ram,0x1929,1);
  PrepareCannon(ram,6); FinishCannon(ram);
  CHECK(!Dkc1WramU16(ram,0x1929),"occupied cannon froze the primary camera");
  Dkc1WramSetU16(ram,0x1929,3);
  PrepareCannon(ram,6); FinishCannon(ram);
  CHECK(Dkc1WramU16(ram,0x1929)==3,"cannon cleared a shared scene-script lock");
  Dkc1Actor_SetRAMTable145DLo(ram,6,0);
  Dkc1WramSetU16(ram,0x1929,1);
  PrepareCannon(ram,6); FinishCannon(ram);
  CHECK(Dkc1WramU16(ram,0x1929)==1,"unflagged cannon cleared another camera lock");
  Dkc1Actor_SetRAMTable145DLo(ram,6,0x200); s_runtime.enabled=0;
  PrepareCannon(ram,6); FinishCannon(ram);
  CHECK(Dkc1WramU16(ram,0x1929)==1,"disabled co-op changed the stock camera lock");
  s_runtime.enabled=1; Dkc1Actor_SetRAMTable145DLo(ram,6,0);
  Dkc1WramSetU16(ram,0x1929,0);
  Dkc1WramSetU16(ram,0x16eb,0x4000);
  PrepareCannon(ram,6);
  CHECK(Dkc1Actor_RAMTable152DLo(ram,6)==4 && s_runtime.actor.cannon.partner==2,
        "second passenger cannot fire the native barrel");
  Dkc1Actor_SetSprXPos(ram,4,0x10b); /* one native moving-barrel update */
  FinishCannon(ram);
  CHECK(Dkc1Actor_SprXPos(ram,2)==0x10b,"moving barrel lost secondary passenger");
  Dkc1WramSetU16(ram,0x16e9,0x8000); Dkc1WramSetU16(ram,0x16eb,0);
  PrepareCannon(ram,6);
  CHECK(Dkc1Actor_RAMTable152DLo(ram,6)==2,"first passenger cannot fire");
  Dkc1Actor_SetRAMTable1029Lo(ram,2,0x28); /* completed native launch */
  Dkc1Actor_SetRAMTable1029Lo(ram,6,1);
  Dkc1Actor_SetSprXVelocity(ram,2,0x500); Dkc1Actor_SetSprYVelocity(ram,2,0x600);
  Dkc1Actor_SetRAMTable1375Lo(ram,2,16); Dkc1Actor_SetRAMTable13E9Lo(ram,2,16);
  Dkc1Actor_SetRAMTable11A1Lo(ram,2,0xc1);
  Dkc1Actor_SetSprFlags(ram,4,0x0c00);
  FinishCannon(ram);
  CHECK(Dkc1Actor_RAMTable1029Lo(ram,4)==0x28 &&
        Dkc1Actor_SprXVelocity(ram,4)==0x500 &&
        Dkc1Actor_RAMTable1375Lo(ram,4)==16 &&
        Dkc1Actor_RAMTable13E9Lo(ram,4)==16,"secondary launch lacks native flight state");
  CHECK(Dkc1Actor_SprAnimID(ram,4)==0x7f &&
        Dkc1Actor_SprAnimScriptPtr(ram,4)==0xb6a1 &&
        Dkc1Actor_SprAnimFrame(ram,4)==0xb54 &&
        Dkc1Actor_SprAnimSpeed(ram,4)==0x200 &&
        Dkc1Actor_SprAnimTimer(ram,4)==0 &&
        Dkc1Actor_SprFlags(ram,4)==0x0c00,"secondary spin or palette not independent");
  Dkc1Actor_SetSprXPos(ram,4,0x140);
  Dkc1CoopEndFrame(ram);
  CHECK(Dkc1Actor_SprXPos(ram,4)==0x140,"flight still glued to other player's position");
  /* Different Kong hitboxes must not split a coincident shared shot when
   * native collision accepts just one passenger at the next barrel edge. */
  CopyCannonFlight(ram,4,2);
  PrepareCannon(ram,6);
  CHECK(s_runtime.actor.cannon.partner==6,"coincident shot not remembered before collision");
  Dkc1Actor_SetRAMTable1029Lo(ram,6,9);
  Dkc1Actor_SetRAMTable1029Lo(ram,2,0x29);
  Dkc1Actor_SetSprXPos(ram,2,0x180);
  Dkc1CoopEntityWrite16(ram,0x1595u,4,1);
  FinishCannon(ram);
  CHECK(Dkc1Actor_RAMTable1029Lo(ram,4)==0x29 &&
        Dkc1Actor_SprXPos(ram,4)==0x180 &&
        !Dkc1CoopEntityRead16(ram,0x1595u,4),"native catch split shared passengers");
  /* Caller-owned transaction must roll back a half-finished barrel update. */
  s_runtime.actor.cannon.object=6; s_runtime.actor.cannon.partner=4;
  CHECK(Dkc1CoopBeginTransaction(),"cannon transaction refused");
  s_runtime.actor.cannon.object=0; s_runtime.actor.cannon.partner=0; Dkc1CoopEndTransaction(0);
  CHECK(s_runtime.actor.cannon.object==6 && s_runtime.actor.cannon.partner==4,"cannon context escaped rollback");

  Dkc1CoopSnapshot cannon_history;
  Dkc1CoopActorSnapshot cannon_wait;
  Dkc1CoopContinuation cannon_continuation;
  s_runtime.actor.mux=1; s_runtime.actor.mux_kong=0; CaptureCanonicalContext(ram);
  Dkc1CoopSuspendActor(ram,0xbfe75b,0x1e0);
  Dkc1CoopSaveState(&cannon_history); Dkc1CoopSaveActorState(&cannon_wait);
  Dkc1CoopSaveContinuation(&cannon_continuation);
  CHECK(cannon_wait.version==4 && cannon_wait.reserved[0]==6 &&
        cannon_wait.reserved[1]==4,"pending barrel missing from actor save");
  Dkc1CoopLoadState(&cannon_history,ram);
  CHECK(Dkc1CoopLoadActorState(&cannon_wait,ram),"pending cannon rejected on load");
  Dkc1CoopLoadContinuation(&cannon_continuation);
  Dkc1CoopResumeActor(ram,0xbfe75b,0x1e0);
  CHECK(s_runtime.actor.cannon.object==6 && s_runtime.actor.cannon.partner==4,"pending cannon lost on resume");
  /* Roundtrip an interrupted partner's camera-fast request through the
   * same save/load ordering used by the game, including canonical WRAM. */
  Setup(); CoopTestSetSecondCamera(1,0x900,0x200);
  Dkc1WramSetU16(ram,0x0579,1);
  Dkc1CoopBeforeActor(ram,4,0);
  Dkc1WramSetU16(ram,0x0579,9);
  Dkc1CoopSuspendActor(ram,0xbf8123,0x1eb);
  CheckContinuation(4);
  CHECK(Dkc1WramU16(ram,0x0579)==1,"saved partner request leaked to primary camera");
  Setup(); g_rom = NULL;
  Dkc1CoopCacheKongSlots(ram, s_runtime.slot);
  Dkc1CoopBeginFrame(ram, 0x002u << 12);
  CHECK(Dkc1WramU16(ram, 0x16eb) == 0x4000 &&
            Dkc1WramU16(ram, 0x16e9) == 0,
        "P2 Y did not reach Diddy's blast-barrel press word");
  Setup();

  /* B6BB27 allocates Necky's nut through the extended normal pool too. */
  for(unsigned nut=6;nut<=0xfe;nut=nut==6?0x80:nut+0x7e) {
    Setup();
    Dkc1Actor_SetSprXPos(ram,4,500);Dkc1Actor_SetSprYPos(ram,4,300);
    Dkc1Actor_SetSpriteIDLo(ram,nut,0x10);
    Dkc1Actor_SetSprCollisionFlags(ram,nut,1);
    Dkc1Actor_SetSprXPos(ram,nut,512);Dkc1Actor_SetSprYPos(ram,nut,310);
    NotePartnerNut(ram,4);
    CHECK(Dkc1Actor_SprEventFlags(ram,4)==1 && Dkc1WramU16(ram,0x16f7)==nut,
          "native or extended Necky nut missed partner collision");
  }

  Setup();
  s_runtime.barrel_hold = 60;
  Dkc1WramSetU16(ram, DKC1_WRAM_Player_CurrentKongLo, 2);
  uint8_t before[sizeof ram];
  memcpy(before, ram, sizeof ram);
  for (unsigned i = 0; i < 10; i++) Dkc1CoopPrepareFrame(NULL, ram);
  CHECK(s_runtime.barrel_hold == 60, "redraw advanced the barrel grace timer without a guest frame");
  CHECK(!memcmp(before, ram, sizeof ram), "redraw changed gameplay WRAM");

  /* Independent controller levels and edges, with no drawing between frames. */
  Setup();
  Dkc1WramSetU16(ram, DKC1_WRAM_JoypadHeld, 0x1234);
  Dkc1WramSetU16(ram, DKC1_WRAM_Global_HeldButtonsLoP2, 0x5678);
  Dkc1CoopBeginFrame(ram, 0x040081); /* DK Right+B, Diddy Left. */
  Dkc1CoopBeforeActor(ram, 2, 0);
  CHECK(Dkc1WramU16(ram, DKC1_WRAM_JoypadHeld) == 0x8100,
        "Donkey did not receive the selected frame input");
  CHECK(Dkc1WramU16(ram, DKC1_WRAM_JoypadPressed) == 0x8100,
        "Donkey press edge was lost");
  Dkc1CoopAfterActor(ram);
  CHECK(Dkc1WramU16(ram, DKC1_WRAM_JoypadHeld) == 0x1234 &&
        Dkc1WramU16(ram, DKC1_WRAM_Global_HeldButtonsLoP2) == 0x5678,
        "actor transaction leaked substituted pad words");
  Dkc1CoopBeforeActor(ram, 4, 0);
  CHECK(Dkc1WramU16(ram, DKC1_WRAM_JoypadHeld) == 0x0200,
        "Diddy received Donkey's controller");
  Dkc1WramSetU16(ram, DKC1_WRAM_CameraX, 0x4321);
  Dkc1CoopAfterActor(ram);
  CHECK(Dkc1WramU16(ram, DKC1_WRAM_CameraX) == 0,
        "non-leader tick moved the camera");
  Dkc1CoopEndFrame(ram);
  Dkc1CoopBeginFrame(ram, 0x040081);
  Dkc1CoopBeforeActor(ram, 4, 0);
  CHECK(Dkc1WramU16(ram, DKC1_WRAM_JoypadPressed) == 0,
        "a held button became a second press without a redraw");
  Dkc1CoopAfterActor(ram);
  Dkc1CoopEndFrame(ram);

  Setup();
  Dkc1WramSetU16(ram,0x32,9);
  Dkc1WramSetU16(ram,DKC1_WRAM_CameraY,0x3a0);
  CoopTestSetSecondCamera(1,0x400,0x340);
  Dkc1CoopBeginFrame(ram,0);
  Dkc1CoopBeforeActor(ram,4,0);
  CHECK(Dkc1CoopRopeClimbCameraY(ram,0x340)==0x340,
        "vertical native rope climb used the other Kong's camera ceiling");
  Dkc1CoopAfterActor(ram);
  CHECK(Dkc1WramU16(ram,DKC1_WRAM_CameraY)==0x3a0,
        "vertical rope ceiling changed the primary camera");

  for (unsigned opener=2;opener<=4;opener+=2) {
    Setup();
    CpuState birth={0};birth.ram=ram;birth.D=0x0d45;
    Dkc1Actor_SetSpriteIDLo(ram,6,0x18);
    Dkc1Actor_SetRAMTable1375Lo(ram,6,opener);
    Dkc1Actor_SetSpriteIDLo(ram,10,0x0d);
    Dkc1WramSetU16(ram,0x82,6);
    Dkc1Actor_SetRAMTable1375Lo(ram,6,4); /* Not a Kong ownership proof. */
    Dkc1WramSetU16(ram,0x56f,opener/2);
    Dkc1CoopBeginFrame(ram,0);
    Dkc1CoopCrateHit(ram,6);
    Dkc1CoopSnapshot pending_box;Dkc1CoopSaveState(&pending_box);
    Dkc1CoopLoadState(&pending_box,ram);
    Dkc1WramSetU16(ram,0x56f,opener==2?2:1);
    Dkc1CoopCrateHit(ram,6); /* Later touches cannot steal the first hit. */

    Dkc1CoopActorCreated(&birth,10);
    unsigned id=opener/2;
    CHECK(SquawksOwner(ram,10)==id,"Squawks inherited ambient Kong instead of crate opener");
    Dkc1CoopCrateHit(ram,6);
    CHECK(SquawksOwner(ram,10)==id,"another crate touch stole the live companion");
    Dkc1CoopBeforeActor(ram,10,0);
    CHECK(Dkc1WramU16(ram,0x56f)==id,"Squawks tick did not retain the creator");
    Dkc1CoopAfterActor(ram);
    Dkc1CoopSnapshot companion;Dkc1CoopSaveState(&companion);
    CHECK(companion.version==5 && sizeof companion==20,"companion save changed legacy wire size");
    Dkc1CoopLoadState(&companion,ram);
    CHECK(SquawksOwner(ram,10)==id,"loading lost Squawks creator");
    Dkc1Actor_SetRAMTable1029Lo(ram,opener,0xd);
    CHECK(SquawksOwner(ram,10)==(id^3),"Squawks did not select surviving Kong");
    Dkc1CoopActorAllocated(8);
    CHECK(s_runtime.squawks!=0,"unrelated allocation cleared companion owner");
    Dkc1CoopActorAllocated(10);
    CHECK(s_runtime.squawks==0,"reused actor handle retained an old companion owner");
    companion.version=3;companion.reserved=0;
    Dkc1CoopLoadState(&companion,ram);
    CHECK(!s_runtime.squawks,"old state invented an unrecorded companion owner");
  }

  /* A non-leader runs screen-relative code with its own camera, while native
   * camera mirrors and lookahead remain owned by the leader after the tick. */
  Setup();
  Dkc1WramSetU16(ram, DKC1_WRAM_CameraX, 0x100);
  Dkc1WramSetU16(ram, DKC1_WRAM_CameraY, 0x80);
  Dkc1WramSetU16(ram, DKC1_WRAM_Level_CameraXPosLo, 0x101);
  Dkc1WramSetU16(ram, DKC1_WRAM_Level_CameraYPosLo, 0x81);
  Dkc1WramSetU16(ram, 0x1a69, 0x222);
  Dkc1WramSetU16(ram, 0x0579, 1);
  CoopTestSetSecondCamera(1, 0x900, 0x200);
  Dkc1CoopBeginFrame(ram, 0);
  Dkc1CoopBeforeActor(ram, 4, 0);
  CHECK(CoopTestCameraCalls() == 1 &&
        Dkc1WramU16(ram, DKC1_WRAM_CameraX) == 0x900 &&
        Dkc1WramU16(ram, DKC1_WRAM_CameraY) == 0x200,
        "Diddy did not receive the second camera during his tick");
  uint16_t primary_x, primary_y;
  Dkc1CoopPrimaryCamera(ram, &primary_x, &primary_y);
  CHECK(primary_x == 0x100 && primary_y == 0x80,
        "primary-camera queries observed the substituted second camera");
  CHECK(Dkc1CoopRopeClimbCameraY(ram,0x200)==0x80,
        "private rope camera lifted the gameplay ceiling");
  CHECK(Dkc1WramU16(ram,DKC1_WRAM_CameraY)==0x200,
        "rope ceiling query mutated the private camera");
  s_runtime.enabled=0;
  CHECK(Dkc1CoopRopeClimbCameraY(ram,0x200)==0x200,
        "disabled co-op changed native rope camera operand");
  s_runtime.enabled=1;
  Dkc1WramSetU16(ram, DKC1_WRAM_CameraX, 0x901);
  Dkc1WramSetU16(ram, DKC1_WRAM_CameraY, 0x201);
  Dkc1WramSetU16(ram, DKC1_WRAM_Level_CameraXPosLo, 0x902);
  Dkc1WramSetU16(ram, DKC1_WRAM_Level_CameraYPosLo, 0x202);
  Dkc1WramSetU16(ram, 0x1a69, 0x333);
  Dkc1WramSetU16(ram, 0x0579, 0xa9); /* partner landing and horizontal lead requests */
  Dkc1CoopAfterActor(ram);
  CHECK(Dkc1WramU16(ram, DKC1_WRAM_CameraX) == 0x100 &&
        Dkc1WramU16(ram, DKC1_WRAM_CameraY) == 0x80 &&
        Dkc1WramU16(ram, DKC1_WRAM_Level_CameraXPosLo) == 0x101 &&
        Dkc1WramU16(ram, DKC1_WRAM_Level_CameraYPosLo) == 0x81 &&
        Dkc1WramU16(ram, 0x1a69) == 0x222,
        "non-leader camera mirrors or lookahead leaked into the leader");
  CHECK(Dkc1WramU16(ram, 0x0579)==0x21,
        "partner landing leaked camera-fast bit or lost shared gameplay flags");
  Dkc1CoopBeforeActor(ram, 2, 0);
  CHECK(CoopTestCameraCalls() == 1,
        "leader tick requested the second camera");
  Dkc1WramSetU16(ram, DKC1_WRAM_CameraX, 0x120);
  Dkc1WramSetU16(ram, 0x1a69, 0x444);
  Dkc1CoopAfterActor(ram);
  CHECK(Dkc1WramU16(ram, DKC1_WRAM_CameraX) == 0x120 &&
        Dkc1WramU16(ram, 0x1a69) == 0x444,
        "leader camera motion or lookahead was rolled back");
  Dkc1CoopEndFrame(ram);

  /* A deadline can bisect a Kong tick. Host rendering and NMI see canonical
   * WRAM; the exact suspended input/camera returns only at the matching PC/S. */
  Setup();
  CoopTestSetSecondCamera(1, 0x900, 0x200);
  Dkc1WramSetU16(ram, DKC1_WRAM_CameraX, 0x100);
  Dkc1WramSetU16(ram, DKC1_WRAM_JoypadHeld, 0x1234);
  Dkc1WramSetU16(ram, 0x0579, 1);
  Dkc1CoopBeginFrame(ram, 0x040081);
  Dkc1CoopBeforeActor(ram, 4, 0);
  Dkc1WramSetU16(ram, DKC1_WRAM_CameraX, 0x901);
  Dkc1WramSetU16(ram, 0x0579, 0x89);
  Dkc1CoopSuspendActor(ram, 0xbf8123, 0x1eb);
  CHECK(Dkc1WramU16(ram,0x0579)==1,"deadline leaked partner camera-fast bit");
  CHECK(Dkc1CoopActorPending() && s_runtime.actor.mux && !s_runtime.actor.camera.active &&
        Dkc1WramU16(ram, DKC1_WRAM_JoypadHeld) == 0x1234 &&
        Dkc1WramU16(ram, DKC1_WRAM_CameraX) == 0x100,
        "deadline leaked the secondary camera or controller into presentation");
  Dkc1CoopEndFrame(ram);
  Dkc1CoopActorSnapshot actor_saved, actor_roundtrip;
  Dkc1CoopSaveActorState(&actor_saved);
  Dkc1CoopBeginFrame(ram, 0x080000); /* A new host level must not alter half a tick. */
  Dkc1WramSetU16(ram, DKC1_WRAM_JoypadHeld, 0x5678); /* NMI reads fresh pads. */
  Dkc1WramSetU16(ram, DKC1_WRAM_CameraX, 0x120);
  Dkc1CoopResumeActor(ram, 0xbf8123, 0x1e7);
  Dkc1CoopResumeActor(ram, 0xbf8124, 0x1eb);
  Dkc1CoopAfterActor(ram); /* An unrelated callback cannot complete the wait. */
  Dkc1CoopSuspendActor(ram, 0x80a970, 0x1e7); /* Another NMI-only deadline. */
  Dkc1CoopSaveActorState(&actor_roundtrip);
  CHECK(!memcmp(&actor_saved, &actor_roundtrip, sizeof actor_saved) &&
        Dkc1WramU16(ram, DKC1_WRAM_JoypadHeld) == 0x5678,
        "NMI consumed or replaced the suspended actor continuation");
  Dkc1CoopResumeActor(ram, 0xbf8123, 0x1eb);
  CHECK(Dkc1WramU16(ram,0x0579)==0x89,"resume lost pending camera-fast bit");
  CHECK(!Dkc1CoopActorPending() && s_runtime.actor.mux &&
        Dkc1WramU16(ram, DKC1_WRAM_JoypadHeld) == 0x0200 &&
        Dkc1WramU16(ram, DKC1_WRAM_JoypadPressed) == 0x0200 &&
        Dkc1WramU16(ram, DKC1_WRAM_CameraX) == 0x901,
        "actor continuation did not recover its original input/camera");
  Dkc1CoopAfterActor(ram);
  CHECK(Dkc1WramU16(ram,0x0579)==1,"completion leaked partner camera-fast bit");
  CHECK(!s_runtime.actor.mux && Dkc1WramU16(ram, DKC1_WRAM_JoypadHeld) == 0x5678 &&
        Dkc1WramU16(ram, DKC1_WRAM_CameraX) == 0x120,
        "completed continuation overwrote fresh NMI input or primary camera");
  Dkc1CoopBeforeActor(ram, 4, 0);
  CHECK(Dkc1WramU16(ram, DKC1_WRAM_JoypadHeld) == 0x0100,
        "next actor did not receive the latest frame input");
  Dkc1CoopAfterActor(ram);

  /* Save/load includes an unfinished script's follower substitution, while
   * the saved WRAM and any number of paused redraws keep both Kongs normal. */
  Setup();
  Dkc1Actor_SetSpriteIDLo(ram, 6, 0x07);
  Dkc1CoopBeginFrame(ram, 0x040081);
  Dkc1CoopBeforeActor(ram, 6, 0);
  Dkc1CoopSuspendActor(ram, 0xb58222, 0x1dd);
  CHECK(!s_runtime.actor.follower.active && Dkc1Actor_RAMTable1029Lo(ram, 4) == 0 &&
        Dkc1Actor_SprCollisionFlags(ram, 4) == 0x101,
        "suspended script leaked its fake follower state");
  Dkc1CoopSaveActorState(&actor_saved);
  Dkc1CoopSnapshot wait_history;
  Dkc1CoopSaveState(&wait_history);
  Dkc1CoopLoadState(&wait_history, ram);
  CHECK(Dkc1CoopLoadActorState(&actor_saved, ram),
        "valid suspended actor state did not load");
  Dkc1CoopSaveActorState(&actor_roundtrip);
  CHECK(!memcmp(&actor_saved, &actor_roundtrip, sizeof actor_saved),
        "suspended actor state did not round-trip");
  s_runtime.enabled = 0; /* A paused mode toggle must still clean up its pending tick. */
  Dkc1CoopBeginFrame(ram, 0);
  Dkc1CoopResumeActor(ram, 0xb58222, 0x1dd);
  CHECK(s_runtime.actor.follower.active && Dkc1Actor_RAMTable1029Lo(ram, 4) == 5 &&
        Dkc1Actor_SprCollisionFlags(ram, 4) == 0x111,
        "script continuation lost its follower substitution");
  Dkc1CoopAfterActor(ram);
  CHECK(!s_runtime.actor.mux && !s_runtime.actor.follower.active && !s_runtime.enabled &&
        Dkc1Actor_RAMTable1029Lo(ram, 4) == 0 &&
        Dkc1Actor_SprCollisionFlags(ram, 4) == 0x101,
        "mode toggle discarded pending actor cleanup");

  Setup();
  CoopTestSetSecondCamera(1, 0x900, 0x200);
  Dkc1CoopBeginFrame(ram, 0);
  Dkc1CoopBeforeActor(ram, 4, 0);
  Dkc1CoopSuspendActor(ram, 0xbf8123, 0x1eb);
  Dkc1WramSetU16(ram, 0x3e, 1);
  Dkc1WramSetU16(ram, DKC1_WRAM_CameraX, 0x777);
  Dkc1CoopResumeActor(ram, 0xbf8123, 0x1eb);
  CHECK(!Dkc1CoopActorPending() && !s_runtime.actor.mux &&
        Dkc1WramU16(ram, DKC1_WRAM_CameraX) == 0x777,
        "scene transition resumed obsolete camera scratch");

  /* The exit never returns to the suspended PC. The next frame still has
   * to install the other Kong's pad. */
  Setup();
  Dkc1CoopBeginFrame(ram, 0x040081);
  Dkc1CoopBeforeActor(ram, 4, 0);
  Dkc1CoopSuspendActor(ram, 0xbf8123, 0x1eb);
  Dkc1WramSetU16(ram, 0x3e, 8);
  Dkc1CoopBeginFrame(ram, 0x040081);
  CHECK(!Dkc1CoopActorPending() && !s_runtime.actor.mux,
        "level change left the suspended actor blocking both pads");
  Dkc1CoopBeforeActor(ram, 4, 0);
  CHECK(s_runtime.actor.mux && Dkc1WramU16(ram, DKC1_WRAM_JoypadHeld) == 0x0200,
        "Diddy did not receive his pad after the level change");
  Dkc1CoopAfterActor(ram);

  /* The same level can abandon the suspended PC. One missed frame is the
   * continuation; the frame after that must install the pad again. */
  Setup();
  Dkc1CoopBeginFrame(ram, 0x040081);
  Dkc1CoopBeforeActor(ram, 4, 0);
  Dkc1CoopSuspendActor(ram, 0xbf8123, 0x1eb);
  Dkc1CoopEndFrame(ram);
  Dkc1CoopBeginFrame(ram, 0x040081);
  CHECK(Dkc1CoopActorPending(),
        "the continuation frame dropped a same-scene actor wait");
  Dkc1CoopBeginFrame(ram, 0x040081);
  CHECK(!Dkc1CoopActorPending(),
        "a missed continuation left both pads blocked");
  Dkc1CoopBeforeActor(ram, 4, 0);
  CHECK(s_runtime.actor.mux && Dkc1WramU16(ram, DKC1_WRAM_JoypadHeld) == 0x0200,
        "Diddy did not receive his pad after the missed continuation");
  Dkc1CoopAfterActor(ram);

  /* Exit/entry scripts need a fixed screen edge to complete. */
  Setup();
  CoopTestSetSecondCamera(1, 0x900, 0x200);
  Dkc1Actor_SetRAMTable1029Lo(ram, 4, 0x1d);
  Dkc1WramSetU16(ram, DKC1_WRAM_CameraX, 0x100);
  Dkc1CoopBeginFrame(ram, 0);
  Dkc1CoopBeforeActor(ram, 4, 0);
  CHECK(CoopTestCameraCalls() == 0 &&
        Dkc1WramU16(ram, DKC1_WRAM_CameraX) == 0x100,
        "scripted fixed edge followed the second player");
  Dkc1CoopAfterActor(ram);
  Dkc1CoopEndFrame(ram);

  /* A scene transition owns its new camera; old actor scratch must not undo it. */
  Setup();
  CoopTestSetSecondCamera(1, 0x900, 0x200);
  Dkc1CoopBeginFrame(ram, 0);
  Dkc1CoopBeforeActor(ram, 4, 0);
  Dkc1WramSetU16(ram, 0x3e, 1);
  Dkc1WramSetU16(ram, DKC1_WRAM_CameraX, 0x1234);
  Dkc1WramSetU16(ram, DKC1_WRAM_CameraY, 0x234);
  Dkc1WramSetU16(ram, 0x1a69, 0x555);
  Dkc1WramSetU16(ram, DKC1_WRAM_Player_CurrentKongLo, 2);
  Dkc1CoopPrimaryCamera(ram, &primary_x, &primary_y);
  CHECK(primary_x == 0x1234 && primary_y == 0x234,
        "scene change exposed the previous camera snapshot");
  Dkc1CoopAfterActor(ram);
  CHECK(Dkc1WramU16(ram, DKC1_WRAM_CameraX) == 0x1234 &&
        Dkc1WramU16(ram, DKC1_WRAM_CameraY) == 0x234 &&
        Dkc1WramU16(ram, 0x1a69) == 0x555,
        "scene change restored stale camera state");
  CHECK(Dkc1WramU16(ram, DKC1_WRAM_Player_CurrentKongLo) == 2,
        "scene change restored the previous Kong selector");
  Dkc1CoopEndFrame(ram);

  /* The map can keep the old mode/entrance while it clears the player pool.
   * A suspended actor's old selector must not replace the final death owner. */
  Setup(); Dkc1CoopBeginFrame(ram, 0);
  Dkc1CoopBeforeActor(ram, 4, 0);
  Dkc1WramSetU16(ram, 0x1b23, 0);
  Dkc1WramSetU16(ram, 0x1b25, 0);
  Dkc1Actor_SetSpriteIDLo(ram, 2, 0);
  Dkc1Actor_SetSpriteIDLo(ram, 4, 0);
  Dkc1WramSetU16(ram, DKC1_WRAM_Player_CurrentKongLo, 2);
  Dkc1CoopAfterActor(ram);
  CHECK(Dkc1WramU16(ram, DKC1_WRAM_Player_CurrentKongLo) == 2,
        "cleared map pool restored entry-time Donkey after Diddy died last");

  /* Limits can remain live until after the native actor pool clear. */
  for (uint16_t last = 1; last <= 2; ++last) {
    Setup(); Dkc1CoopBeginFrame(ram, 0);
    Dkc1CoopBeforeActor(ram, 4, 0);
    Dkc1Actor_SetSpriteIDLo(ram, 2, 0);
    Dkc1Actor_SetSpriteIDLo(ram, 4, 0);
    Dkc1WramSetU16(ram, DKC1_WRAM_Player_CurrentKongLo, last);
    Dkc1CoopAfterActor(ram);
    CHECK(Dkc1WramU16(ram, DKC1_WRAM_Player_CurrentKongLo) == last,
          "empty pool used stale selector fallback while camera bounds remained live");
  }
  for (uint16_t entrant = 1; entrant <= 2; ++entrant) {
    Setup();
    Dkc1Actor_SetRAMTable1029Lo(ram, 2, 0x0c);
    Dkc1Actor_SetRAMTable1029Lo(ram, 4, 0x0c);
    Dkc1WramSetU16(ram, DKC1_WRAM_Player_CurrentKongLo, entrant);
    CHECK(LeaderKong(ram) == entrant,
          "two parked entrance actors replaced the native selected Kong");
  }
  /* Map avatar selection changes animation identity, never its controller. */
  for (uint16_t selected = 1; selected <= 2; ++selected) {
    Setup(); CpuState map = {0}; map.ram = ram;
    Dkc1WramSetU16(ram, 0x1b23, 0);
    Dkc1WramSetU16(ram, 0x1b25, 0);
    Dkc1Actor_SetSpriteIDLo(ram, 0, 0x10);
    Dkc1WramSetU16(ram, 0x56f, selected);
    for (uint16_t animation = 0; animation <= 0x22; animation += 2) {
      Dkc1WramSetU16(ram, 0x1105, animation);
      memcpy(before, ram, sizeof ram);
      Dkc1CoopSelectMapAvatar(&map);
      Dkc1WramSetU16(before, 0x1105,
          (uint16_t)((animation & ~2u) | (selected == 1 ? 2u : 0u)));
      CHECK(!memcmp(before, ram, sizeof ram), "map avatar changed state beyond animation identity");
    }
    Dkc1WramSetU16(ram, 0x1105, 0x46);
    Dkc1CoopSelectMapAvatar(&map);
    CHECK(Dkc1WramU16(ram, 0x1105) == 0x46, "map avatar replaced native special animation");
    Dkc1WramSetU16(ram, 0x1105, 0x1c);
    s_runtime.enabled = 0; Dkc1CoopSelectMapAvatar(&map);
    CHECK(Dkc1WramU16(ram, 0x1105) == 0x1c, "disabled co-op changed map avatar");
  }

  /* Map ownership follows selected Kong and native 0579 bit0 partner presence.
   * Only the effective pad changes; physical pads and CPU flags stay intact. */
  for (uint16_t selected=1; selected<=2; ++selected) {
    for (unsigned both=0; both<=1; ++both) {
      Setup(); CpuState map={0}; map.ram=ram; map.P=0x81; map._flag_C=1;
      Dkc1WramSetU16(ram,0x1b23,0); Dkc1WramSetU16(ram,0x1b25,0);
      Dkc1Actor_SetSpriteIDLo(ram,0,0x10);
      Dkc1WramSetU16(ram,0x56f,selected); Dkc1WramSetU16(ram,0x579,both|0x200);
      Dkc1WramSetU16(ram,0x1105,0x46); /* Input also works in special map poses. */
      Dkc1WramSetU16(ram,0x500,0x8100); Dkc1WramSetU16(ram,0x502,0x4800);
      Dkc1WramSetU16(ram,0x504,0x8000); Dkc1WramSetU16(ram,0x506,0x0800);
      memcpy(before,ram,sizeof ram);
      Dkc1WramSetU16(before,0x50e,both?0xc900:selected==1?0x8100:0x4800);
      Dkc1WramSetU16(before,0x510,both?0x8800:selected==1?0x8000:0x0800);
      CpuState saved=map;
      Dkc1CoopPublishMapPads(&map);
      CHECK(!memcmp(before,ram,sizeof ram),"map input ownership or write scope incorrect");
      CHECK(!memcmp(&saved,&map,sizeof map),"map input changed CPU state");
      if (both) {
        Dkc1WramSetU16(ram,0x500,0x8000);Dkc1WramSetU16(ram,0x502,0x8000);
        Dkc1WramSetU16(ram,0x504,0);Dkc1WramSetU16(ram,0x506,0x8000);
        Dkc1CoopPublishMapPads(&map);
        CHECK(Dkc1WramU16(ram,0x510)==0x8000,
              "held first pad suppressed second player's new map press");
      }
      s_runtime.enabled=0;memcpy(before,ram,sizeof ram);
      Dkc1CoopPublishMapPads(&map);
      CHECK(!memcmp(before,ram,sizeof ram),"disabled co-op rerouted map pad");
    }
  }

  Setup(); {
    CpuState map={0};map.ram=ram;
    Dkc1Actor_SetSpriteIDLo(ram,0,0x10);
    memcpy(before,ram,sizeof ram);Dkc1CoopPublishMapPads(&map);
    CHECK(!memcmp(before,ram,sizeof ram),"map pad routing reached gameplay bounds");
    Dkc1WramSetU16(ram,0x1b23,0);Dkc1WramSetU16(ram,0x1b25,0);
    Dkc1WramSetU16(ram,0x56f,0);memcpy(before,ram,sizeof ram);
    Dkc1CoopPublishMapPads(&map);
    CHECK(!memcmp(before,ram,sizeof ram),"unknown map selection was rerouted");
  }

  /* Script compatibility is scoped to one actor, with neither ID hidden. */
  Setup();
  Dkc1Actor_SetSpriteIDLo(ram, 6, 0x07);
  Dkc1CoopBeginFrame(ram, 0);
  Dkc1CoopBeforeActor(ram, 6, 0);
  CHECK(Dkc1Actor_RAMTable1029Lo(ram, 4) == 5 &&
        Dkc1Actor_SpriteIDLo(ram, 4) == 2, "script follower context was lost");
  Dkc1CoopAfterActor(ram);
  CHECK(Dkc1Actor_RAMTable1029Lo(ram, 4) == 0 &&
        Dkc1Actor_SprCollisionFlags(ram, 4) == 0x101,
        "follower context leaked into the next actor");
  Dkc1CoopBeforeActor(ram, 4, 0);
  CHECK(Dkc1Actor_SpriteIDLo(ram, 4) == 2, "Diddy was hidden before his tick");
  Dkc1CoopAfterActor(ram);
  Dkc1CoopEndFrame(ram);

  Setup();
  Dkc1Actor_SetRAMTable1029Lo(ram, 4, 0x0C);
  Dkc1WramSetU16(ram, 0x0512, 6);
  Dkc1CoopBeginFrame(ram, 0);
  Dkc1CoopBeforeActor(ram, 2, 0);
  Dkc1CoopAfterActor(ram);
  CHECK(Dkc1WramU16(ram, 0x0512) == 6, "barrel handshake was overwritten");
  Dkc1CoopEndFrame(ram);
  CHECK(s_runtime.barrel_hold == 60, "barrel grace was not armed at the guest boundary");
  Dkc1Actor_SetRAMTable1029Lo(ram, 4, 0);
  Dkc1CoopBeginFrame(ram, 0);
  Dkc1CoopEndFrame(ram);
  CHECK(s_runtime.barrel_hold == 59, "barrel grace did not advance once per guest frame");

  /* Independent death preserves the survivor's complete actor state and
   * carried-object links; only native health/handoff bookkeeping changes. */
  for (uint16_t hit = 2; hit <= 4; hit += 2) {
    Setup();
    uint16_t survivor = hit ^ 6u;
    CpuState damage_cpu;
    memset(&damage_cpu, 0, sizeof damage_cpu);
    damage_cpu.ram = ram;
    damage_cpu.X = hit;
    Dkc1Actor_SetRAMTable1029Lo(ram, hit, 0x0d);
    Dkc1Actor_SetRAMTable1029Lo(ram, survivor, 0x18); /* Carrying a barrel. */
    Dkc1Actor_SetSprXVelocity(ram, survivor, 0x280);
    Dkc1Actor_SetSprYVelocity(ram, survivor, 0xff80);
    Dkc1Actor_SetRAMTable1375Lo(ram, survivor, 0x1234);
    Dkc1Actor_SetRAMTable11A1Lo(ram, survivor, 0x4321);
    Dkc1WramSetU16(ram, 0x16f5u + survivor - 2u, 6);
    Dkc1WramSetU16(ram, 0x0579, 0x0201);
    Dkc1WramSetU16(ram, 0x1929, 3);
    Dkc1WramSetU16(ram, 0x1699u + survivor - 2u, 0x123f);
    memcpy(before, ram, sizeof ram);
    Dkc1WramSetU16(before, 0x0579, 0x0200);
    Dkc1WramSetU16(before, 0x1929, 0);
    Dkc1WramSetU16(before, 0x1699u + survivor - 2u, 0x1237);
    Dkc1WramSetU16(before, DKC1_WRAM_Player_CurrentKongLo,
                    Dkc1Actor_SpriteIDLo(ram, survivor));
    CHECK(Dkc1CoopIndependentDamage(&damage_cpu) &&
          !memcmp(before, ram, sizeof ram),
          "partner damage changed survivor pose/velocity/carry state or missed handoff");
    CHECK(s_runtime.lock == Dkc1Actor_SpriteIDLo(ram, survivor),
          "independent damage did not select the living camera owner");
    Dkc1Actor_SetRAMTable1029Lo(ram, survivor, 0x0d);
    CHECK(!Dkc1CoopIndependentDamage(&damage_cpu),
          "second death incorrectly retained a survivor");
    Dkc1Actor_SetRAMTable1029Lo(ram, survivor, 0x23);
    CHECK(!Dkc1CoopIndependentDamage(&damage_cpu),
          "DK-barrel revival was intercepted as independent damage");
    Dkc1Actor_SetRAMTable1029Lo(ram, survivor, 0x14);
    CHECK(!Dkc1CoopIndependentDamage(&damage_cpu),
          "animal transition was intercepted as independent damage");
    Dkc1Actor_SetRAMTable1029Lo(ram, survivor, 0);
    s_runtime.enabled = 0;
    CHECK(!Dkc1CoopIndependentDamage(&damage_cpu),
          "disabled co-op replaced native death behavior");
  }

  /* Save/load and rewind must restore history, not retain the later branch. */
  Setup();
  Dkc1CoopBeginFrame(ram, 0x040081);
  Dkc1Actor_SetRAMTable1029Lo(ram, 2, 0x0D);
  Dkc1CoopEndFrame(ram);
  CHECK(s_runtime.lock == 2, "death handoff did not select the surviving Kong");
  Dkc1Actor_SetRAMTable1029Lo(ram, 2, 0x0C);
  Dkc1Actor_SetSprXVelocity(ram, 2, 0);
  Dkc1CoopEndFrame(ram);
  CHECK(s_runtime.lock == 2, "slow death-run was treated as a DK barrel");
  Dkc1Actor_SetRAMTable1029Lo(ram, 4, 0x1E);
  Dkc1CoopEndFrame(ram);
  CHECK(Dkc1Actor_SpriteIDLo(ram, 2) == 0 &&
            Dkc1Actor_RAMTable1029Lo(ram, 2) == 0,
        "level exit kept the dead Kong for the next entrance");
  Dkc1Actor_SetSpriteIDLo(ram, 2, 1);
  Dkc1Actor_SetRAMTable1029Lo(ram, 4, 0);
  Dkc1Actor_SetRAMTable1029Lo(ram, 2, 0);
  Dkc1CoopEndFrame(ram);
  CHECK(!s_runtime.lock && Dkc1WramU16(ram, DKC1_WRAM_Player_CurrentKongLo) == 1,
        "the upper camera stayed on Diddy after Donkey returned");
  Dkc1Actor_SetRAMTable1029Lo(ram, 2, 0x0D);
  Dkc1CoopEndFrame(ram);
  CHECK(s_runtime.lock == 2, "restoring the fall did not pin Diddy again");
  s_runtime.barrel_hold = 17;
  s_runtime.pose[0] = (CoopPoseRecovery){0x123, 43, 1};
  s_runtime.pose[1] = (CoopPoseRecovery){0x321, 21, 1};
  Dkc1CoopSnapshot saved, restored;
  Dkc1CoopSaveState(&saved);
  s_runtime.lock = 1;
  s_runtime.barrel_hold = 1;
  memset(s_runtime.pose, 0, sizeof s_runtime.pose);
  Dkc1CoopBeginFrame(ram, 0);
  Dkc1CoopLoadState(&saved, ram);
  Dkc1CoopSaveState(&restored);
  CHECK(!memcmp(&saved, &restored, sizeof saved) && s_runtime.pose[0].active && s_runtime.pose[1].active,
        "co-op history or pose owner did not round-trip");
  Dkc1CoopBeginFrame(ram, 0x040081);
  Dkc1CoopBeforeActor(ram, 4, 0);
  CHECK(Dkc1WramU16(ram, DKC1_WRAM_JoypadPressed) == 0,
        "load manufactured a press for an already held button");
  Dkc1CoopAfterActor(ram);
  Dkc1CoopEndFrame(ram);
  CHECK(s_runtime.barrel_hold == 16 && s_runtime.lock == 2, "loaded timer or survivor lock diverged");

  Dkc1WramSetU16(ram, DKC1_WRAM_JoypadHeld, 0x8100);
  Dkc1WramSetU16(ram, DKC1_WRAM_Global_HeldButtonsLoP2, 0x0200);
  Dkc1CoopBeginFrame(ram, 0xFFF000); /* unrelated input on the discarded branch */
  Dkc1CoopLoadState(NULL, ram);
  CHECK(!s_runtime.lock && !s_runtime.barrel_hold && !s_runtime.pose[0].ticks && !s_runtime.pose[1].ticks, "legacy save inherited future history");
  Dkc1CoopBeginFrame(ram, 0x040081);
  CHECK(!s_runtime.pressed[0] && !s_runtime.pressed[1], "legacy save manufactured button edges");
  saved.barrel_hold = 0xFFFF;
  Dkc1CoopLoadState(&saved, ram);
  CHECK(!s_runtime.lock && !s_runtime.barrel_hold, "malformed co-op state was partially restored");
  saved.barrel_hold = 17;
  s_runtime.enabled = 0;
  Dkc1CoopLoadState(&saved, ram);
  CHECK(!s_runtime.enabled && !s_runtime.lock, "loading a save enabled the opt-in mod");
  memcpy(before, ram, sizeof ram);
  Dkc1CoopBeginFrame(ram, 0x040081);
  Dkc1CoopBeforeActor(ram, 2, 0);
  Dkc1CoopAfterActor(ram);
  Dkc1CoopEndFrame(ram);
  Dkc1CoopPrepareFrame(NULL, ram);
  CHECK(!memcmp(before, ram, sizeof ram), "disabled co-op changed WRAM");
  /* Wheel activation uses the same authored predicate for both players. */
  for (uint16_t first = 2; first <= 4; first += 2) {
    Setup(); uint16_t other = first ^ 6u;
    Dkc1Actor_SetSpriteIDLo(ram, 8, 0x71);
    Dkc1Actor_SetRAMTable1029Lo(ram, 8, 1);
    Dkc1CoopEntityWrite16(ram, 0x13e9, 8, 0x3880);
    Dkc1Actor_SetSprXPos(ram, first, 0x3880);
    Dkc1Actor_SetSprXPos(ram, other, 0x387f);
    CpuState wheel = {0}; wheel.ram = ram; wheel.X = 8;
    wheel._flag_C = 1; wheel.P = 1;
    memcpy(before, ram, sizeof ram);
    CHECK(Dkc1CoopBlockWheelDeparture(&wheel) && !wheel._flag_C && !(wheel.P & 1),
          "wheel departed before the other living Kong crossed its authored trigger");
    CHECK(!memcmp(before, ram, sizeof ram), "wheel waiting gate changed cartridge state");
    Dkc1Actor_SetSprXPos(ram, other, 0x3880);
    wheel._flag_C = 1; wheel.P |= 1;
    CHECK(!Dkc1CoopBlockWheelDeparture(&wheel) && wheel._flag_C && (wheel.P & 1),
          "both eligible Kongs did not retain native wheel departure carry");
    Dkc1Actor_SetSprXPos(ram, other, 0x3700);
    Dkc1Actor_SetRAMTable1029Lo(ram, other, 0x0d);
    CHECK(!Dkc1CoopBlockWheelDeparture(&wheel), "dead partner prevented wheel departure");
    Dkc1Actor_SetRAMTable1029Lo(ram, other, 0x3b);
    CHECK(!Dkc1CoopBlockWheelDeparture(&wheel), "final-fall partner prevented wheel departure");
    Dkc1Actor_SetSpriteIDLo(ram, other, 0);
    CHECK(!Dkc1CoopBlockWheelDeparture(&wheel), "missing cached partner prevented wheel departure");
    Dkc1Actor_SetSpriteIDLo(ram, other, other == 2 ? 1 : 2);
    Dkc1Actor_SetRAMTable1029Lo(ram, other, 0);
    Dkc1Actor_SetRAMTable1029Lo(ram, 8, 2);
    CHECK(!Dkc1CoopBlockWheelDeparture(&wheel), "departed wheel stopped when a Kong retreated");
    Dkc1Actor_SetRAMTable1029Lo(ram, 8, 1);
    s_runtime.enabled = 0;
    CHECK(!Dkc1CoopBlockWheelDeparture(&wheel), "disabled co-op changed wheel departure");
  }
  for (unsigned live=0;live<2;live++) {
    Setup();
    CpuState octo={0};octo.ram=ram;octo.X=8;octo._flag_C=1;octo.P=1;
    Dkc1Actor_SetSpriteIDLo(ram,8,0x57);
    Dkc1Actor_SetRAMTable1029Lo(ram,8,0);
    Dkc1Actor_SetRAMTable13E9Lo(ram,8,0x5d0);
    Dkc1Actor_SetRAMTable145DLo(ram,8,0);
    Dkc1Actor_SetSprXPos(ram,2,0x5d1);
    Dkc1Actor_SetSprXPos(ram,4,0x5cf);
    Dkc1CoopBeginFrame(ram,0);
    CHECK(Dkc1CoopBlockCroctopusDeparture(&octo),"octopus departed before both live Kongs crossed");
    Dkc1Actor_SetRAMTable1029Lo(ram,live?4:2,0xd);
    CHECK(Dkc1CoopBlockCroctopusDeparture(&octo)==!live,"single live Kong trigger was incorrect");
    Dkc1Actor_SetSprXPos(ram,4,0x5d0);
    CHECK(!Dkc1CoopBlockCroctopusDeparture(&octo),"native inclusive boundary did not start octopus");
    Dkc1Actor_SetRAMTable1029Lo(ram,2,0x2b);
    Dkc1Actor_SetRAMTable1029Lo(ram,4,0x2b);
    Dkc1Actor_SetSprXPos(ram,2,0x5d0);
    CHECK(!Dkc1CoopBlockCroctopusDeparture(&octo),"both live Kongs did not start octopus");
    Dkc1Actor_SetRAMTable1029Lo(ram,2,0xd);
    Dkc1Actor_SetRAMTable1029Lo(ram,4,0xd);
    CHECK(Dkc1CoopBlockCroctopusDeparture(&octo),"octopus departed with no live Kong");
    s_runtime.enabled=0;
    CHECK(!Dkc1CoopBlockCroctopusDeparture(&octo),"disabled co-op gated octopus");
    s_runtime.enabled=1;
    Dkc1Actor_SetRAMTable1029Lo(ram,8,1);
    Dkc1Actor_SetSprXPos(ram,2,0);
    CHECK(!Dkc1CoopBlockCroctopusDeparture(&octo),"departed octopus was stopped again");
  }

  /* Compare the extracted predicate with exact clean-ROM CMP/ROL/EOR/LSR,
   * including both axes, inversion, optional second bound and unsigned8000. */
  if (sizeof trigger_code > 1) {
    static const uint16_t flags[] = {0, 1, 0x8000, 0x8001};
    static const uint16_t points[] = {0, 0x37ff, 0x3800, 0x3801, 0x7fff, 0x8000, 0x8001, 0xffff};
    Setup();
    for (int a = 0; a < 4; ++a) for (int b = 0; b < 4; ++b)
    for (int limit = 0; limit < 3; ++limit) for (int point = 0; point < 8; ++point) {
      Dkc1CoopEntityWrite16(ram, 0x13e9, 8, limit == 2 ? 0x8000 : 0x3800);
      Dkc1CoopEntityWrite16(ram, 0x145d, 8, flags[a]);
      Dkc1CoopEntityWrite16(ram, 0x1491, 8, limit ? 0x8000 : 0);
      Dkc1CoopEntityWrite16(ram, 0x14c5, 8, flags[b]);
      Dkc1Actor_SetSprXPos(ram, 2, points[point]);
      Dkc1Actor_SetSprYPos(ram, 2, points[7-point]);
      CHECK(PlayerCrossesAuthoredTrigger(ram, 8, 2) == NativeTriggerAllows(8, 2),
            "authored co-op trigger differs from the native byte oracle");
    }
  }
  /* Waiting transport ropes depart only when every live Kong owns this
   * rope. A partner on another rope is not a passenger; death releases wait. */
  for (uint16_t rider = 2; rider <= 4; rider += 2) {
    Setup(); uint16_t other = rider ^ 6u;
    Dkc1Actor_SetSpriteIDLo(ram, 8, 0x30);
    Dkc1Actor_SetRAMTable1029Lo(ram, 8, 4);
    Dkc1Actor_SetRAMTable1029Lo(ram, rider, 0x25);
    s_runtime.rope.rope_latched[rider / 2 - 1] = 8;
    CHECK(!Dkc1CoopRopeCanDepart(ram, 8), "waiting rope departed without the live partner");
    CpuState departure = {0}; departure.ram = ram;
    Dkc1WramSetU16(ram, 0x82, 8);
    CHECK(Dkc1CoopBlockRopeDeparture(&departure), "native departure boundary did not wait");
    CpuState lookahead = {0}; lookahead.ram = ram; lookahead.Y = 8;
    Dkc1WramSetU16(ram, 0x82, rider);
    CHECK(Dkc1CoopRopeWaitCamera(&lookahead), "waiting rope still toggles native camera lead");
    lookahead.Y = 10;
    CHECK(!Dkc1CoopRopeWaitCamera(&lookahead), "unrelated rope suppressed camera lead");
    lookahead.Y = 8;
    Dkc1WramSetU16(ram, 0x82, 8);
    Dkc1Actor_SetRAMTable1029Lo(ram, 8, 5);
    CHECK(!Dkc1CoopBlockRopeDeparture(&departure), "departed rope stopped when a passenger left");
    Dkc1WramSetU16(ram, 0x82, rider);
    CHECK(!Dkc1CoopRopeWaitCamera(&lookahead), "moving rope lost native camera lead");
    Dkc1WramSetU16(ram, 0x82, 8);
    Dkc1Actor_SetRAMTable1029Lo(ram, 8, 1);
    CHECK(!Dkc1CoopBlockRopeDeparture(&departure), "ordinary oscillating rope was gated");
    Dkc1WramSetU16(ram, 0x82, rider);
    CHECK(!Dkc1CoopRopeWaitCamera(&lookahead), "ordinary rope lost native camera lead");
    Dkc1WramSetU16(ram, 0x82, 8);
    Dkc1Actor_SetRAMTable1029Lo(ram, 8, 4);
    Dkc1Actor_SetRAMTable1029Lo(ram, other, 0x25);
    s_runtime.rope.rope_latched[other / 2 - 1] = 10;
    CHECK(!Dkc1CoopRopeCanDepart(ram, 8), "passenger on another rope permitted departure");
    s_runtime.rope.rope_latched[other / 2 - 1] = 8;
    CHECK(Dkc1CoopRopeCanDepart(ram, 8), "both attached passengers did not permit departure");
    Dkc1Actor_SetRAMTable1029Lo(ram, other, 0x0d);
    CHECK(Dkc1CoopRopeCanDepart(ram, 8), "dead partner prevented survivor rope departure");
    Dkc1Actor_SetRAMTable1029Lo(ram, other, 0x0c);
    CHECK(Dkc1CoopRopeCanDepart(ram, 8), "barrel-wait partner prevented survivor departure");
    Dkc1Actor_SetRAMTable1029Lo(ram, other, 0);
    s_runtime.enabled = 0;
    CHECK(Dkc1CoopRopeCanDepart(ram, 8), "disabled co-op gated native departure");
    Dkc1WramSetU16(ram, 0x82, rider);
    CHECK(!Dkc1CoopRopeWaitCamera(&lookahead), "disabled co-op changed camera lead");
  }
  /* A native free-rope contact must survive the other Kong restoring his
   * occupied rope before this airborne Kong consumes the grab. */
  for (uint16_t jumper = 2; jumper <= 4; jumper += 2) {
    Setup(); uint16_t partner = jumper ^ 6u;
    Dkc1Actor_SetRAMTable1029Lo(ram, partner, 0x25);
    Dkc1Actor_SetRAMTable1029Lo(ram, jumper, 1);
    Dkc1Actor_SetSpriteIDLo(ram, 8, 0x30);
    Dkc1Actor_SetSpriteIDLo(ram, 10, 0x30);
    Dkc1Actor_SetSprXPos(ram, 8, 0x4000);
    Dkc1Actor_SetSprXPos(ram, 10, 0x4100);
    Dkc1Actor_SetSprYPos(ram, 10, 0x7c);
    Dkc1Actor_SetSprXPos(ram, jumper, 0x4103);
    Dkc1Actor_SetSprYPos(ram, jumper, 0xf0);
    s_runtime.rope.rope_latched[partner / 2 - 1] = 8;
    CpuState contact = {0}; contact.ram = ram; contact.X = 10; contact.Y = jumper;
    CHECK(Dkc1CoopBeginTransaction(), "contact transaction did not begin");
    Dkc1CoopRememberRopeContact(&contact);
    Dkc1CoopEndTransaction(0);
    CHECK(!s_runtime.rope.rider_hook[jumper / 2 - 1], "private render leaked a rope contact");
    memcpy(before, ram, sizeof ram);
    Dkc1CoopRememberRopeContact(&contact);
    CHECK(!memcmp(before, ram, sizeof ram), "contact capture changed cartridge memory");
    CHECK(s_runtime.rope.rider_hook[jumper / 2 - 1] == (10 | 1u),
          "native rope contact was not retained for the tested player");
    Dkc1CoopContinuation offered;
    Dkc1CoopSaveContinuation(&offered);
    CHECK(offered.version == 5, "tagged rope contact did not advance its wire version");
    s_runtime.rope.rider_hook[jumper / 2 - 1] = 0;
    Dkc1CoopLoadContinuation(&offered);
    CHECK(s_runtime.rope.rider_hook[jumper / 2 - 1] == (10 | 1u),
          "save/load lost the pending native contact");
    offered.version = 4;
    Dkc1CoopLoadContinuation(&offered);
    CHECK(!s_runtime.rope.rider_hook[jumper / 2 - 1], "legacy odd swing handle became a rope offer");
    offered.version = 5; Dkc1CoopLoadContinuation(&offered);
    Dkc1WramSetU16(ram, 0x1afd, 8);
    Dkc1WramSetU16(ram, 0x1b01, 8);
    Dkc1CoopBeginFrame(ram, 0);
    Dkc1CoopBeforeActor(ram, jumper, 0);
    CHECK(Dkc1WramU16(ram, 0x1afd) == 10 && Dkc1WramU16(ram, 0x1b01) == 0,
          "partner rope replaced the jumper's native free-rope contact");
    Dkc1CoopAfterActor(ram);
    s_runtime.rope.rope_release[jumper / 2 - 1] = 1;
    s_runtime.rope.rope_latched[jumper / 2 - 1] = 10;
    Dkc1CoopRememberRopeContact(&contact);
    CHECK(!s_runtime.rope.rider_hook[jumper / 2 - 1], "release reoffered the rope just left");
    s_runtime.rope.rope_release[jumper / 2 - 1] = 0;
    Dkc1CoopRememberRopeContact(&contact);
    Dkc1Actor_SetSprXPos(ram, jumper, 0x4300);
    SnapshotSharedLatch(ram, jumper);
    CHECK(Dkc1WramU16(ram, 0x1afd) != 10 && !s_runtime.rope.rider_hook[jumper / 2 - 1],
          "stale distant contact was reused without native geometry");
  }
  /* Native $34 copies the other vertical climber, rather than reading
   * this player's pad. Promote only an actual shared-rope follower. */
  for (uint16_t follower = 2; follower <= 4; follower += 2) {
    for (unsigned scenario = 0; scenario < 4; ++scenario) {
      Setup(); uint16_t partner = follower ^ 6u;
      Dkc1Actor_SetRAMTable1029Lo(ram, follower, 0x34);
      Dkc1Actor_SetRAMTable1029Lo(ram, partner, scenario == 2 ? 0 : 0x25);
      Dkc1Actor_SetSpriteIDLo(ram, 8, scenario == 3 ? 0x31 : 0x30);
      Dkc1Actor_SetSprXPos(ram, 8, 0x4118);
      Dkc1Actor_SetSprYPos(ram, 8, 0x7c);
      Dkc1Actor_SetSprXPos(ram, follower, scenario == 1 ? 0x4200 : 0x4118);
      Dkc1Actor_SetSprYPos(ram, follower, 0x121);
      Dkc1WramSetU16(ram, 0x1b01, 8);
      Dkc1CoopBeginFrame(ram, follower == 2 ? 0x10 : 0x10000);
      Dkc1CoopBeforeActor(ram, follower, 0);
      CHECK(Dkc1Actor_RAMTable1029Lo(ram, follower) == (scenario ? 0x34 : 0x25),
            "native rope follower did not regain independent control, or invalid attachment was promoted");
      if (!scenario) {
        CHECK(s_runtime.rope.rope_latched[follower / 2 - 1] == 8,
              "promoted rope follower lost its own binding");
        CHECK(Dkc1WramU16(ram, DKC1_WRAM_JoypadHeld) == 0x0800,
              "promoted rope follower did not receive its own pad");
      }
      Dkc1CoopAfterActor(ram);
      CheckContinuation(follower);
    }
  }
  /* The partner's player routine clears the shared rope latch. Put it
   * back unless that partner took the rope himself. */
  Setup();
  Dkc1Actor_SetRAMTable1029Lo(ram, 2, 0x25);
  Dkc1WramSetU16(ram, 0x1B01, 8);
  Dkc1CoopBeginFrame(ram, 0);
  Dkc1CoopBeforeActor(ram, 4, 0);
  Dkc1WramSetU16(ram, 0x1B01, 0);
  Dkc1WramSetU16(ram, 0x1AFD, 0);
  Dkc1CoopAfterActor(ram);
  CHECK(Dkc1WramU16(ram, 0x1B01) == 8 &&
        Dkc1Actor_RAMTable1029Lo(ram, 2) == 0x25,
        "Diddy's tick released Donkey's rope");
  Dkc1CoopBeforeActor(ram, 4, 0);
  Dkc1Actor_SetRAMTable1029Lo(ram, 4, 0x26);
  Dkc1WramSetU16(ram, 0x1B01, 0x0A);
  Dkc1CoopAfterActor(ram);
  CHECK(Dkc1WramU16(ram, 0x1B01) == 0x0A,
        "Diddy's own rope was replaced with Donkey's latch");
  Dkc1CoopEndFrame(ram);
  Setup();
  Dkc1Actor_SetRAMTable1029Lo(ram, 2, 0x25);
  Dkc1WramSetU16(ram, 0x1B01, 8);
  Dkc1CoopBeginFrame(ram, 0);
  Dkc1CoopBeforeActor(ram, 2, 0);
  Dkc1WramSetU16(ram, 0x1B01, 0);
  Dkc1CoopAfterActor(ram);
  CHECK(Dkc1WramU16(ram, 0x1B01) == 0 &&
        Dkc1Actor_RAMTable1029Lo(ram, 2) == 0x25,
        "the climb tick could not clear its own latch");
  Dkc1CoopBeforeActor(ram, 4, 0);
  Dkc1WramSetU16(ram, 0x1B01, 8);
  Dkc1CoopAfterActor(ram);
  CHECK(Dkc1WramU16(ram, 0x1B01) == 0,
        "Diddy published a latch over Donkey's jump");
  Dkc1Actor_SetRAMTable1029Lo(ram, 2, 0);
  Dkc1CoopEndFrame(ram);
  /* A jump that did not just leave the rope must not keep the old latch.
   * Equal $1AFD/$1B01 is "already holding" and never enters $25. */
  Setup();
  Dkc1Actor_SetRAMTable1029Lo(ram, 2, 0x0001);
  Dkc1Actor_SetSpriteIDLo(ram, 8, 0x30);
  Dkc1WramSetU16(ram, 0x1B01, 8);
  Dkc1WramSetU16(ram, 0x1AFD, 8);
  Dkc1CoopBeginFrame(ram, 0);
  Dkc1CoopBeforeActor(ram, 2, 0);
  CHECK(Dkc1WramU16(ram, 0x1B01) == 0,
        "a fresh jump kept the previous rope latch");
  Dkc1CoopAfterActor(ram);
  Setup();
  Dkc1Actor_SetRAMTable1029Lo(ram, 2, 0x25);
  Dkc1Actor_SetRAMTable1029Lo(ram, 4, 0x0001);
  Dkc1Actor_SetSpriteIDLo(ram, 8, 0x30);
  Dkc1WramSetU16(ram, 0x1B01, 8);
  Dkc1WramSetU16(ram, 0x1AFD, 8);
  Dkc1CoopBeginFrame(ram, 0);
  Dkc1CoopBeforeActor(ram, 4, 0);
  Dkc1WramSetU16(ram, 0x1B01, 0);
  Dkc1CoopAfterActor(ram);
  CHECK(Dkc1WramU16(ram, 0x1B01) == 8 &&
        Dkc1Actor_RAMTable1029Lo(ram, 2) == 0x25,
        "Diddy's jump released Donkey's rope");
  Setup();
  Dkc1Actor_SetRAMTable1029Lo(ram, 2, 0x25);
  Dkc1Actor_SetRAMTable1029Lo(ram, 4, 0x0001);
  Dkc1Actor_SetSpriteIDLo(ram, 8, 0x30);
  Dkc1Actor_SetSprXPos(ram, 2, 0x1A42);
  Dkc1Actor_SetSprXPos(ram, 4, 0x1A4A);
  Dkc1Actor_SetSprXPos(ram, 8, 0x1A42);
  Dkc1WramSetU16(ram, 0x1B01, 8);
  Dkc1WramSetU16(ram, 0x1AFD, 8);
  Dkc1CoopBeginFrame(ram, 0);
  Dkc1CoopBeforeActor(ram, 4, 0);
  CHECK(Dkc1WramU16(ram, 0x1B01) == 0 && Dkc1WramU16(ram, 0x1AFD) == 8,
        "Diddy was not offered the rope Donkey is holding");
  Dkc1Actor_SetRAMTable1029Lo(ram, 4, 0x25);
  Dkc1WramSetU16(ram, 0x1B01, 8);
  Dkc1CoopAfterActor(ram);
  CHECK(Dkc1WramU16(ram, 0x1B01) == 8 &&
        Dkc1Actor_RAMTable1029Lo(ram, 2) == 0x25 &&
        Dkc1Actor_RAMTable1029Lo(ram, 4) == 0x25,
        "Diddy's grab dropped Donkey");
  Setup();
  Dkc1Actor_SetRAMTable1029Lo(ram, 2, 0x0001);
  Dkc1Actor_SetRAMTable1029Lo(ram, 4, 0x25);
  Dkc1Actor_SetSpriteIDLo(ram, 8, 0x30);
  Dkc1Actor_SetSprXPos(ram, 2, 0x1A4E);
  Dkc1Actor_SetSprXPos(ram, 4, 0x1A4E);
  Dkc1Actor_SetSprXPos(ram, 8, 0x1A4E);
  Dkc1Actor_SetSprYPos(ram, 2, 0x0055);
  Dkc1Actor_SetSprYPos(ram, 8, 0x0090);
  Dkc1WramSetU16(ram, 0x1B01, 8);
  Dkc1WramSetU16(ram, 0x1AFD, 8);
  Dkc1CoopBeginFrame(ram, 0);
  Dkc1CoopBeforeActor(ram, 2, 0);
  CHECK(Dkc1WramU16(ram, 0x1B01) == 8,
        "a jump below the rope was forced into the climb");
  Setup();
  Dkc1Actor_SetRAMTable1029Lo(ram, 2, 0x25);
  Dkc1WramSetU16(ram, 0x1B01, 8);
  Dkc1CoopBeginFrame(ram, 0);
  Dkc1CoopBeforeActor(ram, 2, 0);
  Dkc1Actor_SetRAMTable1029Lo(ram, 2, 0x0001);
  Dkc1CoopAfterActor(ram);
  Dkc1CoopBeforeActor(ram, 4, 0);
  Dkc1WramSetU16(ram, 0x1B01, 0);
  Dkc1CoopAfterActor(ram);
  CHECK(Dkc1WramU16(ram, 0x1B01) == 8 &&
        Dkc1Actor_RAMTable1029Lo(ram, 2) == 0x0001,
        "the airborne release lost its rope latch");
  /* Both are climbing. Donkey leaves. Diddy's tick must not offer the
   * rope back to him on the next frame. */
  Setup();
  Dkc1Actor_SetRAMTable1029Lo(ram, 2, 0x25);
  Dkc1Actor_SetRAMTable1029Lo(ram, 4, 0x25);
  Dkc1Actor_SetSpriteIDLo(ram, 8, 0x30);
  Dkc1Actor_SetSprXPos(ram, 2, 0x1A42);
  Dkc1Actor_SetSprXPos(ram, 4, 0x1A42);
  Dkc1Actor_SetSprXPos(ram, 8, 0x1A42);
  Dkc1WramSetU16(ram, 0x1B01, 8);
  Dkc1CoopBeginFrame(ram, 0);
  Dkc1CoopBeforeActor(ram, 2, 0);
  Dkc1Actor_SetRAMTable1029Lo(ram, 2, 0x0001);
  Dkc1CoopAfterActor(ram);
  Dkc1CoopBeforeActor(ram, 4, 0);
  Dkc1WramSetU16(ram, 0x1B01, 0);
  Dkc1CoopAfterActor(ram);
  CHECK(Dkc1WramU16(ram, 0x1B01) == 8 &&
        Dkc1Actor_RAMTable1029Lo(ram, 2) == 0x0001 &&
        Dkc1Actor_RAMTable1029Lo(ram, 4) == 0x25,
        "leaving the rope cleared the partner still climbing");
  CheckContinuation(2);
  Dkc1CoopBeforeActor(ram, 2, 0);
  CHECK(Dkc1WramU16(ram, 0x1B01) == 8,
        "the rope was offered again after the jump off");
  /* Donkey has left. A later frame clears the shared latch. Diddy is
   * still climbing, so his tick must see the rope again. */
  Dkc1WramSetU16(ram, 0x1B01, 0);
  Dkc1WramSetU16(ram, 0x1AFD, 0);
  Dkc1CoopAfterActor(ram);
  Dkc1CoopBeforeActor(ram, 4, 0);
  CHECK(Dkc1WramU16(ram, 0x1B01) == 8 && Dkc1WramU16(ram, 0x1AFD) == 8,
        "Diddy lost the rope after Donkey had already left");
  /* Donkey has fallen off the bottom. State $000D is not the airborne
   * release, and the shared latch is gone. Diddy is still climbing. */
  Dkc1Actor_SetRAMTable1029Lo(ram, 2, 0x000D);
  Dkc1WramSetU16(ram, 0x1B01, 0);
  Dkc1WramSetU16(ram, 0x1AFD, 0);
  Dkc1CoopBeforeActor(ram, 2, 0);
  Dkc1CoopAfterActor(ram);
  Dkc1CoopBeforeActor(ram, 4, 0);
  CHECK(Dkc1WramU16(ram, 0x1B01) == 8 && Dkc1WramU16(ram, 0x1AFD) == 8 &&
        Dkc1Actor_RAMTable1029Lo(ram, 4) == 0x25,
        "Diddy let go when Donkey fell off the bottom");
  Dkc1Actor_SetRAMTable1029Lo(ram, 2, 0);
  Dkc1CoopEndFrame(ram);

  /* Either player takes a second moving/climb rope. A native grab owns
   * only that player's latch, including continuation between their ticks. */
  for(unsigned first=2;first<=4;first+=2) {
    unsigned second=first^6u;
    Setup();Dkc1Actor_SetSpriteIDLo(ram,6,0x30);Dkc1Actor_SetSpriteIDLo(ram,8,0x30);
    Dkc1Actor_SetRAMTable1029Lo(ram,first,0x25);
    Dkc1Actor_SetRAMTable1029Lo(ram,second,1);
    Dkc1WramSetU16(ram,0x1B01,6);Dkc1WramSetU16(ram,0x1AFD,6);
    Dkc1CoopBeginFrame(ram,0);Dkc1CoopBeforeActor(ram,first,0);Dkc1CoopAfterActor(ram);
    CHECK(s_runtime.rope.rope_latched[first/2-1]==6,"initial climber did not acquire his rope");
    Dkc1CoopBeforeActor(ram,second,0);
    Dkc1Actor_SetRAMTable1029Lo(ram,second,0x25);Dkc1WramSetU16(ram,0x1B01,8);
    Dkc1CoopAfterActor(ram);
    CHECK(s_runtime.rope.rope_latched[second/2-1]==8,"native new grab not assigned to actual rider");
    CheckContinuation(first);
    Dkc1CoopBeforeActor(ram,first,0);
    CHECK(Dkc1WramU16(ram,0x1B01)==6 && Dkc1WramU16(ram,0x1AFD)==6,
          "partner's new rope replaced original climber's binding");
    Dkc1CoopAfterActor(ram);Dkc1CoopBeforeActor(ram,second,0);
    CHECK(Dkc1WramU16(ram,0x1B01)==8,"second rider could not keep his own rope");
    Dkc1CoopAfterActor(ram);
    /* The first rider releases6, then actually grabs8 on a later air tick. */
    Dkc1CoopBeforeActor(ram,first,0);Dkc1Actor_SetRAMTable1029Lo(ram,first,1);
    Dkc1CoopAfterActor(ram);Dkc1CoopBeforeActor(ram,first,0);
    Dkc1Actor_SetRAMTable1029Lo(ram,first,0x25);Dkc1WramSetU16(ram,0x1B01,8);
    Dkc1CoopAfterActor(ram);
    CHECK(s_runtime.rope.rope_latched[first/2-1]==8 && !s_runtime.rope.rope_release[first/2-1],
          "release/regrab retained previous rope or release marker");
    /* Swing ropes own rider_hook, never the previous vertical rope latch. */
    Dkc1Actor_SetSpriteIDLo(ram,0x0a,0x31);
    Dkc1Actor_SetRAMTable1029Lo(ram,first,0x26);
    s_runtime.rope.rider_hook[first/2-1]=0x0a;
    Dkc1WramSetU16(ram,0x1B01,0x0a);Dkc1WramSetU16(ram,0x1AFD,0);
    Dkc1CoopBeforeActor(ram,first,0);
    CHECK(!s_runtime.rope.rope_latched[first/2-1] && Dkc1WramU16(ram,0x1B01)==0x0a &&
          !Dkc1WramU16(ram,0x1AFD),"swing rider inherited his previous vertical rope");
    Dkc1CoopAfterActor(ram);
  }

  /* A release excludes only its old30 rope. New overlaps retain native
   * reach boundaries and survive continuation for either controller. */
  for(unsigned rider=2;rider<=4;rider+=2) for(unsigned scenario=0;scenario<6;scenario++) {
    Setup();Dkc1Actor_SetSpriteIDLo(ram,6,0x30);Dkc1Actor_SetSpriteIDLo(ram,8,0x30);
    Dkc1Actor_SetRAMTable1029Lo(ram,rider,0x25);
    Dkc1WramSetU16(ram,0x1B01,6);Dkc1WramSetU16(ram,0x1AFD,6);
    Dkc1CoopBeginFrame(ram,0);Dkc1CoopBeforeActor(ram,rider,0);
    Dkc1Actor_SetRAMTable1029Lo(ram,rider,1);Dkc1CoopAfterActor(ram);
    unsigned target=scenario==4?6:8;
    Dkc1Actor_SetSprXPos(ram,target,0x1000);Dkc1Actor_SetSprYPos(ram,target,0x100);
    Dkc1Actor_SetSprXPos(ram,rider,0x1000+(scenario==1?12:scenario==3?100:11));
    Dkc1Actor_SetSprYPos(ram,rider,scenario==5?0xffe8:scenario==2?0xe7:0xe8);
    Dkc1WramSetU16(ram,0x1AFD,(uint16_t)target);
    Dkc1WramSetU16(ram,0x1B01,(uint16_t)target);
    Dkc1CoopBeforeActor(ram,rider,0);
    CHECK(Dkc1WramU16(ram,0x1AFD)==(scenario==0?8:0),
          "release rejected new rope or admitted old/out-of-reach rope");
    if(scenario==0) {
      CHECK(!Dkc1WramU16(ram,0x1B01),"new occupied rope remained marked already held");
      Dkc1CoopAfterActor(ram); /* save at a completed actor boundary */
      CheckContinuation(rider);
      Dkc1CoopBeforeActor(ram,rider,0);
      Dkc1Actor_SetRAMTable1029Lo(ram,rider,0x25);
      Dkc1WramSetU16(ram,0x1B01,8);Dkc1CoopAfterActor(ram);
      CHECK(s_runtime.rope.rope_latched[rider/2-1]==8 && !s_runtime.rope.rope_release[rider/2-1],
            "new native grab retained old rope's release exclusion");
    } else Dkc1CoopAfterActor(ram);
  }

  /* Sprite $31 writes $1AFF after the Kong tick. The next airborne
   * tick must enter $0026 with that slot in $1B01. */
  Setup();
  Dkc1Actor_SetSpriteIDLo(ram, 6, 0x31);
  Dkc1Actor_SetSprXPos(ram, 6, 0x1E50);
  Dkc1Actor_SetSprYPos(ram, 6, 0x0150);
  Dkc1Actor_SetRAMTable1029Lo(ram, 2, 0x000D);
  Dkc1Actor_SetSprXPos(ram, 2, 0x1BED);
  Dkc1Actor_SetSprYPos(ram, 2, 0xFFC0);
  Dkc1Actor_SetRAMTable1029Lo(ram, 4, 0x0001);
  Dkc1Actor_SetSprXPos(ram, 4, 0x1E04);
  Dkc1Actor_SetSprYPos(ram, 4, 0x0128);
  Dkc1WramSetU16(ram, 0x1AFD, 0);
  Dkc1WramSetU16(ram, 0x1AFF, 6);
  Dkc1WramSetU16(ram, 0x1B01, 0);
  Dkc1CoopBeginFrame(ram, 0);
  /* The other Kong's tick clears $1AFF before this grab. */
  Dkc1WramSetU16(ram, 0x1AFF, 0);
  Dkc1CoopBeforeActor(ram, 4, 0);
  CHECK(Dkc1Actor_RAMTable1029Lo(ram, 4) == 0x0026 &&
        Dkc1WramU16(ram, 0x1B01) == 6 && Dkc1WramU16(ram, 0x1AFF) == 0,
        "an airborne Kong did not take the sprite $31 latch");
  Dkc1CoopAfterActor(ram);
  CHECK(Dkc1Actor_RAMTable1029Lo(ram, 4) == 0x0026 &&
        Dkc1WramU16(ram, 0x1B01) == 6,
        "the sprite $31 latch was cleared after the tick");
  Dkc1CoopEndFrame(ram);

  /* Donkey already holds the swinging rope. Diddy is in the air beside
   * him, so equal $1AFF/$1B01 is the holder's latch, not Diddy's. */
  Setup();
  Dkc1Actor_SetSpriteIDLo(ram, 6, 0x31);
  Dkc1Actor_SetRAMTable1029Lo(ram, 2, 0x0026);
  Dkc1Actor_SetSprXPos(ram, 2, 0x1DFE);
  Dkc1Actor_SetSprYPos(ram, 2, 0x0114);
  Dkc1Actor_SetRAMTable1029Lo(ram, 4, 0x0001);
  Dkc1Actor_SetSprXPos(ram, 4, 0x1DF9);
  Dkc1Actor_SetSprYPos(ram, 4, 0x0126);
  Dkc1WramSetU16(ram, 0x1AFD, 0);
  Dkc1WramSetU16(ram, 0x1AFF, 6);
  Dkc1WramSetU16(ram, 0x1B01, 6);
  Dkc1CoopBeginFrame(ram, 0);
  Dkc1CoopBeforeActor(ram, 4, 0);
  CHECK(Dkc1Actor_RAMTable1029Lo(ram, 4) == 0x0026 &&
        Dkc1Actor_RAMTable1029Lo(ram, 2) == 0x0026 &&
        Dkc1WramU16(ram, 0x1B01) == 6,
        "Diddy beside a swinging Kong did not take the same rope");
  Dkc1CoopAfterActor(ram);
  Dkc1CoopBeforeActor(ram, 2, 0);
  CHECK(Dkc1Actor_RAMTable1029Lo(ram, 2) == 0x0026 &&
        Dkc1WramU16(ram, 0x1B01) == 6,
        "Donkey lost the swinging rope when Diddy joined");
  Dkc1Actor_SetSprAnimFrame(ram, 6, 0x2678);
  Dkc1Actor_SetSprFlags(ram, 6, 0x3C80);
  Dkc1Actor_SetSprAnimFrame(ram, 2, 0x0644);
  Dkc1Actor_SetDisplayedPoseLo(ram, 2, 0x0644);
  Dkc1Actor_SetSprAnimFrame(ram, 4, 0x0A50);
  Dkc1Actor_SetDisplayedPoseLo(ram, 4, 0x0A50);
  Dkc1CoopEndFrame(ram);
  CHECK(Dkc1Actor_SprAnimFrame(ram, 4) == 0x0E40 &&
        Dkc1Actor_DisplayedPoseLo(ram, 4) == 0x0E40 &&
        Dkc1Actor_SprAnimFrame(ram, 2) == 0x0644,
        "Diddy kept the jump pose while sharing the swing");
  Dkc1Actor_SetSprFlags(ram, 6, 0x7C80);
  Dkc1CoopEndFrame(ram);
  CHECK(Dkc1Actor_SprAnimFrame(ram, 4) == 0x0EA8 &&
        Dkc1Actor_SprAnimFrame(ram, 2) == 0x06AC,
        "the mirrored rope frame was not shared");
  Dkc1Actor_SetRAMTable1491Lo(ram, 6, 1);
  Dkc1CoopEndFrame(ram);
  CHECK(Dkc1Actor_SprAnimFrame(ram, 4) == 0x0EB0 &&
        Dkc1Actor_SprAnimFrame(ram, 2) == 0x06B4,
        "the rope's single swing frame was not shared");

  Setup();
  Dkc1Actor_SetSpriteIDLo(ram, 6, 0x31);
  Dkc1Actor_SetRAMTable1029Lo(ram, 2, 0x0026);
  Dkc1Actor_SetSprXPos(ram, 2, 0x1DFE);
  Dkc1Actor_SetSprYPos(ram, 2, 0x0114);
  Dkc1Actor_SetRAMTable1029Lo(ram, 4, 0x0001);
  Dkc1Actor_SetSprXPos(ram, 4, 0x1D00);
  Dkc1Actor_SetSprYPos(ram, 4, 0x00CF);
  Dkc1WramSetU16(ram, 0x1AFD, 0);
  Dkc1WramSetU16(ram, 0x1AFF, 6);
  Dkc1WramSetU16(ram, 0x1B01, 6);
  Dkc1CoopBeginFrame(ram, 0);
  Dkc1CoopBeforeActor(ram, 4, 0);
  CHECK(Dkc1Actor_RAMTable1029Lo(ram, 4) == 0x0001,
        "a distant jump joined the swinging rope");
  Dkc1CoopEndFrame(ram);

  /* Donkey has left the swing. The rope still aims $056F at him, so
   * Diddy would stay at the old point. The rope tick must aim at Diddy. */
  Setup();
  Dkc1Actor_SetSpriteIDLo(ram, 6, 0x31);
  Dkc1Actor_SetRAMTable1029Lo(ram, 2, 0x0001);
  Dkc1Actor_SetSprXPos(ram, 2, 0x1DFF);
  Dkc1Actor_SetSprYPos(ram, 2, 0x011E);
  Dkc1Actor_SetRAMTable1029Lo(ram, 4, 0x0026);
  Dkc1Actor_SetSprXPos(ram, 4, 0x1E00);
  Dkc1Actor_SetSprYPos(ram, 4, 0x0110);
  Dkc1WramSetU16(ram, 0x056F, 1);
  Dkc1WramSetU16(ram, 0x1B01, 6);
  Dkc1CoopBeginFrame(ram, 0);
  Dkc1CoopBeforeActor(ram, 6, 0);
  CHECK(Dkc1WramU16(ram, 0x056F) == 2,
        "the rope still aimed at the Kong who had jumped");
  Dkc1CoopAfterActor(ram);
  CHECK(Dkc1WramU16(ram, 0x056F) == 1,
        "the rope tick left $056F on the partner");
  Setup();
  Dkc1Actor_SetSpriteIDLo(ram, 6, 0x31);
  Dkc1Actor_SetSprXPos(ram, 6, 0x1E50);
  Dkc1Actor_SetSprYPos(ram, 6, 0x0150);
  Dkc1Actor_SetRAMTable1029Lo(ram, 2, 0x0026);
  Dkc1Actor_SetSprXPos(ram, 2, 0x1E50);
  Dkc1Actor_SetSprYPos(ram, 2, 0x0150);
  Dkc1Actor_SetRAMTable1029Lo(ram, 4, 0x0026);
  Dkc1Actor_SetSprXPos(ram, 4, 0x1000);
  Dkc1Actor_SetSprYPos(ram, 4, 0x0100);
  Dkc1WramSetU16(ram, 0x056F, 1);
  Dkc1CoopBeginFrame(ram, 0);
  Dkc1CoopBeforeActor(ram, 6, 0);
  CHECK(Dkc1WramU16(ram, 0x056F) == 1,
        "both riders made the rope leave the Kong it was already placing");

  /* Diddy still owns rope 6. Rope 8 must keep Donkey in $056F so its
   * overlap can latch him, and rope 6 must keep aiming at Diddy. */
  Setup();
  Dkc1Actor_SetSpriteIDLo(ram, 6, 0x31);
  Dkc1Actor_SetSprXPos(ram, 6, 0x1E50);
  Dkc1Actor_SetSprYPos(ram, 6, 0x0150);
  Dkc1Actor_SetSpriteIDLo(ram, 8, 0x31);
  Dkc1Actor_SetSprXPos(ram, 8, 0x1FD0);
  Dkc1Actor_SetSprYPos(ram, 8, 0x0150);
  Dkc1Actor_SetRAMTable1029Lo(ram, 4, 0x0026);
  Dkc1Actor_SetSprXPos(ram, 4, 0x1E50);
  Dkc1Actor_SetSprYPos(ram, 4, 0x0110);
  Dkc1WramSetU16(ram, 0x1B01, 6);
  Dkc1WramSetU16(ram, 0x056F, 1);
  Dkc1CoopBeginFrame(ram, 0);
  Dkc1CoopBeforeActor(ram, 4, 0);
  Dkc1CoopAfterActor(ram);
  CHECK(s_runtime.rope.rider_hook[1] == 6, "Diddy did not keep the rope he is swinging");
  Dkc1Actor_SetRAMTable1029Lo(ram, 2, 0x0001);
  Dkc1Actor_SetSprXPos(ram, 2, 0x1FD0);
  Dkc1Actor_SetSprYPos(ram, 2, 0x0140);
  Dkc1WramSetU16(ram, 0x1AFD, 0);
  Dkc1WramSetU16(ram, 0x1AFF, 8);
  Dkc1CoopBeginFrame(ram, 0);
  Dkc1CoopBeforeActor(ram, 8, 0);
  CHECK(Dkc1WramU16(ram, 0x056F) == 1,
        "the next rope aimed at the Kong on the previous rope");
  Dkc1CoopAfterActor(ram);
  Dkc1CoopBeforeActor(ram, 2, 0);
  CHECK(Dkc1Actor_RAMTable1029Lo(ram, 2) == 0x0026 && s_runtime.rope.rider_hook[0] == 8,
        "Donkey did not take the next rope");
  CHECK(s_runtime.rope.rider_hook[1] == 6, "Diddy's rope was replaced by Donkey's grab");
  Dkc1CoopAfterActor(ram);
  CheckContinuation(2);
  Dkc1CoopBeforeActor(ram,2,0);
  Dkc1CoopSuspendActor(ram,0xbf9000,0x1ab);
  CHECK(Dkc1CoopActorPending(),"rope actor was not suspended");
  CheckContinuation(2);
  Dkc1CoopBeforeActor(ram, 6, 0);
  CHECK(Dkc1WramU16(ram, 0x056F) == 2,
        "Diddy's rope stopped aiming at him after Donkey grabbed");
  Dkc1CoopAfterActor(ram);
  Dkc1WramSetU16(ram, 0x1B01, 6);
  Dkc1WramSetU16(ram, 0x056F, 1);
  Dkc1CoopBeforeActor(ram, 8, 0);
  CHECK(Dkc1WramU16(ram, 0x1B01) == 8,
        "Donkey's rope ticked while the latch still named Diddy's rope");
  Dkc1CoopAfterActor(ram);
  CHECK(Dkc1WramU16(ram, 0x1B01) == 6,
        "Donkey's rope kept the latch after its tick");
  /* The pivot is outside the bind reach, so the rope does not select a
   * nearest Kong. $1B01 still names Diddy's rope. Donkey must not stay
   * aimed at it. */
  Dkc1Actor_SetSprXPos(ram, 6, 0);
  Dkc1Actor_SetSprYPos(ram, 6, 0);
  Dkc1WramSetU16(ram, 0x056F, 1);
  Dkc1WramSetU16(ram, 0x1B01, 6);
  Dkc1CoopBeforeActor(ram, 6, 0);
  CHECK(Dkc1WramU16(ram, 0x056F) == 2,
        "Diddy's rope kept Donkey and would carry him across");
  Dkc1CoopAfterActor(ram);
  Dkc1CoopEndFrame(ram);
  uint16_t stayed = Dkc1Actor_SprXPos(ram, 2);
  Dkc1Actor_SetSprXPos(ram, 4, (uint16_t)(Dkc1Actor_SprXPos(ram, 4) + 8));
  Dkc1CoopEndFrame(ram);
  CHECK(Dkc1Actor_SprXPos(ram, 2) == stayed,
        "Donkey was copied onto Diddy's rope");

  /* A loaded save has no host memory. $1B01 names Diddy's rope and
   * $1AFF names Donkey's. Each rider must keep the nearer rope. */
  Setup();
  Dkc1Actor_SetSpriteIDLo(ram, 6, 0x31);
  Dkc1Actor_SetSprXPos(ram, 6, 0x1E50);
  Dkc1Actor_SetSprYPos(ram, 6, 0x0150);
  Dkc1Actor_SetSpriteIDLo(ram, 8, 0x31);
  Dkc1Actor_SetSprXPos(ram, 8, 0x2080);
  Dkc1Actor_SetSprYPos(ram, 8, 0x0150);
  Dkc1Actor_SetRAMTable1029Lo(ram, 2, 0x0026);
  Dkc1Actor_SetSprXPos(ram, 2, 0x208E);
  Dkc1Actor_SetSprYPos(ram, 2, 0x0124);
  Dkc1Actor_SetRAMTable1029Lo(ram, 4, 0x0026);
  Dkc1Actor_SetSprXPos(ram, 4, 0x1E9B);
  Dkc1Actor_SetSprYPos(ram, 4, 0x0118);
  Dkc1WramSetU16(ram, 0x1AFF, 8);
  Dkc1WramSetU16(ram, 0x1B01, 6);
  Dkc1CoopBeginFrame(ram, 0);
  Dkc1CoopBeforeActor(ram, 2, 0);
  Dkc1CoopAfterActor(ram);
  Dkc1CoopBeforeActor(ram, 4, 0);
  Dkc1CoopAfterActor(ram);
  CHECK(s_runtime.rope.rider_hook[0] == 8 && s_runtime.rope.rider_hook[1] == 6,
        "loading gave both riders the rope named by $1B01");
  Dkc1CoopEndFrame(ram);
  stayed = Dkc1Actor_SprXPos(ram, 2);
  Dkc1Actor_SetSprXPos(ram, 4, (uint16_t)(Dkc1Actor_SprXPos(ram, 4) + 4));
  Dkc1CoopEndFrame(ram);
  CHECK(Dkc1Actor_SprXPos(ram, 2) == stayed,
        "the loaded Donkey was moved onto Diddy's rope");

  /* A released rope still occupies shared $1B01. It must not pick the
   * partner who is holding a different, already verified rope. */
  Setup();
  Dkc1Actor_SetSpriteIDLo(ram,6,0x31);
  Dkc1Actor_SetSpriteIDLo(ram,8,0x31);
  Dkc1Actor_SetRAMTable1029Lo(ram,2,0x26);
  Dkc1Actor_SetRAMTable1029Lo(ram,4,1);
  s_runtime.rope.rider_hook[0]=8;
  s_runtime.rope.rope_release[1]=1;
  s_runtime.rope.release_hook[1]=6;
  Dkc1WramSetU16(ram,0x1b01,6);
  Dkc1CoopBeginFrame(ram,0);
  Dkc1CoopBeforeActor(ram,6,0);
  CHECK(Dkc1WramU16(ram,0x1b01)!=6,
        "released swing rope would move the rider on another rope");
  Dkc1CoopAfterActor(ram);
  CHECK(s_runtime.rope.rider_hook[0]==8 && Dkc1WramU16(ram,0x1b01)==6,
        "released swing isolation lost the verified rider or shared context");

  /* Donkey jumps off his rope. Stock puts him back in $0026 from the
   * shared latch, and the swing copy would carry him to Diddy. */
  Setup();
  Dkc1Actor_SetSpriteIDLo(ram, 6, 0x31);
  Dkc1Actor_SetSprXPos(ram, 6, 0x1E50);
  Dkc1Actor_SetSprYPos(ram, 6, 0x0150);
  Dkc1Actor_SetSpriteIDLo(ram, 8, 0x31);
  Dkc1Actor_SetSprXPos(ram, 8, 0x2080);
  Dkc1Actor_SetSprYPos(ram, 8, 0x0150);
  Dkc1Actor_SetRAMTable1029Lo(ram, 4, 0x0026);
  Dkc1Actor_SetSprXPos(ram, 4, 0x1E94);
  Dkc1Actor_SetSprYPos(ram, 4, 0x0116);
  Dkc1Actor_SetRAMTable1029Lo(ram, 2, 0x0001);
  Dkc1Actor_SetSprXPos(ram, 2, 0x208C);
  Dkc1Actor_SetSprYPos(ram, 2, 0x012B);
  Dkc1WramSetU16(ram, 0x1AFF, 0x000A);
  Dkc1WramSetU16(ram, 0x1B01, 6);
  Dkc1Actor_SetSprYVelocity(ram, 2, 0x0700);
  s_runtime.rope.rope_release[0] = 1;
  Dkc1CoopBeginFrame(ram, 0);
  Dkc1CoopBeforeActor(ram, 2, 0);
  Dkc1Actor_SetRAMTable1029Lo(ram, 2, 0x0026);
  Dkc1Actor_SetSprYVelocity(ram, 2, 0);
  Dkc1CoopAfterActor(ram);
  CHECK(Dkc1Actor_RAMTable1029Lo(ram, 2) == 0x0001 &&
        Dkc1Actor_SprXPos(ram, 2) == 0x208C &&
        Dkc1Actor_SprYVelocity(ram, 2) == 0x0700,
        "the jump off Donkey's rope moved him to Diddy");

  Setup();
  Dkc1Actor_SetSpriteIDLo(ram,8,0x31);
  Dkc1Actor_SetSprXPos(ram,8,300);Dkc1Actor_SetSprYPos(ram,8,150);
  Dkc1Actor_SetSprXPos(ram,2,300);Dkc1Actor_SetSprYPos(ram,2,100);
  Dkc1WramSetU16(ram,0x1b01,8);
  Dkc1Actor_SetRAMTable1029Lo(ram, 2, 0x0026);
  Dkc1CoopBeginFrame(ram, 0x001);
  Dkc1CoopBeforeActor(ram, 2, 0);
  Dkc1Actor_SetRAMTable1029Lo(ram, 2, 0x0001);
  Dkc1Actor_SetSprYVelocity(ram, 2, 0x0700);
  Dkc1CoopAfterActor(ram);
  CHECK(Dkc1Actor_SprYVelocity(ram, 2) == 0x0700,
        "the rope jump lost the $0700 impulse");
  CHECK(s_runtime.rope.rope_release[0] && s_runtime.rope.release_hook[0]==8,
        "swing release fixture did not preserve the abandoned hook");
  CheckContinuation(2);

  /* Donkey's DK-barrel exit sits in $0002 with no collision bit $0100.
   * Diddy's ticks must not keep restarting the wait. */
  Setup();
  Dkc1Actor_SetRAMTable1029Lo(ram, 2, 2);
  Dkc1Actor_SetSprCollisionFlags(ram, 2, 0);
  Dkc1Actor_SetSprXPos(ram, 2, 0x1D63);
  Dkc1Actor_SetSprYPos(ram, 2, 0x013A);
  Dkc1Actor_SetRAMTable1029Lo(ram, 4, 0);
  for (int still = 0; still < 45; still++) {
    Dkc1CoopBeforeActor(ram, 2, 0);
    Dkc1CoopAfterActor(ram);
    Dkc1CoopBeforeActor(ram, 4, 0);
    Dkc1CoopAfterActor(ram);
  }
  CHECK(Dkc1Actor_RAMTable1029Lo(ram, 2) == 2,
        "the barrel-exit pose was cleared before it finished");
  Dkc1CoopBeforeActor(ram, 2, 0);
  Dkc1CoopAfterActor(ram);
  CHECK(Dkc1Actor_RAMTable1029Lo(ram, 2) == 0,
        "Donkey stayed locked in the barrel-exit pose");

  /* The tester's locked landing jitters in X and loops its fall animation.
   * Unchanging Y, rather than X, identifies the completed airborne phase. */
  Setup();
  Dkc1Actor_SetRAMTable1029Lo(ram, 2, 2);
  Dkc1Actor_SetSprCollisionFlags(ram, 2, 0);
  Dkc1Actor_SetSprAnimFrame(ram, 2, 0x34c);
  Dkc1Actor_SetSprYPos(ram, 2, 0xf0);
  for (int tick = 0; tick < 46; ++tick) {
    Dkc1Actor_SetSprXPos(ram, 2, (uint16_t)(0x25 + tick % 20));
    Dkc1Actor_SetSprAnimFrame(ram, 2, (uint16_t)(0x34c + (tick % 12) * 4));
    Dkc1CoopBeforeActor(ram, 2, 0); Dkc1CoopAfterActor(ram);
    Dkc1CoopBeforeActor(ram, 4, 0); Dkc1CoopAfterActor(ram);
  }
  CHECK(Dkc1Actor_RAMTable1029Lo(ram, 2) == 0,
        "horizontal jitter kept the looping landing pose locked");
  Setup();
  Dkc1Actor_SetRAMTable1029Lo(ram, 2, 2);
  Dkc1Actor_SetSprCollisionFlags(ram, 2, 0);
  for (int tick = 0; tick < 90; ++tick) {
    Dkc1Actor_SetSprAnimFrame(ram, 2, (uint16_t)(0x34c + tick % 4));
    Dkc1Actor_SetSprYPos(ram, 2, (uint16_t)(0x100 + tick));
    Dkc1CoopBeforeActor(ram, 2, 0); Dkc1CoopAfterActor(ram);
  }
  CHECK(Dkc1Actor_RAMTable1029Lo(ram, 2) == 2,
        "an airborne native fall was interrupted");

  /* Each player's landing recovery must survive the other player's tick. */
  Setup();
  for (unsigned h = 2; h <= 4; h += 2) {
    Dkc1Actor_SetRAMTable1029Lo(ram, h, 2);
    Dkc1Actor_SetSprCollisionFlags(ram, h, 0);
    Dkc1Actor_SetSprYPos(ram, h, 0xf0);
  }
  for (unsigned tick = 0; tick < 46; ++tick) {
    Dkc1CoopBeforeActor(ram, 2, 0); Dkc1CoopAfterActor(ram);
    Dkc1CoopBeforeActor(ram, 4, 0); Dkc1CoopAfterActor(ram);
  }
  CHECK(Dkc1Actor_RAMTable1029Lo(ram, 2) == 0 &&
        Dkc1Actor_RAMTable1029Lo(ram, 4) == 0,
        "two stationary landing poses reset each other's recovery");

  /* Independent counters remain independent through a save/load branch. */
  Setup();
  for (unsigned h=2;h<=4;h+=2) {
    Dkc1Actor_SetRAMTable1029Lo(ram,h,2);
    Dkc1Actor_SetSprCollisionFlags(ram,h,0);
    Dkc1Actor_SetSprYPos(ram,h,0xf0);
  }
  for (unsigned tick=0;tick<23;tick++) for (unsigned h=2;h<=4;h+=2) {
    Dkc1CoopBeforeActor(ram,h,0);Dkc1CoopAfterActor(ram);
  }
  Dkc1CoopSnapshot halfway; Dkc1CoopSaveState(&halfway);
  uint8_t halfway_ram[sizeof ram]; memcpy(halfway_ram,ram,sizeof ram);
  for (unsigned tick=23;tick<46;tick++) for (unsigned h=2;h<=4;h+=2) {
    Dkc1CoopBeforeActor(ram,h,0);Dkc1CoopAfterActor(ram);
  }
  uint8_t completed_ram[sizeof ram]; memcpy(completed_ram,ram,sizeof ram);
  Dkc1CoopSnapshot completed;Dkc1CoopSaveState(&completed);
  memcpy(ram,halfway_ram,sizeof ram);Dkc1CoopLoadState(&halfway,ram);
  for (unsigned tick=23;tick<46;tick++) for (unsigned h=2;h<=4;h+=2) {
    Dkc1CoopBeforeActor(ram,h,0);Dkc1CoopAfterActor(ram);
  }
  Dkc1CoopSaveState(&restored);
  CHECK(!memcmp(ram,completed_ram,sizeof ram) && !memcmp(&completed,&restored,sizeof restored),
        "per-player recovery split save/load diverged");
  for (unsigned h=2;h<=4;h+=2) {
    halfway.version=2;halfway.pose_x=0x123;halfway.pose_y=0x456;
    halfway.pose_stuck=33;halfway.reserved=(uint16_t)h;
    Dkc1CoopLoadState(&halfway,ram);
    CHECK(s_runtime.pose[h/2-1].y==0x456 && s_runtime.pose[h/2-1].ticks==33 &&
          s_runtime.pose[h/2-1].active && !s_runtime.pose[(h/2-1)^1].active,
          "legacy v2 recovery owner was not migrated");
  }
  halfway.version=1;halfway.reserved=0;Dkc1CoopLoadState(&halfway,ram);
  CHECK(!s_runtime.pose[0].active && !s_runtime.pose[1].active,"legacy v1 guessed a recovery owner");

  printf("co-op runtime failures: %u\n", failures);
  return failures ? 1 : 0;
}
