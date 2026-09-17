#ifndef DKC1_HD_SPRITES_H
#define DKC1_HD_SPRITES_H
#include <stdbool.h>
#include <stdint.h>
typedef struct Ppu Ppu;
enum { kDkc1HdScale = 4 };
void Dkc1HdPrepare(Ppu *ppu, const uint8_t *wram, int bias);
void Dkc1HdCaptureLine(Ppu *ppu, int line);
void Dkc1HdFinish(Ppu *ppu);
const uint32_t *Dkc1HdPresent(const uint32_t *native, int width, int height,
                             int *scale);
bool Dkc1HdEnabled(void);
void Dkc1HdToggle(void);
#endif
