#include "dkc1_msu1.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

enum {
  kMsuInputRate = 44100,
  kMsuPcmHeaderSize = 8,
  kMsuMaximumTheme = 31,
  kMsuTrackCount = kMsuMaximumTheme + 1,
  kSpcMuteRomOffset = 0x0AA9E5,
};

typedef struct Dkc1MsuTrack {
  const uint8_t *mapping;
  size_t mapping_size;
  uint32_t total_frames;
  uint32_t loop_frame;
  int descriptor;
  bool present;
} Dkc1MsuTrack;

struct Dkc1Msu1 {
  char directory[PATH_MAX];
  Dkc1MsuTrack tracks[kMsuTrackCount];
  const Dkc1MsuTrack *track;
  uint32_t total_frames;
  uint32_t loop_frame;
  uint32_t source_frame;
  uint32_t phase;
  uint16_t theme;
  unsigned track_number;
  int16_t current_sample[2];
  int16_t next_sample[2];
  double gain;
  bool loop;
  bool playing;
  bool music_state_valid;
  bool started;
};

/* Restoration playback policy, indexed by DKC's zero-based music ID. */
static const uint8_t kLoopTheme[27] = {
  1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 1, 1, 1,
  1, 0, 1, 0, 0, 1, 1, 1, 1, 1, 1, 0, 0,
};

static void SetError(char *error, size_t error_size, const char *message) {
  if (error && error_size)
    snprintf(error, error_size, "%s", message ? message : "unknown error");
}

static uint32_t ReadLittle32(const uint8_t bytes[4]) {
  return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8) |
         ((uint32_t)bytes[2] << 16) | ((uint32_t)bytes[3] << 24);
}

static int16_t ReadLittle16(const uint8_t bytes[2]) {
  return (int16_t)(uint16_t)(bytes[0] | ((uint16_t)bytes[1] << 8));
}

static void CloseTrack(Dkc1Msu1 *player) {
  player->track = NULL;
  player->playing = false;
  player->track_number = 0;
  player->total_frames = 0;
  player->source_frame = 0;
  player->phase = 0;
  memset(player->current_sample, 0, sizeof player->current_sample);
  memset(player->next_sample, 0, sizeof player->next_sample);
}

static bool SeekFrame(Dkc1Msu1 *player, uint32_t frame) {
  if (!player->track || frame >= player->total_frames)
    return false;
  player->source_frame = frame;
  return true;
}

static bool ReadFrame(Dkc1Msu1 *player, int16_t sample[2]) {
  if (!player->track)
    return false;
  if (player->source_frame >= player->total_frames) {
    if (!player->loop || !SeekFrame(player, player->loop_frame))
      return false;
  }
  const size_t offset =
      kMsuPcmHeaderSize + (size_t)player->source_frame * 4u;
  if (offset > player->track->mapping_size ||
      player->track->mapping_size - offset < 4u)
    return false;
  const uint8_t *bytes = player->track->mapping + offset;
  sample[0] = ReadLittle16(bytes);
  sample[1] = ReadLittle16(bytes + 2);
  player->source_frame++;
  return true;
}

static void UnmapTrack(Dkc1MsuTrack *track) {
  if (!track)
    return;
  if (track->mapping && track->mapping_size)
    (void)munmap((void *)track->mapping, track->mapping_size);
  if (track->descriptor >= 0)
    (void)close(track->descriptor);
  *track = (Dkc1MsuTrack){.descriptor = -1};
}

/* Map each PCM once during host startup. Playback then performs deterministic
 * pointer reads instead of tens of thousands of stdio calls on the frame-
 * critical thread. MADV_SEQUENTIAL lets the kernel keep the next PCM pages
 * ahead of the mixer without changing the sample timeline. */
static int MapTrackFile(const char *path, Dkc1MsuTrack *track) {
  if (!path || !track)
    return -1;
  const int descriptor = open(path, O_RDONLY);
  if (descriptor < 0)
    return errno == ENOENT ? 0 : -1;
  struct stat info;
  if (fstat(descriptor, &info) != 0 ||
      info.st_size < kMsuPcmHeaderSize + 4 ||
      (uintmax_t)info.st_size > (uintmax_t)SIZE_MAX) {
    (void)close(descriptor);
    return -1;
  }
  const size_t mapping_size = (size_t)info.st_size;
  const uint8_t *mapping = mmap(
      NULL, mapping_size, PROT_READ, MAP_PRIVATE, descriptor, 0);
  if (mapping == MAP_FAILED) {
    (void)close(descriptor);
    return -1;
  }
  if (memcmp(mapping, "MSU1", 4) != 0) {
    (void)munmap((void *)mapping, mapping_size);
    (void)close(descriptor);
    return -1;
  }
#ifdef F_RDAHEAD
  (void)fcntl(descriptor, F_RDAHEAD, 1);
#endif
#ifdef MADV_SEQUENTIAL
  (void)madvise((void *)mapping, mapping_size, MADV_SEQUENTIAL);
#endif
  track->mapping = mapping;
  track->mapping_size = mapping_size;
  track->total_frames =
      (uint32_t)((mapping_size - kMsuPcmHeaderSize) / 4u);
  track->loop_frame = ReadLittle32(mapping + 4);
  track->descriptor = descriptor;
  track->present = true;
  return 1;
}

static int CacheTrack(Dkc1Msu1 *player, unsigned track_number) {
  if (!player || track_number == 0 || track_number > kMsuTrackCount)
    return -1;
  Dkc1MsuTrack *track = &player->tracks[track_number - 1u];
  const char *patterns[] = {"%s/track-%u.pcm", "%s/dkc_msu-%u.pcm"};
  char path[PATH_MAX];
  for (size_t i = 0; i < sizeof patterns / sizeof patterns[0]; i++) {
    if (snprintf(path, sizeof path, patterns[i], player->directory,
                 track_number) >= (int)sizeof path)
      continue;
    const int mapped = MapTrackFile(path, track);
    if (mapped != 0)
      return mapped;
  }
  return 0;
}

static bool OpenTrack(Dkc1Msu1 *player, unsigned theme) {
  CloseTrack(player);
  if (theme > kMsuMaximumTheme)
    return false;

  const unsigned track = theme + 1;
  const Dkc1MsuTrack *cached = &player->tracks[track - 1u];
  if (!cached->present)
    return false;
  player->track = cached;
  player->total_frames = cached->total_frames;
  player->loop_frame = cached->loop_frame;
  player->loop = theme < sizeof kLoopTheme && kLoopTheme[theme] != 0 &&
                 player->loop_frame < player->total_frames;
  player->track_number = track;
  player->phase = 0;
  if (!SeekFrame(player, 0) ||
      !ReadFrame(player, player->current_sample) ||
      !ReadFrame(player, player->next_sample)) {
    CloseTrack(player);
    return false;
  }
  player->playing = true;
  return true;
}

Dkc1Msu1 *Dkc1Msu1Open(const char *directory, char *error,
                        size_t error_size) {
  if (!directory || !*directory) {
    SetError(error, error_size, "MSU-1 directory is empty");
    return NULL;
  }
  Dkc1Msu1 *player = calloc(1, sizeof *player);
  if (!player) {
    SetError(error, error_size, "out of memory opening MSU-1 pack");
    return NULL;
  }
  if (snprintf(player->directory, sizeof player->directory, "%s", directory) >=
      (int)sizeof player->directory) {
    SetError(error, error_size, "MSU-1 directory path is too long");
    free(player);
    return NULL;
  }
  for (unsigned track = 0; track < kMsuTrackCount; track++)
    player->tracks[track].descriptor = -1;
  for (unsigned track = 1; track <= kMsuTrackCount; track++)
    (void)CacheTrack(player, track);
  if (!player->tracks[0].present) {
    SetError(error, error_size, "MSU-1 pack has no valid track 1");
    Dkc1Msu1Close(player);
    return NULL;
  }
  player->gain = 1.0;
  const char *gain = getenv("DKC1_MSU1_GAIN");
  if (gain && *gain) {
    char *end = NULL;
    const double parsed = strtod(gain, &end);
    if (end && !*end && parsed >= 0.0 && parsed <= 4.0)
      player->gain = parsed;
  }
  SetError(error, error_size, "");
  return player;
}

void Dkc1Msu1Close(Dkc1Msu1 *player) {
  if (!player)
    return;
  CloseTrack(player);
  for (unsigned track = 0; track < kMsuTrackCount; track++)
    UnmapTrack(&player->tracks[track]);
  free(player);
}

int Dkc1Msu1ApplySpcMusicMute(uint8_t *rom, size_t rom_size,
                              char *error, size_t error_size) {
  /* Source bytes at HiROM $CAA9E5 in the checksum-locked USA v1.0 ROM. */
  static const uint8_t expected[2] = {0x01, 0xD4};
  static const uint8_t replacement[2] = {0x00, 0x6F};
  if (!rom || rom_size <= kSpcMuteRomOffset + 1) {
    SetError(error, error_size, "verified ROM is too small for MSU-1 mute");
    return 0;
  }
  if (memcmp(rom + kSpcMuteRomOffset, expected, sizeof expected) != 0) {
    SetError(error, error_size,
             "verified ROM does not match the MSU-1 SPC mute source bytes");
    return 0;
  }
  memcpy(rom + kSpcMuteRomOffset, replacement, sizeof replacement);
  SetError(error, error_size, "");
  return 1;
}

void Dkc1Msu1ObserveMusicState(Dkc1Msu1 *player, uint16_t requested_theme,
                               uint16_t start_state) {
  if (!player)
    return;

  const bool started = start_state != 0;
  const bool request_changed =
      !player->music_state_valid || player->theme != requested_theme;
  const bool start_changed =
      !player->music_state_valid || player->started != started;
  player->theme = requested_theme;
  player->started = started;

  if (!started) {
    if (start_changed || player->playing)
      CloseTrack(player);
  } else if (request_changed || start_changed) {
    (void)OpenTrack(player, requested_theme);
  }
  player->music_state_valid = true;
}

void Dkc1Msu1Reset(Dkc1Msu1 *player) {
  if (player) {
    CloseTrack(player);
    player->music_state_valid = false;
    player->started = false;
  }
}

static int16_t Saturate16(int value) {
  if (value > INT16_MAX)
    return INT16_MAX;
  if (value < INT16_MIN)
    return INT16_MIN;
  return (int16_t)value;
}

void Dkc1Msu1Mix(Dkc1Msu1 *player, int16_t *samples, int frames,
                  int channels, int output_rate) {
  if (!player || !player->playing || !samples || frames <= 0 ||
      channels != 2 || output_rate <= 0)
    return;

  for (int frame = 0; frame < frames && player->playing; frame++) {
    for (int channel = 0; channel < 2; channel++) {
      const int64_t interpolated =
          ((int64_t)player->current_sample[channel] *
               (output_rate - (int)player->phase) +
           (int64_t)player->next_sample[channel] * player->phase) /
          output_rate;
      const int external = (int)(interpolated * player->gain);
      const int index = frame * channels + channel;
      samples[index] = Saturate16((int)samples[index] + external);
    }

    player->phase += kMsuInputRate;
    while (player->phase >= (uint32_t)output_rate) {
      player->phase -= (uint32_t)output_rate;
      player->current_sample[0] = player->next_sample[0];
      player->current_sample[1] = player->next_sample[1];
      if (!ReadFrame(player, player->next_sample)) {
        player->playing = false;
        break;
      }
    }
  }
}

unsigned Dkc1Msu1CurrentTrack(const Dkc1Msu1 *player) {
  return player ? player->track_number : 0;
}

const char *Dkc1Msu1Directory(const Dkc1Msu1 *player) {
  return player ? player->directory : "";
}
