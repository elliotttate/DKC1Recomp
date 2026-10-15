#include <assert.h>
#include "dkc1_coop_mod.c"
#include "coop_test_support.h"
#include "snes/interp816.h"

/* Python fills these instruction spans directly from the private ROM. */
static const uint8_t sink_tail[] = { /* ROM_A3F8 */ };
static const uint8_t damage_gate[] = { /* ROM_A189 */ };
static const uint8_t last_kong[] = { /* ROM_A1F1 */ };
static const uint8_t barrel_revive[] = { /* ROM_C9D3 */ };
static const uint8_t barrel_pop_finish[] = { /* ROM_93CF */ };
static const uint8_t fall_gate[] = { /* ROM_A6D1 */ };
static const uint8_t life_debit[] = { /* ROM_A85F */ };
static const uint8_t animal_release[] = { /* ROM_C927 */ };
static const uint8_t animal_mount[] = { /* ROM_C906 */ };
static const uint8_t animal_capture[] = { /* ROM_88B4 */ };
static const uint8_t bonus_mount_link[] = { /* ROM_83A8 */ };
static const uint8_t carry_mount_save[] = { /* ROM_9EC2 */ };
static const uint8_t carry_mount_restore[] = { /* ROM_840C */ };
static const uint8_t contact_history[] = { /* ROM_A0D5 */ };
static uint8_t ram[0x20000];
int interp816_opcode_hook(uint32_t pc) { return 0; }
int Dkc1VideoLoadCgramFromRom(struct Ppu *p, unsigned dst, uint8_t bank,
                            uint16_t addr, unsigned words) { return 1; }
void Dkc1VideoCopyCgram(struct Ppu *p, unsigned dst, unsigned src, unsigned words) {}
static uint8_t Read(void *context, uint32_t address) {
  if ((uint16_t)address < 0x2000) return ram[(uint16_t)address];
  if (address >= 0x8083a8 && address < 0x8083a8 + sizeof bonus_mount_link)
    return bonus_mount_link[address - 0x8083a8];
  if (address >= 0xbf9ec2 && address < 0xbf9ec2 + sizeof carry_mount_save)
    return carry_mount_save[address - 0xbf9ec2];
  if (address >= 0xb8840c && address < 0xb8840c + sizeof carry_mount_restore)
    return carry_mount_restore[address - 0xb8840c];
  if (address >= 0xbfa0d5 && address < 0xbfa0d5 + sizeof contact_history)
    return contact_history[address - 0xbfa0d5];
  if (address >= 0xbfa3f8 && address < 0xbfa3f8 + sizeof sink_tail)
    return sink_tail[address - 0xbfa3f8];
  if (address >= 0xbfa189 && address < 0xbfa189 + sizeof damage_gate)
    return damage_gate[address - 0xbfa189];
  if (address >= 0xbfa1f1 && address < 0xbfa1f1 + sizeof last_kong)
    return last_kong[address - 0xbfa1f1];
  if (address >= 0xbfc9d3 && address < 0xbfc9d3 + sizeof barrel_revive)
    return barrel_revive[address - 0xbfc9d3];
  if (address >= 0xbf93cf && address < 0xbf93cf + sizeof barrel_pop_finish)
    return barrel_pop_finish[address - 0xbf93cf];
  if (address >= 0xbfa6d1 && address < 0xbfa6d1 + sizeof fall_gate)
    return fall_gate[address - 0xbfa6d1];
  if (address >= 0xb6a85f && address < 0xb6a85f + sizeof life_debit)
    return life_debit[address - 0xb6a85f];
  if (address >= 0xbfc927 && address < 0xbfc927 + sizeof animal_release)
    return animal_release[address - 0xbfc927];
  if (address >= 0xbfc906 && address < 0xbfc906 + sizeof animal_mount)
    return animal_mount[address - 0xbfc906];
  if (address >= 0xbf88b4 && address < 0xbf88b4 + sizeof animal_capture)
    return animal_capture[address - 0xbf88b4];
  fprintf(stderr, "unexpected ROM read %06x\n", (unsigned)address);
  assert(!"native death/barrel continuation escaped its expected instructions");
  return 0;
}
static void Write(void *context, uint32_t address, uint8_t value) {
  assert((uint16_t)address < 0x2000);
  ram[(uint16_t)address] = value;
}
static void RunTo(Interp816 *in, uint16_t pc) {
  unsigned count = 0;
  while (in->pc != pc && count++ < 64) interp816_runOpcode(in);
  assert(in->pc == pc);
}
int main(void) {
  /* A survivor handoff changes $056F before the next actor call, while
   * the cartridge camera still belongs to the fallen Kong. P2 must keep
   * his own viewport for camera-relative fatal-boundary checks. */
  {
    memset(ram,0,sizeof ram);ResetOverlayState();CoopTestResetSupport();
    s_runtime.enabled=1;
    Dkc1WramSetU16(ram,0x32,9);Dkc1WramSetU16(ram,0x3e,0x6d);
    Dkc1WramSetU16(ram,0x4a,0x7000);Dkc1WramSetU16(ram,0x1b25,0x700);
    Dkc1Actor_SetSpriteIDLo(ram,2,1);Dkc1Actor_SetSpriteIDLo(ram,4,2);
    Dkc1CoopCacheKongSlots(ram,s_runtime.slot);
    Dkc1Actor_SetRAMTable1029Lo(ram,2,0x0d);
    Dkc1Actor_SetSprXPos(ram,4,0xa2);Dkc1Actor_SetSprYPos(ram,4,0x63ef);
    Dkc1WramSetU16(ram,0x56f,1);Dkc1WramSetU16(ram,0x579,0x81);
    Dkc1WramSetU16(ram,0x88b,0x3cb);Dkc1WramSetU16(ram,0x895,0xa7a);
    CoopTestSetSecondCamera(1,0x49,0xb61);
    CpuState damage={0};damage.ram=ram;damage.X=2;
    assert(Dkc1CoopIndependentDamage(&damage));
    CaptureCanonicalContext(ram);
    UseKong(ram,2);
    assert(s_runtime.actor.camera.active);
    assert(Dkc1WramU16(ram,0x88b)==0x49 && Dkc1WramU16(ram,0x895)==0xb61);
    assert(Dkc1Actor_SprXPos(ram,4)==0xa2 && Dkc1Actor_SprYPos(ram,4)==0x63ef);
    RestoreCanonicalContext(ram);
    assert(Dkc1WramU16(ram,0x88b)==0x3cb && Dkc1WramU16(ram,0x895)==0xa7a);
    assert(Dkc1WramU16(ram,0x56f)==2 && Dkc1WramU16(ram,0x1929)==0);
    /* A living Kong waiting in a barrel is a native leader choice, not
     * a survivor handoff. Keep that already-accepted camera path intact. */
    Dkc1Actor_SetRAMTable1029Lo(ram,2,0x0c);
    Dkc1Actor_SetSprXVelocity(ram,2,0);
    UseKong(ram,2);
    assert(!s_runtime.actor.camera.active);
    /* Native shared-view bonus scripts have no second-camera capability. */
    Dkc1Actor_SetRAMTable1029Lo(ram,2,0x0d);
    Dkc1WramSetU16(ram,0x32,1);Dkc1WramSetU16(ram,0x1e15,0x22);
    Dkc1WramSetU16(ram,0x1b23,0x6c00);Dkc1WramSetU16(ram,0x1b25,0x6c00);
    UseKong(ram,2);
    assert(!s_runtime.actor.camera.active);
  }
  for (uint16_t rescued = 2; rescued <= 4; rescued += 2)
  for (unsigned shot = 0; shot <= 1; ++shot) {
    memset(ram, 0, sizeof ram); ResetOverlayState(); s_runtime.enabled = 1;
    Dkc1Actor_SetSpriteIDLo(ram, 2, 1); Dkc1Actor_SetSpriteIDLo(ram, 4, 2);
    Dkc1CoopCacheKongSlots(ram, s_runtime.slot);
    Dkc1WramSetU16(ram, 0x82, rescued);
    Dkc1WramSetU16(ram, 0x84, rescued - 2u);
    Dkc1WramSetU16(ram, 0x579, 1);
    Dkc1Actor_SetRAMTable1029Lo(ram, rescued, 2);
    Dkc1Actor_SetRAMTable1029Lo(ram, rescued ^ 6u, shot ? 0x28 : 0);
    Dkc1Actor_SetSprYVelocity(ram, rescued, 0xfea0);
    Dkc1Actor_SetSprYVelocity(ram, rescued ^ 6u, 0xfc10);
    Dkc1Actor_SetSprXVelocity(ram, rescued ^ 6u, 0x661);
    Interp816 native = {0}; native.read = Read; native.write = Write;
    native.k = 0xbf; native.pc = 0x93cf;
    RunTo(&native, 0x93de);
    assert(native.a == 1);
    assert(Dkc1Actor_SprYVelocity(ram, rescued) == 0);
    CpuState cpu = {0}; cpu.ram = ram; cpu.A = native.a;
    Dkc1CoopFinishBarrelRescue(&cpu);
    assert(cpu.A == (shot ? 0x17 : 1));
    assert(Dkc1Actor_RAMTable1029Lo(ram, rescued) == (shot ? 0x28 : 2));
    assert(Dkc1Actor_SprYVelocity(ram, rescued) == (shot ? 0xfc10 : 0));
    if (shot) assert(Dkc1Actor_SprXVelocity(ram, rescued) == 0x661);
  }
  for (uint16_t type = 9; type <= 12; ++type)
  for (uint16_t rider = 2; rider <= 4; rider += 2) {
    memset(ram, 0, sizeof ram); ResetOverlayState(); s_runtime.enabled = 1;
    Dkc1Actor_SetSpriteIDLo(ram, 2, 1); Dkc1Actor_SetSpriteIDLo(ram, 4, 2);
    Dkc1Actor_SetSpriteIDLo(ram, 6, type);
    Dkc1Actor_SetRAMTable1375Lo(ram, 6, rider);
    Dkc1WramSetU16(ram, 0x512 + rider - 2, 6);
    Dkc1WramSetU16(ram, 0x516, type);
    Dkc1CoopCacheKongSlots(ram, s_runtime.slot);
    Dkc1Actor_SetSprCollisionFlags(ram, rider, 0x0101);
    Dkc1CoopAfterActor(ram);
    Dkc1WramSetU16(ram, 0x82, rider);
    Interp816 native = {0}; native.read = Read; native.write = Write;
    native.k = 0xbf; native.pc = 0xa0d5;
    while (native.pc != 0xa0e3 && native.pc != 0xa0e4) interp816_runOpcode(&native);
    assert(native.pc == 0xa0e3); /* Unchanged contact must not invent a landing edge. */
    assert(Dkc1Actor_SprCollisionFlags(ram, rider) == 0x0101);
  }
  for (uint16_t type = 9; type <= 12; ++type)
  for (uint16_t rider = 2; rider <= 4; rider += 2) {
    memset(ram, 0, sizeof ram); ResetOverlayState(); s_runtime.enabled = 1;
    Dkc1Actor_SetSpriteIDLo(ram, 2, 1); Dkc1Actor_SetSpriteIDLo(ram, 4, 2);
    Dkc1Actor_SetSpriteIDLo(ram, 6, type);
    Dkc1Actor_SetRAMTable1375Lo(ram, 6, rider);
    Dkc1WramSetU16(ram, 0x516, type);
    Dkc1WramSetU16(ram, 0x512 + rider - 2, 6);
    CpuState room = {0}; room.ram = ram; room.Y = rider;
    Dkc1CoopRememberRoomMount(&room);
    Dkc1WramSetU16(ram, 0x512, 0); Dkc1WramSetU16(ram, 0x514, 0);
    /* Native bonus carry targets its current leader even if he was on foot. */
    Interp816 native = {0}; native.read = Read; native.write = Write;
    native.k = 0x80; native.y = rider ^ 6u; native.pc = 0x83a8;
    RunTo(&native, 0x83b0);
    assert(Dkc1WramU16(ram, 0x512 + (rider ^ 6u) - 2) == 2);
    Dkc1WramSetU16(ram, 0x512, 0); Dkc1WramSetU16(ram, 0x514, 0);
    room.Y = rider ^ 6u; Dkc1CoopRestoreRoomMount(&room);
    native.y = room.Y; native.pc = 0x83a8; RunTo(&native, 0x83b0);
    assert(Dkc1WramU16(ram, 0x512 + rider - 2) == 2);
    assert(!Dkc1WramU16(ram, 0x512 + (rider ^ 6u) - 2));
    Dkc1WramSetU16(ram, 0x82, rider); Dkc1WramSetU16(ram, 0x579, 1);
    room.A = 3;
    assert(Dkc1CoopPrepareMountedPartner(&room));

    /* Native saved carry also assumes that the leader owned the animal. */
    Dkc1WramSetU16(ram, 0x512 + rider - 2, 6);
    room.Y = rider ^ 6u; Dkc1CoopSaveCarriedMount(&room);
    native.k = 0xbf; native.pc = 0x9ec2; native.y = room.Y;
    RunTo(&native, 0x9ed0);
    assert(Dkc1WramU16(ram, 0x55f) == 6 && Dkc1WramU16(ram, 0x561) == type);
    Dkc1WramSetU16(ram, 0x512, 0); Dkc1WramSetU16(ram, 0x514, 0);
    room.Y = rider ^ 6u; Dkc1CoopRestoreCarriedMount(&room);
    native.k = 0xb8; native.pc = 0x840c; native.y = room.Y;
    RunTo(&native, 0x841a);
    assert(Dkc1WramU16(ram, 0x512 + rider - 2) == 6);
    assert(!Dkc1WramU16(ram, 0x512 + (rider ^ 6u) - 2));
    room.Y = rider ^ 6u; Dkc1CoopRestoreRoomMount(&room);
    native.k = 0x80; native.pc = 0x83a8; native.y = room.Y;
    RunTo(&native, 0x83b0);
    assert(Dkc1WramU16(ram, 0x512 + rider - 2) == 2);
  }
  for (uint16_t rider=2;rider<=4;rider+=2) {
    memset(ram,0,sizeof ram);ResetOverlayState();s_runtime.enabled=1;
    Dkc1Actor_SetSpriteIDLo(ram,2,1);Dkc1Actor_SetSpriteIDLo(ram,4,2);
    Dkc1Actor_SetSpriteIDLo(ram,6,0x0b);
    Dkc1CoopCacheKongSlots(ram,s_runtime.slot);
    uint16_t owner=rider^6u;
    Dkc1WramSetU16(ram,0x512+owner-2,6);
    Dkc1Actor_SetRAMTable1375Lo(ram,6,owner);
    Dkc1Actor_SetRAMTable1029Lo(ram,owner,0x14);
    Dkc1Actor_SetRAMTable1029Lo(ram,6,1);
    Dkc1WramSetU16(ram,0x82,rider);Dkc1WramSetU16(ram,0x84,rider-2);
    CpuState mount={0};mount.ram=ram;mount.A=6;
    uint8_t before_mount[sizeof ram];memcpy(before_mount,ram,sizeof ram);
    assert(Dkc1CoopAnimalMountOccupied(&mount,rider,6));
    assert(!memcmp(before_mount,ram,sizeof ram));
    /* Bypassing the gate reproduces the exact duplicate-link overwrite. */
    Interp816 native={0};native.read=Read;native.write=Write;
    native.k=native.db=0xbf;native.pc=0x88b4;native.a=6;
    RunTo(&native,0x88d8);
    assert(Dkc1WramU16(ram,0x512)==6 && Dkc1WramU16(ram,0x514)==6);
    assert(Dkc1Actor_RAMTable1375Lo(ram,6)==rider);
    assert(Dkc1Actor_SprEventFlags(ram,6)==8);
  }
  for (int fall = 0; fall < 3; ++fall)
  for (uint16_t hit = 2; hit <= 4; hit += 2) {
    memset(ram, 0, sizeof ram);ResetOverlayState();CoopTestResetSupport();s_runtime.enabled = 1;
    Dkc1WramSetU16(ram, 0x1b25, 0x1400);
    Dkc1Actor_SetSpriteIDLo(ram, 2, 1);Dkc1Actor_SetSpriteIDLo(ram, 4, 2);
    Dkc1CoopCacheKongSlots(ram, s_runtime.slot);
    Dkc1Actor_SetRAMTable1029Lo(ram, hit, 0x0d);
    Dkc1WramSetU16(ram, 0x0579, 1);Dkc1WramSetU16(ram, 0x1929, 3);
    CpuState cpu;memset(&cpu, 0, sizeof cpu);cpu.ram = ram;cpu.X = hit;
    if (fall == 1) {
      Dkc1WramSetU16(ram, 0x82, hit);
      Dkc1WramSetU16(ram, 0x575, 6);
      Dkc1Actor_SetRAMTable1029Lo(ram, hit, 1);
      uint16_t other = hit ^ 6u;
      Dkc1Actor_SetRAMTable1029Lo(ram, other, 0x14);
      Dkc1WramSetU16(ram, 0x0512 + other - 2, 6);
      Dkc1WramSetU16(ram, 0x0516, 9);
      Dkc1Actor_SetSpriteIDLo(ram, 6, 9);
      Dkc1Actor_SetSprEventFlags(ram, 6, 0x10);
      assert(Dkc1CoopIndependentFall(&cpu));
      assert(Dkc1WramU16(ram, 0x575) == 6);
      assert(Dkc1Actor_RAMTable1029Lo(ram, hit) == 0x0d);
      assert(Dkc1Actor_RAMTable1029Lo(ram, other) == 0x14);
      assert(Dkc1WramU16(ram, 0x0512 + other - 2) == 6);
      assert(Dkc1WramU16(ram, 0x0516) == 9);
      assert(Dkc1Actor_SprEventFlags(ram, 6) == 0x10);
      /* Neither an absent/dead partner nor disabled co-op suppresses death. */
      Dkc1WramSetU16(ram, 0x82, other);
      assert(!Dkc1CoopIndependentFall(&cpu));
      Dkc1Actor_SetRAMTable1029Lo(ram, hit, 0);
      s_runtime.enabled = 0; assert(!Dkc1CoopIndependentFall(&cpu)); s_runtime.enabled = 1;
      Dkc1Actor_SetRAMTable1029Lo(ram, hit, 0x0d);
    } else {
      if (fall == 2) {
        Dkc1Actor_SetRAMTable1029Lo(ram, hit, 0x3a);
        /* The real water tail must pop the hit slot before the shared hook. */
        Interp816 sink = {0}; sink.read = Read; sink.write = Write;
        sink.k = sink.db = 0xbf; sink.pc = 0xa3f8; sink.sp = 0x1fd;
        ram[0x1fe] = hit; ram[0x1ff] = 0;
        RunTo(&sink, 0xa3f9);
        assert(sink.x == hit && sink.sp == 0x1ff);
        cpu.X = sink.x;
        RunTo(&sink, 0xa40a);
        assert(Dkc1Actor_RAMTable1029Lo(ram, hit ^ 6u) == 0x0e);
        Dkc1Actor_SetRAMTable1029Lo(ram, hit ^ 6u, 0);
        /* Repeat at the same handoff boundary through the co-op policy. */
      }
      uint16_t other = hit ^ 6u;
      Dkc1Actor_SetSprXPos(ram, other, 0x123);
      Dkc1Actor_SetSprYPos(ram, other, 0x4567);
      if (fall == 0) {
        /* The enemy hits the on-foot Kong while the survivor rides Winky.
         * Stock A1D4 overwrites the survivor's rider state with tag-pull. */
        Dkc1Actor_SetRAMTable1029Lo(ram, other, 0x14);
        Dkc1WramSetU16(ram, 0x0512 + other - 2, 6);
        Dkc1WramSetU16(ram, 0x0516, 0x0b);
        Dkc1Actor_SetSpriteIDLo(ram, 6, 0x0b);
        Dkc1Actor_SetRAMTable1029Lo(ram, 6, 1);
        Dkc1Actor_SetRAMTable1375Lo(ram, 6, other);
        Interp816 native = {0}; native.read = Read; native.write = Write;
        native.k = native.db = 0xbf; native.pc = 0xa1d4; native.x = hit;
        RunTo(&native,0xa1eb);
        assert(Dkc1Actor_RAMTable1029Lo(ram,other) == 0x0e);
        Dkc1Actor_SetRAMTable1029Lo(ram,other,0x14);
      }
      assert(Dkc1CoopIndependentDamage(&cpu));
      assert(Dkc1Actor_SprXPos(ram, other) == 0x123);
      assert(Dkc1Actor_SprYPos(ram, other) == 0x4567);
      assert(Dkc1Actor_RAMTable1029Lo(ram, hit) == (fall == 2 ? 0x3a : 0x0d));
      if (fall == 0) {
        assert(Dkc1Actor_RAMTable1029Lo(ram,other) == 0x14);
        assert(Dkc1WramU16(ram,0x0512 + other - 2) == 6);
        assert(Dkc1Actor_RAMTable1029Lo(ram,6) == 1);
        assert(Dkc1Actor_RAMTable1375Lo(ram,6) == other);
        assert(Dkc1Actor_SprEventFlags(ram,6) == 0);
        /* A hit on a rider must still use native animal damage handling. */
        Dkc1WramSetU16(ram,0x0512 + hit - 2,8);
        Dkc1Actor_SetSpriteIDLo(ram,8,0x0b);
        assert(!Dkc1CoopIndependentDamage(&cpu));
        Dkc1WramSetU16(ram,0x0512 + hit - 2,0);
      }
    }
    uint16_t survivor = hit ^ 6u;
    assert(Dkc1WramU16(ram, 0x056f) == survivor / 2);

    Dkc1WramSetU16(ram, 0x0512, 0);Dkc1WramSetU16(ram, 0x0514, 0);
    Dkc1WramSetU16(ram, 0x0516, 0);
    /* The next hit follows the actual ROM's single-Kong death branch. */
    Interp816 in;memset(&in, 0, sizeof in);
    in.read = Read;in.write = Write;in.k = in.db = 0xbf;in.sp = 0x1ff;
    in.pc = 0xa189;in.x = survivor;
    RunTo(&in, 0xa1f1);
    RunTo(&in, 0xa210);
    assert(Dkc1Actor_RAMTable1029Lo(ram, survivor) == 0x11);

    if (fall == 1) {
      Dkc1Actor_SetRAMTable1029Lo(ram, hit, 0x0d);
      Dkc1Actor_SetRAMTable1029Lo(ram, survivor, 1);
      Dkc1WramSetU16(ram, 0x82, survivor);
      Dkc1WramSetU16(ram, 0x84, survivor - 2);
      assert(!Dkc1CoopIndependentFall(&cpu));
      in.pc = 0xa6d1; in.k = in.db = 0xbf;
      RunTo(&in, 0xa6f8);
      assert(Dkc1Actor_RAMTable1029Lo(ram, survivor) == 0x3b);
      assert(!Dkc1CoopKongCam(ram, survivor));
      assert(!Dkc1CoopKongOwnCamera(ram, survivor));
      assert(Dkc1WramU16(ram, 0x1929) == 1);
      in.pc = 0xa85f; in.k = 0xb6;
      RunTo(&in, 0xa867);
      assert(Dkc1WramU16(ram, 0x575) == 5);
    }
    /* Restore the surviving branch and execute native barrel revival through
     * its health commit, before animation/audio subroutine calls. */
    Dkc1Actor_SetRAMTable1029Lo(ram, survivor, 0);
    Dkc1WramSetU16(ram, 0x82, 6);
    Dkc1Actor_SetSprXPos(ram, 6, 0x456);Dkc1Actor_SetSprYPos(ram, 6, 0x123);
    in.pc = 0xc9d3; in.k = in.db = 0xbf;
    RunTo(&in, 0xca15);
    assert(Dkc1Actor_RAMTable1029Lo(ram, hit) == 0x23);
    assert(Dkc1Actor_RAMTable11A1Lo(ram, hit) == 0xc1);
    assert(Dkc1Actor_SprXPos(ram, hit) == 0x456);
    assert(Dkc1Actor_SprYPos(ram, hit) == 0x123);
    assert((Dkc1WramU16(ram, 0x0579) & 1) == 1);
    assert(Dkc1Actor_RAMTable1029Lo(ram, survivor) == 0);
  }
  /* Fatal mounted falls must close the animal's native rider state, too.
   * Execute the cartridge's release and next mount event, rather than
   * mirroring the host's event write in a model. */
  const uint16_t mounts[] = {9, 11, 12};
  for (unsigned m = 0; m < 3; ++m)
  for (uint16_t fallen = 2; fallen <= 4; fallen += 2) {
    memset(ram, 0, sizeof ram); ResetOverlayState(); CoopTestResetSupport(); s_runtime.enabled = 1;
    Dkc1WramSetU16(ram, 0x1b25, 0x1400);
    Dkc1Actor_SetSpriteIDLo(ram, 2, 1); Dkc1Actor_SetSpriteIDLo(ram, 4, 2);
    Dkc1CoopCacheKongSlots(ram, s_runtime.slot);
    Dkc1Actor_SetSpriteIDLo(ram, 6, mounts[m]);
    Dkc1Actor_SetRAMTable1029Lo(ram, 6, 1);
    Dkc1Actor_SetRAMTable1375Lo(ram, 6, fallen);
    Dkc1WramSetU16(ram, 0x0512 + fallen - 2, 6);
    Dkc1WramSetU16(ram, 0x82, fallen);
    CpuState cpu = {0}; cpu.ram = ram;
    /* A stale link into a reused non-animal slot must not receive events. */
    Dkc1Actor_SetSpriteIDLo(ram, 6, 0x42);
    Dkc1Actor_SetSprEventFlags(ram, 6, 0x10);
    assert(Dkc1CoopIndependentFall(&cpu));
    assert(Dkc1Actor_SprEventFlags(ram, 6) == 0x10);
    Dkc1Actor_SetSpriteIDLo(ram, 6, mounts[m]);
    Dkc1WramSetU16(ram, 0x0512 + fallen - 2, 6);
    /* Last-Kong falls remain owned by the original death continuation. */
    Dkc1Actor_SetRAMTable1029Lo(ram, fallen ^ 6u, 0x0d);
    assert(!Dkc1CoopIndependentFall(&cpu));
    assert(Dkc1Actor_SprEventFlags(ram, 6) == 0x10);
    Dkc1Actor_SetRAMTable1029Lo(ram, fallen ^ 6u, 0);
    assert(Dkc1CoopIndependentFall(&cpu));
    assert(Dkc1Actor_SprEventFlags(ram, 6) == 8);
    assert(Dkc1WramU16(ram, 0x0512 + fallen - 2) == 0);
    Dkc1WramSetU16(ram, 0x82, 6);
    Interp816 in = {0}; in.read = Read; in.write = Write;
    in.k = in.db = 0xbf; in.sp = 0x1ff; in.pc = 0xc927;
    RunTo(&in, mounts[m] == 12 ? 0xc99c : 0xc97e);
    assert(Dkc1Actor_RAMTable1029Lo(ram, 6) == (mounts[m] == 12 ? 3 : 0));
    assert(Dkc1Actor_SprEventFlags(ram, 6) == 0);
    /* Enguarde's free pose uses state 3 and a different mount callback. */
    if (mounts[m] == 12) continue;
    Dkc1Actor_SetSprEventFlags(ram, 6, 8);
    in.pc = 0xc906;
    RunTo(&in, 0xc925);
    assert(Dkc1Actor_RAMTable1029Lo(ram, 6) == 1);
    assert(Dkc1Actor_SprEventFlags(ram, 6) == 0);
  }
  /* A seated survivor has shared camera ownership, but remains alive. */
  for(uint16_t survivor=2;survivor<=4;survivor+=2) {
    memset(ram,0,sizeof ram);ResetOverlayState();s_runtime.enabled=1;
    Dkc1Actor_SetSpriteIDLo(ram,2,1);Dkc1Actor_SetSpriteIDLo(ram,4,2);
    Dkc1CoopCacheKongSlots(ram,s_runtime.slot);
    Dkc1WramSetU16(ram,0x32,5);Dkc1WramSetU16(ram,0x1b25,0x3900);
    Dkc1Actor_SetSpriteIDLo(ram,6,0x51);
    Dkc1Actor_SetRAMTable1375Lo(ram,6,survivor);
    Dkc1Actor_SetRAMTable1029Lo(ram,survivor,0x2e);
    uint16_t fallen=survivor^6u;
    Dkc1Actor_SetRAMTable1029Lo(ram,fallen,0x0d);
    CpuState cpu={0};cpu.ram=ram;Dkc1WramSetU16(ram,0x82,fallen);
    Dkc1WramSetU16(ram,0x575,7);
    assert(!Dkc1CoopKongOwnCamera(ram,survivor));
    assert(Dkc1CoopIndependentFall(&cpu));
    assert(Dkc1WramU16(ram,0x1929)==0 && Dkc1WramU16(ram,0x575)==7);
    assert(Dkc1Actor_RAMTable1029Lo(ram,survivor)==0x2e);
    cpu.X=fallen;
    assert(Dkc1CoopIndependentDamage(&cpu));
    Dkc1Actor_SetSpriteIDLo(ram,6,0x38);
    assert(!Dkc1CoopMinecartForKong(ram,survivor));
    Dkc1Actor_SetRAMTable1029Lo(ram,survivor,0x1d);
    assert(!Dkc1CoopIndependentFall(&cpu));
    assert(!Dkc1CoopIndependentDamage(&cpu));
  }
  return 0;
}
