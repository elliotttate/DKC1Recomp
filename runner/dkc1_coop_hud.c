#include "dkc1_coop_oam.h"
#include "dkc1_coop_hud.h"
#include "dkc1_coop_wire.h"
#include "dkc1_coop_mod.h"
#include "dkc1_coop_entities.h"
#include "dkc1_coop_graphics.h"
#include "dkc1_coop_world.h"
#include "dkc1_wram_gen.h"
#include "cpu_state.h"
#include "common_rtl.h"
#include <stddef.h>
#include <string.h>

enum { kHudWireV1Bytes = 568, kHudWireV2Bytes = 12864 };
typedef char HudBatchWireLayout[sizeof(Dkc1CoopHudBatch) == 48 ? 1 : -1];
typedef char HudGraphicsWireLayout[sizeof(Dkc1CoopHudGraphics) == 6148 ? 1 : -1];
typedef char HudV1WireLayout[offsetof(Dkc1CoopHudSnapshot, graphics) == kHudWireV1Bytes ? 1 : -1];
typedef char HudCaptureWireLayout[offsetof(Dkc1CoopHudSnapshot, capture) == 6716 ? 1 : -1];
typedef char HudV2WireLayout[sizeof(Dkc1CoopHudSnapshot) == kHudWireV2Bytes ? 1 : -1];

static Dkc1CoopHudSnapshot s_hud;
static uint16_t Word(const uint8_t *ram, unsigned a) {
  return Dkc1WramU16(ram, a);
}

void Dkc1CoopHudReset(void) { memset(&s_hud, 0, sizeof s_hud); }
size_t Dkc1CoopHudStateSize(uint32_t host_version) {
  return host_version >= kDkc1WsSaveVersionCoopHudGraphics ?
      kHudWireV2Bytes : kHudWireV1Bytes;
}
void Dkc1CoopHudSaveState(Dkc1CoopHudSnapshot *state) {
  if (state) { *state = s_hud; state->version = 2; }
}
void Dkc1CoopHudLoadState(const Dkc1CoopHudSnapshot *state) {
  Dkc1CoopHudReset();
  if (state && (state->version == 1 || state->version == 2) &&
      state->first <= 128 && state->count <= 128 - state->first) {
    if (state->version == 1) {
      memcpy(&s_hud, state, kHudWireV1Bytes);
      return;
    }
    const Dkc1CoopHudGraphics *sets[] = {&state->graphics, &state->capture};
    for (unsigned set = 0; set < 2; set++) {
      const Dkc1CoopHudGraphics *graphics = sets[set];
      if (graphics->count > 128 || graphics->pending > 1 ||
          (graphics->pending && graphics->count == 128)) return;
      for (unsigned i = 0; i < graphics->count + graphics->pending; i++) {
        const Dkc1CoopHudBatch *batch = &graphics->batches[i];
        if (batch->first > 128 || batch->count > 128 - batch->first ||
            !Dkc1CoopEntityIsHandle(batch->handle)) return;
      }
    }
    s_hud = *state;
  }
}
static int Native(const CpuState *cpu) {
  return cpu && cpu->ram && cpu->D == 0 && Dkc1CoopEntitiesActive() &&
         !RtlSpeculativeExecutionActive();
}
void Dkc1CoopHudBegin(CpuState *cpu) {
  if (!Native(cpu)) return;
  unsigned cursor = Word(cpu->ram, 0x8e);
  s_hud.pending = cursor >= 0x200 && cursor <= 0x400 && !(cursor & 3);
  s_hud.first = (uint16_t)((cursor - 0x200) / 4);
  s_hud.pending_entrance = Word(cpu->ram, 0x3e);
  s_hud.pending_mode = Word(cpu->ram, 0x32);
  s_hud.pending_frame = Word(cpu->ram, 0x28);
  memset(&s_hud.capture, 0, sizeof s_hud.capture);
}

void Dkc1CoopHudSpriteBegin(CpuState *cpu) {
  if (!Native(cpu) || s_hud.capture.count >= 128) return;
  unsigned cursor = Word(cpu->ram, 0x8e);
  if (cursor < 0x200 || cursor > 0x400 || (cursor & 3) ||
      !Dkc1CoopEntityIsHandle(cpu->Y)) return;
  if (!s_hud.pending) {
    /* 809A55..9A62 draws sprite75 after 80A150's HUD/world draw. Its
     * actor coordinates are not a world-space rectangle. Append this
     * proven direct job to the same frame's completed HUD capture;
     * ordinary out-of-scope calls retain their previous behavior. */
    if (!s_hud.valid || s_hud.mode != 0x0d || s_hud.entrance != 0x68 ||
        s_hud.mode != Word(cpu->ram,0x32) ||
        s_hud.entrance != Word(cpu->ram,0x3e) ||
        s_hud.frame != Word(cpu->ram,0x28) || cpu->m_flag || cpu->x_flag ||
        cpu->X != 0x10a0 || cpu->A != 0x0e10 || cpu->Y != Word(cpu->ram,0xdd) ||
        Dkc1CoopEntityRead16(cpu->ram,0x0d45,cpu->Y) != 0x75 ||
        cursor < 0x200u + (s_hud.first + s_hud.count) * 4u) return;
    s_hud.capture = s_hud.graphics;
  }
  Dkc1CoopHudBatch *batch = &s_hud.capture.batches[s_hud.capture.count];
  memset(batch, 0, sizeof *batch);
  batch->first = (uint16_t)((cursor - 0x200) / 4);
  batch->handle = cpu->Y;
  batch->pose = Dkc1CoopEntityRead16(cpu->ram, 0x0d11, cpu->Y);
  batch->x = cpu->X; batch->y = cpu->A;
  batch->flags = Dkc1CoopEntityRead16(cpu->ram, 0x0c69, cpu->Y);
  /* $B6A814-$B6A839 gives life icon $69 DK's shared palette instead of
   * allocating one. Resolve that binding to the logical Kong's virtual
   * palette; its physical slot can display Rambi while DK is mounted. */
  uint16_t palette_owner = cpu->Y;
  if (Dkc1CoopEntityRead16(cpu->ram, 0x0d45, cpu->Y) == 0x69)
    palette_owner = 2;
  batch->palette_valid = (uint16_t)Dkc1CoopGraphicsGetPalette(palette_owner, batch->palette);
  s_hud.capture.pending = 1;
}

void Dkc1CoopHudSpriteEnd(CpuState *cpu) {
  if (!Native(cpu) || !s_hud.capture.pending) return;
  s_hud.capture.pending = 0;
  Dkc1CoopHudBatch *batch = &s_hud.capture.batches[s_hud.capture.count];
  unsigned cursor = Word(cpu->ram, 0x8e);
  if (cursor < 0x200 + batch->first * 4u || cursor > 0x400 ||
      (cursor & 3)) return;
  batch->count = (uint16_t)((cursor - 0x200) / 4 - batch->first);
  if (batch->count) s_hud.capture.count++;
  if (!s_hud.pending && s_hud.valid &&
      s_hud.entrance == Word(cpu->ram,0x3e) && s_hud.mode == Word(cpu->ram,0x32) &&
      s_hud.frame == Word(cpu->ram,0x28) &&
      batch->first >= s_hud.first + s_hud.count &&
      s_hud.count + batch->count <= 128u - s_hud.first) {
    for (unsigned i = 0; i < batch->count; i++)
      Dkc1CoopOamCopy(s_hud.oam,s_hud.high,s_hud.count+i,
          cpu->ram + 0x200 + (batch->first+i)*4,
          Dkc1CoopOamHigh(cpu->ram+0x400,batch->first+i));
    /* World OAM between the HUD and this direct draw is not part of the
     * capture. Pack the direct range, retaining its resource batch mapping. */
    batch->first = (uint16_t)(s_hud.first + s_hud.count);
    s_hud.count = (uint16_t)(s_hud.count + batch->count);
    s_hud.graphics = s_hud.capture;
  }
}
void Dkc1CoopHudEnd(CpuState *cpu) {
  if (!Native(cpu) || !s_hud.pending) return;
  const uint8_t *ram = cpu->ram;
  unsigned cursor = Word(ram, 0x8e);
  if (cursor < 0x200 || cursor > 0x400 || (cursor & 3) ||
      (cursor - 0x200) / 4 < s_hud.first ||
      s_hud.pending_entrance != Word(ram, 0x3e) ||
      s_hud.pending_mode != Word(ram, 0x32) ||
      s_hud.pending_frame != Word(ram, 0x28)) {
    s_hud.pending = s_hud.valid = 0;
    return;
  }
  s_hud.count = (uint16_t)((cursor - 0x200) / 4 - s_hud.first);
  s_hud.entrance = s_hud.pending_entrance;
  s_hud.mode = s_hud.pending_mode;
  s_hud.frame = s_hud.pending_frame;
  memset(s_hud.oam, 0, sizeof s_hud.oam);
  memset(s_hud.high, 0, sizeof s_hud.high);
  for (unsigned i = 0; i < s_hud.count; i++)
    Dkc1CoopOamCopy(s_hud.oam, s_hud.high, i, ram + 0x200 + (s_hud.first + i) * 4,
        Dkc1CoopOamHigh(ram + 0x400, s_hud.first + i));
  s_hud.graphics = s_hud.capture;
  s_hud.valid = 1;
  s_hud.pending = 0;
}

const Dkc1CoopHudBatch *Dkc1CoopHudBatchForSlot(unsigned slot, unsigned total) {
  if (!s_hud.valid || total < s_hud.count || slot >= total ||
      slot < total - s_hud.count) return NULL;
  unsigned native_slot = slot - (total - s_hud.count) + s_hud.first;
  for (unsigned i = 0; i < s_hud.graphics.count; i++) {
    const Dkc1CoopHudBatch *batch = &s_hud.graphics.batches[i];
    if (native_slot >= batch->first && native_slot - batch->first < batch->count)
      return batch;
  }
  return NULL;
}
int Dkc1CoopHudBuildOam(const uint8_t *ram, unsigned view,
                      uint8_t *oam, uint8_t *high) {
  if (!ram || !oam || !high || view > 1) return -1;
  Dkc1CoopOamClear(oam, high);
  if (!Dkc1CoopEntitiesActive() || !s_hud.valid ||
      s_hud.entrance != Word(ram, 0x3e) || s_hud.mode != Word(ram, 0x32))
    return -1;
  unsigned count = 0;
  /* Exact $80A503: the effect pool's X/Y are screen coordinates in 8.8,
   * every live entry is one large OBJ with X-high zero. No tick/award path.
   * Bubbles belong to the cartridge camera. The other view keeps one on the
   * same world point, and drops it once it leaves the screen. A banana
   * banana/KONG flight is spawned while that Kong's camera is installed, so its
   * pixels are already on his picture. Adding the cartridge gap again parks
   * the pop on a different point of his view. */
  uint16_t cam_x = 0, cam_y = 0, view_x = 0, view_y = 0;
  int shift = view != 0 &&
              Dkc1CoopWorldGetCamera(ram, 0, &cam_x, &cam_y) &&
              Dkc1CoopWorldGetCamera(ram, 1, &view_x, &view_y);
  for (unsigned h = 0x34; h < 0x74; h += 2) {
    unsigned id = Word(ram, 0x0d45 + h);
    int own_view = Dkc1CoopEnabled() && Dkc1CoopEffectUsesOwnView((uint16_t)id);
    if (!id || (own_view && Dkc1CoopEffectView((uint16_t)h) != view)) continue;
    /* B5871F spawns previous KONG letters in state2 without screen X/Y.
     * BFD4F5 initializes both on their next tick, then selects state4.
     * Private redraw happens after spawning; do not expose stale pool bytes. */
    if (id == 0x41 && Word(ram, 0x1029 + h) == 2) continue;
    uint8_t sprite[4] = {ram[0x0b1a + h], ram[0x0bc2 + h],
                         ram[0x0c69 + h], ram[0x0c6a + h]};
    unsigned bits = 2;
    if (shift && !own_view) {
      int sx = (int)sprite[0] + (int)cam_x - (int)view_x;
      int sy = (int)sprite[1] + (int)cam_y - (int)view_y;
      if (sx < -32 || sx >= 256 + 32 || sy < -32 || sy >= 256)
        continue;
      if (sx < 0)
        sx += 512;
      if (sx >= 256)
        bits |= 1u;
      sprite[0] = (uint8_t)sx;
      sprite[1] = (uint8_t)sy;
    }
    Dkc1CoopOamCopy(oam, high, count++, sprite, bits);
  }
  for (unsigned i = 0; i < s_hud.count && count < 128; i++)
    Dkc1CoopOamCopy(oam, high, count++, s_hud.oam + i * 4, Dkc1CoopOamHigh(s_hud.high, i));
  return (int)count;
}
