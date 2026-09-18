#ifndef DKC1_HD_SCENE_H
#define DKC1_HD_SCENE_H
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include "dkc1_hd_gpu_types.h"
typedef struct Ppu Ppu;

/* Kong's Banana Hoard is a locked Mode-1 interior on the same DA:0000/BF00
 * cave tileset as Jungle Bonus 1. Presentation-only: HD and host widescreen
 * may opt in to this exact tuple. Cartridge streaming stays stock. */
static inline uint16_t Dkc1HdWram16(const uint8_t *w, unsigned addr) {
  return (uint16_t)(w[addr] | ((uint16_t)w[addr + 1u] << 8));
}
static inline bool Dkc1HdHoardSceneEligible(const uint8_t *w) {
  return w && Dkc1HdWram16(w, 0x003eu) == 0x47u &&
         Dkc1HdWram16(w, 0x0030u) == 0x2du &&
         Dkc1HdWram16(w, 0x0032u) == 1u && w[0xd5] == 0xdau &&
         Dkc1HdWram16(w, 0x00d3u) == 0 &&
         Dkc1HdWram16(w, 0x1b11u) == 0xbf00u &&
         Dkc1HdWram16(w, 0x1b23u) == 0xb000u &&
         Dkc1HdWram16(w, 0x1b25u) == 0xb000u;
}
/* DK's Tree House is another locked interior, but it uses a subscreen-color
 * composition that the tiled HD path cannot reproduce. Its exact tuple may
 * opt into a host-only fixed background plate while OAM remains live. */
static inline bool Dkc1HdTreehouseSceneEligible(const uint8_t *w) {
  return w && Dkc1HdWram16(w, 0x003eu) == 0x5cu &&
         Dkc1HdWram16(w, 0x0030u) == 0x40u &&
         Dkc1HdWram16(w, 0x0032u) == 0x0du && w[0xd5] == 0xe3u &&
         Dkc1HdWram16(w, 0x00d3u) == 0 &&
         Dkc1HdWram16(w, 0x1b11u) == 0x0200u &&
         Dkc1HdWram16(w, 0x1b23u) == 0 &&
         Dkc1HdWram16(w, 0x1b25u) == 0;
}
bool Dkc1HdScenePrepare(Ppu *ppu, const uint8_t *wram, int bias);
void Dkc1HdSceneCaptureLine(Ppu *ppu, int line);
void Dkc1HdSceneFinish(Ppu *ppu);
const uint32_t *Dkc1HdScenePresent(const uint32_t *native, int width, int height);
/* Immutable presentation exports. Call only on the emulation/producer thread. */
size_t Dkc1HdSceneGpuFrameSize(const uint32_t *native,int width,int height);
bool Dkc1HdSceneCopyGpuFrame(void *destination,size_t size);
unsigned Dkc1HdSceneResidentCount(void);
uint64_t Dkc1HdSceneResidentBytes(void);
const uint32_t *Dkc1HdSceneResidentPixels(unsigned index,int *width,int *height);
#endif
