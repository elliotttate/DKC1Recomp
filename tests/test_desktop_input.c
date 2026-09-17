#include "desktop_input.h"
#include <assert.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

static void ExpectInput(const char *name, uint32_t expected,
                        uint32_t buttons, int16_t x, int16_t y) {
  uint32_t actual = Dkc1MapGamepad(buttons, x, y, 7849);
  if (actual != expected) {
    (void)fprintf(stderr, "%s: expected $%03x, got $%03x\n",
                  name, (unsigned)expected, (unsigned)actual);
    exit(EXIT_FAILURE);
  }
}

static bool SyntheticKeyPressed(int scancode, void *context) {
  const int *pressed = (const int *)context;
  return scancode == *pressed;
}

int main(void) {
  ExpectInput("neutral and stick deadzone", 0, 0, 7849, -7849);
  ExpectInput("face buttons", UINT32_C(0x303),
              kDkc1GamepadA | kDkc1GamepadB |
                  kDkc1GamepadX | kDkc1GamepadY,
              0, 0);
  ExpectInput("menu and shoulders", UINT32_C(0xC0C),
              kDkc1GamepadBack | kDkc1GamepadStart |
                  kDkc1GamepadLeftShoulder |
                  kDkc1GamepadRightShoulder,
              0, 0);
  ExpectInput("D-pad", UINT32_C(0x0F0),
              kDkc1GamepadDpadUp | kDkc1GamepadDpadDown |
                  kDkc1GamepadDpadLeft | kDkc1GamepadDpadRight,
              0, 0);
  ExpectInput("left stick", UINT32_C(0x090), 0, 7850, 7850);
  ExpectInput("left stick negative", UINT32_C(0x060), 0, -7850, -7850);
  if (Dkc1MapHostActions(30, 30, 30) != 0 ||
      Dkc1MapHostActions(31, 0, 30) != kDkc1HostRewind ||
      Dkc1MapHostActions(0, 31, 30) != kDkc1HostFastForward ||
      Dkc1MapHostActions(255, 255, 30) !=
          (kDkc1HostRewind | kDkc1HostFastForward)) {
    (void)fputs("host trigger mapping failed\n", stderr);
    return EXIT_FAILURE;
  }

  const Dkc1GamepadState pads[2] = {
      {.buttons = kDkc1GamepadA, .left_trigger = 31},
      {.buttons = kDkc1GamepadB, .right_trigger = 31},
  };
  const int deadzones[2] = {24, 24};
  uint32_t actions = 0;
  const int keyboard_gamepad[2] = {
      kDkc1InputSourceKeyboard, kDkc1InputSourceGamepad};
  uint32_t routed = Dkc1RoutePlayerInputs(
      UINT32_C(0x008), pads, 2, keyboard_gamepad, deadzones, 30, &actions);
  if (routed != UINT32_C(0x001008) || actions != kDkc1HostRewind) {
    (void)fprintf(stderr,
                  "keyboard/P2 gamepad route failed: $%06x actions=$%x\n",
                  (unsigned)routed, (unsigned)actions);
    return EXIT_FAILURE;
  }

  const int two_gamepads[2] = {
      kDkc1InputSourceGamepad, kDkc1InputSourceGamepad};
  routed = Dkc1RoutePlayerInputs(
      UINT32_C(0xFFF), pads, 2, two_gamepads, deadzones, 30, &actions);
  if (routed != UINT32_C(0x100001) ||
      actions != (kDkc1HostRewind | kDkc1HostFastForward)) {
    (void)fprintf(stderr,
                  "two-gamepad route failed: $%06x actions=$%x\n",
                  (unsigned)routed, (unsigned)actions);
    return EXIT_FAILURE;
  }

  const int two_keyboards[2] = {
      kDkc1InputSourceKeyboard, kDkc1InputSourceKeyboard};
  routed = Dkc1RoutePlayerInputs(
      UINT32_C(0x842), NULL, 0, two_keyboards, deadzones, 30, &actions);
  if (routed != UINT32_C(0x842842) || actions != 0) {
    (void)fprintf(stderr,
                  "two-keyboard route failed: $%06x actions=$%x\n",
                  (unsigned)routed, (unsigned)actions);
    return EXIT_FAILURE;
  }

  const int no_sources[2] = {kDkc1InputSourceNone, kDkc1InputSourceNone};
  routed = Dkc1RoutePlayerInputs(
      UINT32_C(0xFFF), pads, 2, no_sources, deadzones, 30, &actions);
  if (routed != 0 || actions != 0) {
    (void)fputs("disabled-player routing failed\n", stderr);
    return EXIT_FAILURE;
  }

  int key_bindings[DKC1_MAX_BINDINGS] = {0};
  key_bindings[4] = 42; /* logical SNES A -> packed bit 8 */
  int pressed_key = 42;
  if (Dkc1MapKeyboardBindings(key_bindings, SyntheticKeyPressed,
                              &pressed_key) != UINT32_C(0x100)) {
    (void)fputs("custom keyboard binding failed\n", stderr);
    return EXIT_FAILURE;
  }

  int pad_bindings[DKC1_MAX_BINDINGS] = {0};
  pad_bindings[5] = DKC1_PAD_BUTTON(1); /* physical B -> SNES B */
  if (Dkc1MapGamepadBindings(&pads[1], 7849, 30, pad_bindings) != 1) {
    (void)fputs("custom gamepad binding failed\n", stderr);
    return EXIT_FAILURE;
  }

  int assist_keys[DKC1_MAX_ASSIST_BINDINGS] = {0};
  int assist_pads[DKC1_MAX_ASSIST_BINDINGS] = {0};
  assist_keys[2] = 42;
  assist_pads[0] = DKC1_PAD_AXIS(4, 1);
  if (Dkc1MapAssistBindings(
          assist_keys, assist_pads, SyntheticKeyPressed, &pressed_key,
          pads, 2, 30) != (kDkc1HostRewind | kDkc1HostSaveState)) {
    (void)fputs("custom Assist binding failed\n", stderr);
    return EXIT_FAILURE;
  }

  if (Dkc1ApplyAssistGate(
          kDkc1HostRewind | kDkc1HostSaveState, 0, false) != 0 ||
      Dkc1ApplyAssistGate(
          kDkc1HostRewind, kDkc1HostSaveState, false) !=
          kDkc1HostSaveState ||
      Dkc1ApplyAssistGate(
          kDkc1HostFastForward, kDkc1HostLoadState, false) !=
          kDkc1HostLoadState ||
      Dkc1ApplyAssistGate(
          kDkc1HostRewind, kDkc1HostSaveState, true) !=
          (kDkc1HostRewind | kDkc1HostSaveState)) {
    (void)fputs("native Quick State Assist gate policy failed\n", stderr);
    return EXIT_FAILURE;
  }

  /* Existing DKC1 defaults combine keyboard/pad, including analog D-pad.
   * The next player still receives the next connected pad. */
  int mixed_sources[2] = {kDkc1InputSourceBoth, kDkc1InputSourceGamepad};
  uint32_t mixed_keys[2] = {0x100, 0};
  int mixed_bindings[2][12] = {{0},{0}};
  mixed_bindings[0][0] = DKC1_PAD_BUTTON(11);
  mixed_bindings[1][5] = DKC1_PAD_BUTTON(1);
  Dkc1GamepadState mixed_pads[2] = {{.left_y = 12000}, {.buttons = kDkc1GamepadB}};
  int dz[2] = {25,25};
  assert(Dkc1RoutePlayerInputsWithBindings(mixed_keys, mixed_pads, 2,
      mixed_sources, dz, mixed_bindings) == 0x1110);
  dz[0] = 50;
  assert(Dkc1RoutePlayerInputsWithBindings(mixed_keys, mixed_pads, 2,
      mixed_sources, dz, mixed_bindings) == 0x1100);
  assert(Dkc1RoutePlayerInputsWithBindings(mixed_keys, NULL, 0,
      mixed_sources, dz, mixed_bindings) == 0x100);

  (void)puts("Desktop gamepad mapping tests passed");
  return EXIT_SUCCESS;
}
