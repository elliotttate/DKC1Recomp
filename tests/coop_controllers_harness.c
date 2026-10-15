#include <stdio.h>
#include <string.h>

typedef int SDL_JoystickID;
typedef struct SDL_JoystickGUID { unsigned char bytes[16]; } SDL_JoystickGUID;
typedef struct SDL_Joystick {
  SDL_JoystickID id;
  SDL_JoystickGUID guid;
} SDL_Joystick;
typedef SDL_Joystick SDL_GameController;
static SDL_Joystick devices[2] = {{10, {{7}}}, {11, {{7}}}};
static SDL_GameController *s_controllers[2];
static unsigned opens;
static int SDL_NumJoysticks(void) { return 2; }
static int SDL_IsGameController(int i) { return 1; }
static SDL_JoystickID SDL_JoystickGetDeviceInstanceID(int i) { return devices[i].id; }
static SDL_JoystickGUID SDL_JoystickGetDeviceGUID(int i) { return devices[i].guid; }
static SDL_Joystick *SDL_GameControllerGetJoystick(SDL_GameController *c) { return c; }
static SDL_JoystickGUID SDL_JoystickGetGUID(SDL_Joystick *j) { return j->guid; }
static SDL_JoystickID SDL_JoystickInstanceID(SDL_Joystick *j) { return j->id; }
static SDL_GameController *SDL_GameControllerOpen(int i) { opens++; return &devices[i]; }

/* ENUMERATE_CONTROLLERS */

int main(void) {
  OpenFirstController();
  if (!s_controllers[0] || !s_controllers[1] || opens != 2) {
    puts("the second physical controller was discarded because its model GUID matched");
    return 1;
  }
  OpenFirstController();
  if (opens != 2) return 2;
  memset(s_controllers, 0, sizeof s_controllers);
  devices[1].id = devices[0].id;
  devices[1].guid.bytes[0] = 8;
  opens = 0;
  OpenFirstController();
  if (opens != 1 || s_controllers[1]) return 3;
  return 0;
}
