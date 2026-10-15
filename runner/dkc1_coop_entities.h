#ifndef DKC1_COOP_ENTITIES_H
#define DKC1_COOP_ENTITIES_H

#include <stddef.h>
#include <stdint.h>

struct CpuState;

enum {
  kDkc1CoopEntityExtraFirst = 0x80,
  kDkc1CoopEntityExtraLast = 0xfe,
  kDkc1CoopEntityExtraCount = 64,
  kDkc1CoopEntityEnd = 0x100
};

/* Handles remain even bytes: source bookmarks keep their cartridge format.
 * $34..$72 belong to the separate effect pool; $ff is a group bookmark. */
int Dkc1CoopEntityIsHandle(uint16_t handle);
int Dkc1CoopEntitiesActive(void);
int Dkc1CoopEntityIsField(uint16_t base);
uint16_t Dkc1CoopEntityNextHandle(uint16_t handle);
uint16_t Dkc1CoopEntityPreviousHandle(uint16_t handle);
uint16_t Dkc1CoopEntityLastHandle(void);
/* Native KONG/banana flights use screen coordinates in their creator's view. */
static inline int Dkc1CoopEffectUsesOwnView(uint16_t id) {
  return id == 0x41u || id == 0x42u;
}
unsigned Dkc1CoopEffectView(uint16_t handle);
uint16_t Dkc1CoopEntityRead16(const uint8_t *wram, uint16_t base,
                           uint16_t handle);
void Dkc1CoopEntityWrite16(uint8_t *wram, uint16_t base, uint16_t handle,
                         uint16_t value);

/* Generated adapters keep the original literal table base until this call;
 * intercepting an already-added bus address would alias another actor field. */
uint8_t Dkc1CoopIndexedRead8(struct CpuState *cpu, uint8_t bank,
                          uint16_t base, uint16_t index);
uint16_t Dkc1CoopIndexedRead16(struct CpuState *cpu, uint8_t bank,
                            uint16_t base, uint16_t index);
void Dkc1CoopIndexedWrite8(struct CpuState *cpu, uint8_t bank,
                         uint16_t base, uint16_t index, uint8_t value);
void Dkc1CoopIndexedWrite16(struct CpuState *cpu, uint8_t bank,
                          uint16_t base, uint16_t index, uint16_t value);

/* Called only at the authentic allocator's exhaustion label. On success X
 * receives a free virtual handle and execution rejoins its normal success path. */
int Dkc1CoopEntityAllocate(struct CpuState *cpu, unsigned native_pool_end);
void Dkc1CoopEntityAllocated(struct CpuState *cpu);
void Dkc1CoopEntityScriptWrite16(struct CpuState *cpu, uint16_t value);
void Dkc1CoopEntityClearRange(uint16_t base, uint16_t size);
uint16_t Dkc1CoopEntityLoopIndex(uint16_t index);
uint16_t Dkc1CoopEntityLiveLoopIndex(uint16_t index);
uint16_t Dkc1CoopEntityBackwardIndex(uint16_t index);
uint16_t Dkc1CoopEntityPoolLoopIndex(uint16_t index, unsigned native_pool_end);
uint16_t Dkc1CoopEntityPoolBackwardIndex(uint16_t index, unsigned native_pool_end);
void Dkc1CoopEntityCollisionCursor(struct CpuState *cpu);
uint16_t Dkc1CoopEntityLoopEnd(uint16_t native_end);

void Dkc1CoopEntitiesReset(void);
void Dkc1CoopEntitiesPoolReset(void);
int Dkc1CoopEntitiesBeginTransaction(void);
void Dkc1CoopEntitiesEndTransaction(int commit);
size_t Dkc1CoopEntitiesStateSize(void);
int Dkc1CoopEntitiesSaveState(void *destination, size_t size);
int Dkc1CoopEntitiesLoadState(const void *source, size_t size);

#endif
