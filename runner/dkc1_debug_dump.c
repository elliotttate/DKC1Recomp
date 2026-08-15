#include "dkc1_debug_dump.h"

#include "common_rtl.h"
#include "sha256.h"
#include "snes/ppu.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <direct.h>
#define MakeDir(path) _mkdir(path)
#else
#include <sys/stat.h>
#define MakeDir(path) mkdir(path, 0777)
#endif

enum {
  kWramSize = 0x20000,
  kOamShadowBase = 0x0200,
  kOamBytes = 544,
  kActorFirst = 0x02,
  kActorLast = 0x32,
  kActorCount = (kActorLast - kActorFirst) / 2 + 1,
  kBookkeepingBase = 0x192B,
  kBookkeepingLength = 0x100,
  kLifecycleKeyframeInterval = 64,
};

/* WRAM semantics verified by the SuperZSNES effort
 * (DKCObjectLifecycleTracer/DkcTraceModel.cs, opcode-checked against the
 * clean ROM). Indexed arrays take the raw even actor index $02..$32. */
enum {
  kAddrEntranceId = 0x003E,
  kAddrScannerCursorPrimary = 0x00A0,
  kAddrScannerCursorSecondary = 0x00A2,
  kAddrScannerRecordIndex = 0x00A4,
  kAddrScannerWindowLeft = 0x00EF,
  kAddrScannerWindowRight = 0x00F1,
  kAddrActorPose = 0x0AE5,
  kAddrActorX = 0x0B19,
  kAddrActorY = 0x0BC1,
  kAddrActorGraphics = 0x0C69,
  kAddrActorCurrentPose = 0x0D11,
  kAddrActorId = 0x0D45,
  kAddrActorXSpeed = 0x0E89,
  kAddrActorYSpeed = 0x0EF1,
  kAddrActorState = 0x1029,
  kAddrActorAnimation = 0x10D1,
  kAddrActorSourceRecord = 0x15FD,
  kAddrCameraLowerBound = 0x1B23,
  kAddrCameraUpperBound = 0x1B25,
  kAddrLayerX = 0x088B,
  kAddrLayerY = 0x0895,
  kAddrSectionState = 0x1E03,
  kAddrSectionPointer = 0x1E05,
  kAddrSectionCurrent = 0x1E07,
  kAddrSectionPending = 0x1E09,
  kAddrSectionLimit = 0x1E0B,
  kEntranceCount = 0xE6,
};

static bool s_initialized;
static FILE *s_hash_log;
static FILE *s_oam_bin;
static FILE *s_oam_index;
static FILE *s_lifecycle;
static const char *s_session_dir = "session";
static FILE *s_checkpoint_index;

typedef struct ActorShadow {
  uint16_t id, source, state, animation;
} ActorShadow;
static ActorShadow s_actors[kActorCount];
static uint8_t s_bookkeeping[kBookkeepingLength];
static uint16_t s_scanner[5];
static uint16_t s_section[5];
static bool s_lifecycle_primed;
static int s_lifecycle_last_keyframe = -1000000;

static uint16_t Wram16(uint32_t address) {
  return (uint16_t)(g_ram[address] | ((uint16_t)g_ram[address + 1] << 8));
}

static uint64_t Fnv1a(const uint8_t *bytes, size_t size) {
  uint64_t hash = UINT64_C(1469598103934665603);
  for (size_t i = 0; i < size; i++) {
    hash ^= bytes[i];
    hash *= UINT64_C(1099511628211);
  }
  return hash;
}

static void HashHex(const uint8_t *data, size_t size, char out[65]) {
  uint8_t digest[32];
  sha256_compute(data, size, digest);
  for (int i = 0; i < 32; i++)
    snprintf(out + i * 2, 3, "%02x", digest[i]);
}

static FILE *OpenSuffixed(const char *prefix, const char *suffix,
                          const char *mode) {
  char path[512];
  snprintf(path, sizeof path, "%s%s", prefix, suffix);
  return fopen(path, mode);
}

static void Initialize(void) {
  if (s_initialized) return;
  s_initialized = true;

  const char *setting = getenv("DKC1_WRAM_HASH_LOG");
  if (setting && *setting)
    s_hash_log = fopen(setting, "wb");

  setting = getenv("DKC1_OAM_LOG");
  if (setting && *setting) {
    s_oam_bin = OpenSuffixed(setting, ".bin", "wb");
    s_oam_index = OpenSuffixed(setting, ".jsonl", "wb");
  }

  setting = getenv("DKC1_LIFECYCLE_TRACE");
  if (setting && *setting)
    s_lifecycle = fopen(setting, "wb");

  setting = getenv("DKC1_SESSION_DIR");
  if (setting && *setting)
    s_session_dir = _strdup(setting);
}

static void CollectOam(uint8_t out[2][kOamBytes]) {
  memcpy(out[0], g_ram + kOamShadowBase, kOamBytes);
  memcpy(out[1], g_ppu->oam, sizeof g_ppu->oam);
  memcpy(out[1] + sizeof g_ppu->oam, g_ppu->highOam, sizeof g_ppu->highOam);
}

/* ---- transition-only lifecycle trace -------------------------------- */

static bool GameplayGate(void) {
  const uint16_t entrance = Wram16(kAddrEntranceId);
  const uint16_t lower = Wram16(kAddrCameraLowerBound);
  const uint16_t upper = Wram16(kAddrCameraUpperBound);
  return entrance < kEntranceCount && lower != 0 && upper > lower;
}

static void EmitActor(FILE *out, const char *event, int frame, int slot) {
  const uint32_t index = (uint32_t)(kActorFirst + slot * 2);
  fprintf(out,
      "{\"event\":\"%s\",\"frame\":%d,\"slot\":%u,"
      "\"id\":%u,\"source\":%d,\"x\":%u,\"y\":%u,"
      "\"xs\":%d,\"ys\":%d,\"state\":%u,\"anim\":%u,"
      "\"pose\":%u,\"gfx\":%u,"
      "\"camera\":[%u,%u]}\n",
      event, frame, index,
      Wram16(kAddrActorId + index),
      (int16_t)Wram16(kAddrActorSourceRecord + index),
      Wram16(kAddrActorX + index), Wram16(kAddrActorY + index),
      (int16_t)Wram16(kAddrActorXSpeed + index),
      (int16_t)Wram16(kAddrActorYSpeed + index),
      Wram16(kAddrActorState + index),
      Wram16(kAddrActorAnimation + index),
      Wram16(kAddrActorPose + index),
      Wram16(kAddrActorGraphics + index),
      Wram16(kAddrLayerX), Wram16(kAddrLayerY));
}

static void LifecycleFrame(int frame) {
  if (!s_lifecycle) return;
  if (!GameplayGate()) {
    if (s_lifecycle_primed) {
      fprintf(s_lifecycle,
              "{\"event\":\"gameplay_exit\",\"frame\":%d}\n", frame);
      s_lifecycle_primed = false;
    }
    return;
  }

  if (!s_lifecycle_primed) {
    fprintf(s_lifecycle,
        "{\"event\":\"gameplay_enter\",\"frame\":%d,\"entrance\":%u,"
        "\"bounds\":[%u,%u]}\n",
        frame, Wram16(kAddrEntranceId), Wram16(kAddrCameraLowerBound),
        Wram16(kAddrCameraUpperBound));
    memset(s_actors, 0, sizeof s_actors);
    memcpy(s_bookkeeping, g_ram + kBookkeepingBase, kBookkeepingLength);
    memset(s_scanner, 0xFF, sizeof s_scanner);
    memset(s_section, 0xFF, sizeof s_section);
    s_lifecycle_primed = true;
    s_lifecycle_last_keyframe = -1000000;
  }

  for (int slot = 0; slot < kActorCount; slot++) {
    const uint32_t index = (uint32_t)(kActorFirst + slot * 2);
    ActorShadow now;
    now.id = Wram16(kAddrActorId + index);
    now.source = Wram16(kAddrActorSourceRecord + index);
    now.state = Wram16(kAddrActorState + index);
    now.animation = Wram16(kAddrActorAnimation + index);
    ActorShadow *prev = &s_actors[slot];
    if (now.id != prev->id || now.source != prev->source) {
      if (prev->id == 0 && now.id != 0)
        EmitActor(s_lifecycle, "slot_alloc", frame, slot);
      else if (prev->id != 0 && now.id == 0)
        fprintf(s_lifecycle,
            "{\"event\":\"slot_free\",\"frame\":%d,\"slot\":%u,"
            "\"prev_id\":%u,\"prev_source\":%d}\n",
            frame, (unsigned)index, prev->id, (int16_t)prev->source);
      else
        EmitActor(s_lifecycle, "slot_retype", frame, slot);
    } else if (now.id != 0 && now.state != prev->state) {
      EmitActor(s_lifecycle, "slot_state", frame, slot);
    }
    *prev = now;
  }

  const uint8_t *book = g_ram + kBookkeepingBase;
  for (int i = 0; i < kBookkeepingLength; i++) {
    if (book[i] != s_bookkeeping[i]) {
      fprintf(s_lifecycle,
          "{\"event\":\"bookmark\",\"frame\":%d,\"record\":%d,"
          "\"from\":%u,\"to\":%u}\n",
          frame, i, s_bookkeeping[i], book[i]);
      s_bookkeeping[i] = book[i];
    }
  }

  const uint16_t scanner[5] = {
    Wram16(kAddrScannerWindowLeft), Wram16(kAddrScannerWindowRight),
    (uint16_t)g_ram[kAddrScannerCursorPrimary],
    (uint16_t)g_ram[kAddrScannerCursorSecondary],
    (uint16_t)g_ram[kAddrScannerRecordIndex],
  };
  if (memcmp(scanner, s_scanner, sizeof scanner) != 0) {
    fprintf(s_lifecycle,
        "{\"event\":\"scanner\",\"frame\":%d,\"window\":[%u,%u],"
        "\"cursors\":[%u,%u,%u]}\n",
        frame, scanner[0], scanner[1], scanner[2], scanner[3], scanner[4]);
    memcpy(s_scanner, scanner, sizeof scanner);
  }

  const uint16_t section[5] = {
    Wram16(kAddrSectionState), Wram16(kAddrSectionPointer),
    Wram16(kAddrSectionCurrent), Wram16(kAddrSectionPending),
    Wram16(kAddrSectionLimit),
  };
  if (memcmp(section, s_section, sizeof section) != 0) {
    fprintf(s_lifecycle,
        "{\"event\":\"section\",\"frame\":%d,\"state\":%u,\"pointer\":%u,"
        "\"current\":%u,\"pending\":%u,\"limit\":%u}\n",
        frame, section[0], section[1], section[2], section[3], section[4]);
    memcpy(s_section, section, sizeof section);
  }

  if (frame - s_lifecycle_last_keyframe >= kLifecycleKeyframeInterval) {
    s_lifecycle_last_keyframe = frame;
    fprintf(s_lifecycle,
        "{\"event\":\"keyframe\",\"frame\":%d,\"camera\":[%u,%u],"
        "\"bounds\":[%u,%u],\"slots\":[",
        frame, Wram16(kAddrLayerX), Wram16(kAddrLayerY),
        Wram16(kAddrCameraLowerBound), Wram16(kAddrCameraUpperBound));
    for (int slot = 0; slot < kActorCount; slot++) {
      const uint32_t index = (uint32_t)(kActorFirst + slot * 2);
      fprintf(s_lifecycle, "%s[%u,%d,%u,%u]", slot ? "," : "",
              Wram16(kAddrActorId + index),
              (int16_t)Wram16(kAddrActorSourceRecord + index),
              Wram16(kAddrActorX + index), Wram16(kAddrActorY + index));
    }
    fprintf(s_lifecycle, "]}\n");
  }
}

/* ---- public entry points -------------------------------------------- */

void Dkc1DebugDumpFrame(int frame) {
  Initialize();

  if (s_hash_log)
    fprintf(s_hash_log, "%d %016llx\n", frame,
            (unsigned long long)Fnv1a(g_ram, kWramSize));

  if (s_oam_bin) {
    uint8_t oam[2][kOamBytes];
    CollectOam(oam);
    uint32_t header = (uint32_t)frame;
    fwrite(&header, sizeof header, 1, s_oam_bin);
    fwrite(oam, 1, sizeof oam, s_oam_bin);
    char shadow_hash[65], ppu_hash[65];
    HashHex(oam[0], kOamBytes, shadow_hash);
    HashHex(oam[1], kOamBytes, ppu_hash);
    fprintf(s_oam_index,
        "{\"frame\":%d,\"shadow\":\"%s\",\"ppu\":\"%s\"}\n",
        frame, shadow_hash, ppu_hash);
  }

  LifecycleFrame(frame);
}

bool Dkc1DebugCheckpoint(const char *name, int frame) {
  Initialize();
  MakeDir(s_session_dir);
  if (!s_checkpoint_index) {
    char path[512];
    snprintf(path, sizeof path, "%s/checkpoints.jsonl", s_session_dir);
    s_checkpoint_index = fopen(path, "ab");
    if (!s_checkpoint_index) return false;
  }

  char path[512];
  snprintf(path, sizeof path, "%s/%s.wram.bin", s_session_dir, name);
  FILE *wram = fopen(path, "wb");
  if (!wram) return false;
  const bool wrote = fwrite(g_ram, 1, kWramSize, wram) == kWramSize;
  fclose(wram);
  if (!wrote) return false;

  char wram_hash[65], vram_hash[65], shadow_hash[65], ppu_hash[65];
  uint8_t oam[2][kOamBytes];
  CollectOam(oam);
  HashHex(g_ram, kWramSize, wram_hash);
  HashHex((const uint8_t *)g_ppu->vram, sizeof g_ppu->vram, vram_hash);
  HashHex(oam[0], kOamBytes, shadow_hash);
  HashHex(oam[1], kOamBytes, ppu_hash);
  fprintf(s_checkpoint_index,
      "{\"name\":\"%s\",\"frame\":%d,\"wram\":\"%s\",\"vram\":\"%s\","
      "\"oam_shadow\":\"%s\",\"oam_ppu\":\"%s\"}\n",
      name, frame, wram_hash, vram_hash, shadow_hash, ppu_hash);
  fflush(s_checkpoint_index);
  return true;
}

void Dkc1DebugDumpClose(void) {
  if (s_hash_log) fclose(s_hash_log);
  if (s_oam_bin) fclose(s_oam_bin);
  if (s_oam_index) fclose(s_oam_index);
  if (s_lifecycle) fclose(s_lifecycle);
  if (s_checkpoint_index) fclose(s_checkpoint_index);
  s_hash_log = NULL;
  s_oam_bin = s_oam_index = s_lifecycle = s_checkpoint_index = NULL;
  s_initialized = false;
}
