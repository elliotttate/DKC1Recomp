#ifndef DKC1_BLANK_SCAN_H
#define DKC1_BLANK_SCAN_H

#include <stdint.h>

/* Rendered-blank margin detector (env-gated: DKC1_BLANK_SCAN=<jsonl>).
 * Call after Dkc1DrawPpuFrame with the presented framebuffer. Host-side
 * only; never touches emulated state. */
void Dkc1BlankScanFrame(long host_frame, const uint8_t *pixels, int width,
                        int height);

/* Total suspect frames so far (for auto-export triggers). */
long Dkc1BlankScanEventCount(void);

#endif
