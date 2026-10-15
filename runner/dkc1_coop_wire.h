#ifndef DKC1_COOP_WIRE_H
#define DKC1_COOP_WIRE_H

/* Frozen DWS8 container versions. Nested co-op payload versions have their
 * own migration rules; these versions choose which payload bytes are present. */
enum {
  kDkc1WsSaveVersion = 1,
  kDkc1WsSaveVersionCoop = 2,
  kDkc1WsSaveVersionCoopWorld = 3,
  kDkc1WsSaveVersionCoopHud = 4,
  kDkc1WsSaveVersionCoopActor = 5,
  kDkc1WsSaveVersionCoopHudGraphics = 6,
  kDkc1WsSaveVersionCoopFade = 7,
  kDkc1WsSaveVersionCoopContinuation = 8,
  kDkc1WsSaveVersionCoopNativeCamera = 9,
  kDkc1WsSaveVersionCoopDryCamera = 10,
};

#endif
