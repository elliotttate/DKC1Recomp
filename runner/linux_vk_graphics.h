#ifndef DKC1_LINUX_VK_GRAPHICS_H
#define DKC1_LINUX_VK_GRAPHICS_H
#include <SDL.h>
#include <stdint.h>
#include "desktop_graphics.h"

/* Vulkan 1.0 backend: the default/primary Linux renderer.
 *
 * Status (first pass): a real swapchain + a single textured-quad pass
 * (the "flat"/sharp-bilinear upscale dkc1_flat already does on the other
 * platforms), driven by the same nearest/sharp-bilinear settings field.
 * The CRT/reconstruct/bloom multi-pass pipeline (lines/beam/down/blur/
 * compose in the GL and Metal backends) is NOT ported yet -- that needs
 * several more offscreen render targets and is real follow-up work, not
 * a quick addition. Requesting CRT display mode currently still renders
 * through this same flat pass rather than failing.
 *
 * Returns false from Init on any failure (missing Vulkan, no suitable
 * device, etc.) so the caller (linux_presenter.c) can fall back to the
 * OpenGL backend cleanly. */
bool Dkc1LinuxVkGraphicsInit(SDL_Window *window);
void Dkc1LinuxVkGraphicsDraw(const uint32_t *pixels, int w, int h,
                             int display_width,
                             const Dkc1GraphicsSettings *settings);
void Dkc1LinuxVkGraphicsSwap(void);
void Dkc1LinuxVkGraphicsClose(void);

/* Blocks until all submitted GPU work completes. Used by the presenter's
 * Flush() so a rewind/quickload can't briefly present a frame that was
 * still in the pipeline from before the jump. */
void Dkc1LinuxVkGraphicsWaitIdle(void);

#endif
