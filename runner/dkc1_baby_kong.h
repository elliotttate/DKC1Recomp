#ifndef DKC1_BABY_KONG_H
#define DKC1_BABY_KONG_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct Ppu Ppu;

/* The mod never contains DKC3 art. It decodes Kiddy Kong frames in memory
 * only after the user selects the exact supported, headerless DKC3 ROM. */
bool Dkc1BabyKongLoadRom(const char *path, char *error, size_t error_size);
void Dkc1BabyKongUnload(void);
bool Dkc1BabyKongReady(void);
size_t Dkc1BabyKongFrameCount(void);

void Dkc1BabyKongSetEnabled(bool enabled);
bool Dkc1BabyKongEnabled(void);
const char *Dkc1BabyKongStatus(void);

/* Called by the game adapter. Disabled/unloaded paths are strict no-ops. */
void Dkc1BabyKongInitializeFromEnvironment(void);
void Dkc1BabyKongApplyMoves(uint8_t *wram);
void Dkc1BabyKongPrepareFrame(Ppu *ppu, const uint8_t *wram,
                              int presentation_bias);
void Dkc1BabyKongDrawFrame(Ppu *ppu);

#endif
