#ifndef DKC1_MARGIN_PROXY_H
#define DKC1_MARGIN_PROXY_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct CpuState;

/*
 * Experimental, fail-closed host presentation for placed actors that are
 * visible only in the added widescreen margins.  The cartridge's object
 * scanner remains native-width; approved source records are initialized and
 * advanced inside full-WRAM transactions, then borrowed into free normal
 * actor slots only while DKC's authentic OAM renderer runs.
 *
 * Disabled unless DKC1_MARGIN_PROXIES=1.  The generated manifest is an
 * authorization set, not a sprite-class wildcard.
 */
bool Dkc1MarginProxyEnabled(void);
void Dkc1MarginProxyReset(void);

/* Called immediately around each authentic CODE_BBA849 OAM draw call. */
void Dkc1MarginProxyBeginRender(struct CpuState *cpu);
void Dkc1MarginProxyEndRender(struct CpuState *cpu);

/* Versioned opaque state used by DKC1's host save-state extension. */
size_t Dkc1MarginProxySnapshotSize(void);
bool Dkc1MarginProxySnapshotSave(void *data, size_t size);
bool Dkc1MarginProxySnapshotLoad(const void *data, size_t size);

#endif
