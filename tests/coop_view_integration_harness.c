/* Compile the entire production view in this TU. Only external cartridge,
 * host-state and PPU boundaries are fixtures. The scroll interpreter, terrain
 * calibration, HDMA reader, effect composition and view rollback are real.
 * Pixels below are a deterministic resource/register probe, not a SNES PPU
 * oracle; native light/fog byte oracles remain in the existing tests. */
#include <assert.h>
#ifndef COOP_VIEW_IMPLEMENTATION
#define COOP_VIEW_IMPLEMENTATION "dkc1_coop_view.c"
#endif
#include COOP_VIEW_IMPLEMENTATION

uint8_t g_ram[0x20000];
static uint8_t fixture_rom[0x400000];
const uint8_t *g_rom = fixture_rom;
static Ppu fixture_ppu;
static Dma fixture_dma;
static Cart fixture_cart;
static Snes fixture_snes;
Ppu *g_ppu = &fixture_ppu;
Dma *g_dma = &fixture_dma;
Snes *g_snes = &fixture_snes;
CpuState g_cpu;
uint64_t g_main_cpu_cycles_estimate, g_apu_pace_cycles_estimate;
uint64_t g_apu_last_sync_cycles, g_apu_last_sync_master;
uint8_t g_snesrecomp_last_hdmaen, g_snes_ppu_dbg_layer_mask;
uint8_t s_coop_ppu_dbg_layer_mask;
int snes_frame_counter;

static unsigned capabilities, preparation_failure, speculative, transaction;
static uint32_t violation;
static unsigned generic_calls, light_calls, raster_calls, decoded_tiles;
static unsigned missing_font;
static uint16_t second_x = 0x180, second_y = 0x60;
static struct Observation {
  unsigned lines;
  uint16_t x, y, background_x, background_y, fixed_color;
  uint8_t screens[2], tables[0x600];
} observed[2];
static uint16_t raster_vram[0x8000], raster_cgram[256];
static uint8_t original_ram[0x20000];
static Ppu original_ppu;
static Dma original_dma;
static CpuState original_cpu;
static uint8_t primary[kMaxWidth * kHeight * 4];

static uint16_t Tile(unsigned x, unsigned y) {
  return (uint16_t)(1u + (x & 63u) + (y & 31u) * 64u);
}

/* Host-owned co-op inputs and metadata. No view helper is replaced. */
int Dkc1CoopEnabled(void) { return 1; }
int Dkc1CoopEntitiesActive(void) { return 1; }
int Dkc1CoopInLevel(const uint8_t *ram) { (void)ram; return 1; }
unsigned Dkc1CoopSceneCapabilities(const uint8_t *ram) {
  (void)ram; return capabilities;
}
int Dkc1CoopEntityIsHandle(uint16_t h) { return h >= 2 && h <= 0x32 && !(h & 1); }
uint16_t Dkc1CoopEntityNextHandle(uint16_t h) { return (uint16_t)(h + 2); }
uint16_t Dkc1CoopEntityRead16(const uint8_t *ram, uint16_t base, uint16_t h) {
  return Dkc1WramU16(ram, base + h);
}
void Dkc1CoopEntityWrite16(uint8_t *ram, uint16_t base, uint16_t h, uint16_t v) {
  Dkc1WramSetU16(ram, base + h, v);
}
int Dkc1CoopEntitiesBeginTransaction(void) { assert(!transaction); transaction = 1; return 1; }
void Dkc1CoopEntitiesEndTransaction(int commit) { assert(transaction && !commit); transaction = 0; }
unsigned Dkc1CoopAnimalBonusOwner(const uint8_t *ram) { (void)ram; return 0; }
uint16_t Dkc1CoopMinecartViewOwner(const uint8_t *ram) { (void)ram; return 0; }
int Dkc1CoopSharedCannonCamera(const uint8_t *ram) { (void)ram; return 0; }
void Dkc1CoopCacheKongSlots(const uint8_t *ram, uint32_t slots[2]) {
  (void)ram; slots[0] = 2; slots[1] = 4;
}
int Dkc1CoopKongCam(const uint8_t *ram, uint32_t slot) { (void)ram; return slot == 2 || slot == 4; }
int Dkc1CoopWorldGetCamera(const uint8_t *ram, int view, uint16_t *x, uint16_t *y) {
  *x = view ? second_x : Dkc1WramU16(ram, 0x88b);
  *y = view ? second_y : Dkc1WramU16(ram, 0x895); return 1;
}
int Dkc1CoopHudBuildOam(const uint8_t *ram, unsigned view, uint8_t *oam, uint8_t *high) {
  (void)ram; (void)view; (void)oam; (void)high; return -1;
}
const Dkc1CoopHudBatch *Dkc1CoopHudBatchForSlot(unsigned slot, unsigned total) {
  (void)slot; (void)total; return NULL;
}
uint16_t Dkc1CoopActorPaletteOwner(const uint8_t *ram, uint16_t h) { (void)ram; return h; }
uint16_t Dkc1CoopActorPaletteSource(const uint8_t *ram, uint16_t h) { (void)ram; (void)h; return 0; }
int Dkc1CoopGraphicsGetPalette(uint16_t h, uint16_t colors[16]) {
  (void)h; for (unsigned i = 0; i < 16; i++) colors[i] = (uint16_t)(0x6600 + i); return 1;
}
int Dkc1CoopGraphicsReadPalette(uint16_t source, uint16_t colors[16]) {
  (void)source; for (unsigned i = 0; i < 16; i++) colors[i] = (uint16_t)(0x1230 + i); return 1;
}
unsigned Dkc1CoopFormationsDrawWide(const uint8_t *ram, const uint8_t *rom,
    uint16_t x, uint16_t y, unsigned left, unsigned right,
    Dkc1CoopFormationEmit emit, void *context) {
  (void)ram; (void)rom; (void)x; (void)y; (void)left; (void)right;
  (void)emit; (void)context; return 0;
}

void RtlSpeculativeExecutionBegin(void) { assert(!speculative); speculative = 1; violation = 0; }
void RtlSpeculativeExecutionEnd(void) { assert(speculative); speculative = 0; }
bool RtlSpeculativeExecutionActive(void) { return speculative != 0; }
uint32_t RtlSpeculativeExecutionViolation(void) { return violation; }
void cpu_write8(CpuState *cpu, uint8_t bank, uint16_t a, uint8_t v) {
  (void)cpu; assert(!bank && a < 0x2000); g_ram[a] = v;
}
int interp816_opcode_hook(uint32_t pc) { (void)pc; return 0; }

/* Cartridge boundary uses only synthetic storage/programs, never game art. */
uint8_t *cart_getRomPtr(Cart *cart, uint8_t bank, uint16_t a) {
  (void)cart;
  if (missing_font && bank == 0xe9 && a >= 0xd6dc + missing_font * 16u) return NULL;
  return fixture_rom + ((((unsigned)bank << 16) | a) & 0x3fffffu);
}
int Dkc1VideoWidth(void) { return 342; }
int Dkc1VideoExtra(void) { return 43; }
uint8_t Dkc1VideoPpuWideLayerMask(uint8_t mode, const uint8_t sc[4], uint8_t main, uint8_t sub) {
  (void)mode; (void)sc; (void)main; (void)sub; return 1;
}
int Dkc1VideoTerrainLayer(uint8_t mask, const uint8_t sc[4], uint16_t hint) {
  (void)mask; (void)sc; (void)hint; return 0;
}
bool Dkc1VideoFindTransparent4bppTile(const uint16_t *vram, size_t words, uint16_t base, uint16_t *tile) {
  (void)vram; (void)words; (void)base; *tile = 0; return true;
}
bool Dkc1VideoDecodeLevelTile(Dkc1LevelLayout layout, uint8_t bank, uint8_t definition,
    uint16_t map, uint16_t metatile, uint32_t x, uint32_t y, uint16_t *tile) {
  (void)layout; (void)bank; (void)definition; (void)map; (void)metatile;
  decoded_tiles++; *tile = Tile(x, y); return true;
}

/* Generated native calls may write scratch WRAM/clocks, just as production
 * preparation may. Distinct table shapes make a wrong owner observable. */
static RecompReturn NativeCall(CpuState *cpu, unsigned which) {
  assert(speculative && cpu != &g_cpu && cpu->D == 0 && cpu->DB == 0x7e);
  g_main_cpu_cycles_estimate += 13; g_apu_pace_cycles_estimate += 17;
  g_apu_last_sync_cycles += 19; g_apu_last_sync_master += 23;
  g_ram[0x12345] ^= 0xa5;
  return preparation_failure == which ? RECOMP_RETURN_SKIP_1 : RECOMP_RETURN_NORMAL;
}
RecompReturn CODE_BBA849_M0X0(CpuState *cpu) { return NativeCall(cpu, 4); }
RecompReturn CODE_BBA772_M0X0(CpuState *cpu) { return NativeCall(cpu, 5); }
RecompReturn CODE_80A7ED_M0X0(CpuState *cpu) { return NativeCall(cpu, 6); }
RecompReturn CODE_80B788_M0X0(CpuState *cpu) { light_calls++; return NativeCall(cpu, 1); }
RecompReturn CODE_80B891_M0X0(CpuState *cpu) {
  RecompReturn r = NativeCall(cpu, 2);
  for (unsigned buffer = 0; buffer < 2; buffer++) {
    unsigned a = 0x10200 + buffer * 0x300;
    g_ram[a] = 1; Dkc1WramSetU16(g_ram, a + 1, (uint16_t)(Read16(0xdd) - Read16(0x88b)));
    g_ram[a + 3] = 0;
  }
  return r;
}
RecompReturn HDMA_BuildParallaxScrollTables_M0X0(CpuState *cpu) {
  generic_calls++;
  g_ram[0x10200] = 1;
  Dkc1WramSetU16(g_ram, 0x10201, (uint16_t)(Read16(0x88b) + 0x11));
  Dkc1WramSetU16(g_ram, 0x10203, (uint16_t)(Read16(0x895) + 0x22));
  g_ram[0x10205] = 0;
  return NativeCall(cpu, 3);
}

/* PPU boundary: retain the values the real view/HDMA code sends, then encode
 * them in pixels. Sprite accumulation samples the installed VRAM/CGRAM. */
void PpuSetExtraSpace(Ppu *ppu, uint8_t extra) { ppu->extraLeftCur = ppu->extraRightCur = extra; ppu->wsLayerRepeat = 0; }
void PpuSetExtraSpaceCentered(Ppu *ppu, uint8_t extra) { PpuSetExtraSpace(ppu, extra); }
void PpuSetWidescreenPresentationXBias(Ppu *ppu, int bias) { (void)ppu; (void)bias; }
void PpuSetWidescreenLayerMask(Ppu *ppu, uint8_t mask) { (void)ppu; (void)mask; }
void PpuSetWidescreenLayerMirror(Ppu *ppu, uint8_t mask) { (void)ppu; (void)mask; }
void PpuSetWidescreenLayerRepeat(Ppu *ppu, uint8_t mask) { ppu->wsLayerRepeat = mask; }
void PpuSetWidescreenLayerClamp(Ppu *ppu, uint8_t mask) { (void)ppu; (void)mask; }
void PpuSetWidescreenBg3Widen(Ppu *ppu, uint8_t line) { (void)ppu; (void)line; }
void Dkc1Private_ppu_write(Ppu *ppu, uint8_t reg, uint8_t value) {
  if (reg >= 0x0d && reg <= 0x14) {
    unsigned plane = (reg - 0x0d) / 2;
    if (reg & 1) {
      ppu->hScroll[plane] = ((value << 8) | (ppu->scrollPrev & 0xf8) | (ppu->scrollPrev2 & 7)) & 0x3ff;
      ppu->scrollPrev2 = value;
    } else ppu->vScroll[plane] = ((value << 8) | ppu->scrollPrev) & 0x3ff;
    ppu->scrollPrev = value;
  }
}
void Dkc1Private_AccumulateSprites(Ppu *ppu, uint16_t *depth, uint16_t *colors) {
  raster_calls++;
  memcpy(raster_vram, ppu->vram, sizeof raster_vram);
  memcpy(raster_cgram, ppu->cgram, sizeof raster_cgram);
  const uint8_t *oam = (const uint8_t *)ppu->oam;
  for (unsigned i = 0; i < 128; i++) {
    if (oam[i * 4 + 1] >= 224) continue;
    unsigned flags = oam[i * 4 + 3], tile = oam[i * 4 + 2];
    int x = oam[i * 4] | ((Dkc1CoopOamHigh(ppu->highOam, i) & 1u) << 8);
    if (x >= 256) x -= 512;
    x += Dkc1VideoExtra();
    if (x < 0 || x >= kPpuBufWidth) continue;
    unsigned a = ((flags & 1 ? PPU_objTileAdr2(ppu) : PPU_objTileAdr1(ppu)) + tile * 16u) * 2u;
    unsigned pixel = oam[i * 4 + 1] * kPpuBufWidth + (unsigned)x;
    colors[pixel] = (uint16_t)(((uint8_t *)ppu->vram)[a] |
        ((ppu->cgram[128u + ((flags >> 1) & 7u) * 16u] & 255u) << 8));
    depth[pixel] = 0x0400;
  }
}
void Dkc1Private_ppu_runLine(Ppu *ppu, int line) {
  unsigned seat = ppu->renderBuffer == s_lower;
  struct Observation *o = &observed[seat]; o->lines++;
  if (!line) {
    o->x = ppu->hScroll[0]; o->y = ppu->vScroll[0];
    memcpy(o->tables, g_ram + 0x10200, sizeof o->tables);
    memcpy(o->screens, ppu->screenEnabled, sizeof o->screens);
    o->fixed_color = ppu->fixedColor;
    /* Observe the actual decoded secondary ring, not just its scroll. */
    unsigned tx = (ppu->hScroll[0] + 256u) >> 3, ty = ppu->vScroll[0] >> 3;
    assert(ppu->vram[TileAddress(0x6000, 1, tx, ty)] == Tile(tx, ty));
    return;
  }
  if (line == 1) { o->background_x = ppu->hScroll[1]; o->background_y = ppu->vScroll[1]; }
  uint8_t *out = ppu->renderBuffer + (line - 1u) * ppu->renderPitch;
  for (unsigned x = 0; x < (unsigned)Dkc1VideoWidth(); x++) {
    unsigned pixel = (line - 1u) * kPpuBufWidth + x;
    if (s_obj_depth[pixel] == 0x0400) {
      out[x * 4] = (uint8_t)s_obj_color[pixel];
      out[x * 4 + 1] = (uint8_t)(s_obj_color[pixel] >> 8); out[x * 4 + 2] = 0xee;
    } else {
      out[x * 4] = (uint8_t)ppu->hScroll[0];
      out[x * 4 + 1] = (uint8_t)ppu->hScroll[1]; out[x * 4 + 2] = (uint8_t)ppu->vScroll[1];
    }
    out[x * 4 + 3] = 0xff;
  }
}

static void Remember(void) {
  memcpy(original_ram, g_ram, sizeof original_ram);
  original_ppu = fixture_ppu; original_dma = fixture_dma; original_cpu = g_cpu;
}
static void Restored(void) {
  assert(!memcmp(g_ram, original_ram, sizeof original_ram));
  assert(!memcmp(&fixture_ppu, &original_ppu, sizeof original_ppu));
  assert(!memcmp(&fixture_dma, &original_dma, sizeof original_dma));
  assert(!memcmp(&g_cpu, &original_cpu, sizeof original_cpu));
  assert(g_main_cpu_cycles_estimate == 101 && g_apu_pace_cycles_estimate == 202);
  assert(g_apu_last_sync_cycles == 303 && g_apu_last_sync_master == 404);
  assert(g_snesrecomp_last_hdmaen == 2 && !speculative && !transaction);
}
static void Reset(uint16_t nmi, uint16_t update) {
  memset(g_ram, 0, sizeof g_ram); memset(fixture_rom, 0, sizeof fixture_rom);
  memset(&fixture_ppu, 0, sizeof fixture_ppu); memset(&fixture_dma, 0, sizeof fixture_dma);
  memset(&g_cpu, 0, sizeof g_cpu); memset(observed, 0, sizeof observed);
  memset(&s_fade, 0, sizeof s_fade); memset(&s_fog, 0, sizeof s_fog);
  capabilities = preparation_failure = speculative = transaction = missing_font = 0;
  generic_calls = light_calls = raster_calls = decoded_tiles = 0;
  second_x = 0x180; second_y = 0x60;
  fixture_cart.rom = fixture_rom; fixture_cart.romSize = sizeof fixture_rom;
  fixture_snes.cart = &fixture_cart;
  fixture_ppu.bgmode = 1; fixture_ppu.bgXsc[0] = 0x61;
  fixture_ppu.hScroll[0] = 0x100; fixture_ppu.vScroll[0] = 0x40;
  fixture_ppu.screenEnabled[0] = 0x13;
  for (unsigned y = 0; y < 32; y++) for (unsigned x = 0; x < 64; x++)
    fixture_ppu.vram[TileAddress(0x6000, 1, x, y)] = Tile(x, y);
  for (unsigned i = 0; i < 256; i++) fixture_ppu.cgram[i] = (uint16_t)(0x5500 + i);
  Dkc1CoopOamClear((uint8_t *)fixture_ppu.oam, fixture_ppu.highOam);
  Dkc1WramSetU16(g_ram, 0x3e, 10); Dkc1WramSetU16(g_ram, 0x32, 1); Dkc1WramSetU16(g_ram, 0x28, 10);
  Dkc1WramSetU16(g_ram, 0x88b, 0x100); Dkc1WramSetU16(g_ram, 0x895, 0x40); g_ram[0xd5] = 0x81;
  Dkc1WramSetU16(fixture_rom, 0xc5bd + 10u * 4u, nmi); Dkc1WramSetU16(fixture_rom, 0xc5bf + 10u * 4u, update);
  /* Synthetic NMI publishes both halves of BG2 scroll through the real bus. */
  const uint8_t program[] = {0xad,0x8b,0x08,0xe2,0x20,0x8d,0x0f,0x21,0xeb,
    0x8d,0x0f,0x21,0xc2,0x20,0xad,0x95,0x08,0xe2,0x20,0x8d,0x10,0x21,
    0xeb,0x8d,0x10,0x21,0xc2,0x20,0x60};
  memcpy(fixture_rom + nmi, program, sizeof program);
  g_dma->channel[1].mode = 3; g_dma->channel[1].bAdr = 0x0f;
  g_dma->channel[1].aBank = 0x7f; g_dma->channel[1].aAdr = 0x200;
  g_snesrecomp_last_hdmaen = 2; g_cpu.S = 0x1ff; g_cpu.A = 0xbeef;
  g_main_cpu_cycles_estimate = 101; g_apu_pace_cycles_estimate = 202;
  g_apu_last_sync_cycles = 303; g_apu_last_sync_master = 404;
  snes_frame_counter = 123; memset(primary, 0x5c, sizeof primary);
  Dkc1CoopViewInvalidate(); Remember();
}
static const uint8_t *Draw(void) {
  Dkc1CoopViewCapture(&fixture_ppu); Dkc1CoopViewDraw(); Restored();
  assert(Dkc1PresentationHeight() == 448);
  return Dkc1PresentationPixels(primary, 342 * 4);
}
static void Pixel(const uint8_t *pixels, unsigned seat, unsigned x, unsigned y,
                  uint8_t a, uint8_t b, uint8_t c) {
  const uint8_t expected[] = {a,b,c,255};
  assert(!memcmp(pixels + ((seat * 224u + y) * 342u + x) * 4u, expected, 4));
}
static void Ready(void) {
  assert(Dkc1CoopViewReady() == 3);
  assert(observed[0].lines == 225 && observed[1].lines == 225);
  assert(observed[0].x == 0x100 && observed[0].y == 0x40);
  assert(observed[1].x == 0x180 && observed[1].y == 0x60);
}

static void Ordinary(void) {
  Reset(0x9000, 0);
  const uint8_t *pixels = Draw(); Ready();
  assert(decoded_tiles > 1000 && generic_calls == 2);
  assert(observed[0].tables[0] == 1 && Dkc1WramU16(observed[0].tables, 1) == 0x111);
  assert(observed[1].tables[0] == 1 && Dkc1WramU16(observed[1].tables, 1) == 0x191);
  Pixel(pixels, 0, 43, 0, 0, 0x11, 0x62); Pixel(pixels, 1, 43, 0, 0x80, 0x91, 0x82);
  static uint8_t prior[sizeof s_composite], state[sizeof s_fade + sizeof s_fog];
  memcpy(prior, pixels, sizeof prior); assert(Dkc1CoopViewSaveState(state, sizeof state));
  memset(observed, 0, sizeof observed); Dkc1CoopViewDraw(); Restored(); Ready();
  assert(!memcmp(prior, Dkc1PresentationPixels(primary, 342 * 4), sizeof prior));
  static uint8_t again[sizeof state]; assert(Dkc1CoopViewSaveState(again, sizeof again));
  assert(!memcmp(state, again, sizeof state));
}

static void FogTables(void) {
  for (unsigned buffer = 0; buffer < 2; buffer++) for (unsigned row = 0; row < 56; row++) {
    unsigned a = 0x10200 + buffer * 0x120u + row * 5u;
    g_ram[a] = 4; Dkc1WramSetU16(g_ram, a + 1, (uint16_t)(0x310 + row)); Dkc1WramSetU16(g_ram, a + 3, (uint16_t)(0x170 - row));
  }
}
static void ShipTables(void) {
  for (unsigned buffer = 0; buffer < 2; buffer++) {
    unsigned a = 0x10200 + buffer * 0x200u;
    g_ram[a] = 1; Dkc1WramSetU16(g_ram, a + 1, 0x155); Dkc1WramSetU16(g_ram, a + 3, 0x77);
  }
  /* Synthetic scroll-only span mirrors X; JMP reaches the exclusive stop.
   * A side-effecting instruction at the stop must never execute. */
  const uint8_t tail[] = {0xad,0x8b,0x08,0x8f,0x01,0x02,0x7f,0x8f,0x01,0x04,0x7f,
    0x4c,0xa2,0x9a};
  memcpy(fixture_rom + 0x9a6a, tail, sizeof tail);
  fixture_rom[0x9aa2] = 0x8d; fixture_rom[0x9aa3] = 0; fixture_rom[0x9aa4] = 0x40;
}
static void Special(void) {
  const uint16_t lights[] = {0x8f9c, 0x9137, 0x9196};
  for (unsigned i = 0; i < 3; i++) {
    Reset(lights[i], 0);
    Dkc1WramSetU16(g_ram, 0xd45 + 6, 0x5a); Dkc1WramSetU16(g_ram, 0xb19 + 6, 0x1c0);
    Dkc1WramSetU16(g_ram, 0xbc1 + 6, 0x198); Dkc1WramSetU16(g_ram, 0x579, 0x200);
    g_dma->channel[1].mode = 2; g_dma->channel[1].bAdr = 0x11;
    g_ram[0x10200] = 1; Dkc1WramSetU16(g_ram, 0x10201, 0x123); Remember();
    const uint8_t *pixels = Draw(); Ready();
    assert(observed[0].tables[0] == 1 && Dkc1WramU16(observed[0].tables, 1) == 0x123);
    assert(observed[1].tables[0] == 1 && Dkc1WramU16(observed[1].tables, 1) == 0x40);
    assert(!generic_calls && light_calls == 1);
    assert(observed[1].screens[0] == (i == 2 ? 0x13 : 4));
    assert(observed[1].screens[1] == (i == 2 ? 4 : 0x13));
    assert(s_view.vScroll[2] == 0x20c);
    if (i == 2) assert(observed[1].fixed_color == 0xc63);
    Pixel(pixels, 1, 43, 0, 0x80, 0x80, 0x60);
  }
  Reset(0x8ffe, 0); FogTables(); Remember();
  const uint8_t *pixels = Draw(); Ready(); assert(!generic_calls && !light_calls);
  assert(observed[0].tables[0] == 4 && Dkc1WramU16(observed[0].tables, 1) == 0x310);
  assert(observed[1].tables[0] == 4 && Dkc1WramU16(observed[1].tables, 1) == 0x3b0);
  assert(Dkc1WramU16(observed[1].tables, 3) == 0x190);
  Pixel(pixels, 0, 43, 0, 0, 0x10, 0x70); Pixel(pixels, 1, 43, 0, 0x80, 0xb0, 0x90);
  Dkc1CoopFogState fog = s_fog;
  Dkc1CoopViewDraw(); Restored(); assert(!memcmp(&fog, &s_fog, sizeof fog));
  Reset(0x9604, 0x9a02); ShipTables(); Remember();
  pixels = Draw(); Ready(); assert(!generic_calls && !light_calls);
  assert(observed[0].tables[0] == 1 && Dkc1WramU16(observed[0].tables, 1) == 0x155);
  assert(observed[1].tables[0] == 1 && Dkc1WramU16(observed[1].tables, 1) == 0x180);
  assert(Dkc1WramU16(observed[1].tables, 0x201) == 0x180);
  Pixel(pixels, 0, 43, 0, 0, 0x55, 0x77); Pixel(pixels, 1, 43, 0, 0x80, 0x80, 0x77);
}

static void Resources(void) {
  Reset(0x9000, 0); Dkc1WramSetU16(g_ram, 0x32, 3); Dkc1WramSetU16(g_ram, 0x1e23, 0x220);
  Dkc1WramSetU16(g_ram, 0x136b5, 0x220); Dkc1WramSetU16(g_ram, 0x136fd, (0x100 + 20) * 4);
  Dkc1WramSetU16(g_ram, 0x13745, (0x40 + 30) * 4);
  const uint8_t record[] = {0,0x94,0x40,0,0,6,0xd3,7};
  memcpy(fixture_rom + 0x358ccd, record, sizeof record);
  fixture_rom[0x358ccd + 19] = 0x88; fixture_rom[0x358ccd + 20] = 0xf2; fixture_rom[0x358ccd + 21] = 0x86;
  memset(fixture_rom + 0x130600, 0xc7, 7 * 32); Remember();
  const uint8_t *pixels = Draw(); Ready(); assert(raster_calls == 2);
  Pixel(pixels, 0, 63, 30, 0xc7, 0x30, 0xee); Pixel(pixels, 1, 63, 30, 0xc7, 0x30, 0xee);
  assert(((uint8_t *)raster_vram)[0x400] == 0xc7 && raster_cgram[144] == 0x1230);
  assert(!memcmp((uint8_t *)s_view.vram + 0x400, (uint8_t *)fixture_ppu.vram + 0x400, 7 * 32));
  assert(!memcmp(s_view.cgram, fixture_ppu.cgram, sizeof s_view.cgram));

  Reset(0x9000, 0); capabilities = kDkc1CoopScenePresentation | kDkc1CoopSceneFixed | kDkc1CoopSceneSingleView;
  Dkc1WramSetU16(g_ram, 0x136bb, 1);
  Dkc1CoopOamWrite((uint8_t *)fixture_ppu.oam, fixture_ppu.highOam, 0, 10, 200, 0x01a0, 0);
  memset(fixture_rom + 0x29d6dc, 0xab, 96 * 16); Remember();
  pixels = Draw(); assert(Dkc1CoopViewReady() == 1 && raster_calls == 1);
  Pixel(pixels, 0, 53, 200, 0xab, 0x80, 0xee);
  assert(((uint8_t *)raster_vram)[0x3400] == 0xab && ((uint8_t *)raster_vram)[0x3410] == 0);
  assert(!memcmp(s_view.vram, fixture_ppu.vram, sizeof s_view.vram));
  assert(!memcmp(s_view.cgram, fixture_ppu.cgram, sizeof s_view.cgram));
  /* Partial font reads unwind both loans before reporting failure. */
  s_view = fixture_ppu; memset(s_view.vram, 0x91, sizeof s_view.vram); memset(s_view.cgram, 0x82, sizeof s_view.cgram);
  static uint16_t vram_before[0x8000], cgram_before[256];
  memcpy(vram_before, s_view.vram, sizeof vram_before); memcpy(cgram_before, s_view.cgram, sizeof cgram_before);
  missing_font = 23; assert(!StampCastText());
  assert(!memcmp(vram_before, s_view.vram, sizeof vram_before));
  assert(!memcmp(cgram_before, s_view.cgram, sizeof cgram_before)); missing_font = 0;

  /* Rope's full64KiB loan is deliberately larger than the other two. */
  s_rope_chr_owner = 6; s_rope_chr_mask = 1; memset(s_rope_chr[0], 0x67, 32);
  Dkc1WramSetU16(s_backup, 0xc69 + 6, 0x0c80);
  ClearOam(); Dkc1CoopOamWrite((uint8_t *)s_view.oam, s_view.highOam, 0, 12, 20, 0x0c80, 1);
  AccumulateWithRopeSheet(1);
  assert(((uint8_t *)raster_vram)[0x1000] == 0x67 && raster_cgram[224] == 0x6600);
  assert(raster_vram[0x7000] == fixture_ppu.vram[0x7000]);
  assert(!memcmp(vram_before, s_view.vram, sizeof vram_before));
  assert(!memcmp(cgram_before, s_view.cgram, sizeof cgram_before));
}

static void Failures(void) {
  for (unsigned failed = 1; failed <= 2; failed++) {
    Reset(0x8f9c, 0); Dkc1WramSetU16(g_ram, 0xd45 + 6, 0x5a);
    Dkc1WramSetU16(g_ram, 0xb19 + 6, 0x1c0); Dkc1WramSetU16(g_ram, 0xbc1 + 6, 0x198);
    preparation_failure = failed; Remember();
    const uint8_t *pixels = Draw();
    assert(Dkc1CoopViewReady() == 1 && !observed[1].lines && !generic_calls);
    for (unsigned i = 342 * 224 * 4; i < 342 * 448 * 4; i++) assert(!pixels[i]);
  }
  Reset(0x8ffe, 0); FogTables(); g_ram[0x10200 + 55 * 5] = 3; Remember();
  const uint8_t *pixels = Draw(); assert(!Dkc1CoopViewReady() && !generic_calls);
  assert(!observed[0].lines && !observed[1].lines);
  assert(!memcmp(pixels, primary, sizeof primary));
  Reset(0x9604, 0x9a02); ShipTables(); fixture_rom[0x9a6a] = 0xdb; Remember();
  Draw(); assert(Dkc1CoopViewReady() == 1 && !observed[1].lines && !generic_calls);
  Reset(0x9000, 0); preparation_failure = 3; Remember();
  pixels = Draw(); assert(!Dkc1CoopViewReady() && !observed[0].lines && !observed[1].lines);
  assert(!memcmp(pixels, primary, sizeof primary));
}

int main(void) {
#ifndef COOP_VIEW_CASE
#define COOP_VIEW_CASE 0
#endif
  if (!COOP_VIEW_CASE || COOP_VIEW_CASE == 1) Ordinary();
  if (!COOP_VIEW_CASE || COOP_VIEW_CASE == 2) Special();
  if (!COOP_VIEW_CASE || COOP_VIEW_CASE == 3) Resources();
  if (!COOP_VIEW_CASE || COOP_VIEW_CASE == 4) Failures();
  puts("compiled production view integration passed"); return 0;
}
