#pragma once

#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>

#define DKC1_MAX_BINDINGS 12
#define DKC1_MAX_ASSIST_BINDINGS 4
#define DKC1_PAD_BUTTON(code) (1 + (code))
#define DKC1_PAD_AXIS(code, positive) (100 + (code) * 2 + !!(positive))
#define DKC1_PAD_IS_BUTTON(v) ((v) > 0 && (v) < 100)
#define DKC1_PAD_IS_AXIS(v) ((v) >= 100 && (v) < 112)
#define DKC1_PAD_BUTTON_CODE(v) ((v) - 1)
#define DKC1_PAD_AXIS_CODE(v) (((v) - 100) / 2)
#define DKC1_PAD_AXIS_POSITIVE(v) (((v) - 100) & 1)

typedef struct Dkc1Controls {
  int source[2]; /* None, Keyboard, Gamepad, Keyboard + Gamepad */
  int deadzone[2];
  int keys[2][12];
  int pads[2][12];
  int assist_keys[4], assist_pads[4];
  int assist_enabled;
} Dkc1Controls;

enum {
  kDkc1DesktopPlayerCount = 2,
  kDkc1InputSourceNone = 0,
  kDkc1InputSourceKeyboard = 1,
  kDkc1InputSourceGamepad = 2,
  kDkc1InputSourceBoth = 3,
  kDkc1GamepadDpadUp = 0x0001,
  kDkc1GamepadDpadDown = 0x0002,
  kDkc1GamepadDpadLeft = 0x0004,
  kDkc1GamepadDpadRight = 0x0008,
  kDkc1GamepadStart = 0x0010,
  kDkc1GamepadBack = 0x0020,
  kDkc1GamepadLeftShoulder = 0x0100,
  kDkc1GamepadRightShoulder = 0x0200,
  kDkc1GamepadA = 0x1000,
  kDkc1GamepadB = 0x2000,
  kDkc1GamepadX = 0x4000,
  kDkc1GamepadY = 0x8000,
  kDkc1GamepadGuide = 0x00010000,
  kDkc1GamepadLeftStick = 0x00020000,
  kDkc1GamepadRightStick = 0x00040000,
  kDkc1HostRewind = 1u << 0,
  kDkc1HostFastForward = 1u << 1,
  kDkc1HostSaveState = 1u << 2,
  kDkc1HostLoadState = 1u << 3,
};

typedef struct Dkc1GamepadState {
  uint32_t buttons;
  int16_t left_x;
  int16_t left_y;
  int16_t right_x;
  int16_t right_y;
  uint8_t left_trigger;
  uint8_t right_trigger;
} Dkc1GamepadState;

typedef bool (*Dkc1KeyPressedFn)(int scancode, void *context);

uint32_t Dkc1MapGamepad(uint32_t buttons, int16_t left_x, int16_t left_y,
                        int16_t deadzone);
uint32_t Dkc1MapHostActions(uint8_t left_trigger, uint8_t right_trigger,
                            uint8_t threshold);
uint32_t Dkc1MapKeyboardBindings(
    const int bindings[DKC1_MAX_BINDINGS],
    Dkc1KeyPressedFn pressed, void *context);
uint32_t Dkc1MapGamepadBindings(
    const Dkc1GamepadState *gamepad, int16_t deadzone, uint8_t axis_threshold,
    const int bindings[DKC1_MAX_BINDINGS]);
uint32_t Dkc1MapAssistBindings(
    const int key_bindings[DKC1_MAX_ASSIST_BINDINGS],
    const int pad_bindings[DKC1_MAX_ASSIST_BINDINGS],
    Dkc1KeyPressedFn pressed, void *context,
    const Dkc1GamepadState *gamepads, size_t gamepad_count,
    uint8_t axis_threshold);
/* Keeps configured Assist shortcuts behind the opt-in gate while allowing
 * explicit native-platform Quick Save/Load menu commands through. */
uint32_t Dkc1ApplyAssistGate(uint32_t mapped_actions,
                             uint32_t platform_actions,
                             bool assist_tools);
uint32_t Dkc1RoutePlayerInputsWithBindings(
    const uint32_t keyboard_inputs[kDkc1DesktopPlayerCount],
    const Dkc1GamepadState *gamepads, size_t gamepad_count,
    const int player_sources[kDkc1DesktopPlayerCount],
    const int deadzone_percent[kDkc1DesktopPlayerCount],
    const int pad_bindings[kDkc1DesktopPlayerCount]
                          [DKC1_MAX_BINDINGS]);

/* Routes the launcher's None/Keyboard/Gamepad choices to the two packed
 * 12-bit controller words accepted by RtlRunFrame. Connected gamepads are
 * assigned in XInput user order to players that selected Gamepad. */
uint32_t Dkc1RoutePlayerInputs(
    uint32_t keyboard_input, const Dkc1GamepadState *gamepads,
    size_t gamepad_count, const int player_sources[kDkc1DesktopPlayerCount],
    const int deadzone_percent[kDkc1DesktopPlayerCount],
    uint8_t trigger_threshold, uint32_t *host_actions);
