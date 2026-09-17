#ifndef DKC1_MACOS_CONTROLS_H
#define DKC1_MACOS_CONTROLS_H
#include "desktop_input.h"
void Dkc1MacSaveControls(const Dkc1Controls *controls);
void Dkc1MacLoadControls(Dkc1Controls *controls);
/* Modal native settings; the caller suspends gameplay/audio around it. */
int Dkc1MacEditControls(Dkc1Controls *controls);
#endif
