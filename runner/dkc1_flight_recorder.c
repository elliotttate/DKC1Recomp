#include "dkc1_flight_recorder.h"

#include "common_rtl.h"
#include "sha256.h"
#include "snes/ppu.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#ifdef _WIN32
#include <direct.h>
#include <process.h>
#define MakeDir(path) _mkdir(path)
#define ProcessId() _getpid()
#define PathSeparator '\\'
#else
#include <sys/stat.h>
#include <unistd.h>
#define MakeDir(path) mkdir(path, 0777)
#define ProcessId() getpid()
#define PathSeparator '/'
#endif

enum {
  kInputCapacity = 3600,       /* about one minute at NTSC cadence */
  kAnchorInterval = 300,       /* five seconds */
  kAnchorCount = 16,           /* comfortably covers the input ring */
  kWramSize = 0x20000,
  kPpuOamSize = 544,
};

typedef struct InputFrame {
  long frame;
  uint32_t mask;
} InputFrame;

typedef struct SnapshotAnchor {
  long frame;
  uint8_t *data;
  size_t size;
  int valid;
} SnapshotAnchor;

static int s_initialized;
static int s_enabled;
static char s_root[768] = "flight-recorder";
static InputFrame s_inputs[kInputCapacity];
static SnapshotAnchor s_anchors[kAnchorCount];
static int s_next_anchor;

static void SetError(char *error, size_t error_size, const char *message) {
  if (error && error_size)
    snprintf(error, error_size, "%s", message ? message : "unknown error");
}

static void HashHex(const uint8_t *data, size_t size, char output[65]) {
  uint8_t digest[32];
  sha256_compute(data, size, digest);
  for (int i = 0; i < 32; i++)
    snprintf(output + i * 2, 3, "%02x", digest[i]);
}

static int WriteExact(const char *path, const void *data, size_t size) {
  FILE *stream = fopen(path, "wb");
  int ok = stream && fwrite(data, 1, size, stream) == size;
  if (stream && fclose(stream) != 0) ok = 0;
  return ok;
}

static int CaptureAnchor(long frame, char *error, size_t error_size) {
  size_t needed = RtlSaveSnapshotToMemory(NULL, 0);
  if (!needed) {
    SetError(error, error_size, "unable to size native snapshot");
    return 0;
  }
  SnapshotAnchor *anchor = &s_anchors[s_next_anchor];
  if (anchor->size < needed) {
    uint8_t *replacement = (uint8_t *)realloc(anchor->data, needed);
    if (!replacement) {
      SetError(error, error_size, "out of memory allocating snapshot anchor");
      return 0;
    }
    anchor->data = replacement;
    anchor->size = needed;
  }
  size_t written = RtlSaveSnapshotToMemory(anchor->data, anchor->size);
  if (written != needed) {
    anchor->valid = 0;
    SetError(error, error_size, "native snapshot size changed during capture");
    return 0;
  }
  anchor->frame = frame;
  anchor->size = written;
  anchor->valid = 1;
  s_next_anchor = (s_next_anchor + 1) % kAnchorCount;
  return 1;
}

int Dkc1FlightRecorderInitialize(char *error, size_t error_size) {
  if (s_initialized) return s_enabled ? 1 : 0;
  s_initialized = 1;
  const char *setting = getenv("DKC1_FLIGHT_RECORDER");
  if (!setting || !*setting || strcmp(setting, "0") == 0)
    return 0;
  const char *root = getenv("DKC1_FLIGHT_RECORDER_DIR");
  if (root && *root) {
    if (strlen(root) >= sizeof s_root) {
      SetError(error, error_size, "flight-recorder root path is too long");
      return -1;
    }
    snprintf(s_root, sizeof s_root, "%s", root);
  }
  if (MakeDir(s_root) != 0) {
    /* An existing directory is expected; prove it is usable below by
     * capturing in memory and defer filesystem validation to export. */
  }
  if (!CaptureAnchor(0, error, error_size))
    return -1;
  s_enabled = 1;
  return 1;
}

int Dkc1FlightRecorderEnabled(void) {
  return s_enabled;
}

void Dkc1FlightRecorderRecord(long completed_frame, uint32_t input_mask) {
  if (!s_enabled || completed_frame <= 0) return;
  const size_t index = (size_t)completed_frame % kInputCapacity;
  s_inputs[index].frame = completed_frame;
  s_inputs[index].mask = input_mask;
  if (completed_frame % kAnchorInterval == 0) {
    char ignored[1];
    (void)CaptureAnchor(completed_frame, ignored, sizeof ignored);
  }
}

static const SnapshotAnchor *OldestCoveredAnchor(long current_frame) {
  const long oldest_input = current_frame > kInputCapacity
                                ? current_frame - kInputCapacity : 0;
  const SnapshotAnchor *best = NULL;
  for (int i = 0; i < kAnchorCount; i++) {
    const SnapshotAnchor *candidate = &s_anchors[i];
    if (!candidate->valid || candidate->frame < oldest_input ||
        candidate->frame > current_frame)
      continue;
    if (!best || candidate->frame < best->frame)
      best = candidate;
  }
  return best;
}

static void BuildPpuOam(uint8_t output[kPpuOamSize]) {
  for (int i = 0; i < 256; i++) {
    output[i * 2] = (uint8_t)g_ppu->oam[i];
    output[i * 2 + 1] = (uint8_t)(g_ppu->oam[i] >> 8);
  }
  memcpy(output + 512, g_ppu->highOam, 32);
}

static int MakeBundlePath(long frame, char *path, size_t path_size) {
  time_t now = time(NULL);
  struct tm local;
#ifdef _WIN32
  localtime_s(&local, &now);
#else
  localtime_r(&now, &local);
#endif
  char stamp[32];
  if (!strftime(stamp, sizeof stamp, "%Y%m%d-%H%M%S", &local))
    return 0;
  int length = snprintf(path, path_size, "%s%ccapture-f%08ld-%s-p%d",
                        s_root, PathSeparator, frame, stamp, ProcessId());
  return length > 0 && (size_t)length < path_size;
}

int Dkc1FlightRecorderExport(long completed_frame,
                             char *bundle_path, size_t bundle_path_size,
                             char *error, size_t error_size) {
  if (!s_enabled) {
    SetError(error, error_size,
             "flight recorder is disabled (set DKC1_FLIGHT_RECORDER=1)");
    return 0;
  }
  const SnapshotAnchor *anchor = OldestCoveredAnchor(completed_frame);
  if (!anchor) {
    SetError(error, error_size, "no covered snapshot anchor is available");
    return 0;
  }
  char bundle[1024];
  if (!MakeBundlePath(completed_frame, bundle, sizeof bundle) ||
      MakeDir(bundle) != 0) {
    SetError(error, error_size, "unable to create repro bundle directory");
    return 0;
  }

  const long replay_frames = completed_frame - anchor->frame;
  const size_t input_size = replay_frames > 0 ? (size_t)replay_frames * 5 : 0;
  char *input_text = (char *)malloc(input_size + 1);
  if (!input_text) {
    SetError(error, error_size, "out of memory exporting input history");
    return 0;
  }
  size_t input_offset = 0;
  for (long frame = anchor->frame + 1; frame <= completed_frame; frame++) {
    const InputFrame *entry = &s_inputs[(size_t)frame % kInputCapacity];
    if (entry->frame != frame) {
      free(input_text);
      SetError(error, error_size, "rolling input history has a gap");
      return 0;
    }
    input_offset += (size_t)snprintf(input_text + input_offset,
                                     input_size - input_offset + 1,
                                     "%03X\n", entry->mask & 0xfffu);
  }

  size_t current_size = RtlSaveSnapshotToMemory(NULL, 0);
  uint8_t *current = (uint8_t *)malloc(current_size ? current_size : 1);
  if (!current || !current_size ||
      RtlSaveSnapshotToMemory(current, current_size) != current_size) {
    free(current);
    free(input_text);
    SetError(error, error_size, "unable to capture final native snapshot");
    return 0;
  }

  char path[1200];
#define WRITE_BUNDLE_FILE(name, data, size)                                \
  do {                                                                     \
    snprintf(path, sizeof path, "%s%c%s", bundle, PathSeparator, name);   \
    if (!WriteExact(path, data, size)) goto write_failed;                  \
  } while (0)
  WRITE_BUNDLE_FILE("anchor.snapshot", anchor->data, anchor->size);
  WRITE_BUNDLE_FILE("current.snapshot", current, current_size);
  WRITE_BUNDLE_FILE("inputs.txt", input_text, input_offset);
  WRITE_BUNDLE_FILE("final.wram.bin", g_ram, kWramSize);
  WRITE_BUNDLE_FILE("final.vram.bin", g_ppu->vram, sizeof g_ppu->vram);
  WRITE_BUNDLE_FILE("final.cgram.bin", g_ppu->cgram, sizeof g_ppu->cgram);
  WRITE_BUNDLE_FILE("final.wram-oam.bin", g_ram + 0x0200, kPpuOamSize);
  uint8_t ppu_oam[kPpuOamSize];
  BuildPpuOam(ppu_oam);
  WRITE_BUNDLE_FILE("final.ppu-oam.bin", ppu_oam, sizeof ppu_oam);
#undef WRITE_BUNDLE_FILE

  char anchor_hash[65], current_hash[65], input_hash[65], wram_hash[65];
  char vram_hash[65], cgram_hash[65], wram_oam_hash[65], ppu_oam_hash[65];
  HashHex(anchor->data, anchor->size, anchor_hash);
  HashHex(current, current_size, current_hash);
  HashHex((const uint8_t *)input_text, input_offset, input_hash);
  HashHex(g_ram, kWramSize, wram_hash);
  HashHex((const uint8_t *)g_ppu->vram, sizeof g_ppu->vram, vram_hash);
  HashHex((const uint8_t *)g_ppu->cgram, sizeof g_ppu->cgram, cgram_hash);
  HashHex(g_ram + 0x0200, kPpuOamSize, wram_oam_hash);
  HashHex(ppu_oam, sizeof ppu_oam, ppu_oam_hash);
  char manifest[3072];
  int manifest_length = snprintf(
      manifest, sizeof manifest,
      "{\n"
      "  \"schema\": \"dkc1.flight-recorder.v1\",\n"
      "  \"anchor_frame\": %ld,\n"
      "  \"current_frame\": %ld,\n"
      "  \"snes_frame\": %d,\n"
      "  \"replay_frames\": %ld,\n"
      "  \"scene\": {\"mode\": %u, \"level\": %u, "
      "\"entrance\": %u},\n"
      "  \"rom_sha256\": "
      "\"fa8cacf5bbfc39ee6bbaa557adf89133d60d42f6cf9e1db30d5a36a469f74d15\",\n"
      "  \"files\": {\n"
      "    \"anchor.snapshot\": \"%s\",\n"
      "    \"current.snapshot\": \"%s\",\n"
      "    \"inputs.txt\": \"%s\",\n"
      "    \"final.wram.bin\": \"%s\",\n"
      "    \"final.vram.bin\": \"%s\",\n"
      "    \"final.cgram.bin\": \"%s\",\n"
      "    \"final.wram-oam.bin\": \"%s\",\n"
      "    \"final.ppu-oam.bin\": \"%s\"\n"
      "  }\n"
      "}\n",
      anchor->frame, completed_frame, snes_frame_counter, replay_frames,
      (unsigned)(g_ram[0x0032] | ((uint16_t)g_ram[0x0033] << 8)),
      (unsigned)(g_ram[0x0030] | ((uint16_t)g_ram[0x0031] << 8)),
      (unsigned)(g_ram[0x003e] | ((uint16_t)g_ram[0x003f] << 8)),
      anchor_hash, current_hash, input_hash, wram_hash, vram_hash,
      cgram_hash, wram_oam_hash, ppu_oam_hash);
  if (manifest_length <= 0 || (size_t)manifest_length >= sizeof manifest) {
    free(current);
    free(input_text);
    SetError(error, error_size, "flight recorder manifest overflow");
    return 0;
  }
  snprintf(path, sizeof path, "%s%cmanifest.json", bundle, PathSeparator);
  if (!WriteExact(path, manifest, (size_t)manifest_length))
    goto write_failed;

  free(current);
  free(input_text);
  if (bundle_path && bundle_path_size)
    snprintf(bundle_path, bundle_path_size, "%s", bundle);
  return 1;

write_failed:
  free(current);
  free(input_text);
  SetError(error, error_size, "unable to write one or more bundle files");
  return 0;
}

void Dkc1FlightRecorderClose(void) {
  for (int i = 0; i < kAnchorCount; i++) {
    free(s_anchors[i].data);
    memset(&s_anchors[i], 0, sizeof s_anchors[i]);
  }
  memset(s_inputs, 0, sizeof s_inputs);
  s_enabled = 0;
  s_initialized = 0;
  s_next_anchor = 0;
}
