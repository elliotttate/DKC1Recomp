#ifndef DKC1_COOP_RASTER_H
#define DKC1_COOP_RASTER_H

#include <stddef.h>
#include <string.h>

typedef struct Dkc1CoopRasterSpan {
  void *target, *saved;
  size_t bytes;
} Dkc1CoopRasterSpan;
typedef struct Dkc1CoopRasterLoan {
  Dkc1CoopRasterSpan spans[2]; /* Caller-validated VRAM and CGRAM footprints. */
} Dkc1CoopRasterLoan;

/* Raster resources only. Native preparation/metadata rollback has already
 * ended; OAM assembly, PPU settings and asset validation stay at the caller. */
static inline void Dkc1CoopRasterBegin(Dkc1CoopRasterLoan *loan) {
  for (unsigned i = 0; i < 2u; ++i) {
    Dkc1CoopRasterSpan *span = &loan->spans[i];
    memcpy(span->saved, span->target, span->bytes);
  }
}
static inline void Dkc1CoopRasterEnd(Dkc1CoopRasterLoan *loan) {
  for (unsigned i = 2; i > 0; --i) {
    Dkc1CoopRasterSpan *span = &loan->spans[i - 1u];
    memcpy(span->target, span->saved, span->bytes);
  }
}

#endif
