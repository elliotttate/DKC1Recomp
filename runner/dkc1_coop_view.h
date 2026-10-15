#ifndef DKC1_COOP_VIEW_H
#define DKC1_COOP_VIEW_H
#include <stddef.h>
#include <stdint.h>
struct Ppu;
/* Latch the registers before scanout. Secondary scanout never touches the
 * cartridge PPU, frame scheduler, or the primary streaming shadow. */
void Dkc1CoopViewCapture(const struct Ppu *ppu);
void Dkc1CoopViewDraw(void);
void Dkc1CoopViewInvalidate(void);
size_t Dkc1CoopViewStateSize(void);
int Dkc1CoopViewSaveState(void *data, size_t size);
int Dkc1CoopViewLoadState(const void *data, size_t size);
int Dkc1CoopViewReady(void);
#endif
