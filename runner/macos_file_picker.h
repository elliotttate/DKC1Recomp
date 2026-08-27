#ifndef DKC1_MACOS_FILE_PICKER_H
#define DKC1_MACOS_FILE_PICKER_H

enum Dkc1MacMenuCommand {
  kDkc1MacMenuQuit = 1,
  kDkc1MacMenuPause,
  kDkc1MacMenuStep,
  kDkc1MacMenuQuickSave,
  kDkc1MacMenuQuickLoad,
  kDkc1MacMenuExportRepro,
  kDkc1MacMenuFullscreen,
  kDkc1MacMenuAspectNative,
  kDkc1MacMenuAspectWidescreen,
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
void Dkc1MacUpdateMenuState(int paused, int fullscreen, int widescreen,
                            unsigned char layer_mask, int provenance);
void Dkc1MacMenuCommand(int command);

#endif
