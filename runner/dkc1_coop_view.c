#include "dkc1_coop_oam.h"
#include "dkc1_coop_view.h"
#include "dkc1_game.h"
#include "dkc1_coop_mod.h"
#include "dkc1_coop_decision.h"
#include "dkc1_coop_movement.h"
#include "dkc1_coop_world.h"
#include "dkc1_coop_entities.h"
#include "dkc1_coop_graphics.h"
#include "dkc1_coop_formations.h"
#include "dkc1_coop_hud.h"
#include "dkc1_coop_light.h"
#include "dkc1_coop_scroll.h"
#include "dkc1_coop_fog.h"
#include "dkc1_coop_background.h"
#include "dkc1_coop_raster.h"
#include "dkc1_video.h"
#include "dkc1_wram_gen.h"
#include "common_rtl.h"
#include "dkc1_coop_clock.h"
#include "cpu_state.h"
#include "snes/ppu.h"
#include "snes/dma.h"
#include "snes/cart.h"
#include <string.h>
#include <stdlib.h>

extern RecompReturn CODE_BBA849_M0X0(CpuState *cpu);
extern RecompReturn CODE_BBA772_M0X0(CpuState *cpu);
extern RecompReturn CODE_80A7ED_M0X0(CpuState *cpu);
extern RecompReturn CODE_80B788_M0X0(CpuState *cpu);
extern RecompReturn CODE_80B891_M0X0(CpuState *cpu);
extern RecompReturn HDMA_BuildParallaxScrollTables_M0X0(CpuState *cpu);
extern void Dkc1Private_ppu_runLine(Ppu *ppu, int line);
extern void Dkc1Private_ppu_write(Ppu *ppu, uint8_t reg, uint8_t value);
extern void Dkc1Private_AccumulateSprites(Ppu *ppu, uint16_t *depth,
                                         uint16_t *colors);
extern uint8_t s_coop_ppu_dbg_layer_mask;

enum { kHeight = 224, kMaxWidth = kDkc1VideoWidescreenWidth };
static Ppu s_seed, s_view;
static uint8_t s_backup[0x20000];
enum { kFadeFrames = 30 };
typedef struct CoopViewFade {
  uint32_t version, width, entrance, frame;
  uint8_t valid, hidden, fade[2];
  uint8_t pixels[2][kMaxWidth * kHeight * 4];
} CoopViewFade;
static CoopViewFade s_fade;
static Dkc1CoopFogState s_fog;
static Dkc1CoopScrollBus s_fog_bus;
#define s_upper s_fade.pixels[0]
#define s_lower s_fade.pixels[1]

size_t Dkc1CoopViewStateSize(void) { return sizeof s_fade + sizeof s_fog; }
int Dkc1CoopViewSaveState(void *data, size_t size) {
  if (!data || size != Dkc1CoopViewStateSize()) return 0;
  memcpy(data, &s_fade, sizeof s_fade);
  memcpy((uint8_t *)data + sizeof s_fade, &s_fog, sizeof s_fog);
  return 1;
}
int Dkc1CoopViewLoadState(const void *data, size_t size) {
  memset(&s_fade, 0, sizeof s_fade);
  memset(&s_fog, 0, sizeof s_fog);
  if (!data || (size != sizeof s_fade && size != Dkc1CoopViewStateSize())) return 0;
  const CoopViewFade *state = (const CoopViewFade *)data;
  if (state->version > 1 || state->width > kMaxWidth ||
      state->valid > 3 || state->hidden > 3 ||
      state->fade[0] > kFadeFrames || state->fade[1] > kFadeFrames) return 0;
  if (size == Dkc1CoopViewStateSize()) {
    Dkc1CoopFogState fog;
    memcpy(&fog, (const uint8_t *)data + sizeof s_fade, sizeof fog);
    if (!Dkc1CoopFogStateValid(&fog)) return 0;
    s_fog = fog;
  }
  memcpy(&s_fade, state, sizeof s_fade);
  return 1;
}
static uint8_t s_composite[kMaxWidth * kHeight * 8];
static uint16_t s_obj_depth[kHeight * kPpuBufWidth];
static uint16_t s_obj_color[kHeight * kPpuBufWidth];
static int s_seed_valid, s_ready, s_width, s_hidden;
/* Effects project for the displayed seat, independently of scratch surface. */
static int s_projection_seat = -1;
static uint16_t s_native_cam_x, s_native_cam_y;
/* Sprite $31's link sheet. The draw queue also appends later actors that
 * reuse tiles $80..$9F, and an idle rope does not upload again, so those
 * slots keep the other actor's picture. The first $D6 record is the sheet
 * the cartridge stores for this pose. */
static uint8_t s_rope_chr[0x20][32];
static uint32_t s_rope_chr_mask;
static uint16_t s_rope_chr_owner;
/* Handles whose angled chain was refused because the hook is above this
 * camera. The links further down can still cross the other view. */
static uint8_t s_rope_culled[32];
static Dkc1CoopClockSnapshot s_clock;

bool Dkc1CoopViewEvaluateSprites(Ppu *ppu, int line) {
  if ((unsigned)line >= kHeight) return false;
  memcpy(ppu->objBuffer.data, s_obj_depth + line * kPpuBufWidth,
         sizeof ppu->objBuffer.data);
  return true;
}
uint16_t Dkc1CoopViewColor(Ppu *ppu, uint16_t pixel, unsigned x, unsigned y) {
  unsigned layer = (pixel >> 8) & 15u;
  if ((layer == 4 || layer == 6) && x < kPpuBufWidth && y < kHeight)
    return s_obj_color[y * kPpuBufWidth + x];
  return ppu->cgram[pixel & 255u];
}

static uint16_t Read16(unsigned a) {
  return Dkc1WramU16(g_ram, a);
}
static void Write16(unsigned a, uint16_t value) {
  Dkc1WramSetU16(g_ram, a, value);
}
static uint16_t TileAddress(uint16_t base, uint8_t size,
                            uint32_t x, uint32_t y) {
  return (uint16_t)((base + ((y & 31u) << 5) + (x & 31u) +
                     ((size & 1u) && (x & 32u) ? 0x400u : 0u) +
                     ((size & 2u) && (y & 32u) ?
                         (size & 1u ? 0x800u : 0x400u) : 0u)) & 0x7fffu);
}

/* Scroll aliases repeat with this plane's tilemap, not the PPU register's
 * ten-bit range. Keep the ring origin near the native world camera so an
 * alias cannot become an offset into a different authored terrain row. */
static uint32_t UnwrapTilemapScroll(uint16_t scroll, uint32_t anchor,
                                   unsigned period) {
  uint32_t origin = (anchor & ~(period - 1u)) | (scroll & (period - 1u));
  if (origin + period / 2u < anchor) origin += period;
  else if (origin > anchor + period / 2u && origin >= period) origin -= period;
  return origin;
}

/* The minecart NMI ($8090C3) scrolls BG2 with BG1, but BG2 is not
 * reconstructed from the terrain layout. Its spare ring columns can hold
 * unrelated art even while the native viewport contains a valid platform.
 * Keep this unproven overlay continuation inside native bounds; BG1 terrain
 * and the authored BG3 background remain wide. */
static int MineOverlayNeedsNativeBounds(void) {
  unsigned descriptor = 0xc5bdu + (unsigned)Read16(0x3e) * 4u;
  if (descriptor > 0xfffeu || (s_seed.bgmode & 7u) != 1u ||
      PPU_bigTiles((&s_seed), 1)) return 0;
  const uint8_t *p = cart_getRomPtr(g_snes->cart, 0x80, (uint16_t)descriptor);
  return p && p[0] == 0xc3 && p[1] == 0x90;
}

/* Calibrate against the cartridge image, then build the second ring from the
 * accepted ROM layout. Overlapping live tiles keep game-authored changes. */
static int PrepareTerrain(int independent, uint16_t x1, uint16_t y1,
                           uint16_t x2, uint16_t y2) {
  const Ppu *seed = &s_seed;
  uint8_t mask = Dkc1VideoPpuWideLayerMask(s_seed.bgmode, s_seed.bgXsc,
      s_seed.screenEnabled[0], s_seed.screenEnabled[1]);
  int layer = Dkc1VideoTerrainLayer(mask, s_seed.bgXsc, Read16(0x1b13));
  if (layer < 0 || PPU_bigTiles(seed, layer)) return 0;
  uint16_t base = (uint16_t)PPU_bgTilemapAdr(seed, layer);
  uint8_t map_size = s_seed.bgXsc[layer] & 3u;
  uint8_t bank = g_ram[0xd5], definition = 0;
  uint16_t map = Read16(0xd3), metatile = Read16(0x1b11);
  uint32_t wx = UnwrapTilemapScroll(s_seed.hScroll[layer], x1,
                                  map_size & 1u ? 512u : 256u);
  uint32_t wy = UnwrapTilemapScroll(s_seed.vScroll[layer], y1,
                                  map_size & 2u ? 512u : 256u);
  Dkc1LevelLayout layout = kDkc1LayoutUnknown;
  int best = 0, decoded_count = 0, offset_x = 0, offset_y = 0;
  for (int candidate = kDkc1LayoutHorizontal;
       candidate <= kDkc1LayoutVertical; candidate++) {
    uint8_t banks[2] = {
      candidate == kDkc1LayoutHorizontal ? bank : 0x81u, g_ram[0xd6]};
    for (int b = 0; b < 2; b++) {
      if (!banks[b] || (b && banks[b] == banks[0])) continue;
      for (int anchor = 0; anchor < 2; anchor++) {
        uint32_t ax = anchor ? x1 : wx, ay = anchor ? y1 : wy;
        int matched = 0, decoded = 0;
        for (unsigned r = 0; r < 28; r += 2) {
          for (unsigned c = 0; c < 32; c += 2) {
            uint32_t tx = (ax >> 3) + c, ty = (ay >> 3) + r;
            uint16_t tile;
            if (Dkc1VideoDecodeLevelTile((Dkc1LevelLayout)candidate, bank,
                  banks[b], map, metatile, tx, ty, &tile)) {
              decoded++;
              matched += tile == s_seed.vram[TileAddress(base, map_size, tx, ty)];
            }
          }
        }
        if (matched > best) {
          int px = (int)ax - (int)wx, py = (int)ay - (int)wy;
          best = matched; decoded_count = decoded;
          layout = (Dkc1LevelLayout)candidate; definition = banks[b];
          offset_x = px >= 0 ? (px + 3) / 8 : (px - 3) / 8;
          offset_y = py >= 0 ? (py + 3) / 8 : (py - 3) / 8;
        }
      }
    }
  }
  if (decoded_count < 64 || best * 10 < decoded_count * 7) return 0;
  /* The native PPU scroll can still belong to the previous frame. Applying
   * a current-camera delta to it leaks P1's movement into a stationary P2
   * view. Project P2 from the actual ring origin; keep P1's native phase. */
  int dx = independent ? (int)x2 - (int)wx : 0;
  int dy = independent ? (int)y2 - (int)wy : 0;
  int ox = (int)wx + dx, oy = (int)wy + dy;
  s_view.hScroll[layer] = (uint16_t)(s_seed.hScroll[layer] + dx);
  s_view.vScroll[layer] = (uint16_t)(s_seed.vScroll[layer] + dy);
  uint16_t blank = 0;
  if (!Dkc1VideoFindTransparent4bppTile(s_seed.vram, 0x8000,
          (uint16_t)PPU_bgTileAdr(seed, layer), &blank)) return 0;
  /* Populate precisely the visible ring and its fine-scroll guard. No row
   * from the other camera can wrap into an uninitialized secondary column. */
  int first_x = (ox - Dkc1VideoExtra() - 8) >> 3;
  int last_x = (ox + 256 + Dkc1VideoExtra() + 8) >> 3;
  int first_y = oy >> 3, last_y = (oy + 231) >> 3;
  for (int ty = first_y; ty <= last_y; ty++) {
    for (int tx = first_x; tx <= last_x; tx++) {
      uint16_t tile = blank;
      if (tx >= 0 && ty >= 0) {
        if ((unsigned)tx >= (wx >> 3) &&
            (unsigned)tx <= ((wx + 255) >> 3) &&
            (unsigned)ty >= (wy >> 3) &&
            (unsigned)ty <= ((wy + 223) >> 3)) {
          tile = s_seed.vram[TileAddress(base, map_size, tx, ty)];
        } else if (tx + offset_x >= 0 && ty + offset_y >= 0) {
          Dkc1VideoDecodeLevelTile(layout, bank, definition, map, metatile,
              (uint32_t)(tx + offset_x), (uint32_t)(ty + offset_y), &tile);
        }
      }
      s_view.vram[TileAddress(base, map_size, (uint32_t)tx, (uint32_t)ty)] = tile;
    }
  }
  PpuSetExtraSpace(&s_view, (uint8_t)Dkc1VideoExtra());
  PpuSetWidescreenPresentationXBias(&s_view, 0);
  PpuSetWidescreenLayerMask(&s_view, mask);
  PpuSetWidescreenLayerMirror(&s_view, 0);
  /* PpuSetExtraSpace clears layer policies, including the repeat mask copied
   * from the primary renderer. A 32-column plane wraps every 256 pixels on
   * hardware. Ropey Rampage rain is that plane (BG3); leaving it cleared
   * draws the drops only inside the native image. A 64-column plane keeps
   * the widened tilemap path instead of tiling. */
  uint8_t repeat = 0;
  uint8_t enabled = (uint8_t)((s_view.screenEnabled[0] |
                               s_view.screenEnabled[1]) & 0x07u);
  for (int plane = 0; plane < 3; plane++) {
    uint8_t bit = (uint8_t)(1u << plane);
    if ((enabled & bit) && (s_view.bgXsc[plane] & 1u) == 0)
      repeat = (uint8_t)(repeat | bit);
  }
  PpuSetWidescreenLayerRepeat(&s_view, repeat);
  if (layer != 1 && MineOverlayNeedsNativeBounds())
    PpuSetWidescreenLayerClamp(&s_view, 0x02);
  return 1;
}

static int RunPreparation(RecompReturn (*function)(CpuState *), uint8_t bank,
                          const Dkc1CoopHudBatch *hud) {
  CpuState scratch = g_cpu;
  scratch.D = 0; scratch.DB = 0x7e; scratch.PB = bank;
  scratch.m_flag = scratch.x_flag = 0;
  scratch.P &= (uint8_t)~0x38u;
  scratch._flag_D = 0;
  scratch.host_return_valid = 0;
  if (hud) { scratch.X = hud->x; scratch.A = hud->y; scratch.Y = hud->handle; }
  if (function == CODE_80A7ED_M0X0 || function == CODE_80B788_M0X0 ||
      function == CODE_80B891_M0X0) {
    if (function == CODE_80A7ED_M0X0) scratch.X = Read16(0x82);
    cpu_push_jsr_return_frame(&scratch);
  } else {
    cpu_push_jsl_return_frame(&scratch);
  }
  RecompReturn result = function(&scratch);
  unsigned violation = RtlSpeculativeExecutionViolation();
  if (result != RECOMP_RETURN_NORMAL || violation) {
    return 0;
  }
  return 1;
}

typedef struct ViewPreparation {
  int entities, speculative;
} ViewPreparation;
/* One owner spans all native preparation calls in a batch. Caller scratch
 * writes stay between acquisition and execution; raster resource loans begin
 * only after this scope has rolled back entity metadata. */
static int BeginViewPreparation(ViewPreparation *scope) {
  memset(scope, 0, sizeof *scope);
  scope->entities = Dkc1CoopEntitiesBeginTransaction();
  return scope->entities;
}
static void ExecuteViewPreparation(ViewPreparation *scope) {
  RtlSpeculativeExecutionBegin();
  scope->speculative = 1;
}
static void EndViewPreparation(ViewPreparation *scope) {
  if (scope->speculative) RtlSpeculativeExecutionEnd();
  if (scope->entities) Dkc1CoopEntitiesEndTransaction(0);
  memset(scope, 0, sizeof *scope);
}

static void PrepareMillstoneAnchor(uint16_t x) {
    uint16_t wheel_x = Read16(0xdd), wheel_y = Read16(0xdf);
    unsigned nearest = 0xffffu;
    /* Each millstone actor writes the shared anchor during its tick.
     * Select a live owner in this view instead of inheriting the last
     * actor's anchor. The native animation sheet is shared by the wheels. */
    for (uint16_t h = 2; Dkc1CoopEntityIsHandle(h);
         h = Dkc1CoopEntityNextHandle(h)) {
      if (Dkc1CoopEntityRead16(g_ram, 0x0d45, h) != 0x71u) continue;
      uint16_t wx = Dkc1CoopEntityRead16(g_ram, 0x0b19, h);
      uint16_t delta = (uint16_t)(x + 0x80u - wx);
      if (delta >= 0xc0u && delta < 0xff40u) continue;
      unsigned distance = delta < 0xc0u ? delta : (uint16_t)(0u - delta);
      if (distance >= nearest) continue;
      nearest = distance;
      wheel_x = wx;
      wheel_y = Dkc1CoopEntityRead16(g_ram, 0x0bc1, h);
    }
    Write16(0xdd, wheel_x); Write16(0xdf, wheel_y);
}

static Dkc1CoopScrollBus s_scroll;
static Dkc1CoopBackgroundProtocol ReadBackgroundProtocol(void) {
  unsigned entrance = Read16(0x3e);
  unsigned address = 0xc5bdu + entrance * 4u;
  const uint8_t *descriptor = address <= 0xfffeu ?
      cart_getRomPtr(g_snes->cart, 0x80, (uint16_t)address) : NULL;
  return Dkc1CoopBackgroundDecode(entrance, descriptor, address <= 0xfffcu);
}

static int PrepareParallax(uint16_t x, uint16_t y,
                          const Dkc1CoopBackgroundProtocol *protocol) {
  if (protocol->entrance >= 0xe6u || !protocol->native) return 0;
  uint16_t routine = protocol->nmi;
  if (routine == 0x9326) PrepareMillstoneAnchor(x);
  memset(&s_scroll, 0, sizeof s_scroll);
  memcpy(s_scroll.ram, g_ram, sizeof s_scroll.ram);
  s_scroll.rom = g_rom;
  Dkc1WramSetU16(s_scroll.ram, 0x88b, x);
  Dkc1WramSetU16(s_scroll.ram, 0x895, y);
  /* Private cameras publish integer positions. Native fractional bytes
   * belong to P1 and must not animate P2's otherwise stationary parallax. */
  s_scroll.ram[0x889] = s_scroll.ram[0x88a] = 0;
  s_scroll.ram[0x893] = s_scroll.ram[0x894] = 0;
  memcpy(s_scroll.x, s_view.hScroll, sizeof s_scroll.x);
  memcpy(s_scroll.y, s_view.vScroll, sizeof s_scroll.y);
  s_scroll.prev = s_view.scrollPrev; s_scroll.prev2 = s_view.scrollPrev2;
  s_scroll.hdma = g_snesrecomp_last_hdmaen;
  for (unsigned i = 0; i < 8; i++) {
    const DmaChannel *c = &g_dma->channel[i];
    uint8_t *r = s_scroll.dma + i * 16;
    r[0] = c->mode | (c->fixed ? 8 : 0) | (c->decrement ? 16 : 0) |
           (c->indirect ? 64 : 0) | (c->fromB ? 128 : 0);
    r[1] = c->bAdr; r[2] = (uint8_t)c->aAdr; r[3] = c->aAdr >> 8;
    r[4] = c->aBank; r[5] = (uint8_t)c->size; r[6] = c->size >> 8;
    r[7] = c->indBank;
  }
  if (!Dkc1CoopScrollRun(&s_scroll, routine)) {
    return 0;
  }
  /* 9A6A..9AA1 rebuilds the ship's two mirrored 64-record scroll ramps.
   * Stop before 9AA2's jump to gameplay; NMI supplies the vertical table.
   * Keep native headers and execute only on this camera's isolated bus. */
  if (protocol->owner == kCoopShip &&
      !Dkc1CoopScrollRunUntil(&s_scroll, 0x809a6au, 0x809aa2u)) {
    return 0;
  }
  /* Terrain has its own ring projection. Backgrounds and their HDMA scratch
   * use the native NMI evaluated with this camera, never the other viewport. */
  for (unsigned i = 1; i < 4; i++) {
    if (s_scroll.written & (1u << (i * 2))) s_view.hScroll[i] = s_scroll.x[i];
    if (s_scroll.written & (2u << (i * 2))) s_view.vScroll[i] = s_scroll.y[i];
  }
  memcpy(g_ram, s_scroll.ram, sizeof s_scroll.ram);
  return 1;
}

typedef struct GraphicsRecord {
  uint16_t bank, source;
  unsigned size, target;
} GraphicsRecord;
/* Decode only the native eight-byte queue record. Callers keep their own
 * acceptance rules and bank-local source wrapping. */
static int DecodeGraphicsRecord(unsigned offset, GraphicsRecord *record) {
  record->bank = Read16(0x1715 + offset);
  if (!(record->bank & 0x8000)) return 0;
  record->size = Read16(0x170f + offset);
  record->target = Read16(0x1711 + offset) * 2u;
  record->source = Read16(0x1713 + offset);
  return 1;
}

static int UploadGraphics(void) {
  for (unsigned offset = 0; offset < 0x218; offset += 8) {
    GraphicsRecord record;
    if (!DecodeGraphicsRecord(offset, &record)) return 1;
    if (!record.size || (record.size & 1u) ||
        record.target + record.size > sizeof s_view.vram)
      return 0;
    for (unsigned i = 0; i < record.size; i++) {
      const uint8_t *p = cart_getRomPtr(g_snes->cart, (uint8_t)record.bank,
                                      (uint16_t)(record.source + i));
      if (!p) return 0;
      ((uint8_t *)s_view.vram)[record.target + i] = *p;
    }
  }
  return 0;
}

static int UploadStaticGraphics(uint16_t handle) {
  /* B5833B's $9400 spawn records upload these shared sheets once. Their
   * low animation poses emit OAM without another DMA. Virtual actors reuse
   * the same tile address, so restore the owning sheet for each batch. */
  static const uint16_t records[] = {0x8663,0x8c5f,0x8c87,0x8cab,0xe375,0x8ccd};
  unsigned id = Dkc1CoopEntityRead16(s_backup, 0x0d45, handle);
  if (id < 0x3b || id > 0x40) return 1;
  const uint8_t *record = cart_getRomPtr(g_snes->cart, 0xb5, records[id - 0x3b]);
  if (!record || record[0] || record[1] != 0x94 ||
      record[2] != id || record[3]) return 0;
  unsigned size = (unsigned)record[7] << 5;
  uint16_t source = (uint16_t)(record[4] | ((uint16_t)record[5] << 8));
  uint16_t flags = Dkc1CoopEntityRead16(s_backup, 0x0c69, handle);
  const Ppu *view = &s_view;
  unsigned target = ((flags & 0x100 ? PPU_objTileAdr2(view) :
                      PPU_objTileAdr1(view)) + (flags & 255u) * 16u) * 2u;
  if (!size || target + size > sizeof s_view.vram) return 0;
  for (unsigned i = 0; i < size; ++i) {
    const uint8_t *p = cart_getRomPtr(g_snes->cart, record[6], (uint16_t)(source + i));
    if (!p) return 0;
    ((uint8_t *)s_view.vram)[target + i] = *p;
  }
  return 1;
}

/* HDMA uses the same cartridge tables, but writes only the private PPU. Its
 * reader keeps every address in its SNES bank, like the original DMA unit. */
typedef struct ViewHdma {
  uint16_t table, indirect;
  uint8_t bank, indirect_bank, mode, reg, repeat, enabled;
  uint16_t initial_hold;
  uint8_t initial_transferred;
} ViewHdma;
static int BusByte(uint8_t bank, uint16_t address, uint8_t *value) {
  if (bank == 0x7e || bank == 0x7f) {
    *value = g_ram[((unsigned)(bank & 1) << 16) | address]; return 1;
  }
  if ((bank & 0x7fu) < 0x40u && address < 0x2000u) {
    *value = g_ram[address]; return 1;
  }
  const uint8_t *p = cart_getRomPtr(g_snes->cart, bank, address);
  if (!p) return 0;
  *value = *p; return 1;
}
static void HdmaLine(ViewHdma *h) {
  static const uint8_t length[8] = {1,2,2,4,4,4,2,4};
  static const uint8_t offsets[8][4] = {
    {0,0,0,0},{0,1,0,1},{0,0,0,0},{0,0,1,1},
    {0,1,2,3},{0,1,0,1},{0,0,0,0},{0,0,1,1}};
  int transfer = 0;
  if (!h->enabled) return;
  /* A private camera can place the beam more than 127 lines below the
   * top. Its leading constant record needs a host count: bit 7 in a SNES
   * HDMA header means sequential transfers, not a longer constant hold. */
  if (h->initial_hold && h->initial_transferred) {
    h->initial_hold--;
    return;
  }
  if (!(h->repeat & 127u)) {
    if (!BusByte(h->bank, h->table++, &h->repeat) ||
        (!h->repeat && !h->initial_hold)) {
      h->enabled = 0; return;
    }
    if (h->initial_hold) h->repeat = 1;
    if (h->mode & 64) {
      uint8_t lo, hi;
      if (!BusByte(h->bank, h->table++, &lo) ||
          !BusByte(h->bank, h->table++, &hi)) { h->enabled = 0; return; }
      h->indirect = (uint16_t)(lo | ((uint16_t)hi << 8));
    }
    transfer = 1;
  }
  if (transfer || (h->repeat & 128u)) {
    for (unsigned i = 0; i < length[h->mode & 7]; i++) {
      uint8_t value;
      if (!BusByte(h->mode & 64 ? h->indirect_bank : h->bank,
                   h->mode & 64 ? h->indirect++ : h->table++, &value)) {
        h->enabled = 0; return;
      }
      Dkc1Private_ppu_write(&s_view,
          (uint8_t)(h->reg + offsets[h->mode & 7][i]), value);
    }
  }
  h->repeat--;
  if (h->initial_hold) {
    h->initial_hold--;
    h->initial_transferred = 1;
    h->repeat = 0;
  }
}

void Dkc1CoopViewInvalidate(void) {
  s_seed_valid = s_ready = 0;
}

void Dkc1InvalidatePresentation(void) { Dkc1CoopViewInvalidate(); }
int Dkc1CoopViewReady(void) { return s_ready; }

static int FixedRoomGameplay(void) {
  unsigned caps = Dkc1CoopSceneCapabilities(g_ram);
  return (caps & (kDkc1CoopScenePresentation | kDkc1CoopSceneFixed)) ==
                (kDkc1CoopScenePresentation | kDkc1CoopSceneFixed);
}

void Dkc1CoopViewCapture(const Ppu *ppu) {
  /* Particle projection uses the canonical camera at native scanout.
   * Ropes are rebuilt from each owning actor and its private resources. */
  if (ppu && g_ram) {
    s_native_cam_x = Read16(0x088b);
    s_native_cam_y = Read16(0x0895);
  }
  s_ready = 0;
  s_seed_valid = Dkc1CoopEntitiesActive() && ppu &&
                (Dkc1CoopInLevel(g_ram) || FixedRoomGameplay());
  if (s_seed_valid) s_seed = *ppu;
}

static void ResetScratch(uint16_t x, uint16_t y) {
  memcpy(g_ram, s_backup, sizeof s_backup);
  Write16(0x088b, x); Write16(0x0895, y);
  Write16(0x008e, 0x0200); Write16(0x006e, 0x0400);
  Dkc1CoopOamClear(g_ram + 0x200, g_ram + 0x400);
  memset(g_ram + 0x170d, 0, 0x220);
  Write16(0x003c, 0x200);
  memset(g_ram + 0x0ab1, 0, 0x34);
  Write16(0x0d11, 0); /* slot zero is the empty draw-list sentinel */
}

static int MaterializeHud(int number) {
  uint8_t oam[512], high[32];
  int count = Dkc1CoopHudBuildOam(s_backup, (unsigned)number, oam, high);
  if (count < 0) return 1;
  for (int i = 0; i < count;) {
    const Dkc1CoopHudBatch *batch = Dkc1CoopHudBatchForSlot((unsigned)i, (unsigned)count);
    if (batch) {
      if (!Dkc1CoopEntityIsHandle(batch->handle)) return 0;
      ResetScratch(Read16(0x088b), Read16(0x0895));
      ViewPreparation scope;
      if (!BeginViewPreparation(&scope)) return 0;
      Dkc1CoopEntityWrite16(g_ram, 0x0ae5, batch->handle, 0);
      Dkc1CoopEntityWrite16(g_ram, 0x0d11, batch->handle, batch->pose);
      Dkc1CoopEntityWrite16(g_ram, 0x0c69, batch->handle, batch->flags);
      ExecuteViewPreparation(&scope);
      int okay = RunPreparation(CODE_BBA772_M0X0, 0xbb, batch) && UploadGraphics();
      EndViewPreparation(&scope);
      if (!okay) return 0;
    }
    memcpy(s_view.cgram, s_seed.cgram, sizeof s_view.cgram);
    if (batch && batch->palette_valid)
      memcpy(s_view.cgram + 0x80 + ((batch->flags >> 9) & 7) * 16,
             batch->palette, sizeof batch->palette);
    Dkc1CoopOamClear((uint8_t *)s_view.oam,s_view.highOam);
    unsigned first = (unsigned)i;
    do {
      unsigned slot = (unsigned)i - first;
      Dkc1CoopOamCopy((uint8_t *)s_view.oam, s_view.highOam, slot,
          oam + i * 4, Dkc1CoopOamHigh(high, (unsigned)i));
      i++;
    } while (i < count && Dkc1CoopHudBatchForSlot((unsigned)i, (unsigned)count) == batch);
    s_view.wsOamRightHintStrict = 0;
    s_view.wsHudOamSlots = s_view.wsHudOamSlots2 = 0;
    Dkc1Private_AccumulateSprites(&s_view, s_obj_depth, s_obj_color);
  }
  return 1;
}

typedef struct RopeSheetCheckpoint {
  uint32_t mask;
  uint16_t owner;
  uint8_t chr[0x20][32];
} RopeSheetCheckpoint;

static void CaptureRopeSheetCheckpoint(RopeSheetCheckpoint *checkpoint) {
  checkpoint->mask = s_rope_chr_mask;
  checkpoint->owner = s_rope_chr_owner;
  memcpy(checkpoint->chr, s_rope_chr, sizeof checkpoint->chr);
}

static void RestoreRopeSheetCheckpoint(const RopeSheetCheckpoint *checkpoint) {
  memcpy(s_rope_chr, checkpoint->chr, sizeof checkpoint->chr);
  s_rope_chr_mask = checkpoint->mask;
  s_rope_chr_owner = checkpoint->owner;
}

static void CaptureRopeSheet(uint16_t handle) {
  if (s_rope_chr_owner != handle) s_rope_chr_mask = 0;
  s_rope_chr_owner = handle;
  /* Capture the bank-$D6 sheet queued by this rope's preparation. Each
   * rope batch starts with an empty mask, so reused OBJ tiles retain the
   * owning actor's graphics until that batch has been rasterized. */
  for (unsigned offset = 0; offset < 0x218; offset += 8) {
    GraphicsRecord record;
    if (!DecodeGraphicsRecord(offset, &record)) return;
    if ((record.bank & 0xff) != 0xd6 || record.size < 512 ||
        (record.size & 31u)) continue;
    unsigned base = (unsigned)PPU_objTileAdr1((&s_view)) * 2u;
    for (unsigned i = 0; i + 32 <= record.size; i += 32) {
      unsigned byte = record.target + i;
      if (byte < base) continue;
      unsigned tile = (byte - base) / 32u;
      if (tile < 0x80 || tile > 0x9f || ((byte - base) & 31u)) continue;
      unsigned slot = tile - 0x80u;
      if (s_rope_chr_mask & (1u << slot)) continue;
      for (unsigned b = 0; b < 32; b++) {
        const uint8_t *p = cart_getRomPtr(g_snes->cart, (uint8_t)record.bank,
                                          (uint16_t)(record.source + i + b));
        if (!p) return;
        s_rope_chr[slot][b] = *p;
      }
      s_rope_chr_mask |= 1u << slot;
    }
  }
}

static void RestoreSharedRopeTiles(void) {
  /* The rope preparation writes its sheet over tiles $80–$9F. Rambi and
   * the other actors on this screen already own some of those tiles in
   * the native frame. Put that picture back before they are drawn; the
   * rope links are rasterized later from the captured sheet. */
  unsigned base = (unsigned)PPU_objTileAdr1((&s_view)) * 2u + 0x80u * 32u;
  unsigned bytes = 0x20u * 32u;
  if (base + bytes > sizeof s_view.vram) return;
  memcpy((uint8_t *)s_view.vram + base, (uint8_t *)s_seed.vram + base, bytes);
}

static struct { int x, y; uint16_t attributes; } s_bubble[32];
static int s_bubble_n;
static void AccumulateWithRopeSheet(int paint);

static int Materialize(int number, uint16_t handle, uint16_t x, uint16_t y,
                       int swing_links) {
  if (!handle) {
    /* HUD counters, life awards and MMIO run exactly once in the native
     * frame. Reuse their captured OAM; effects are projected without a tick. */
    /* A shared cart is rasterized into the upper scratch surface even when
     * Diddy owns the active lower screen. Effects follow that owner, not the
     * scratch surface, while terrain keeps the native camera preparation. */
    return MaterializeHud(s_projection_seat >= 0 ? s_projection_seat : number);
  }
  ResetScratch(x, y);
  ViewPreparation scope;
  if (!BeginViewPreparation(&scope)) return 0;
  Write16(0x0ab1, handle);
  Dkc1CoopEntityWrite16(g_ram, 0x0ae5, handle, 0);
  ExecuteViewPreparation(&scope);
  int okay = RunPreparation(CODE_BBA849_M0X0, 0xbb, NULL);
  if (okay && !UploadGraphics()) {
    okay = 0;
  }
  uint16_t sprite_id = Dkc1CoopEntityRead16(s_backup, 0x0d45, handle);
  /* Rasterize this chain with the sheet its own preparation queued.
   * Preserve the outer capture context across the actor batch. */
  int own_sheet = okay && swing_links && sprite_id == 0x31u;
  RopeSheetCheckpoint sheet_checkpoint;
  if (own_sheet) {
    CaptureRopeSheetCheckpoint(&sheet_checkpoint);
    s_rope_chr_mask = 0;
  }
  if (okay && sprite_id == 0x31u) CaptureRopeSheet(handle);
  {
    uint16_t id = sprite_id;
    /* $80A7BE walks every rope that registered during the native tick and
     * emits its shaft after that tick. The list is empty by the time a
     * view is built, so each rope actor emits its own shaft here. The
     * ordinary pose does not include those sprites. */
    /* Sprite $31 emits its swinging links in its own preparation. The
     * extra shaft applies only to the straight vertical rope ($30). */
    if (okay && id == 0x30u) {
      Write16(0x82, handle);
      Write16(0x50, 3); Write16(0x52, 0x28);
      okay = RunPreparation(CODE_80A7ED_M0X0, 0x80, NULL);
    }
  }
  EndViewPreparation(&scope);
  if (!okay || !UploadStaticGraphics(handle)) {
    if (own_sheet) {
      RestoreRopeSheetCheckpoint(&sheet_checkpoint);
    }
    return 0;
  }
  memcpy(s_view.oam, g_ram + 0x200, sizeof s_view.oam);
  memcpy(s_view.highOam, g_ram + 0x400, sizeof s_view.highOam);
  if (sprite_id == 0x31u && handle < 256) {
    int any = 0;
    for (unsigned i = 0; i < 128; i++)
      if (g_ram[0x201 + i * 4] != 0xf0) any = 1;
    unsigned bit = 1u << (handle & 7);
    if (any) s_rope_culled[handle >> 3] = (uint8_t)(s_rope_culled[handle >> 3] & ~bit);
    else if (!swing_links)
      s_rope_culled[handle >> 3] = (uint8_t)(s_rope_culled[handle >> 3] | bit);
  }
  /* OAM is rebuilt from slot zero. The primary frame's slot annotations
   * describe different sprites and cannot be carried into this batch. */
  s_view.wsOamRightHintStrict = 0;
  memset(s_view.wsOamRightHint, 0, sizeof s_view.wsOamRightHint);
  s_view.wsHudOamSlots = s_view.wsHudOamSlots2 = 0;
  memcpy(s_view.cgram, s_seed.cgram, sizeof s_view.cgram);
  uint16_t palette[16];
  uint16_t palette_owner = Dkc1CoopActorPaletteOwner(s_backup, handle);
  if (Dkc1CoopGraphicsGetPalette(palette_owner, palette)) {
    uint16_t flags = Dkc1CoopEntityRead16(s_backup, 0x0c69, handle);
    memcpy(s_view.cgram + 0x80 + ((flags >> 9) & 7) * 16,
           palette, sizeof palette);
  }
  uint16_t kong_palette = Dkc1CoopActorPaletteSource(s_backup, handle);
  if (kong_palette) {
    uint16_t flags = Dkc1CoopEntityRead16(s_backup, 0x0c69, handle);
    if (Dkc1CoopGraphicsReadPalette(kong_palette, palette))
      memcpy(s_view.cgram + 0x80 + ((flags >> 9) & 7) * 16,
             palette, sizeof palette);
  }
  if (own_sheet) {
    uint8_t src[512], high_src[32];
    memcpy(src, s_view.oam, sizeof src);
    memcpy(high_src, s_view.highOam, sizeof high_src);
    int written=Dkc1CoopOamReplay(src,high_src,(uint8_t *)s_view.oam,
        s_view.highOam,0,Dkc1VideoExtra());
    if (written) {
      if (!s_rope_chr_mask && sheet_checkpoint.owner == handle) {
        RestoreRopeSheetCheckpoint(&sheet_checkpoint);
      }
      AccumulateWithRopeSheet(1);
    }
    RestoreRopeSheetCheckpoint(&sheet_checkpoint);
    RestoreSharedRopeTiles();
    return 1;
  }
  /* The dedicated rope pass owns the entire chain and its resources. */
  if (sprite_id != 0x31u) {
    Dkc1Private_AccumulateSprites(&s_view, s_obj_depth, s_obj_color);
  } else {
    RestoreSharedRopeTiles();
  }
  return 1;
}

static void ClearOam(void) {
  Dkc1CoopOamClear((uint8_t *)s_view.oam,s_view.highOam);
}

static void EmitFormation(void *context, int16_t x, uint8_t y,
                          uint16_t attributes) {
  unsigned *count = context;
  if (*count == 128) {
    Dkc1Private_AccumulateSprites(&s_view, s_obj_depth, s_obj_color);
    ClearOam();
    *count = 0;
  }
  const unsigned i = (*count)++;
  Dkc1CoopOamWrite((uint8_t *)s_view.oam,s_view.highOam,i,x,y,attributes,1);
}

static int ActorVisible(const uint8_t *ram, uint16_t handle,
                        uint16_t x, uint16_t y, int extra) {
  if (!Dkc1CoopEntityRead16(ram, 0x0d45, handle) ||
      !Dkc1CoopEntityRead16(ram, 0x0d11, handle)) return 0;
  int px = Dkc1CoopEntityRead16(ram, 0x0b19, handle);
  int py = (int)(ram[0x4a] | ((uint16_t)ram[0x4b] << 8)) -
           Dkc1CoopEntityRead16(ram, 0x0bc1, handle);
  /* $80A7ED draws the straight rope upward from its bottom anchor,
   * clipping that anchor to the viewport when it lies below the screen.
   * The ordinary pose rectangle cannot bound this separate shaft. */
  int shaft = Dkc1CoopEntityRead16(ram, 0x0d45, handle) == 0x30u;
  return px >= (int)x - extra - 128 && px <= (int)x + 384 + extra &&
         py >= (int)y - 128 && (shaft || py <= (int)y + 352);
}

static unsigned BuildActorOrder(const uint8_t *ram, uint16_t x, uint16_t y,
                                 int extra, uint16_t order[128]) {
  uint8_t seen[128] = {0};
  unsigned count = 0;
  for (unsigned i = 0; i < 0x34; i += 2) {
    uint16_t h = (uint16_t)(ram[0x0ab1 + i] |
                            ((uint16_t)ram[0x0ab2 + i] << 8));
    if (!Dkc1CoopEntityIsHandle(h) || seen[h >> 1]) continue;
    seen[h >> 1] = 1;
    if (ActorVisible(ram, h, x, y, extra)) order[count++] = h;
  }
  /* B88355..B88361 swaps only when the next unsigned $0A7D is larger.
   * Its two partial passes do not justify sorting the existing native list
   * again. Keep that subsequence intact and insert missing actors before the
   * first lower depth. Equal native/host depths retain their previous order. */
  for (uint16_t h = 2; Dkc1CoopEntityIsHandle(h);
       h = Dkc1CoopEntityNextHandle(h)) {
    if (seen[h >> 1] || !ActorVisible(ram, h, x, y, extra)) continue;
    unsigned insert = 0;
    const uint16_t depth = Dkc1CoopEntityRead16(ram, 0x0a7d, h);
    while (insert < count &&
           Dkc1CoopEntityRead16(ram, 0x0a7d, order[insert]) >= depth)
      insert++;
    memmove(order + insert + 1, order + insert,
            (count - insert) * sizeof *order);
    order[insert] = h;
    count++;
  }
  return count;
}

static void StampBubbles(void) {
  /* $8097A5 spawns temporary sprite $40 from $B5:8CCD. Its $9400
   * command uploads seven tiles from $D3:0600; $BFFCFC leaves the base
   * flags in $1E23 for the independent particle list ($80A49D). The actor
   * can disappear while that list remains alive, and other actors reuse
   * its VRAM/palette. Restore its resources only for this private batch. */
  if (!s_bubble_n || !g_snes || !g_snes->cart) return;
  const uint8_t *record = cart_getRomPtr(g_snes->cart, 0xb5, 0x8ccd);
  static const uint8_t header[8] = {0,0x94,0x40,0,0,6,0xd3,7};
  if (!record || memcmp(record, header, sizeof header) ||
      record[18] || record[19] != 0x88 ||
      record[20] != 0xf2 || record[21] != 0x86) return;
  const uint16_t flags = Dkc1WramU16(s_backup, 0x1e23);
  const unsigned target = ((flags & 0x100 ? PPU_objTileAdr2((&s_view)) :
                           PPU_objTileAdr1((&s_view))) +
                          (flags & 255u) * 16u) * 2u;
  uint8_t sheet[7 * 32], saved_sheet[sizeof sheet];
  uint16_t palette[16], saved_palette[16];
  if (target + sizeof sheet > sizeof s_view.vram ||
      (flags & 255u) > 249u ||
      !Dkc1CoopGraphicsReadPalette(0x86f2, palette)) return;
  for (unsigned i = 0; i < sizeof sheet; ++i) {
    const uint8_t *p = cart_getRomPtr(g_snes->cart, 0xd3, (uint16_t)(0x0600 + i));
    if (!p) return;
    sheet[i] = *p;
  }
  const unsigned row = 0x80 + ((flags >> 9) & 7) * 16;
  Dkc1CoopRasterLoan loan = {{{(uint8_t *)s_view.vram + target,
      saved_sheet, sizeof saved_sheet},
      {s_view.cgram + row, saved_palette, sizeof saved_palette}}};
  Dkc1CoopRasterBegin(&loan);
  memcpy((uint8_t *)s_view.vram + target, sheet, sizeof sheet);
  memcpy(s_view.cgram + row, palette, sizeof palette);
  ClearOam();
  for (int i = 0; i < s_bubble_n; ++i) {
    Dkc1CoopOamWrite((uint8_t *)s_view.oam,s_view.highOam,(unsigned)i,
        s_bubble[i].x,(unsigned)s_bubble[i].y,s_bubble[i].attributes,0);
  }
  Dkc1Private_AccumulateSprites(&s_view, s_obj_depth, s_obj_color);
  Dkc1CoopRasterEnd(&loan);
}

static int StampCastText(void) {
  /* 81D3B7 emits small font OBJ directly, after the HUD/world draw. The
   * completed native OAM is the text/timing oracle, including scrolling
   * captions. Cast actors use virtual row6; the font keeps its native row. */
  unsigned count=0;
  uint8_t attributes=(uint8_t)Dkc1WramU16(s_backup,0x136bb);
  if (!(attributes & 1u) || PPU_objTileAdr2((&s_seed)) + 0xa0u*16u != 0x1a00u)
    return 1;
  ClearOam();
  const uint8_t *native=(const uint8_t *)s_seed.oam;
  for (unsigned i=0;i<128;i++) {
    const uint8_t *sprite=native+i*4;
    unsigned bits=Dkc1CoopOamHigh(s_seed.highOam,i);
    if (sprite[1]>=224 || sprite[2]<0xa0 || sprite[3]!=attributes || (bits&2u))
      continue;
    Dkc1CoopOamCopy((uint8_t *)s_view.oam,s_view.highOam,count++,sprite,bits);
  }
  if (!count) return 1;
  /* 81D323 loads96 two-plane glyphs at VRAM1A00 and pads their upper
   * planes with zeros. Restore only for this batch, then leave BG VRAM
   * exactly as the preceding actor preparation left it. */
  uint8_t saved[96*32];
  uint16_t saved_palette[256];
  uint8_t *font=(uint8_t *)s_view.vram+0x3400;
  Dkc1CoopRasterLoan loan = {{{font, saved, sizeof saved},
      {s_view.cgram, saved_palette, sizeof saved_palette}}};
  Dkc1CoopRasterBegin(&loan);
  for (unsigned tile=0;tile<96;tile++) {
    const uint8_t *source=cart_getRomPtr(g_snes->cart,0xe9,
        (uint16_t)(0xd6dc+tile*16));
    if (!source) { Dkc1CoopRasterEnd(&loan); return 0; }
    memcpy(font+tile*32,source,16);memset(font+tile*32+16,0,16);
  }
  memcpy(s_view.cgram,s_seed.cgram,sizeof s_view.cgram);
  s_view.wsOamRightHintStrict=0;
  s_view.wsHudOamSlots=s_view.wsHudOamSlots2=0;
  Dkc1Private_AccumulateSprites(&s_view,s_obj_depth,s_obj_color);
  Dkc1CoopRasterEnd(&loan);
  return 1;
}

static void PaintRopeSheet(void) {
  if (!s_rope_chr_mask) return;
  unsigned base = (unsigned)PPU_objTileAdr1((&s_view)) * 2u;
  for (unsigned slot = 0; slot < 32; slot++) {
    if ((s_rope_chr_mask & (1u << slot)) == 0) continue;
    unsigned byte = base + (0x80u + slot) * 32u;
    if (byte + 32 > sizeof s_view.vram) continue;
    memcpy((uint8_t *)s_view.vram + byte, s_rope_chr[slot], 32);
  }
}

static void AccumulateWithRopeSheet(int paint) {
  /* Start from native resources, then install this rope batch's captured
   * sheet. Restore the scratch tiles after accumulation because later
   * actors may reuse the same OBJ tile addresses. */
  static uint16_t saved_vram[0x8000];
  uint16_t saved_cgram[256], palette[16];
  Dkc1CoopRasterLoan loan = {{{s_view.vram, saved_vram, sizeof saved_vram},
      {s_view.cgram, saved_cgram, sizeof saved_cgram}}};
  Dkc1CoopRasterBegin(&loan);
  memcpy(s_view.vram, s_seed.vram, sizeof saved_vram);
  /* Fixed rows keep native lighting. Allocated rope colors belong to this
   * actor, however: another actor may now occupy its shared physical row.
   * The graphics getter applies the same scene weather/lighting policy. */
  memcpy(s_view.cgram, s_seed.cgram, sizeof s_view.cgram);
  if (paint) {
    PaintRopeSheet();
    if (Dkc1CoopGraphicsGetPalette(s_rope_chr_owner, palette)) {
      unsigned flags=Dkc1CoopEntityRead16(s_backup,0x0c69,s_rope_chr_owner);
      unsigned row=128u+((flags >> 8) & 0x0eu)*8u;
      memcpy(s_view.cgram+row,palette,sizeof palette);
    }
  }
  Dkc1Private_AccumulateSprites(&s_view, s_obj_depth, s_obj_color);
  Dkc1CoopRasterEnd(&loan);
}

static void ReplayNativeBubbles(void) {
  s_bubble_n = 0;
  if (Dkc1WramU16(s_backup, 0x32) != 3) return;
  /* 80:A49D consumes these 18 authored particle records. Their producer
   * owns the graphics, independent of actor tile names and OAM size bits.
   * Coordinates are 14.2 world values projected by the cartridge camera;
   * retain its screen pixels on each view, as in the native particle pass. */
  for (unsigned i = 0; i < 0x24; i += 2) {
    uint16_t attributes = Dkc1WramU16(s_backup, 0x136b5 + i);
    if (!attributes) continue;
    int x = ((Dkc1WramU16(s_backup, 0x136fd + i) >> 2) - s_native_cam_x) & 0xff;
    int y = ((Dkc1WramU16(s_backup, 0x13745 + i) >> 2) - s_native_cam_y) & 0xff;
    if (y >= 224) y -= 256;
    s_bubble[s_bubble_n].x = x;
    s_bubble[s_bubble_n].y = y;
    s_bubble[s_bubble_n].attributes = attributes;
    s_bubble_n++;
  }
}

static void EmitCulledRope(uint16_t handle, uint16_t view_x, uint16_t view_y) {
  /* $BBA849$ drops the whole angled chain once the hook is above the
   * camera, even when lower links still cross the screen. Place the hook
   * where that test passes, then move every sprite by the same distance
   * the real hook sits above that stand-in. */
  int base = (int)Dkc1WramU16(s_backup, 0x004a) - 0x80 - (int)view_y +
             (int)Dkc1WramU16(s_backup, 0x0887);
  if (base < 0x40) return;
  int fake_y = base - 0x20;
  if (fake_y < 0 || fake_y > 0xffff) return;
  int real_y = Dkc1CoopEntityRead16(s_backup, 0x0bc1, handle);
  int delta = real_y - fake_y;
  /* Another rope may already own tiles $80–$9F. This chain is rasterized
   * with the sheet its own preparation queues, then that sheet is put back. */
  RopeSheetCheckpoint sheet_checkpoint;
  CaptureRopeSheetCheckpoint(&sheet_checkpoint);
  s_rope_chr_mask = 0;
  ResetScratch(view_x, view_y);
  ViewPreparation scope;
  if (!BeginViewPreparation(&scope)) {
    RestoreRopeSheetCheckpoint(&sheet_checkpoint);
    return;
  }
  Write16(0x0ab1, handle);
  Dkc1CoopEntityWrite16(g_ram, 0x0ae5, handle, 0);
  Dkc1CoopEntityWrite16(g_ram, 0x0bc1, handle, (uint16_t)fake_y);
  ExecuteViewPreparation(&scope);
  int okay = RunPreparation(CODE_BBA849_M0X0, 0xbb, NULL);
  if (okay) CaptureRopeSheet(handle);
  EndViewPreparation(&scope);
  if (!okay) {
    RestoreRopeSheetCheckpoint(&sheet_checkpoint);
    return;
  }
  uint8_t src[512], high_src[32];
  memcpy(src, g_ram + 0x200, sizeof src);
  memcpy(high_src, g_ram + 0x400, sizeof high_src);
  int written=Dkc1CoopOamReplay(src,high_src,(uint8_t *)s_view.oam,
      s_view.highOam,delta,Dkc1VideoExtra());
  if (written) {
    if (!s_rope_chr_mask && sheet_checkpoint.owner == handle) {
      RestoreRopeSheetCheckpoint(&sheet_checkpoint);
    }
    AccumulateWithRopeSheet(1);
  }
  RestoreRopeSheetCheckpoint(&sheet_checkpoint);
}

static int PrepareCeilingLight(int number, uint16_t x, uint16_t y,
                               ViewHdma channels[8],
                               const Dkc1CoopBackgroundProtocol *protocol) {
  if (protocol->owner != kCoopCeilingLight) return kCoopEffectNotApplicable;
  uint16_t routine = protocol->nmi;
  /* These protocols own $7F0200/$7F0500. The generic parallax builder
   * would replace their beam shape, including on the primary view. */
  if (!number) return kCoopEffectOwnsTables;
  uint16_t light = 0;
  unsigned best = 0;
  for (uint16_t h = 2; Dkc1CoopEntityIsHandle(h); h = Dkc1CoopEntityNextHandle(h)) {
    if (Dkc1CoopEntityRead16(s_backup, 0x0d45, h) != 0x5a) continue;
    uint16_t lx = Dkc1CoopEntityRead16(s_backup, 0x0b19, h);
    if ((uint16_t)(lx - x + 0x40u) >= 0x180u) continue;
    int dx = (int16_t)(lx - x - 128u);
    unsigned distance = (unsigned)(dx < 0 ? -dx : dx);
    if (!light || distance < best) { light = h; best = distance; }
  }
  /* The primary's disabled/selected light cannot select the second view. */
  s_view.screenEnabled[0] = 0; s_view.screenEnabled[1] = 0x13;
  if (routine == 0x9196) {
    s_view.screenEnabled[0] = 0x13; s_view.screenEnabled[1] = 0;
    s_view.fixedColor = (uint16_t)((Read16(0x579) & 0x200u ? 3u : 9u) * 0x421u);
  }
  if (!light) return kCoopEffectOwnsTables;
  uint16_t lx = Dkc1CoopEntityRead16(s_backup, 0x0b19, light);
  uint16_t ly = Dkc1CoopEntityRead16(s_backup, 0x0bc1, light);
  Write16(0xdd, lx); Write16(0xdf, ly);
  /* Native update builds the opposite HDMA buffer for the next NMI.
   * Rebuild the displayed buffer using its preceding update phase; otherwise
   * this view consumes the untouched primary-camera buffer. */
  uint16_t clock = Read16(0x28);
  Write16(0x28, (uint16_t)(clock - 1u));
  RtlSpeculativeExecutionBegin();
  int okay = RunPreparation(CODE_80B788_M0X0, 0x80, NULL) &&
             RunPreparation(CODE_80B891_M0X0, 0x80, NULL);
  RtlSpeculativeExecutionEnd();
  Write16(0x28, clock);
  if (!okay) return kCoopEffectRejected;
  Dkc1CoopLightPlan p = routine == 0x9137
      ? Dkc1CoopPlanStopGoLight(x, y, lx, ly, clock, Read16(0x579))
      : routine == 0x9196
      ? Dkc1CoopPlanLoopyLight(x, y, lx, ly, clock, Read16(0x579))
      : Dkc1CoopPlanLight(x, y, lx, ly, clock, Read16(0x579));
  /* Stop & Go's $80B206 always uses BG3 on main and selects the alternate
   * red color pair at $2808/$2C0A while the switch flag is set. */
  unsigned color_offset = routine == 0x9137 && (Read16(0x579) & 0x200u)
                              ? 0x808u : 0;
  s_view.screenEnabled[0] = (uint8_t)p.screens;
  s_view.screenEnabled[1] = (uint8_t)(p.screens >> 8);
  for (unsigned i = 0; i < 3; i++) channels[i + 1].table = p.table[i];
  if (p.headers) {
    g_ram[0x10200] = g_ram[0x10500] = p.scroll_header;
    g_ram[0x2000 + color_offset] = g_ram[0x2402 + color_offset] = p.color_header;
    unsigned delay = (uint16_t)(0x1f8u - ly - y) + 1u;
    if (delay >= 128u) channels[1].initial_hold = (uint16_t)delay;
    delay += (clock & 1u) * 2u;
    if (delay >= 128u) {
      channels[2].initial_hold = channels[3].initial_hold = (uint16_t)delay;
    }
  }
  s_view.vScroll[2] = p.scroll_y;
  if (routine == 0x9196) s_view.fixedColor = p.fixed_color;
  return kCoopEffectOwnsTables;
}

static int PrepareFog(int number, uint16_t x, uint16_t y,
                      ViewHdma channels[8],
                      const Dkc1CoopBackgroundProtocol *protocol) {
  unsigned entrance = protocol->entrance;
  if (entrance >= 0xe6u) return kCoopEffectNotApplicable;
  if (protocol->owner != kCoopFog) {
    memset(&s_fog, 0, sizeof s_fog); return kCoopEffectNotApplicable;
  }
  /* 80:B490 selects the displayed fog buffer. 80:B4CA owns its 56
   * four-line BG2 scroll records; 81:9489 uses the same RAM for an
   * unrelated parallax format. Preserve the native wave/animation phase. */
  ViewHdma *fog = &channels[1];
  if (!fog->enabled || fog->bank != 0x7f || fog->mode != 3 ||
      fog->reg != 0x0f || (fog->table != 0x200 && fog->table != 0x320))
    return kCoopEffectRejected;
  unsigned base = 0x10000u + fog->table;
  for (unsigned i = 0; i < 56; i++)
    if (g_ram[base + i * 5u] != 4) return kCoopEffectRejected;
  if (number) {
    if (!Dkc1CoopFogPrepare(&s_fog, &s_fog_bus, g_rom, s_backup,
          (uint16_t)entrance, s_native_cam_x, s_native_cam_y, x, y)) return kCoopEffectRejected;
    memcpy(g_ram + 0x10200, s_fog.tables, sizeof s_fog.tables);
  }
  return kCoopEffectOwnsTables;
}

static Dkc1CoopScrollBus s_flash;
static int s_flash_native;
static void PrepareFlashWindow(uint16_t x,uint16_t y) {
  s_flash.flash_ready=0;
  s_flash_native=x==Dkc1WramU16(s_backup,0x88b) && y==Dkc1WramU16(s_backup,0x895);
  const uint8_t *nmi=cart_getRomPtr(g_snes->cart,0x80,
      (uint16_t)(0xc5bdu+Dkc1WramU16(s_backup,0x3e)*4u));
  uint16_t bird=Dkc1WramU16(s_backup,0xddu);
  if (!nmi || nmi[0]!=0x38 || nmi[1]!=0x8e ||
      !Dkc1CoopEntityIsHandle(bird) ||
      Dkc1CoopEntityRead16(s_backup,0x0d45,bird)!=0x0du) return;
  memset(&s_flash,0,sizeof s_flash);s_flash.rom=g_rom;
  memcpy(s_flash.ram,s_backup,sizeof s_flash.ram);
  Dkc1WramSetU16(s_flash.ram,0x88b,x);Dkc1WramSetU16(s_flash.ram,0x895,y);
  if (bird>=0x80u) {
    const uint16_t fields[]={0x0d11,0x0c69,0x0b19,0x0bc1};
    for (unsigned i=0;i<4;i++) Dkc1WramSetU16(s_flash.ram,fields[i]+0x32u,
        Dkc1CoopEntityRead16(s_backup,fields[i],bird));
    Dkc1WramSetU16(s_flash.ram,0xddu,0x32u);
  }
  if (!Dkc1CoopScrollRunAt(&s_flash,0x819374u)) s_flash.flash_ready=0;
}
void Dkc1CoopViewWindow(Ppu *ppu,int y,int lo,int hi,int *left,int *right) {
  if (ppu!=&s_view || !s_flash.flash_ready || y<1 || y>224) return;
  int edge=s_flash.flash_edge[y-1];
  if (s_flash_native && s_flash.flash_origin>=0 && s_flash.flash_origin<256) {
    /* Native HDMA carries its precise game-update phase. Keep its center
     * bounds, extending only the clipped edge into the host margins. */
    unsigned l=ppu->window1left,r=ppu->window1right;
    if (l<=r) {
      if (l || r<255u) edge=s_flash.flash_left[y-1]?(int)r:(int)l;
      else edge=s_flash.flash_left[y-1]?(edge<255?255:edge):(edge>0?0:edge);
    } else edge=s_flash.flash_left[y-1]?(edge>=0?-1:edge):(edge<256?256:edge);
  }
  *left=s_flash.flash_left[y-1]?lo:(edge<lo?lo:edge);
  *right=s_flash.flash_left[y-1]?(edge>=hi?hi-1:edge):hi-1;
  if (*left>=hi || *right<lo) { *left=lo;*right=lo-1; }
}

/* 8EFE..8F1E presents the native three-step turn flash using backdrop only.
 * These are complete pictures even though no terrain layer is enabled. */
static int NativeFlashBackdrop(void) {
  if (s_seed.screenEnabled[0] || s_seed.screenEnabled[1] ||
      s_seed.cgwsel || s_seed.cgadsub || !(Dkc1WramU16(s_backup,0x1e15)&4u)) return 0;
  unsigned entrance=Dkc1WramU16(s_backup,0x3e);
  if (entrance>=0xe6u) return 0;
  const uint8_t *nmi=cart_getRomPtr(g_snes->cart,0x80,(uint16_t)(0xc5bdu+entrance*4u));
  const uint8_t *colors=cart_getRomPtr(g_snes->cart,0x80,0x8f20);
  if (!nmi || !colors || nmi[0]!=0x38 || nmi[1]!=0x8e) return 0;
  for (unsigned i=0;i<3;i++)
    if (s_seed.cgram[0]==(uint16_t)(colors[i*2]|colors[i*2+1]<<8)) return 1;
  return 0;
}

static int DrawView(int number, uint16_t x1, uint16_t y1,
                    uint16_t x, uint16_t y) {
  PrepareFlashWindow(x,y);
  s_view = s_seed;
  s_view.renderBuffer = number ? s_lower : s_upper;
  s_view.renderPitch = (uint32_t)s_width * 4;
  memset(s_view.renderBuffer, 0, (size_t)s_width * kHeight * 4);
  memset(s_view.overlayRenderBuffer, 0, sizeof s_view.overlayRenderBuffer);
  memset(s_view.overlayCaptures, 0, sizeof s_view.overlayCaptures);
  s_view.widescreenLineEnhancer = NULL;
  s_view.widescreenLineEnhancerContext = NULL;
  s_view.renderFlags |= kPpuRenderFlags_NewRenderer;
  memcpy(g_ram, s_backup, sizeof s_backup);
  if (NativeFlashBackdrop()) {
    s_flash.flash_ready=0;
    PpuSetExtraSpaceCentered(&s_view,(uint8_t)Dkc1VideoExtra());
    for (int line=0;line<=kHeight;line++) Dkc1Private_ppu_runLine(&s_view,line);
    /* The ordinary fail-closed viewport has zero margins with no layers.
     * This admitted picture is uniform: extend its native backdrop pixel. */
    for (unsigned row=0;row<kHeight;row++) {
      uint8_t *pixels=s_view.renderBuffer+row*s_view.renderPitch;
      uint8_t color[4];memcpy(color,pixels+Dkc1VideoExtra()*4u,4);
      for (unsigned column=0;column<(unsigned)s_width;column++)
        memcpy(pixels+column*4u,color,4);
    }
    return 1;
  }
  const Dkc1CoopBackgroundProtocol protocol = ReadBackgroundProtocol();
  int fixed_room = FixedRoomGameplay();
  if (!fixed_room && !PrepareTerrain(number != 0, x1, y1, x, y)) {
    if (x != x1 || y != y1) {
      return 0;
    }
    PpuSetExtraSpaceCentered(&s_view, (uint8_t)Dkc1VideoExtra());
  }
  if (s_flash.flash_ready) {
    /* Repeat native tiles, never an already window-clipped center line. */
    unsigned masked=(s_view.screenWindowed[0]|s_view.screenWindowed[1]) & 7u;
    s_view.wsLayerRepeat=(uint8_t)(s_view.wsLayerRepeat & ~masked);
    if (masked & 4u) PpuSetWidescreenBg3Widen(&s_view,1);
  }
  for (unsigned i = 0; i < kHeight * kPpuBufWidth; i++)
    s_obj_depth[i] = 0x0500;
  memset(s_obj_color, 0, sizeof s_obj_color);
  memset(s_rope_culled, 0, sizeof s_rope_culled);
  if (!Materialize(number, 0, x, y, 0)) return 0;
  uint16_t order[128];
  unsigned count = BuildActorOrder(s_backup, x, y, Dkc1VideoExtra(), order);
  for (unsigned i = 0; i < count; i++) {
    uint16_t h = order[i];
    if (!Materialize(number, h, x, y, 0)) return 0;
  }
  ReplayNativeBubbles();
  StampBubbles();
  /* A rope the leader camera did draw is in that list. Any other angled
   * rope still has to be built for this camera: Donkey's links keep the
   * list non-empty, which used to drop the second rope whenever his swing
   * was using tiles $80–$9F. */
  for (unsigned i = 0; i < count; i++) {
    uint16_t h = order[i];
    if (Dkc1CoopEntityRead16(s_backup, 0x0d45, h) != 0x31u) continue;
    if (!Materialize(number, h, x, y, 1)) return 0;
  }
  for (unsigned i = 0; i < count; i++) {
    uint16_t h = order[i];
    if (h < 256 && (s_rope_culled[h >> 3] & (1u << (h & 7))))
      EmitCulledRope(h, x, y);
  }
  unsigned formation_count = 0;
  ClearOam();
  memcpy(s_view.cgram, s_seed.cgram, sizeof s_view.cgram);
  int native_objects = Dkc1CoopSceneCapabilities(s_backup) & kDkc1CoopSceneNativeView;
  if (Dkc1CoopInLevel(s_backup) || native_objects)
    Dkc1CoopFormationsDrawWide(s_backup, g_snes->cart->rom, x, y,
                              native_objects ? 0 : Dkc1VideoExtra(),
                              native_objects ? 0 : Dkc1VideoExtra(),
                              EmitFormation, &formation_count);
  if (formation_count)
    Dkc1Private_AccumulateSprites(&s_view, s_obj_depth, s_obj_color);
  if ((Dkc1CoopSceneCapabilities(s_backup) & kDkc1CoopSceneSingleView) &&
      !StampCastText()) return 0;
  memcpy(g_ram, s_backup, sizeof s_backup);
  Write16(0x088b, x); Write16(0x0895, y);
  memcpy(s_view.cgram, s_seed.cgram, sizeof s_view.cgram);
  int private_scroll = number && !fixed_room;
  if (private_scroll && !PrepareParallax(x, y, &protocol)) return 0;
  ViewHdma channels[8];
  memset(channels, 0, sizeof channels);
  for (int i = 0; i < 8; i++) {
    const DmaChannel *c = &g_dma->channel[i];
    channels[i].enabled = (g_snesrecomp_last_hdmaen >> i) & 1;
    channels[i].table = c->aAdr; channels[i].bank = c->aBank;
    channels[i].indirect_bank = c->indBank;
    channels[i].mode = (uint8_t)(c->mode | (c->indirect ? 64 : 0));
    channels[i].reg = c->bAdr;
    if (private_scroll) {
      const uint8_t *r = s_scroll.dma + i * 16;
      channels[i].enabled = (s_scroll.hdma >> i) & 1;
      channels[i].mode = r[0]; channels[i].reg = r[1];
      channels[i].table = r[2] | ((uint16_t)r[3] << 8);
      channels[i].bank = r[4]; channels[i].indirect_bank = r[7];
    }
  }
  if (PrepareCeilingLight(number, x, y, channels, &protocol) == kCoopEffectRejected)
    return 0;
  if (PrepareFog(number, x, y, channels, &protocol) == kCoopEffectRejected) {
    return 0;
  }
  /* The primary keeps completed cartridge tables; PrepareParallax rebuilt
   * this protocol for the secondary. 81:9489 would overwrite them with
   * ordinary-level records, producing stripes in the clouds and sea. */
  if (!fixed_room && protocol.owner == kCoopOrdinaryParallax) {
    RtlSpeculativeExecutionBegin();
    int okay = RunPreparation(HDMA_BuildParallaxScrollTables_M0X0, 0x81, NULL);
    RtlSpeculativeExecutionEnd();
    if (!okay) return 0;
  }
  s_coop_ppu_dbg_layer_mask = g_snes_ppu_dbg_layer_mask;
  /* Every secondary HDMA table now comes from its own NMI scratch. */
  for (int line = 0; line <= kHeight; line++) {
    Dkc1Private_ppu_runLine(&s_view, line);
    for (int c = 0; c < 8; c++) HdmaLine(&channels[c]);
  }
  return 1;
}

typedef struct CoopViewPlan {
  uint16_t x[2], y[2];
  unsigned hidden;
  int have_second, projection_seat, draw_primary, draw_secondary;
  Dkc1CoopPictureSource picture_source;
  unsigned destinations;
} CoopViewPlan;
/* Choose ownership/projection before rendering. This function never advances
 * fade history, ticks an actor, materializes sprites or writes cartridge RAM. */
static CoopViewPlan BuildViewPlan(void) {
  CoopViewPlan plan={0};
  int projection_seat=-1;
  unsigned hidden=0;
  uint16_t x1=0, y1=0, x2=0, y2=0;
  Dkc1CoopWorldGetCamera(g_ram, 0, &x1, &y1);
  unsigned scene_caps = Dkc1CoopSceneCapabilities(g_ram);
  if (scene_caps & kDkc1CoopSceneSingleView) {
    plan.x[0]=plan.x[1]=x1; plan.y[0]=plan.y[1]=y1;
    plan.projection_seat=0; plan.picture_source=kCoopSharedNativePicture;
    plan.destinations=1; plan.hidden=2; plan.draw_primary=1;
    return plan;
  }
  unsigned bonus_owner = Dkc1CoopAnimalBonusOwner(g_ram);
  if (bonus_owner) {
    /* One native animal challenge belongs to its third-token collector.
     * Use the same native-picture routing as a single shared cart owner. */
    plan.x[0]=plan.x[1]=x1; plan.y[0]=plan.y[1]=y1;
    plan.projection_seat=(int)bonus_owner-1;
    plan.picture_source=kCoopSharedNativePicture;
    plan.destinations=1u<<plan.projection_seat;
    plan.hidden=(~(1u << plan.projection_seat))&3u;
    plan.have_second=bonus_owner==2; plan.draw_primary=1;
    return plan;
  }
  if ((scene_caps & kDkc1CoopSceneNativeView) ||
      (FixedRoomGameplay() && (Read16(0x1e15) & 0x20u))) {
    /* Fixed bonus framing is shared, but screen ownership still belongs to
     * each player. Native minigames can park a living partner in $0C, so
     * their party flag, not camera-target eligibility, selects the panes. */
    unsigned seats=1;
    if (Dkc1CoopEnabled()) {
      unsigned id=Read16(0x056f);
      seats=(id==1 || id==2) ?
          ((Read16(0x0579) & 1u) ? 3u : 1u << (id-1u)) : 0u;
    }
    plan.x[0]=plan.x[1]=x1; plan.y[0]=plan.y[1]=y1;
    plan.hidden=(~seats)&3u; plan.projection_seat=-1;
    plan.destinations=seats; plan.have_second=plan.draw_secondary=(seats&2u)!=0;
    plan.draw_primary=(seats&1u)!=0;
    return plan;
  }
  int have_second = Dkc1CoopWorldGetCamera(g_ram, 1, &x2, &y2);
  if (!have_second && Dkc1CoopEnabled() && FixedRoomGameplay() &&
      Dkc1CoopEntityRead16(g_ram, 0x0d45, 4) == 2 &&
      Dkc1CoopKongCam(g_ram, 4)) {
    x2 = x1; y2 = y1;
    have_second = 1;
  }
  if (Dkc1CoopEnabled()) {
    uint32_t slots[2];
    Dkc1CoopCacheKongSlots(g_ram, slots);
    if (!Dkc1CoopKongCam(g_ram, slots[0])) hidden |= 1;
    if (!Dkc1CoopKongCam(g_ram, slots[1])) hidden |= 2;
    /* BF:9B72 keeps the final-fall scene alive for $1375's countdown:
     * at $40 it starts the life balloon pop, and only below zero requests
     * the native fade. Camera eligibility is already off for $3B, but
     * presentation must still animate the HUD on the fixed native camera.
     * Only a view that was still visible may continue this countdown.
     * Valid pixels can also belong to a partner lost much earlier; a later
     * global death flag must not revive that already hidden screen. */
    for (unsigned i = 0; i < 2; ++i) {
      unsigned bit = 1u << i;
      if ((s_fade.valid & bit) && !(s_fade.hidden & bit) &&
          slots[i] && Read16(0x1929) &&
          Dkc1CoopEntityRead16(g_ram, 0x1029u, (uint16_t)slots[i]) == 0x3Bu &&
          (int16_t)Dkc1CoopEntityRead16(g_ram, 0x1375u, (uint16_t)slots[i]) >= 0) {
        hidden &= ~bit;
        if (i == 1) { x2 = x1; y2 = y1; have_second = 1; }
      }
    }
  }
  /* Authored camera regions can frame the next cannon above the passengers.
   * Share that native framing while both occupy one cannon; following their
   * pose independently would discard the cartridge's deliberate look-ahead.
   * Keep this presentation choice out of cartridge state and camera bounds. */
  int shared_cannon = have_second && Dkc1CoopSharedCannonCamera(g_ram);
  uint16_t trip_owner = Dkc1CoopMinecartViewOwner(g_ram);
  if (trip_owner == 1 || trip_owner == 2) projection_seat = trip_owner - 1;
  plan.x[0]=x1;plan.y[0]=y1;
  plan.x[1]=shared_cannon?x1:x2;plan.y[1]=shared_cannon?y1:y2;
  plan.hidden=hidden;plan.have_second=have_second;plan.projection_seat=projection_seat;
  plan.picture_source=projection_seat>=0?kCoopSharedNativePicture:kCoopPerPlayerPictures;
  plan.draw_primary=!(hidden&1) || (projection_seat==1 && !(hidden&2));
  plan.draw_secondary=projection_seat<0 && !(hidden&2) && have_second;
  plan.destinations=projection_seat>=0?1u<<projection_seat:
      (plan.draw_primary?1u:0u)|(plan.draw_secondary?2u:0u);
  return plan;
}

void Dkc1CoopViewDraw(void) {
  s_ready = s_hidden = 0;
  s_projection_seat = -1;
  /* Wider mobile/custom surfaces retain primary output; legacy co-op buffers
   * and save payloads have capacity for the largest desktop preset. */
  if (Dkc1VideoWidth() > kMaxWidth) return;
  if (!Dkc1CoopEntitiesActive() || !s_seed_valid) {
    memset(&s_fade, 0, sizeof s_fade);
    return;
  }
  s_width = Dkc1VideoWidth();
  if (s_fade.width != (uint32_t)s_width || s_fade.entrance != Read16(0x3e)) {
    memset(&s_fade, 0, sizeof s_fade);
    s_fade.version = 1;
    s_fade.width = (uint32_t)s_width;
    s_fade.entrance = Read16(0x3e);
    s_fade.frame = UINT32_MAX;
  }
  CoopViewPlan plan=BuildViewPlan();
  s_hidden=(int)plan.hidden;
  s_projection_seat=plan.projection_seat;
  for (unsigned i = 0; i < 2; ++i) {
    unsigned bit = 1u << i;
    if (!(s_hidden & bit)) s_fade.fade[i] = 0;
    else if (!(s_fade.valid & bit)) s_fade.fade[i] = kFadeFrames;
    else if (s_fade.frame != (uint32_t)snes_frame_counter &&
             s_fade.fade[i] < kFadeFrames) s_fade.fade[i]++;
  }
  s_fade.hidden = (uint8_t)s_hidden;
  s_fade.frame = (uint32_t)snes_frame_counter;
  s_rope_chr_mask = 0;
  s_rope_chr_owner = 0;
  memcpy(s_backup, g_ram, sizeof s_backup);
  Dkc1CoopClockCapture(&s_clock);
  int okay = plan.draw_primary &&
      DrawView(0,plan.x[0],plan.y[0],plan.x[0],plan.y[0]) ? 1 : 0;
  if (plan.picture_source==kCoopSharedNativePicture) {
    /* Native picture uses upper scratch. Keep visible history only for its
     * declared destination; a scratch surface is not an active player's pane. */
    s_fade.valid &= (uint8_t)plan.destinations;
    if (okay&1) {
      if (plan.destinations&2u)
        memcpy(s_lower,s_upper,(size_t)s_width*kHeight*4);
      okay=(int)plan.destinations;
    }
  } else if (plan.draw_secondary &&
             DrawView(1,plan.x[0],plan.y[0],plan.x[1],plan.y[1])) okay |= 2;
  s_fade.valid |= (uint8_t)okay;
  memcpy(g_ram, s_backup, sizeof s_backup);
  Dkc1CoopClockRestore(&s_clock);
  s_ready = okay;
}

static void FadeRow(uint8_t *out, const uint8_t *in, size_t bytes, unsigned age) {
  if (age >= kFadeFrames) { memset(out, 0, bytes); return; }
  for (size_t x = 0; x < bytes; x += 4) {
    for (unsigned c = 0; c < 3; ++c)
      out[x + c] = (uint8_t)((unsigned)in[x + c] * (kFadeFrames - age) / kFadeFrames);
    out[x + 3] = in[x + 3];
  }
}

int Dkc1PresentationHeight(void) {
  return Dkc1CoopEnabled() && Dkc1VideoWidth() <= kMaxWidth ? 448 : 224;
}
const uint8_t *Dkc1PresentationPixels(const uint8_t *primary, size_t pitch) {
  if (!primary || Dkc1VideoWidth() > kMaxWidth || !Dkc1CoopEntitiesActive()) return primary;
  if (!s_seed_valid) {
    Dkc1CoopViewCapture(g_ppu);
    Dkc1CoopViewDraw();
  }
  size_t row = (size_t)Dkc1VideoWidth() * 4;
  for (int y = 0; y < kHeight; y++) {
    /* A dead player's view stays black; a failed primary render still has
     * the native image as its fallback. These are different outcomes. */
    if (s_projection_seat == 1)
      memset(s_composite + row * y, 0, row);
    else if (s_hidden & 1)
      FadeRow(s_composite + row * y, s_upper + row * y, row, s_fade.fade[0]);
    else memcpy(s_composite + row * y,
           (s_ready & 1) && s_width == Dkc1VideoWidth() ? s_upper + row * y :
                                                   primary + pitch * y, row);
    if (s_projection_seat == 0)
      memset(s_composite + row * (y + kHeight), 0, row);
    else if (s_hidden & 2)
      FadeRow(s_composite + row * (y + kHeight), s_lower + row * y, row, s_fade.fade[1]);
    else if ((s_ready & 2) && s_width == Dkc1VideoWidth())
      memcpy(s_composite + row * (y + kHeight), s_lower + row * y, row);
    else
      memset(s_composite + row * (y + kHeight), 0, row);
  }
  return s_composite;
}
