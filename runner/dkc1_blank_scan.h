#ifndef DKC1_BLANK_SCAN_H
#define DKC1_BLANK_SCAN_H

#include <stdbool.h>
#include <stdint.h>

/* Rendered-blank margin detector (enabled by DKC1_BLANK_SCAN=<jsonl> or by
 * DKC1_AUTO_EXPORT=1 without requiring a diagnostic log file).
 * Call after Dkc1DrawPpuFrame with the presented framebuffer. Host-side
 * only; never touches emulated state. */
void Dkc1BlankScanFrame(long host_frame, const uint8_t *pixels, int width,
                        int height, bool extended_gameplay);

/* Total suspect frames so far (for auto-export triggers). */
long Dkc1BlankScanEventCount(void);

#endif
