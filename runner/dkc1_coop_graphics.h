#ifndef DKC1_COOP_GRAPHICS_H
#define DKC1_COOP_GRAPHICS_H

#include <stddef.h>
#include <stdint.h>

struct CpuState;

/* Physical OBJ slots are presentation resources in co-op. The cartridge
 * still receives valid tile/palette bits; each view materializes the owning
 * entity's graphics independently before rasterization. */
enum {
  kDkc1CoopGraphicsTileBase = 0x80,
  kDkc1CoopGraphicsPaletteBits = 0x0c00
};

void Dkc1CoopGraphicsSetRom(const uint8_t *rom, size_t size);
void Dkc1CoopGraphicsReset(void);
/* Observe palette progress once per guest frame, independently of redraws. */
void Dkc1CoopGraphicsNoteFrame(void);
void Dkc1CoopGraphicsResetEntity(uint16_t handle);
void Dkc1CoopGraphicsSeedNative(void);
unsigned Dkc1CoopGraphicsBlocks(uint16_t handle);
int Dkc1CoopGraphicsGetPalette(uint16_t handle, uint16_t colors[16]);
/* Resolve an explicit identity through the cartridge lighting palette bank. */
int Dkc1CoopGraphicsReadPalette(uint16_t source, uint16_t colors[16]);
void Dkc1CoopGraphicsCopyPalette(uint16_t destination, uint16_t source);
void Dkc1CoopGraphicsUseFixedPalette(uint16_t handle);

/* Source-owned adapters return nonzero only when they handled this call.
 * They leave the generated hardware RTS/RTL machinery in place. */
int Dkc1CoopGraphicsAllocate(struct CpuState *cpu, int contiguous);
int Dkc1CoopGraphicsRelease(struct CpuState *cpu, uint16_t handle);
int Dkc1CoopGraphicsCapacity(struct CpuState *cpu);
int Dkc1CoopGraphicsCancelKongPaletteUpload(struct CpuState *cpu);
int Dkc1CoopGraphicsAllocatePalette(struct CpuState *cpu,
                                  uint32_t return_pc);
int Dkc1CoopGraphicsReplacePalette(struct CpuState *cpu, int change_flags);
int Dkc1CoopGraphicsReleasePalette(struct CpuState *cpu,
                                 uint32_t return_pc);

int Dkc1CoopGraphicsBeginTransaction(void);
void Dkc1CoopGraphicsEndTransaction(int commit);
size_t Dkc1CoopGraphicsStateSize(void);
int Dkc1CoopGraphicsSaveState(void *destination, size_t size);
int Dkc1CoopGraphicsLoadState(const void *source, size_t size);

#endif
