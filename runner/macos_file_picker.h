#ifndef DKC1_MACOS_FILE_PICKER_H
#define DKC1_MACOS_FILE_PICKER_H

#include "dkc1_video.h"

typedef enum Dkc1MacFullscreenScaling {
  kDkc1MacFullscreenSmooth = 0,
  kDkc1MacFullscreenSharpBilinear = 1,
  kDkc1MacFullscreenPixelSharp = 2,
  kDkc1MacFullscreenScalingCount
} Dkc1MacFullscreenScaling;

enum Dkc1MacMenuCommand {
  kDkc1MacMenuQuit = 1,
  kDkc1MacMenuPause,
  kDkc1MacMenuStep,
  kDkc1MacMenuQuickSave,
  kDkc1MacMenuQuickLoad,
  kDkc1MacMenuExportRepro,
  kDkc1MacMenuToggleBabyKong,
  kDkc1MacMenuChooseBabyKongRom,
  kDkc1MacMenuChooseMusicPack,
  kDkc1MacMenuDisableMusicPack,
  kDkc1MacMenuFullscreen,
  kDkc1MacMenuFullscreenSmooth,
  kDkc1MacMenuFullscreenSharpBilinear,
  kDkc1MacMenuFullscreenPixelSharp,
  kDkc1MacMenuAspectNative,
  kDkc1MacMenuAspect16x10,
  kDkc1MacMenuAspect16x9,
  kDkc1MacMenuLayerComposite,
  kDkc1MacMenuLayerBg1,
  kDkc1MacMenuLayerBg2,
  kDkc1MacMenuLayerBg3,
  kDkc1MacMenuLayerObj,
  kDkc1MacMenuProvenance,
  kDkc1MacMenuEdgeReflect,
  kDkc1MacMenuEdgeBars,
  kDkc1MacMenuEdgeShift,
  kDkc1MacMenuEdgeGlide,
  kDkc1MacMenuCommandCount
};

/* Returns a malloc-owned UTF-8 path, or NULL when the panel is cancelled. */
char *Dkc1MacChooseRom(void);

/* Baby Kong uses a user-owned DKC3 ROM as its in-memory sprite source. */
char *Dkc1MacChooseBabyKongRom(void);
char *Dkc1MacSavedBabyKongRom(void);
void Dkc1MacSetBabyKongRom(const char *path);
int Dkc1MacSavedBabyKongEnabled(void);
void Dkc1MacSetBabyKongEnabled(int enabled);

/* Selects an extracted MSU-1 directory or extracts a .msu1 archive into the
 * app's Application Support directory, saves the selection, and returns a
 * malloc-owned directory path. */
char *Dkc1MacChooseMsu1(void);
char *Dkc1MacSavedMsu1(void);
void Dkc1MacClearMsu1(void);

/* Fullscreen sampling is a host-only preference. Sharp Bilinear is the
 * default when the three-state preference has never been set. */
Dkc1MacFullscreenScaling Dkc1MacSavedFullscreenScaling(void);
void Dkc1MacSetFullscreenScaling(Dkc1MacFullscreenScaling scaling);

/* Level-wall presentation (dkc1_edge_policy.h) is a host-only preference.
 * glide is the default when it has never been set. */
Dkc1EdgePolicy Dkc1MacSavedWidescreenEdge(void);
void Dkc1MacSetWidescreenEdge(Dkc1EdgePolicy policy);

/* Installs the native menu bar. Dkc1MacMenuCommand is implemented by the
 * SDL host and receives menu actions on the application's main thread. */
void Dkc1MacInstallMenu(void);
void Dkc1MacUpdateMenuState(int paused, int fullscreen,
                            Dkc1MacFullscreenScaling fullscreen_scaling,
                            Dkc1VideoAspect aspect, Dkc1EdgePolicy edge,
                            unsigned char layer_mask, int provenance,
                            int replacement_music, int baby_kong_enabled,
                            int baby_kong_ready);
void Dkc1MacMenuCommand(int command);

/* Runs a display-linked cadence source on a private run loop. The SDL host
 * keeps all rendering on the main thread; this bridge only supplies the
 * display's actual callback cadence and never acquires a Metal drawable. */
int Dkc1MacDisplayLinkStart(void *native_window, double preferred_fps);
int Dkc1MacDisplayLinkWait(unsigned long long after_callback_number,
                           double timeout_seconds, double *timestamp,
                           double *target_timestamp, double *duration,
                           unsigned long long *callback_number);
void Dkc1MacDisplayLinkStop(void);

#endif
