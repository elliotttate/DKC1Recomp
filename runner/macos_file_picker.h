#ifndef DKC1_MACOS_FILE_PICKER_H
#define DKC1_MACOS_FILE_PICKER_H

#include "dkc1_video.h"

enum Dkc1MacMenuCommand {
  kDkc1MacMenuQuit = 1,
  kDkc1MacMenuPause,
  kDkc1MacMenuStep,
  kDkc1MacMenuQuickSave,
  kDkc1MacMenuQuickLoad,
  kDkc1MacMenuExportRepro,
  kDkc1MacMenuFullscreen,
  kDkc1MacMenuAspectNative,
  kDkc1MacMenuAspect16x10,
  kDkc1MacMenuAspect16x9,
  kDkc1MacMenuLayerComposite,
  kDkc1MacMenuLayerBg1,
  kDkc1MacMenuLayerBg2,
  kDkc1MacMenuLayerBg3,
  kDkc1MacMenuLayerObj,
  kDkc1MacMenuProvenance,
  kDkc1MacMenuCommandCount
};

/* Returns a malloc-owned UTF-8 path, or NULL when the panel is cancelled. */
char *Dkc1MacChooseRom(void);

/* Installs the native menu bar. Dkc1MacMenuCommand is implemented by the
 * SDL host and receives menu actions on the application's main thread. */
void Dkc1MacInstallMenu(void);
void Dkc1MacUpdateMenuState(int paused, int fullscreen,
                            Dkc1VideoAspect aspect,
                            unsigned char layer_mask, int provenance);
void Dkc1MacMenuCommand(int command);

/* Runs a display-linked cadence source on a private run loop. The SDL host
 * keeps all rendering on the main thread; this bridge only supplies the
 * display's actual callback cadence and never acquires a Metal drawable. */
int Dkc1MacDisplayLinkStart(void *native_window, double preferred_fps);
int Dkc1MacDisplayLinkWait(double timeout_seconds, double *timestamp,
                           double *target_timestamp, double *duration,
                           unsigned long long *callback_number);
void Dkc1MacDisplayLinkStop(void);

#endif
