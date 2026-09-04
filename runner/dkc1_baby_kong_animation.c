#include "dkc1_baby_kong_animation.h"

#include <limits.h>
#include <stddef.h>
#include <string.h>

typedef struct AnimationMap {
  const char *group;
  Dkc1BabyKongAlignment alignment;
  Dkc1BabyKongPlayback playback;
  uint8_t delay;
} AnimationMap;

#define LOOP(name, delay_) \
  { name, kDkc1BabyKongAlignFeet, kDkc1BabyKongPlaybackLoop, delay_ }
#define LOOP_CENTER(name, delay_) \
  { name, kDkc1BabyKongAlignCenter, kDkc1BabyKongPlaybackLoop, delay_ }
#define ONCE(name, delay_) \
  { name, kDkc1BabyKongAlignFeet, kDkc1BabyKongPlaybackOnce, delay_ }
#define ONCE_CENTER(name, delay_) \
  { name, kDkc1BabyKongAlignCenter, kDkc1BabyKongPlaybackOnce, delay_ }
#define JUMP(name) \
  { name, kDkc1BabyKongAlignCenter, kDkc1BabyKongPlaybackJumpArc, 1 }
#define HOLD(name) \
  { name, kDkc1BabyKongAlignFeet, kDkc1BabyKongPlaybackHold, 1 }
#define HOLD_CENTER(name) \
  { name, kDkc1BabyKongAlignCenter, kDkc1BabyKongPlaybackHold, 1 }

/* DKC1 Donkey animation IDs $0001-$0068, mapped to the closest available
 * Kiddy gameplay group. The numeric IDs are stable cartridge metadata. */
static const AnimationMap kDonkeyAnimationMap[0x69] = {
  [0x01] = LOOP("Kiddy_LookAroundIdle", 7),
  [0x02] = LOOP("Kiddy_Run", 3),
  [0x03] = LOOP("Kiddy_Walk", 4),
  [0x04] = ONCE("Kiddy_SitDown", 5),
  [0x05] = JUMP("Kiddy_Jump"),
  [0x06] = ONCE("Kiddy_Turn", 3),
  [0x07] = JUMP("Kiddy_Jump"),
  [0x08] = JUMP("Kiddy_Jump"),
  [0x09] = ONCE("Kiddy_Victory", 5),
  [0x0a] = ONCE("Kiddy_Duck", 4),
  [0x0b] = LOOP("Kiddy_LookAroundIdle", 7),
  [0x0c] = ONCE_CENTER("Kiddy_Hurt", 3),
  [0x0d] = ONCE_CENTER("Kiddy_Hurt", 3),
  [0x0e] = LOOP("Kiddy_Run", 3),
  [0x0f] = LOOP_CENTER("Kiddy_Swim", 4),
  [0x10] = ONCE_CENTER("Kiddy_Hurt", 4),
  [0x11] = ONCE("Kiddy_Tantrum", 4),
  [0x12] = LOOP("Kiddy_Tantrum", 5),
  [0x13] = ONCE("Kiddy_Tantrum", 4),
  [0x14] = LOOP("Kiddy_Run", 3),
  [0x15] = JUMP("Kiddy_Jump"),
  [0x16] = ONCE("Kiddy_Land", 3),
  [0x17] = JUMP("Kiddy_Jump"),
  [0x18] = LOOP("Kiddy_Roll", 2),
  [0x19] = ONCE("Kiddy_Land", 3),
  [0x1a] = ONCE("Kiddy_Land", 3),
  [0x1b] = LOOP("Kiddy_SitOnAnimalBuddy", 5),
  [0x1c] = LOOP("Kiddy_SitOnAnimalBuddy", 5),
  [0x1d] = LOOP("Kiddy_SitOnAnimalBuddy", 5),
  [0x1e] = LOOP("Kiddy_IdleOnAnimalBuddy", 6),
  [0x1f] = LOOP("Kiddy_IdleOnAnimalBuddy", 6),
  [0x20] = LOOP("Kiddy_IdleOnAnimalBuddy", 6),
  [0x21] = LOOP_CENTER("Kiddy_IdleOnAnimalBuddy", 6),
  [0x22] = JUMP("Kiddy_Jump"),
  [0x23] = JUMP("Kiddy_Jump"),
  [0x24] = JUMP("Kiddy_Jump"),
  [0x25] = JUMP("Kiddy_Jump"),
  [0x26] = LOOP("Kiddy_SitOnAnimalBuddy", 4),
  [0x27] = LOOP("Kiddy_SitOnAnimalBuddy", 4),
  [0x28] = LOOP("Kiddy_SitOnAnimalBuddy", 4),
  [0x29] = LOOP_CENTER("Kiddy_SitOnAnimalBuddy", 4),
  [0x2a] = ONCE("Kiddy_Turn", 3),
  [0x2b] = ONCE("Kiddy_Turn", 3),
  [0x2c] = ONCE("Kiddy_Turn", 3),
  [0x2d] = ONCE_CENTER("Kiddy_TurnWhileSwimming", 3),
  [0x2e] = JUMP("Kiddy_Jump"),
  [0x2f] = JUMP("Kiddy_Jump"),
  [0x30] = JUMP("Kiddy_Jump"),
  [0x31] = JUMP("Kiddy_Jump"),
  [0x32] = ONCE("Kiddy_SitOnAnimalBuddy", 4),
  [0x33] = ONCE("Kiddy_SitOnAnimalBuddy", 4),
  [0x34] = ONCE("Kiddy_SitOnAnimalBuddy", 4),
  [0x35] = ONCE_CENTER("Kiddy_SitOnAnimalBuddy", 4),
  [0x36] = JUMP("Kiddy_Jump"),
  [0x37] = JUMP("Kiddy_Jump"),
  [0x38] = JUMP("Kiddy_Jump"),
  [0x39] = JUMP("Kiddy_Jump"),
  [0x3a] = ONCE("Kiddy_Throw", 3),
  [0x3b] = ONCE("Kiddy_Throw", 3),
  [0x3c] = ONCE("Kiddy_Throw", 3),
  [0x3d] = ONCE_CENTER("Kiddy_Throw", 3),
  [0x3e] = ONCE_CENTER("Kiddy_SitOnAnimalBuddy", 4),
  [0x3f] = ONCE("Kiddy_SitOnAnimalBuddy", 4),
  [0x40] = ONCE("Kiddy_SitOnAnimalBuddy", 4),
  [0x41] = ONCE("Kiddy_SitOnAnimalBuddy", 4),
  [0x42] = LOOP_CENTER("Kiddy_IdleOnAnimalBuddy", 5),
  [0x43] = LOOP_CENTER("Kiddy_HangFromSquawks", 4),
  [0x44] = LOOP_CENTER("Kiddy_HangFromSquawks", 4),
  [0x45] = LOOP_CENTER("Kiddy_HangFromSquawks", 4),
  [0x46] = LOOP_CENTER("Kiddy_HangFromSquawks", 4),
  [0x47] = ONCE("Kiddy_Pickup", 3),
  [0x48] = HOLD("Kiddy_HoldWalk"),
  [0x49] = LOOP("Kiddy_HoldWalk", 4),
  [0x4a] = ONCE("Kiddy_Throw", 3),
  [0x4b] = ONCE("Kiddy_Pickup", 3),
  [0x4c] = ONCE("Kiddy_Turn", 3),
  [0x4d] = JUMP("Kiddy_Jump"),
  [0x4e] = LOOP("Kiddy_RideSteelKeg", 4),
  [0x4f] = ONCE("Kiddy_KiddyTakesLead", 4),
  [0x50] = ONCE("Kiddy_DixieTakesLead", 4),
  [0x51] = ONCE("Kiddy_Victory", 5),
  [0x52] = JUMP("Kiddy_Jump"),
  [0x53] = JUMP("Kiddy_Jump"),
  [0x54] = ONCE("Kiddy_Duck", 4),
  [0x55] = ONCE("Kiddy_Duck", 4),
  [0x56] = ONCE("Kiddy_Duck", 3),
  [0x57] = HOLD("Kiddy_Duck"),
  [0x58] = ONCE("Kiddy_Turn", 3),
  [0x59] = ONCE("Kiddy_Duck", 3),
  [0x5a] = LOOP("Kiddy_Duck", 4),
  [0x5b] = LOOP_CENTER("Kiddy_SitOnAnimalBuddy", 5),
  [0x5c] = LOOP_CENTER("Kiddy_ClimbUpSingleVerticalRope", 4),
  [0x5d] = LOOP_CENTER("Kiddy_ClimbUpSingleVerticalRope", 4),
  [0x5e] = HOLD_CENTER("Kiddy_HangOnVerticalRope"),
  [0x5f] = ONCE_CENTER("Kiddy_TurnOnVerticalRope", 4),
  [0x60] = LOOP_CENTER("Kiddy_Swim", 4),
  [0x61] = ONCE("Kiddy_LookAroundIdle", 6),
  [0x62] = ONCE("Kiddy_LookAroundIdle", 5),
  [0x63] = ONCE("Kiddy_Victory", 5),
  [0x64] = ONCE_CENTER("Kiddy_Hurt", 4),
  [0x65] = ONCE_CENTER("Kiddy_Swim", 4),
  [0x66] = ONCE_CENTER("Kiddy_Hurt", 4),
  [0x67] = LOOP("Kiddy_Tantrum", 4),
  [0x68] = JUMP("Kiddy_Jump"),
};

static Dkc1BabyKongAnimationChoice ChoiceFromMap(AnimationMap map) {
  Dkc1BabyKongAnimationChoice choice;
  choice.group = map.group;
  choice.alignment = map.alignment;
  choice.playback = map.playback;
  choice.delay = map.delay;
  return choice;
}

bool Dkc1BabyKongStateIsGrounded(uint16_t state) {
  return state == 0u || state == 18u || state == 19u;
}

bool Dkc1BabyKongStateIsAirborne(uint16_t state) {
  return state == 1u;
}

Dkc1BabyKongAnimationChoice Dkc1BabyKongClassifyAnimation(
    const Dkc1BabyKongAnimationInput *input) {
  if (input && input->animation_id <
                   sizeof kDonkeyAnimationMap / sizeof kDonkeyAnimationMap[0]) {
    const AnimationMap map = kDonkeyAnimationMap[input->animation_id];
    if (map.group)
      return ChoiceFromMap(map);
  }

  if (input && input->state == 43u)
    return ChoiceFromMap((AnimationMap)LOOP_CENTER("Kiddy_Swim", 4));
  if (input && Dkc1BabyKongStateIsAirborne(input->state))
    return ChoiceFromMap((AnimationMap)JUMP("Kiddy_Jump"));
  if (input && (input->x_velocity > 0x0180 ||
                input->x_velocity < -0x0180))
    return ChoiceFromMap((AnimationMap)LOOP("Kiddy_Run", 3));
  if (input && (input->x_velocity > 0x0020 ||
                input->x_velocity < -0x0020))
    return ChoiceFromMap((AnimationMap)LOOP("Kiddy_Walk", 4));
  return ChoiceFromMap((AnimationMap)LOOP("Kiddy_LookAroundIdle", 7));
}

void Dkc1BabyKongAnimationReset(Dkc1BabyKongAnimationTracker *tracker) {
  if (tracker)
    memset(tracker, 0, sizeof *tracker);
}

unsigned Dkc1BabyKongAnimationFrame(
    Dkc1BabyKongAnimationTracker *tracker,
    const Dkc1BabyKongAnimationInput *input,
    Dkc1BabyKongAnimationChoice choice, unsigned frame_count) {
  if (!tracker || !input || !choice.group || frame_count == 0)
    return 0;

  if (!tracker->initialized ||
      tracker->animation_id != input->animation_id ||
      !tracker->group || strcmp(tracker->group, choice.group) != 0) {
    tracker->initialized = true;
    tracker->animation_id = input->animation_id;
    tracker->native_pose = input->native_pose;
    tracker->group = choice.group;
    tracker->elapsed = 0;
  } else {
    tracker->native_pose = input->native_pose;
    if (tracker->elapsed != UINT_MAX)
      tracker->elapsed++;
  }

  if (choice.playback == kDkc1BabyKongPlaybackHold)
    return 0;
  if (choice.playback == kDkc1BabyKongPlaybackJumpArc) {
    const int velocity = input->y_velocity < -0x600 ? -0x600
                       : input->y_velocity > 0x600 ? 0x600
                       : input->y_velocity;
    return (unsigned)((velocity + 0x600) *
                      (int)(frame_count - 1u) / 0x0c00);
  }

  const unsigned delay = choice.delay ? choice.delay : 1u;
  const unsigned ordinal = tracker->elapsed / delay;
  if (choice.playback == kDkc1BabyKongPlaybackOnce)
    return ordinal < frame_count ? ordinal : frame_count - 1u;
  return ordinal % frame_count;
}

#undef LOOP
#undef LOOP_CENTER
#undef ONCE
#undef ONCE_CENTER
#undef JUMP
#undef HOLD
#undef HOLD_CENTER
