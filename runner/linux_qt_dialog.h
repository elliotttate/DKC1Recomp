#ifndef DKC1_LINUX_QT_DIALOG_H
#define DKC1_LINUX_QT_DIALOG_H

#ifdef __cplusplus
extern "C" {
#endif
#include "desktop_graphics.h"
#include "desktop_input.h"

/* Real Qt6 settings/pause panel, replacing the linux_platform.c stub.
 * `native_window` is the SDL_Window*; used only to center the dialog.
 * `graphics_page` mirrors the other platforms' convention: 4 opens
 * straight to the Controls tab (from the keyboard-remap entry point),
 * anything else opens on Graphics. Returns 1 to resume, 0 to keep the
 * pause state as it was (mirroring Dkc1MacShowPauseMenu's contract). */
int Dkc1LinuxQtShowDialog(void *native_window, Dkc1GraphicsSettings *settings,
                          Dkc1Controls *controls, int graphics_page);

#ifdef __cplusplus
}
#endif
#endif
