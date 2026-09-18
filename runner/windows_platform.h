#ifndef DKC1_WINDOWS_PLATFORM_H
#define DKC1_WINDOWS_PLATFORM_H
#include <SDL.h>
#include "desktop_graphics.h"
void Dkc1WindowsAttach(SDL_Window *window);
void Dkc1WindowsEvent(const SDL_Event *event);
void Dkc1WindowsDetach(void);
void Dkc1WindowsShowMenuBar(int visible);
int Dkc1WindowsMenuBarVisible(void);
int Dkc1WindowsSavedHaptics(void);
void Dkc1WindowsSetHaptics(int enabled);
void Dkc1WindowsUpdateHapticsMenu(int enabled);
/* [Host] FrameGen / SquarePixels preferences and their View menu checks. */
int Dkc1WindowsSavedFrameGen(void);
void Dkc1WindowsSetFrameGen(int enabled);
int Dkc1WindowsSavedSquarePixels(void);
void Dkc1WindowsSetSquarePixels(int enabled);
void Dkc1WindowsUpdateHostMenu(int framegen_enabled, int square_pixels);
/* Game > Change ROM: verified picker that always prompts; the accepted path
 * replaces the remembered one. Returns a malloc-owned UTF-8 path or NULL. */
char *Dkc1WindowsPickRom(void);
bool Dkc1WindowsGraphicsInit(SDL_Window *window);
void Dkc1WindowsGraphicsDraw(const uint32_t *pixels,int w,int h,int display_width,
                             const Dkc1GraphicsSettings *settings);
void Dkc1WindowsGraphicsSwap(void);
void Dkc1WindowsGraphicsClose(void);
int Dkc1WindowsGraphicsTest(void);
int Dkc1WindowsPlatformTest(const char *directory);
#endif
