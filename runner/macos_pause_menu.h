#ifndef DKC1_MACOS_PAUSE_MENU_H
#define DKC1_MACOS_PAUSE_MENU_H
#include "desktop_graphics.h"
#include "desktop_input.h"
void Dkc1MacLoadGraphics(Dkc1GraphicsSettings *settings);
void Dkc1MacSaveGraphics(const Dkc1GraphicsSettings *settings);
/* Native, draggable in-game menu. Caller owns pause/audio and runs this only
 * after a completed guest frame. All actions stay on that same main thread. */
int Dkc1MacShowPauseMenu(void *window, Dkc1GraphicsSettings *settings,
                         Dkc1Controls *controls, int graphics_page);
int Dkc1MacPauseMenuIsOpen(void);
/* Host callbacks: neither the view nor the display thread advances the guest. */
void Dkc1MacApplyGraphics(Dkc1GraphicsSettings *settings);
void Dkc1MacAssistEnabled(int enabled);
unsigned Dkc1MacPauseMenuController(void);
const char *Dkc1MacHostStatus(void);
#endif
