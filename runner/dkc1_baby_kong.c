#include "dkc1_baby_kong.h"

#include "dkc1_baby_kong_animation.h"
#include "dkc1_baby_kong_layout.h"
#include "dkc1_baby_kong_movement.h"
#include "dkc1_wram_gen.h"

#include "sha256.h"
#include "snes/ppu.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
  kDkc3RomSize = 0x400000,
  kDkc3KiddyPaletteOffset = 0x3d34cf,
  kDkc1BabyCaptureWidth = kPpuBufWidth,
  kDkc1BabyCaptureHeight = 240,
};

typedef struct Dkc1BabyFrameManifest {
  const char *name;
  uint32_t rom_offset;
  uint16_t graphics_size;
} Dkc1BabyFrameManifest;

static const Dkc1BabyFrameManifest kFrameManifest[] = {
#include "dkc1_baby_kong_frames.inc"
};

typedef struct Dkc1BabyFrame {
  const char *name;
  int16_t x0;
  int16_t y0;
  int16_t opaque_y0;
  int16_t opaque_y1;
  uint16_t width;
  uint16_t height;
  uint8_t *pixels;
} Dkc1BabyFrame;

static const uint8_t kExpectedDkc3Sha256[32] = {
  0x22, 0x77, 0xa2, 0xd8, 0xdd, 0xdb, 0x01, 0xfe,
  0x5c, 0xb0, 0xae, 0x9a, 0x0f, 0xa2, 0x25, 0xd4,
  0x2b, 0x3a, 0x11, 0xad, 0xcc, 0xae, 0xaf, 0xa1,
  0x8e, 0x3c, 0x33, 0x9b, 0x37, 0x94, 0xa3, 0x2b,
};

static Dkc1BabyFrame *s_frames;
static size_t s_frame_count;
static uint16_t s_palette[16];
static bool s_enabled;
static char s_status[192] = "select a supported DKC3 ROM";
static uint8_t s_capture[kDkc1BabyCaptureWidth *
                         kDkc1BabyCaptureHeight * 4];
static const Dkc1BabyFrame *s_draw_frame;
static int s_draw_anchor_x;
static bool s_draw_flip;
static Dkc1BabyKongAlignment s_draw_alignment;
static Dkc1BabyKongAnimationTracker s_animation_tracker;

static void SetError(char *error, size_t error_size, const char *message) {
  if (error && error_size)
    (void)snprintf(error, error_size, "%s", message);
  (void)snprintf(s_status, sizeof s_status, "%s", message);
}

static void FreeFrames(Dkc1BabyFrame *frames, size_t count) {
  if (!frames)
    return;
  for (size_t i = 0; i < count; i++)
    free(frames[i].pixels);
  free(frames);
}

static uint8_t DecodeTilePixel(const uint8_t *tile, int x, int y) {
  const int shift = 7 - x;
  const uint8_t p0 = tile[y * 2];
  const uint8_t p1 = tile[y * 2 + 1];
  const uint8_t p2 = tile[16 + y * 2];
  const uint8_t p3 = tile[16 + y * 2 + 1];
  return (uint8_t)(((p0 >> shift) & 1u) |
                   (((p1 >> shift) & 1u) << 1) |
                   (((p2 >> shift) & 1u) << 2) |
                   (((p3 >> shift) & 1u) << 3));
}

static bool DecodeFrame(const uint8_t *rom,
                        const Dkc1BabyFrameManifest *manifest,
                        Dkc1BabyFrame *frame) {
  const size_t start = manifest->rom_offset;
  if (start + 8 > kDkc3RomSize)
    return false;
  const unsigned large_count = rom[start];
  const unsigned small_count = rom[start + 1];
  const unsigned small_start = rom[start + 2];
  const unsigned extra_small_count = rom[start + 3];
  const unsigned extra_small_start = rom[start + 4];
  const unsigned dma1_count = rom[start + 5];
  const unsigned dma2_start = rom[start + 6];
  const unsigned dma2_count = rom[start + 7];
  const unsigned piece_count = large_count + small_count + extra_small_count;
  const size_t header_size = 8u + piece_count * 2u;
  const size_t expected_graphics = (dma1_count + dma2_count) * 32u;
  if (!piece_count || piece_count > 64u ||
      large_count * 4u + small_count + extra_small_count !=
          dma1_count + dma2_count ||
      expected_graphics != manifest->graphics_size ||
      start + header_size + expected_graphics > kDkc3RomSize)
    return false;

  int x0 = 32767;
  int y0 = 32767;
  int x1 = -32768;
  int y1 = -32768;
  for (unsigned i = 0; i < piece_count; i++) {
    const int x = (int)rom[start + 8u + i * 2u] - 0x80;
    const int y = (int)rom[start + 9u + i * 2u] - 0x80;
    const int size = i < large_count ? 16 : 8;
    if (x < x0) x0 = x;
    if (y < y0) y0 = y;
    if (x + size > x1) x1 = x + size;
    if (y + size > y1) y1 = y + size;
  }
  const int width = x1 - x0;
  const int height = y1 - y0;
  if (width <= 0 || width > 128 || height <= 0 || height > 128)
    return false;

  uint8_t *pixels = (uint8_t *)calloc((size_t)width * height, 1);
  if (!pixels)
    return false;
  const uint8_t *graphics = rom + start + header_size;
  int opaque_y0 = 32767;
  int opaque_y1 = -32768;
  for (unsigned i = 0; i < piece_count; i++) {
    const int piece_x = (int)rom[start + 8u + i * 2u] - 0x80;
    const int piece_y = (int)rom[start + 9u + i * 2u] - 0x80;
    const int piece_size = i < large_count ? 16 : 8;
    const unsigned tiles = piece_size == 16 ? 4u : 1u;
    for (unsigned tile = 0; tile < tiles; tile++) {
      const int tile_x = piece_x + (int)(tile & 1u) * 8;
      const int tile_y = piece_y + (int)(tile >> 1) * 8;
      unsigned virtual_tile;
      if (i < large_count) {
        virtual_tile = Dkc1BabyKongLargeTile(
            i, tile & 1u, tile >> 1);
      } else if (i < large_count + small_count) {
        virtual_tile = small_start + i - large_count;
      } else {
        virtual_tile = extra_small_start +
            i - large_count - small_count;
      }
      unsigned source_tile;
      if (!Dkc1BabyKongResolveTile((uint8_t)dma1_count,
                                   (uint8_t)dma2_start,
                                   (uint8_t)dma2_count,
                                   virtual_tile, &source_tile)) {
        free(pixels);
        return false;
      }
      const uint8_t *source = graphics + source_tile * 32u;
      for (int y = 0; y < 8; y++) {
        for (int x = 0; x < 8; x++) {
          const uint8_t pixel = DecodeTilePixel(source, x, y);
          if (pixel) {
            const size_t destination =
                (size_t)(tile_y + y - y0) * (size_t)width +
                (size_t)(tile_x + x - x0);
            pixels[destination] = pixel;
            if (tile_y + y < opaque_y0)
              opaque_y0 = tile_y + y;
            if (tile_y + y > opaque_y1)
              opaque_y1 = tile_y + y;
          }
        }
      }
    }
  }
  if (opaque_y1 == -32768) {
    free(pixels);
    return false;
  }

  frame->name = manifest->name;
  frame->x0 = (int16_t)x0;
  frame->y0 = (int16_t)y0;
  frame->opaque_y0 = (int16_t)opaque_y0;
  frame->opaque_y1 = (int16_t)opaque_y1;
  frame->width = (uint16_t)width;
  frame->height = (uint16_t)height;
  frame->pixels = pixels;
  return true;
}

bool Dkc1BabyKongLoadRom(const char *path, char *error, size_t error_size) {
  if (!path || !*path) {
    SetError(error, error_size, "no DKC3 ROM selected");
    return false;
  }
  FILE *stream = fopen(path, "rb");
  if (!stream) {
    SetError(error, error_size, "unable to open the selected DKC3 ROM");
    return false;
  }
  uint8_t *rom = (uint8_t *)malloc(kDkc3RomSize);
  if (!rom) {
    fclose(stream);
    SetError(error, error_size, "out of memory while loading DKC3 ROM");
    return false;
  }
  const size_t read_size = fread(rom, 1, kDkc3RomSize, stream);
  const int trailing = fgetc(stream);
  const int read_error = ferror(stream);
  fclose(stream);
  if (read_size != kDkc3RomSize || trailing != EOF || read_error) {
    free(rom);
    SetError(error, error_size,
             "DKC3 ROM must be the headerless 4 MiB North American En,Fr release");
    return false;
  }

  uint8_t actual_hash[32];
  sha256_compute(rom, kDkc3RomSize, actual_hash);
  if (memcmp(actual_hash, kExpectedDkc3Sha256, sizeof actual_hash) != 0) {
    free(rom);
    SetError(error, error_size,
             "unsupported DKC3 ROM (expected SHA-256 2277a2d8...94a32b)");
    return false;
  }

  const size_t count = sizeof kFrameManifest / sizeof kFrameManifest[0];
  Dkc1BabyFrame *frames =
      (Dkc1BabyFrame *)calloc(count, sizeof frames[0]);
  if (!frames) {
    free(rom);
    SetError(error, error_size, "out of memory while decoding Kiddy frames");
    return false;
  }
  size_t decoded = 0;
  for (; decoded < count; decoded++) {
    if (!DecodeFrame(rom, &kFrameManifest[decoded], &frames[decoded])) {
      FreeFrames(frames, decoded);
      free(rom);
      SetError(error, error_size,
               "the verified DKC3 ROM did not match the Kiddy frame map");
      return false;
    }
  }
  uint16_t palette[16] = {0};
  for (int i = 1; i < 16; i++) {
    const size_t offset = kDkc3KiddyPaletteOffset + (size_t)(i - 1) * 2u;
    palette[i] = (uint16_t)(rom[offset] | ((uint16_t)rom[offset + 1] << 8));
  }
  free(rom);

  FreeFrames(s_frames, s_frame_count);
  s_frames = frames;
  s_frame_count = count;
  memcpy(s_palette, palette, sizeof s_palette);
  Dkc1BabyKongAnimationReset(&s_animation_tracker);
  (void)snprintf(s_status, sizeof s_status,
                 "%zu Kiddy Kong frames ready", s_frame_count);
  if (error && error_size)
    error[0] = '\0';
  return true;
}

void Dkc1BabyKongUnload(void) {
  FreeFrames(s_frames, s_frame_count);
  s_frames = NULL;
  s_frame_count = 0;
  s_enabled = false;
  s_draw_frame = NULL;
  Dkc1BabyKongAnimationReset(&s_animation_tracker);
  (void)snprintf(s_status, sizeof s_status,
                 "select a supported DKC3 ROM");
}

bool Dkc1BabyKongReady(void) {
  return s_frames != NULL && s_frame_count != 0;
}

size_t Dkc1BabyKongFrameCount(void) {
  return s_frame_count;
}

void Dkc1BabyKongSetEnabled(bool enabled) {
  s_enabled = enabled && Dkc1BabyKongReady();
  if (enabled && !Dkc1BabyKongReady())
    (void)snprintf(s_status, sizeof s_status,
                   "select a supported DKC3 ROM first");
  else if (s_enabled)
    (void)snprintf(s_status, sizeof s_status,
                   "Baby Kong mod enabled");
  else
    (void)snprintf(s_status, sizeof s_status,
                   "Baby Kong mod disabled");
}

bool Dkc1BabyKongEnabled(void) {
  return s_enabled && Dkc1BabyKongReady();
}

const char *Dkc1BabyKongStatus(void) {
  return s_status;
}

void Dkc1BabyKongInitializeFromEnvironment(void) {
  const char *path = getenv("DKC1_BABY_KONG_ROM");
  const char *enabled = getenv("DKC1_BABY_KONG");
  if (path && *path) {
    char error[192];
    if (Dkc1BabyKongLoadRom(path, error, sizeof error))
      Dkc1BabyKongSetEnabled(enabled && *enabled && *enabled != '0');
  }
}

static uint32_t FindDonkeySlot(const uint8_t *wram) {
  for (uint32_t slot = DKC1_ACTOR_SLOT_FIRST;
       slot <= DKC1_ACTOR_SLOT_LAST; slot += DKC1_ACTOR_SLOT_STEP) {
    if (Dkc1WramU16(wram, DKC1_WRAM_NorSpr_SpriteIDLo + slot) == 1u)
      return slot;
  }
  return 0;
}

void Dkc1BabyKongApplyMoves(uint8_t *wram) {
  if (!Dkc1BabyKongEnabled() || !wram ||
      Dkc1WramU16(wram, DKC1_WRAM_Player_CurrentKongLo) != 1u)
    return;
  const uint32_t slot = FindDonkeySlot(wram);
  if (!slot)
    return;

  int16_t x_velocity = (int16_t)Dkc1Actor_SprXVelocity(wram, slot);
  int16_t y_velocity = (int16_t)Dkc1Actor_SprYVelocity(wram, slot);
  const uint16_t state = Dkc1Actor_RAMTable1029Lo(wram, slot);
  const bool grounded = Dkc1BabyKongStateIsGrounded(state);
  if (!grounded && !Dkc1BabyKongStateIsAirborne(state))
    return;

  Dkc1BabyKongTuneVelocity(
      Dkc1WramU16(wram, DKC1_WRAM_JoypadHeld),
      Dkc1WramU16(wram, DKC1_WRAM_JoypadPressed), grounded,
      &x_velocity, &y_velocity);
  Dkc1Actor_SetSprXVelocity(wram, slot, (uint16_t)x_velocity);
  Dkc1Actor_SetSprYVelocity(wram, slot, (uint16_t)y_velocity);
}

static bool FrameBelongsToGroup(const char *name, const char *prefix) {
  const size_t prefix_size = strlen(prefix);
  return strcmp(name, prefix) == 0 ||
      (strncmp(name, prefix, prefix_size) == 0 &&
       isdigit((unsigned char)name[prefix_size]));
}

static const Dkc1BabyFrame *FindGroupFrame(const char *prefix,
                                            unsigned ordinal) {
  unsigned seen = 0;
  for (size_t i = 0; i < s_frame_count; i++) {
    const char *name = s_frames[i].name;
    if (FrameBelongsToGroup(name, prefix)) {
      if (seen++ == ordinal)
        return &s_frames[i];
    }
  }
  return NULL;
}

static unsigned GroupSize(const char *prefix) {
  unsigned count = 0;
  for (size_t i = 0; i < s_frame_count; i++) {
    if (FrameBelongsToGroup(s_frames[i].name, prefix))
      count++;
  }
  return count;
}

static const Dkc1BabyFrame *SelectFrame(const uint8_t *wram,
                                        uint32_t slot,
                                        Dkc1BabyKongAlignment *alignment) {
  const Dkc1BabyKongAnimationInput input = {
    .animation_id = Dkc1Actor_SprAnimID(wram, slot),
    .state = Dkc1Actor_RAMTable1029Lo(wram, slot),
    .native_pose = Dkc1Actor_DisplayedPoseLo(wram, slot),
    .x_velocity = (int16_t)Dkc1Actor_SprXVelocity(wram, slot),
    .y_velocity = (int16_t)Dkc1Actor_SprYVelocity(wram, slot),
  };
  const Dkc1BabyKongAnimationChoice choice =
      Dkc1BabyKongClassifyAnimation(&input);
  const unsigned count = GroupSize(choice.group);
  if (!count)
    return FindGroupFrame("Kiddy_Walk", 0);
  *alignment = choice.alignment;
  const unsigned ordinal = Dkc1BabyKongAnimationFrame(
      &s_animation_tracker, &input, choice, count);
  return FindGroupFrame(choice.group,
                        ordinal < count ? ordinal : count - 1u);
}

static int DecodeOamX(const Ppu *ppu, int slot) {
  const uint16_t word = ppu->oam[slot * 2];
  const unsigned shift = (unsigned)(slot & 3) * 2u;
  int x = (word & 0xffu) |
      (((ppu->highOam[slot >> 2] >> shift) & 1u) << 8);
  if (x >= 256)
    x -= 512;
  return x;
}

static bool OamMatchesDonkey(const Ppu *ppu, int slot, int anchor_x,
                             uint8_t properties) {
  const uint16_t position = ppu->oam[slot * 2];
  const uint8_t y = (uint8_t)(position >> 8);
  const uint8_t slot_properties = (uint8_t)(ppu->oam[slot * 2 + 1] >> 8);
  const int dx = DecodeOamX(ppu, slot) - anchor_x;
  return y != 0xf0u && (slot_properties & 0x7eu) == (properties & 0x7eu) &&
         Dkc1BabyKongOamXMatches(dx);
}

static bool FindDonkeyOamRun(const Ppu *ppu, int anchor_x,
                             uint8_t properties, uint8_t *first,
                             uint8_t *count) {
  int best_first = -1;
  int best_count = 0;
  for (int slot = 0; slot < 128;) {
    if (!OamMatchesDonkey(ppu, slot, anchor_x, properties)) {
      slot++;
      continue;
    }
    const int start = slot;
    while (slot < 128 &&
           OamMatchesDonkey(ppu, slot, anchor_x, properties))
      slot++;
    const int run = slot - start;
    if (run > best_count) {
      best_first = start;
      best_count = run;
    }
  }
  if (best_count < 2)
    return false;
  *first = (uint8_t)best_first;
  *count = (uint8_t)best_count;
  return true;
}

void Dkc1BabyKongPrepareFrame(Ppu *ppu, const uint8_t *wram,
                              int presentation_bias) {
  s_draw_frame = NULL;
  if (ppu)
    (void)PpuBindOverlaySurface(ppu, kPpuOverlaySource_Obj, NULL, 0);
  if (!Dkc1BabyKongEnabled() || !ppu || !wram ||
      PPU_forcedBlank(ppu) || PPU_brightness(ppu) == 0 ||
      ((ppu->screenEnabled[0] | ppu->screenEnabled[1]) & 0x10u) == 0 ||
      Dkc1WramU16(wram, DKC1_WRAM_Player_CurrentKongLo) != 1u)
    return;

  const uint32_t slot = FindDonkeySlot(wram);
  if (!slot)
    return;
  Dkc1BabyKongAlignment alignment = kDkc1BabyKongAlignFeet;
  const Dkc1BabyFrame *frame = SelectFrame(wram, slot, &alignment);
  if (!frame)
    return;
  const int frame_width = (int)(ppu->renderPitch / 4u);
  if (frame_width < kPpuXPixels || frame_width > kDkc1BabyCaptureWidth)
    return;

  const int actor_x =
      (int16_t)(Dkc1Actor_SprXPos(wram, slot) -
                Dkc1WramU16(wram, DKC1_WRAM_CameraX));
  const int anchor_x = actor_x - presentation_bias;
  const uint8_t properties =
      (uint8_t)(Dkc1Actor_SprFlags(wram, slot) >> 8);
  uint8_t first = 0;
  uint8_t count = 0;
  if (!FindDonkeyOamRun(ppu, anchor_x, properties, &first, &count))
    return;

  if (!PpuBindOverlaySurface(ppu, kPpuOverlaySource_Obj, s_capture,
                             ppu->renderPitch) ||
      !PpuSetOverlayCapture(ppu, kPpuOverlaySource_Obj,
                            anchor_x - 80, 0, 160, 224,
                            kPpuOverlayFlag_RemoveFromGame) ||
      !PpuSetOverlayOamRange(ppu, first, count)) {
    (void)PpuBindOverlaySurface(ppu, kPpuOverlaySource_Obj, NULL, 0);
    return;
  }

  s_draw_frame = frame;
  s_draw_anchor_x = anchor_x;
  s_draw_flip = (properties & 0x40u) != 0;
  s_draw_alignment = alignment;
}

static bool CapturedOpaqueBounds(const Ppu *ppu, int *top, int *bottom) {
  const int output_width = (int)(ppu->renderPitch / 4u);
  const int output_extra = (output_width - kPpuXPixels) / 2;
  int x0 = s_draw_anchor_x - 80 + output_extra;
  int x1 = s_draw_anchor_x + 80 + output_extra;
  if (x0 < 0) x0 = 0;
  if (x1 > output_width) x1 = output_width;
  int found_top = -1;
  int found_bottom = -1;
  for (int y = 0; y < 224; y++) {
    const uint32_t *row = (const uint32_t *)(
        s_capture + (size_t)y * ppu->renderPitch);
    for (int x = x0; x < x1; x++) {
      if (row[x] != 0) {
        if (found_top < 0)
          found_top = y;
        found_bottom = y;
        break;
      }
    }
  }
  if (found_top < 0)
    return false;
  *top = found_top;
  *bottom = found_bottom;
  return true;
}

void Dkc1BabyKongDrawFrame(Ppu *ppu) {
  const Dkc1BabyFrame *frame = s_draw_frame;
  s_draw_frame = NULL;
  if (!frame || !ppu || !ppu->renderBuffer)
    return;
  int native_top = 0;
  int native_bottom = 0;
  if (!CapturedOpaqueBounds(ppu, &native_top, &native_bottom))
    return;
  const int draw_anchor_y = s_draw_alignment == kDkc1BabyKongAlignCenter
      ? Dkc1BabyKongAnchorFromOpaqueCenters(
            native_top, native_bottom, frame->opaque_y0, frame->opaque_y1)
      : Dkc1BabyKongAnchorFromOpaqueBottom(
            native_bottom, frame->opaque_y1);
  const int output_width = (int)(ppu->renderPitch / 4u);
  const int output_extra = (output_width - kPpuXPixels) / 2;
  for (unsigned y = 0; y < frame->height; y++) {
    const int screen_y = draw_anchor_y + frame->y0 + (int)y;
    if (screen_y < 0 || screen_y >= 224)
      continue;
    uint32_t *destination =
        (uint32_t *)(ppu->renderBuffer + (size_t)screen_y * ppu->renderPitch);
    for (unsigned x = 0; x < frame->width; x++) {
      const uint8_t index = frame->pixels[(size_t)y * frame->width + x];
      if (!index)
        continue;
      const int local_x = frame->x0 + (int)x;
      const int screen_x = s_draw_anchor_x +
          (s_draw_flip ? -1 - local_x : local_x);
      const int output_x = screen_x + output_extra;
      if (output_x < 0 || output_x >= output_width)
        continue;
      const uint16_t color = s_palette[index & 0x0fu];
      const uint8_t red = ppu->brightnessMult[color & 0x1fu];
      const uint8_t green = ppu->brightnessMult[(color >> 5) & 0x1fu];
      const uint8_t blue = ppu->brightnessMult[(color >> 10) & 0x1fu];
      destination[output_x] = 0xff000000u | (uint32_t)red << 16 |
                              (uint32_t)green << 8 | blue;
    }
  }
}
