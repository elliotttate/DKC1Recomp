/* Exercise the actual opt-in overlay against source-shaped WRAM formations. */
#include "dkc1_coop_mod.c"

int Dkc1VideoLoadCgramFromRom(struct Ppu *p, unsigned dst, uint8_t bank,
                            uint16_t addr, unsigned words) { return 0; }
void Dkc1VideoCopyCgram(struct Ppu *p, unsigned dst, unsigned src,
                       unsigned words) {}

static uint8_t ram[0x20000];
static int failures;
#define CHECK(cond, msg) do { if (!(cond)) { puts(msg); failures++; } } while (0)

static void Setup(void) {
  memset(ram, 0, sizeof ram);
  ResetOverlayState();
  s_runtime.enabled = 1;
  Dkc1WramSetU16(ram, 0x1B25, 0x1400);
  Dkc1WramSetU16(ram, DKC1_WRAM_CameraX, 0x500);
  Dkc1WramSetU16(ram, DKC1_WRAM_CameraY, 0x80);
  ram[0xC4] = 1;
  Dkc1Actor_SetSpriteIDLo(ram, 2, 1);
  Dkc1Actor_SetSpriteIDLo(ram, 4, 2);
  Dkc1Actor_SetSprXPos(ram, 2, 0x541);
  Dkc1Actor_SetSprYPos(ram, 2, 0xEF);
  Dkc1Actor_SetSprXPos(ram, 4, 0x400);
  Dkc1Actor_SetSprYPos(ram, 4, 0x80);
  Dkc1Actor_SetSprCollisionFlags(ram, 2, 0x101);
  Dkc1Actor_SetSprCollisionFlags(ram, 4, 0x101);
  /* Jungle's low row: three bananas, 24 pixels apart, one bit per piece. */
  Dkc1WramSetU16(ram, 0xC040, 0x520);
  Dkc1WramSetU16(ram, 0xC042, 0x100);
  Dkc1WramSetU16(ram, 0xC044, 0xE);
  Dkc1WramSetU16(ram, 0xC046, 0x38);
  Dkc1WramSetU16(ram, 0xC048, 1);
  Dkc1WramSetU16(ram, 0xC04A, 0xD000);
  Dkc1WramSetU16(ram, 0xC04C, 0x30);
  Dkc1WramSetU16(ram, 0xC04E, 0x50);
  Dkc1WramSetU16(ram, 0xC050, 0xFFFF);
  Dkc1WramSetU16(ram, 0xD000, 0x8000);
  Dkc1WramSetU16(ram, 0xD006, 0x8000);
  Dkc1WramSetU16(ram, 0xD00C, 0x8000);
}

static unsigned Pops(void) {
  unsigned n = 0;
  for (unsigned slot = 0x34; slot < 0x74; slot += 2)
    n += Dkc1Actor_SpriteIDLo(ram, slot) == 0x42;
  return n;
}

static int publish = 1;
static void Tick(uint32_t slot, uint16_t left, uint16_t top) {
  Dkc1CoopBeginFrame(ram, 0);
  Dkc1CoopBeforeActor(ram, slot, 0);
  /* Native $BF8711 publishes the current Kong's pose bounds during its tick. */
  Dkc1WramSetU16(ram, 0xC5, left);
  Dkc1WramSetU16(ram, 0xC7, top);
  Dkc1WramSetU16(ram, 0xC9, 18);
  Dkc1WramSetU16(ram, 0xCB, 18);
  if (publish) {
    CpuState cpu = {0}; cpu.ram = ram;
    Dkc1WramSetU16(ram, 0x82, (uint16_t)slot);
    Dkc1CoopPublishPickupBounds(&cpu);
  }
  Dkc1CoopAfterActor(ram);
}

int main(void) {
  Setup();
  Tick(2, 0x538, 0xF0);
  CHECK(Dkc1WramU16(ram, 0xC040) == 0x520, "pickup moved the entire formation");
  CHECK(Dkc1WramU16(ram, 0xC04C) == 0x30, "pickup changed a traversal link");
  CHECK(Dkc1WramU16(ram, 0xD000) == 0x8000, "left neighbour was collected");
  CHECK(Dkc1WramU16(ram, 0xD006) == 0, "touched middle banana was not consumed");
  CHECK(Dkc1WramU16(ram, 0xD00C) == 0x8000, "right neighbour was collected");
  CHECK(Pops() == 1, "one contact must create one flight");
  CHECK(Dkc1Actor_SprXPos(ram, 0x34) == 0x3800 &&
        Dkc1Actor_SprYPos(ram, 0x34) == 0x8000,
        "flight must start at the selected piece, before its first motion tick");
  CHECK(Dkc1WramU16(ram, DKC1_WRAM_BananaCount) == 0, "credit happened before flight completion");
  for (int i = 0; i < 40; i++) Tick(2, 0x538, 0xF0);
  CHECK(Pops() == 1, "standing still collected the same banana twice");
  Dkc1Actor_SetSprXPos(ram, 4, 0x541);
  Tick(4, 0x538, 0xF0);
  CHECK(Pops() == 1, "second Kong collected an already consumed banana");
  Tick(2, 0x520, 0xF0);
  CHECK(Dkc1WramU16(ram, 0xD000) == 0 && Dkc1WramU16(ram, 0xD00C) == 0x8000,
        "moving to the left banana affected the right banana");
  Tick(2, 0x550, 0xF0);
  CHECK(Pops() == 3 && Dkc1WramU16(ram, 0xD00C) == 0, "three separate contacts must produce three flights");

  Setup();
  Dkc1Actor_SetSprXPos(ram, 2, 0x554);
  Dkc1WramSetU16(ram, 0xD006, 0);
  Dkc1WramSetU16(ram, 0xD00C, 0);
  Tick(2, 0x550, 0xF0);
  CHECK(Pops() == 0 && Dkc1WramU16(ram, 0xD000) == 0x8000, "52-pixel bias collected a distant banana");

  Setup();
  Dkc1Actor_SetRAMTable1029Lo(ram, 4, 0xC);
  Tick(2, 0x538, 0xF0);
  CHECK(Pops() == 0 && Dkc1WramU16(ram, 0xD006) == 0x8000, "one-Kong play must use the original collector");

  Setup();
  Dkc1WramSetU16(ram, 0xC044, 0xF);
  Tick(2, 0x538, 0xF0);
  CHECK(Pops() == 0, "malformed formation did not fail closed");

  Setup();
  for (unsigned slot = 0x34; slot < 0x74; slot += 2)
    Dkc1Actor_SetSpriteIDLo(ram, slot, 0x42);
  Tick(2, 0x538, 0xF0);
  CHECK(Dkc1WramU16(ram, 0xD006) == 0x8000 &&
        Dkc1WramU16(ram, DKC1_WRAM_BananaCount) == 0,
        "full flight pool silently consumed or credited a banana");

  Setup();
  Dkc1WramSetU16(ram, DKC1_WRAM_CameraX, 0x600);
  Tick(2, 0x538, 0xF0);
  CHECK(Pops() == 0 && Dkc1WramU16(ram, 0xD006) == 0x8000,
        "offscreen piece was clamped into a visible flight");

  Setup();
  ram[0xC4] = 0;
  Tick(2, 0x538, 0xF0);
  CHECK(Pops() == 0, "inactive formation collision phase collected a banana");

  Setup();
  Dkc1Actor_SetRAMTable1029Lo(ram, 2, 0x26);
  Dkc1Actor_SetSprCollisionFlags(ram, 2, 0);
  Tick(2, 0x538, 0xF0);
  CHECK(Pops() == 1 && Dkc1WramU16(ram, 0xD006) == 0,
        "rope swing with collision word 0 missed the published box");

  Setup();
  Dkc1Actor_SetRAMTable1029Lo(ram, 4, 0x30);
  Dkc1Actor_SetSprCollisionFlags(ram, 4, 0);
  Dkc1Actor_SetSpriteIDLo(ram, 0x12, 0x0C);
  Dkc1WramSetU16(ram, 0x0514, 0x12);
  Dkc1Actor_SetSprXPos(ram, 4, 0x541);
  Tick(4, 0x538, 0xF0);
  CHECK(Pops() == 1 && Dkc1WramU16(ram, 0xD006) == 0,
        "mounted partner missed the published box");

  /* Mounted jumping uses $15 with collision word zero, not idle $30.
   * Exercise both seats and the native shared mount protocol. */
  const uint16_t animals[] = {0x09,0x0b,0x0c};
  for (unsigned seat=0;seat<2;seat++) for (unsigned a=0;a<3;a++) {
    Setup();
    unsigned kong=2+seat*2;
    Dkc1Actor_SetSprXPos(ram, kong, 0x541);
    Dkc1Actor_SetRAMTable1029Lo(ram, kong, 0x15);
    Dkc1Actor_SetSprCollisionFlags(ram, kong, 0);
    Dkc1Actor_SetSpriteIDLo(ram, 0x12, animals[a]);
    Dkc1WramSetU16(ram, 0x0512+seat*2, 0x12);
    Tick(kong, 0x538, 0xF0);
    CHECK(Pops()==1 && Dkc1WramU16(ram,0xD006)==0 &&
          Dkc1WramU16(ram,0xD000)==0x8000 && Dkc1WramU16(ram,0xD00C)==0x8000,
          "mounted jump missed its banana or consumed a neighbour");
  }
  Setup();
  Dkc1Actor_SetRAMTable1029Lo(ram,4,0x15);
  Dkc1Actor_SetSprCollisionFlags(ram,4,0);
  publish=0; Tick(4,0x538,0xF0); publish=1;
  CHECK(Pops()==0,"unlinked $15 consumed a stale formation box");

  Setup();
  Dkc1Actor_SetRAMTable1029Lo(ram, 4, 0x30);
  Dkc1Actor_SetSprCollisionFlags(ram, 4, 0);
  publish=0; Tick(4, 0x538, 0xF0); publish=1;
  CHECK(Pops() == 0 && Dkc1WramU16(ram, 0xD006) == 0x8000,
        "state $0030 without a mount used a stale box");

  /* A dying partner still ticks and replaces the shared native rectangle.
   * The survivor must collect before that happens, for either identity. */
  for (uint16_t survivor=2;survivor<=4;survivor+=2) {
    Setup();
    Dkc1Actor_SetRAMTable1029Lo(ram,survivor^6u,0x0d);
    Tick(survivor,0x538,0xf0);
    CHECK(Pops()==1 && Dkc1WramU16(ram,0xD006)==0 &&
          Dkc1WramU16(ram,0xD000)==0x8000 && Dkc1WramU16(ram,0xD00C)==0x8000,
          "survivor lost his banana rectangle to the dead partner");
    CHECK(Dkc1WramU16(ram,DKC1_WRAM_BananaCount)==0,
          "survivor awarded before native flight completion");
    Tick(survivor^6u,0x800,0x300);
    Tick(survivor,0x538,0xf0);
    CHECK(Pops()==1,"dead partner or repeated contact duplicated pickup");
  }

  /* A sole Diddy still needs his camera for the pickup flight. Native
   * $97F2 publishes bounds with collision zero for a minecart passenger. */
  Setup();
  Dkc1WramSetU16(ram,0x32,5);
  Dkc1Actor_SetRAMTable1029Lo(ram,2,0x0c);
  Dkc1Actor_SetRAMTable1029Lo(ram,4,0x2e);
  Dkc1Actor_SetSprCollisionFlags(ram,4,0);
  Dkc1Actor_SetSpriteIDLo(ram,6,0x51);
  Dkc1Actor_SetRAMTable1375Lo(ram,6,4);
  CoopTestSetSecondCamera(1,0x520,0x80);
  Tick(4,0x538,0xf0);
  CHECK(Pops()==1 && Dkc1WramU16(ram,0xD006)==0 &&
        Dkc1WramU16(ram,0xD000)==0x8000 && Dkc1WramU16(ram,0xD00C)==0x8000,
        "sole minecart Diddy missed his banana or collected neighbours");
  CHECK(Dkc1Actor_SprXPos(ram,0x34)==0x1800 &&
        Dkc1Actor_SprYPos(ram,0x34)==0x8000,
        "sole Diddy's pickup flight used the primary camera");
  CHECK(Dkc1WramU16(ram,DKC1_WRAM_CameraX)==0x500 &&
        Dkc1WramU16(ram,DKC1_WRAM_BananaCount)==0,
        "minecart pickup moved the primary camera or awarded early");
  Setup();
  Dkc1WramSetU16(ram,0x32,5);
  Dkc1Actor_SetRAMTable1029Lo(ram,4,0x2e);
  Dkc1Actor_SetSprCollisionFlags(ram,4,0);
  publish=0; Tick(4,0x538,0xf0); publish=1;
  CHECK(Pops()==0,"unbound minecart pose consumed a stale box");

  /* Native BB4B/BBB4 floor (leading gap - 8) / 8: 15 touches, 16 does not. */
  for (unsigned axis=0;axis<2;axis++) for (unsigned gap=15;gap<=16;gap++) {
    Setup();
    Dkc1WramSetU16(ram,0xD000,0);
    Dkc1WramSetU16(ram,0xD00C,0);
    Tick(2,0x538+(axis?0:gap),0x100+(axis?gap:0));
    CHECK(Pops()==(gap==15),"pickup disagrees with native leading grid rounding");
  }

  Setup();
  Dkc1Actor_SetRAMTable1029Lo(ram,2,0x53);
  CoopTestSetSecondCamera(1,0x520,0x80);
  Tick(4,0x538,0xf0);
  CHECK(Pops()==1 && Dkc1Actor_SprXPos(ram,0x34)==0x1800,
        "sole on-foot Diddy used the primary pickup camera");

  /* BF97D6 ($1A, carrying jump/toss) reaches BF858B -> BF870D even
   * with collision bit 0 clear. Its box belongs to this playable Kong. */
  for (unsigned slot=2;slot<=4;slot+=2) {
    Setup();
    Dkc1Actor_SetRAMTable1029Lo(ram,slot,0x1a);
    Dkc1Actor_SetSprCollisionFlags(ram,slot,0);
    Tick(slot,0x538,0xf0);
    CHECK(Pops()==1 && Dkc1WramU16(ram,0xD006)==0,
          "carrying jump missed its banana");
    CHECK(Dkc1WramU16(ram,0xD000)==0x8000 &&
          Dkc1WramU16(ram,0xD00C)==0x8000,
          "carrying jump consumed neighbouring bananas");
    CHECK(Dkc1WramU16(ram,DKC1_WRAM_BananaCount)==0,
          "carrying jump awarded before flight completion");
  }

  Setup();
  Dkc1Actor_SetRAMTable1029Lo(ram,4,0x1f); /* no host pose whitelist */
  Dkc1Actor_SetSprCollisionFlags(ram,4,0);
  Tick(4,0x538,0xf0);
  CHECK(Pops()==1,"a playable native publisher was rejected by its pose");
  Setup();publish=0;Tick(2,0x538,0xf0);publish=1;
  CHECK(Pops()==0,"actor completion consumed another actor's stale bounds");
  Setup();
  Dkc1CoopBeforeActor(ram,2,0);
  Dkc1WramSetU16(ram,0xc5,0x538);Dkc1WramSetU16(ram,0xc7,0xf0);
  Dkc1WramSetU16(ram,0xc9,18);Dkc1WramSetU16(ram,0xcb,18);
  CpuState wrong={0};wrong.ram=ram;Dkc1WramSetU16(ram,0x82,4);
  Dkc1CoopPublishPickupBounds(&wrong);Dkc1CoopAfterActor(ram);
  CHECK(Pops()==0,"mismatched native publisher consumed a formation");

  Setup();
  s_runtime.enabled = 0;
  uint8_t original[sizeof ram];
  memcpy(original, ram, sizeof ram);
  Dkc1CoopBeginFrame(ram, 0);
  Dkc1CoopBeforeActor(ram, 2, 0);
  Dkc1CoopAfterActor(ram);
  Dkc1CoopEndFrame(ram);
  CHECK(!memcmp(original, ram, sizeof ram), "disabled overlay modified WRAM");
  printf("banana contract failures: %d\n", failures);
  return failures ? 1 : 0;
}
