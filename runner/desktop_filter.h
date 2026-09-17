#ifndef DKC1_DESKTOP_FILTER_H
#define DKC1_DESKTOP_FILTER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum Dkc1DesktopScreenFilter {
  kDkc1ScreenRaw = 0,
  kDkc1ScreenCrt = 1,
  kDkc1ScreenComposite = 2,
  kDkc1ScreenTrinitron = 3,
  kDkc1ScreenFilterCount = 4,
} Dkc1DesktopScreenFilter;

typedef struct Dkc1DesktopColorFilter {
  int screen_kind;
} Dkc1DesktopColorFilter;

bool Dkc1DesktopScreenFilterValid(int filter);
const char *Dkc1DesktopScreenFilterName(int filter);
bool Dkc1DesktopScreenFilterFromName(const char *name, int *filter);
bool Dkc1DesktopColorFilterInit(Dkc1DesktopColorFilter *filter,
                                int screen_kind);
void Dkc1DesktopColorFilterDestroy(Dkc1DesktopColorFilter *filter);

/* Returns source unchanged for Raw. For an opted-in screen model, writes a
 * present-only BGRX8888 frame to destination and returns destination. */
const uint8_t *Dkc1DesktopColorFilterApply(
    const Dkc1DesktopColorFilter *filter, const uint8_t *source,
    uint8_t *destination, size_t pixel_count);

#endif
