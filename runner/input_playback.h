#ifndef DKC1_INPUT_PLAYBACK_H
#define DKC1_INPUT_PLAYBACK_H

#include <stddef.h>
#include <stdint.h>

typedef struct Dkc1InputPlayback {
  uint32_t *frames;
  size_t count;
} Dkc1InputPlayback;

int Dkc1InputPlaybackLoad(const char *path, Dkc1InputPlayback *playback,
                          char *error, size_t error_size);
void Dkc1InputPlaybackFree(Dkc1InputPlayback *playback);
uint32_t Dkc1InputPlaybackFrame(const Dkc1InputPlayback *playback,
                                size_t frame);

#endif
