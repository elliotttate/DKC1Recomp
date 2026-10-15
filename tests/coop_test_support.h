#ifndef DKC1_COOP_TEST_SUPPORT_H
#define DKC1_COOP_TEST_SUPPORT_H

#include <stdint.h>

/* Native-pool fixtures for the controller/pickup/palette contracts. Extended
 * storage and actual camera geometry have their own entity/world tests. */
void CoopTestResetSupport(void);
void CoopTestSetSecondCamera(int available, uint16_t x, uint16_t y);
unsigned CoopTestCameraCalls(void);

#endif
