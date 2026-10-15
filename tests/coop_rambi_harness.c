#include "dkc1_coop_mod.c"

static unsigned palette_writes;
static unsigned palette_destinations;
int Dkc1VideoLoadCgramFromRom(struct Ppu *p, unsigned dst, uint8_t bank,
                            uint16_t addr, unsigned words) {
  palette_writes++;
  palette_destinations |= 1u << ((dst - 0x80u) / 16u);
  return 1;
}
void Dkc1VideoCopyCgram(struct Ppu *p, unsigned dst, unsigned src,
                       unsigned words) {
  palette_writes++;
  palette_destinations |= 1u << ((dst - 0x80u) / 16u);
}

static uint8_t ram[0x20000];
static struct Ppu ppu;
static unsigned failures;
#define CHECK(cond, msg) do { if (!(cond)) { puts(msg); failures++; } } while (0)

static void Setup(unsigned rider, unsigned state, unsigned ccc) {
  memset(ram, 0, sizeof ram);
  ResetOverlayState();
  s_runtime.enabled = 1;
  s_runtime.ppu = &ppu;
  Dkc1WramSetU16(ram, 0x1B25, 0x1400);
  Dkc1Actor_SetSpriteIDLo(ram, 2, 1);
  Dkc1Actor_SetSpriteIDLo(ram, 4, 2);
  Dkc1Actor_SetSprFlags(ram, 2, 0x2200);
  Dkc1Actor_SetSprFlags(ram, 4, 0x2400);
  Dkc1Actor_SetRAMTable1029Lo(ram, rider, state);
  Dkc1Actor_SetSprFlags(ram, rider, (uint16_t)(0x2000 | ccc << 9));
  Dkc1Actor_SetSpriteIDLo(ram, 0x12, 9);
  Dkc1WramSetU16(ram, 0x0512 + rider - 2, 0x12);
  Dkc1WramSetU16(ram, 0x0516, 9);
  Dkc1Actor_SetRAMTable1375Lo(ram, 0x12, (uint16_t)rider);
  Dkc1CoopCacheKongSlots(ram, s_runtime.slot);
  palette_writes = 0;
  palette_destinations = 0;
}

int main(void) {
  Setup(4, 0x14, 6);
  CpuState invalid = {0}; invalid.ram = ram; invalid.Y = 2;
  Dkc1CoopSaveCarriedMount(&invalid);
  CHECK(invalid.Y == 4 && s_runtime.carried_mount == 0x0904, "valid carried rider was lost");
  invalid.Y = 2;
  Dkc1WramSetU16(ram, 0x55f, 0x12); Dkc1WramSetU16(ram, 0x561, 11);
  Dkc1CoopRestoreCarriedMount(&invalid);
  CHECK(invalid.Y == 2 && !s_runtime.carried_mount && !s_runtime.room_mount,
        "mismatched animal type restored a stale rider");
  Dkc1WramSetU16(ram, 0x512, 0x12);
  Dkc1CoopSaveCarriedMount(&invalid);
  CHECK(invalid.Y == 2 && !s_runtime.carried_mount, "duplicate claims changed the native carrier");
  Dkc1WramSetU16(ram, 0x512, 0);
  s_runtime.enabled = 0;
  Dkc1CoopSaveCarriedMount(&invalid);
  CHECK(invalid.Y == 2 && !s_runtime.carried_mount, "disabled co-op changed animal carry");
  s_runtime.enabled = 1; invalid.m_flag = 1;
  Dkc1CoopSaveCarriedMount(&invalid);
  CHECK(invalid.Y == 2 && !s_runtime.carried_mount, "unsupported CPU width changed animal carry");
  for (unsigned type = 9; type <= 12; ++type)
  for (unsigned rider = 2; rider <= 4; rider += 2) {
    Setup(rider, 0x14, 6);
    Dkc1Actor_SetSpriteIDLo(ram, 0x12, (uint16_t)type);
    Dkc1WramSetU16(ram, 0x516, (uint16_t)type);
    Dkc1CoopEndFrame(ram);
    CHECK(s_runtime.room_mount == (uint16_t)((type << 8) | rider),
          "scrolling scene failed to preserve reciprocal animal ownership");
    Dkc1WramSetU16(ram, 0x512 + (rider ^ 6) - 2, 0x12);
    Dkc1CoopEndFrame(ram);
    CHECK(!s_runtime.room_mount, "ambiguous animal claims selected a transition rider");
  }
  for (unsigned type = 9; type <= 12; ++type)
  for (unsigned rider = 2; rider <= 4; rider += 2) {
    Setup(rider, 0, 6);
    Dkc1Actor_SetSpriteIDLo(ram, 0x12, (uint16_t)type);
    Dkc1WramSetU16(ram, 0x516, (uint16_t)type);
    CpuState cpu = {0}; cpu.ram = ram; cpu.A = 3;
    cpu.Y = (uint16_t)rider;
    Dkc1CoopRememberRoomMount(&cpu);
    Dkc1CoopContinuation room;
    Dkc1CoopSaveContinuation(&room);
    CHECK(room.version == 5 && room.weather.reserved == (type << 2 | rider / 2),
          "room transition did not remember the actual animal rider");
    s_runtime.room_mount = 0;
    Dkc1CoopLoadContinuation(&room);
    CHECK(s_runtime.room_mount == (type << 8 | rider), "room rider lost across save/load");
    room.version = 4; s_runtime.room_mount = 0;
    Dkc1CoopLoadContinuation(&room);
    CHECK(s_runtime.room_mount == (type << 8 | rider), "v4 animal intent did not remain readable");
    CHECK(Dkc1CoopBeginTransaction(), "room ownership rollback did not begin");
    s_runtime.room_mount = 0;
    Dkc1CoopEndTransaction(0);
    CHECK(s_runtime.room_mount == (type << 8 | rider), "rollback lost pending room rider");
    cpu.Y = (uint16_t)(rider ^ 6);
    Dkc1CoopRestoreRoomMount(&cpu);
    CHECK(cpu.Y == rider && !s_runtime.room_mount,
          "bonus exit assigned the mount to the other Kong");
    cpu.A = 3;
    room.version = 2; room.weather.reserved = 1;
    Dkc1CoopLoadContinuation(&room);
    CHECK(!s_runtime.room_mount && !s_runtime.weather.reserved,
          "withdrawn lighting marker was interpreted as an animal rider");
    room.version = 3; room.weather.reserved = 0x0b06;
    Dkc1CoopLoadContinuation(&room);
    CHECK(!s_runtime.room_mount, "invalid room rider accepted");
    room.weather.reserved = (uint16_t)(type << 8 | rider);
    Dkc1CoopLoadContinuation(&room);
    CHECK(s_runtime.room_mount == room.weather.reserved && !s_runtime.carried_mount,
          "v3 room rider did not remain readable");
    cpu.Y = (uint16_t)(rider ^ 6u);
    Dkc1CoopSaveCarriedMount(&cpu);
    CHECK(cpu.Y == rider && s_runtime.carried_mount == (type << 8 | rider),
          "on-foot initiator failed to carry the actual animal rider");
    s_runtime.room_mount = (uint16_t)(type << 8 | (rider ^ 6u));
    Dkc1CoopSaveContinuation(&room);
    s_runtime.room_mount = s_runtime.carried_mount = 0;
    Dkc1CoopLoadContinuation(&room);
    CHECK(s_runtime.room_mount == (type << 8 | (rider ^ 6u)) &&
          s_runtime.carried_mount == (type << 8 | rider),
          "bonus rider overwrote the separately saved entry rider");
    CHECK(Dkc1CoopBeginTransaction(), "carried ownership rollback did not begin");
    s_runtime.carried_mount = 0;
    Dkc1CoopEndTransaction(0);
    CHECK(s_runtime.carried_mount == (type << 8 | rider), "rollback lost carried rider");
    Dkc1WramSetU16(ram, 0x55f, 0x12);
    Dkc1WramSetU16(ram, 0x561, (uint16_t)type);
    cpu.Y = (uint16_t)(rider ^ 6u);
    Dkc1CoopRestoreCarriedMount(&cpu);
    CHECK(cpu.Y == rider && !s_runtime.carried_mount && s_runtime.room_mount == (type << 8 | rider),
          "saved animal returned to the bonus rider or current leader");
    room.weather.reserved = 0xffff;
    Dkc1CoopLoadContinuation(&room);
    CHECK(s_runtime.room_mount == (type << 8 | rider), "invalid v4 intent modified ownership");
    Dkc1WramSetU16(ram, 0x82, rider);
    Dkc1WramSetU16(ram, 0x579, 1);
    /* Room initialization has cleared the old pool. Its link still records
     * which partner needs a newly allocated mount. */
    Dkc1Actor_SetSpriteIDLo(ram, 0x12, 0);
    CHECK(Dkc1CoopPrepareMountedPartner(&cpu), "mounted follower skipped native rider entry");
    CHECK(cpu.X == rider && cpu.Y == rider - 2 && cpu.A == 3,
          "rider entry lost actor index or entrance animation kind");
    Dkc1Actor_SetSpriteIDLo(ram, 0x12, (uint16_t)type);
    Dkc1Actor_SetRAMTable1029Lo(ram, rider, 0x46);
    Dkc1WramSetU16(ram, 0x56f, (rider ^ 6) / 2);
    Dkc1Actor_SetSprXPos(ram, rider ^ 6, 0x8004);
    Dkc1Actor_SetSprXPos(ram, rider, 0x7fe8);
    Dkc1CoopEntityWrite16(ram, 0x1375, rider ^ 6, 0x8054);
    Dkc1CoopFinishMountedPartner(ram);
    CHECK(Dkc1CoopEntityRead16(ram, 0x1375, rider) == 0x8038,
          "mounted partner lost translated walk-in destination across world $8000");
    Dkc1Actor_SetRAMTable1029Lo(ram, rider, 0x0c);
    cpu.Y = (uint16_t)(rider ^ 6);
    CHECK(Dkc1CoopRestoreMountedExit(&cpu), "mounted partner exit branch was skipped");
    CHECK(Dkc1Actor_RAMTable1029Lo(ram, rider) == 0x14,
          "bonus exit turned mounted partner into a walking Kong");
    Dkc1WramSetU16(ram, 0x82, rider ^ 6u);
    CHECK(Dkc1CoopBlockTeamSwap(&cpu),
          "on-foot Kong exchanged a mounted partner");
    Dkc1WramSetU16(ram, 0x82, rider);
    CHECK(!Dkc1CoopBlockTeamSwap(&cpu), "native rider dismount was blocked");
    Dkc1WramSetU16(ram, 0x579, 0);
    CHECK(!Dkc1CoopPrepareMountedPartner(&cpu), "absent partner acquired a mount");
    Dkc1WramSetU16(ram, 0x579, 1);
    Dkc1WramSetU16(ram, 0x512 + rider - 2, 0);
    CHECK(!Dkc1CoopPrepareMountedPartner(&cpu), "unmounted follower changed entry path");
    s_runtime.enabled = 0;
    CHECK(!Dkc1CoopPrepareMountedPartner(&cpu), "disabled co-op changed stock entry");
  }
  for (uint16_t owner=2; owner<=4; owner+=2) {
    Setup(owner,0x14,6);
    CpuState cpu={0};cpu.ram=ram;
    CHECK(Dkc1CoopAnimalMountOccupied(&cpu,owner^6u,0x12),
          "second Kong stole an occupied animal");
    CHECK(!Dkc1CoopAnimalMountOccupied(&cpu,owner,0x12),
          "owner's native animal path was blocked");
    Dkc1Actor_SetSpriteIDLo(ram,0x10,0x0b);
    CHECK(!Dkc1CoopAnimalMountOccupied(&cpu,owner^6u,0x10),
          "different free animal was blocked");
    s_runtime.enabled=0;
    CHECK(!Dkc1CoopAnimalMountOccupied(&cpu,owner^6u,0x12),
          "disabled mod changed mounting");
    s_runtime.enabled=1;
    /* Historical duplicate links must resolve colors by the recorded rider. */
    Dkc1WramSetU16(ram,0x512+(owner^6u)-2,0x12);
    CHECK(Dkc1CoopActorPaletteOwner(ram,0x12)==owner,
          "duplicate link stole the rider palette");
    CHECK(Dkc1CoopActorPaletteOwner(ram,owner^6u)==(owner^6u),
          "invalid second owner acquired animal colors");
  }
  /* Actual log: jump $15; cave $1F/$20/$24 then entry $46, row 6 -> 3. */
  static const unsigned states[] = {0x14, 0x15, 0x16, 0x1F, 0x20, 0x24, 0x46};
  for (unsigned rider = 2; rider <= 4; rider += 2) {
    for (unsigned i = 0; i < sizeof states / sizeof states[0]; i++) {
      Setup(rider, states[i], i == 6 ? 3 : 6);
      CHECK(AnimalRide(ram), "linked Rambi was lost when the rider changed state");
      CHECK(Dkc1CoopActorPaletteOwner(ram,(uint16_t)rider)==0x12 &&
            Dkc1CoopActorPaletteOwner(ram,0x12)==rider,
            "private renderer did not follow the exchanged animal/rider poses");
      CHECK(Dkc1CoopActorPaletteSource(ram,0x12)==kKongLeaderPalOff[rider==4] &&
            Dkc1CoopActorPaletteSource(ram,(uint16_t)rider)==0,
            "rider and animal inherited each other's colors");
      PresentKongPalettes(ram);
      CHECK(!(palette_destinations & (1u << (i == 6 ? 3 : 6))),
            "Kong colors overwrote the linked animal palette");
      CHECK(palette_writes == 1 &&
            palette_destinations == (1u << (rider == 2 ? 2 : 1)),
            "unmounted partner was left with the cartridge's dim palette");
      CaptureCanonicalContext(ram);
      UseKong(ram, rider == 2 ? 1 : 2);
      CHECK(Dkc1WramU16(ram, 0x0512 + rider - 2) == 0x12 &&
            Dkc1WramU16(ram, 0x0516) == 9,
            "pad mux cleared the native rider link");
      /* Native code clears the link on dismount: restore must not resurrect it. */
      Dkc1WramSetU16(ram, 0x0512 + rider - 2, 0);
      Dkc1WramSetU16(ram, 0x0516, 0);
      Dkc1Actor_SetRAMTable1029Lo(ram, rider, 0);
      RestoreCanonicalContext(ram);
      CHECK(Dkc1WramU16(ram, 0x0512 + rider - 2) == 0 &&
            Dkc1WramU16(ram, 0x0516) == 0,
            "frame restore resurrected a dismounted animal link");
    }
  }
  /* Latest tester tail: DK rides in palette 3; unmounted Diddy uses row 2. */
  Setup(2, 0x14, 3);
  Dkc1Actor_SetSprCollisionFlags(ram, 2, 0x0121);
  Dkc1Actor_SetSprCollisionFlags(ram, 4, 0x0101);
  Dkc1Actor_SetSprCollisionFlags(ram, 0x12, 0x0101);
  s_runtime.actor.mux = 1;
  Dkc1CoopAfterActor(ram);
  CHECK(Dkc1Actor_SprCollisionFlags(ram, 2) == 0x0121 &&
        Dkc1Actor_SprCollisionFlags(ram, 4) == 0x0101 &&
        Dkc1Actor_SprCollisionFlags(ram, 0x12) == 0x0101,
        "co-op changed native contact history after the actor tick");
  CHECK(Dkc1WramU16(ram, 0x0512) == 0x12 && Dkc1WramU16(ram, 0x0516) == 9,
        "actor completion dropped the Rambi link");
  PresentKongPalettes(ram);
  CHECK(palette_writes == 1 && palette_destinations == (1u << 2),
        "standing Diddy stayed gray while Donkey rode Rambi");
  Dkc1Actor_SetRAMTable1029Lo(ram, 2, 0);
  Dkc1WramSetU16(ram, 0x0512, 0);
  Dkc1WramSetU16(ram, 0x0516, 0);
  /* Ghost $0516=9 with no Rambi must not come back, and a rope handle
   * written during the tick must stay. A partner-follow handle still returns. */
  Setup(2, 0, 6);
  Dkc1Actor_SetSpriteIDLo(ram, 0x12, 0);
  Dkc1Actor_SetRAMTable1029Lo(ram, 2, 0);
  Dkc1WramSetU16(ram, 0x0512, 4);
  Dkc1WramSetU16(ram, 0x0514, 0);
  Dkc1WramSetU16(ram, 0x0516, 9);
  Dkc1WramSetU16(ram, 0x0518, 0);
  CaptureCanonicalContext(ram);
  UseKong(ram, 1);
  CHECK(Dkc1WramU16(ram, 0x0512) == 0 && Dkc1WramU16(ram, 0x0516) == 0,
        "stale Rambi id or partner link survived the Kong tick");
  Dkc1WramSetU16(ram, 0x0512, 8);
  RestoreCanonicalContext(ram);
  CHECK(Dkc1WramU16(ram, 0x0512) == 8 && Dkc1WramU16(ram, 0x0516) == 0 &&
            Dkc1WramU16(ram, 0x0514) == 0,
        "rope latch was replaced by the stale Rambi link");
  Dkc1WramSetU16(ram, 0x0512, 0);
  RestoreCanonicalContext(ram);
  CHECK(Dkc1WramU16(ram, 0x0512) == 4,
        "partner-follow handle was dropped after the tick");

  /* Do not overwrite an animal palette even if an unsupported layout aliases it. */
  Setup(2, 0x14, 3);
  Dkc1Actor_SetSprFlags(ram, 4, 0x2600);
  PresentKongPalettes(ram);
  CHECK(palette_writes == 0, "partner refresh overwrote a shared animal palette");

  Setup(4, 0x46, 3);
  Dkc1Actor_SetSpriteIDLo(ram, 0x12, 0x2C);
  CHECK(!AnimalRide(ram), "stale link to a reused enemy slot counted as Rambi");
  CHECK(Dkc1CoopActorPaletteOwner(ram,4)==4 &&
        Dkc1CoopActorPaletteOwner(ram,0x12)==0x12,
        "stale animal link redirected a private palette");
  Setup(4, 0, 2);
  Dkc1WramSetU16(ram, 0x0514, 0xFFFF);
  CHECK(!AnimalRide(ram), "invalid animal slot was accepted");
  Setup(4, 0, 2);
  Dkc1WramSetU16(ram, 0x0514, 0);
  Dkc1WramSetU16(ram, 0x0516, 0);
  PresentKongPalettes(ram);
  CHECK(palette_writes == 2, "unmounted Kong palette refresh stopped working");
  /* Both presentation paths must retain the cartridge's live weather colors. */
  Dkc1WramSetU16(ram, 0x1e15, 0x10);
  for (unsigned seat = 0; seat < 2; ++seat) {
    uint16_t address = (uint16_t)(kKongLeaderPalOff[seat] + 0xa5ad);
    ram[0x10000u + address] = (uint8_t)(0x31 + seat);
    ram[0x10000u + address + 1] = 0x12;
    ApplyKongLeaderCgram(ram, (int)seat);
    unsigned ccc = KongCcc(ram, s_runtime.slot[seat]);
    CHECK(ppu.cgram[0x81 + ccc * 16] == 0x1231 + seat,
          "leader refresh bypassed the weather palette");
  }
  printf("Rambi contract failures: %u\n", failures);
  return failures ? 1 : 0;
}
