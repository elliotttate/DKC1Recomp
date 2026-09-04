#ifndef DKC1_BABY_KONG_ANIMATION_H
#define DKC1_BABY_KONG_ANIMATION_H

#include <stdbool.h>
#include <stdint.h>

typedef enum Dkc1BabyKongAlignment {
  kDkc1BabyKongAlignFeet,
  kDkc1BabyKongAlignCenter,
} Dkc1BabyKongAlignment;

typedef enum Dkc1BabyKongPlayback {
  kDkc1BabyKongPlaybackLoop,
  kDkc1BabyKongPlaybackOnce,
  kDkc1BabyKongPlaybackJumpArc,
  kDkc1BabyKongPlaybackHold,
} Dkc1BabyKongPlayback;

typedef struct Dkc1BabyKongAnimationInput {
  uint16_t animation_id;
  uint16_t state;
  uint16_t native_pose;
  int16_t x_velocity;
  int16_t y_velocity;
} Dkc1BabyKongAnimationInput;

typedef struct Dkc1BabyKongAnimationChoice {
  const char *group;
  Dkc1BabyKongAlignment alignment;
  Dkc1BabyKongPlayback playback;
  uint8_t delay;
} Dkc1BabyKongAnimationChoice;

typedef struct Dkc1BabyKongAnimationTracker {
  uint16_t animation_id;
  uint16_t native_pose;
  const char *group;
  unsigned elapsed;
  bool initialized;
} Dkc1BabyKongAnimationTracker;

/* DKC1's animation ID is the semantic oracle. Velocity is only a fallback
 * when a transition briefly exposes animation ID zero. */
Dkc1BabyKongAnimationChoice Dkc1BabyKongClassifyAnimation(
    const Dkc1BabyKongAnimationInput *input);
unsigned Dkc1BabyKongAnimationFrame(
    Dkc1BabyKongAnimationTracker *tracker,
    const Dkc1BabyKongAnimationInput *input,
    Dkc1BabyKongAnimationChoice choice, unsigned frame_count);
void Dkc1BabyKongAnimationReset(Dkc1BabyKongAnimationTracker *tracker);

/* Live DKC1 traces show normal ground as state 0 with Y velocity -$0300.
 * Actor state, not zero vertical velocity, therefore owns this decision. */
bool Dkc1BabyKongStateIsGrounded(uint16_t state);
bool Dkc1BabyKongStateIsAirborne(uint16_t state);

#endif
