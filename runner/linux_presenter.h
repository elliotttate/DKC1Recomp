#ifndef DKC1_LINUX_PRESENTER_H
#define DKC1_LINUX_PRESENTER_H
#include <SDL.h>
#include <stdint.h>
#include <stdbool.h>
#include "desktop_graphics.h"
#include "macos_file_picker.h" /* Dkc1MacFullscreenScaling */

/* Vulkan-primary, OpenGL-fallback presenter for the Linux desktop host.
 * Mirrors the shape of macos_metal_presenter.h / windows_graphics.c: one
 * "enhanced" presenter that owns the window's swapchain/GL context, so
 * Linux gets accelerated presentation rather than a plain blit. The
 * OpenGL fallback draws the same reconstruct/CRT/bloom shader passes
 * macOS and Windows do (windows_graphics.c's pipeline, ported nearly
 * unchanged); the Vulkan backend does NOT yet -- it currently only
 * implements the flat/sharp-bilinear present pass (see
 * linux_vk_graphics.h). CRT display mode still renders through that same
 * pass on Vulkan rather than failing, it just isn't visually different
 * from Flat yet.
 *
 * Dkc1LinuxPresenterStart tries Vulkan first (if built with
 * DKC1_LINUX_VULKAN); on any failure it tries OpenGL 3.3 core next. If
 * both fail it returns 0 and sdl_host.c's existing SDL_Renderer fallback
 * (already in PreparePresentation/SubmitPresentation) takes over --
 * exactly the same fallthrough macOS uses when its Metal presenter is
 * disabled or unavailable.
 */
int Dkc1LinuxPresenterStart(SDL_Window *window, double preferred_hz,
                            Dkc1MacFullscreenScaling scaling, int fullscreen);
void Dkc1LinuxPresenterQueueFrame(const uint32_t *pixels, int width,
                                  int height, int presentation_width);
void Dkc1LinuxPresenterSetGeometry(int presentation_width, int fullscreen);
void Dkc1LinuxPresenterSetScaling(Dkc1MacFullscreenScaling scaling);
void Dkc1LinuxPresenterSetActive(int active);
void Dkc1LinuxPresenterFlush(void);
void Dkc1LinuxPresenterStop(void);
void Dkc1LinuxPresenterSetGraphics(const Dkc1GraphicsSettings *settings);

/* Which backend actually ended up active. 0 = none (SDL_Renderer
 * fallback owns presentation), 1 = Vulkan, 2 = OpenGL. */
int Dkc1LinuxPresenterActiveBackend(void);

/* Cheap pre-flight check: creates a throwaway Vulkan instance and confirms
 * at least one physical device exists (no surface/present-support check --
 * that still happens for real in Dkc1LinuxPresenterStart). Called from
 * sdl_host.c's InitVideo() BEFORE the real window exists, so the window
 * can be created with the correct SDL_WINDOW_VULKAN-or-not flag from the
 * start, rather than needing to recreate it if Vulkan init fails later:
 * SDL_GL_CreateContext cannot get a context on a window created with
 * SDL_WINDOW_VULKAN. Returns false (no Vulkan possible/likely) when built
 * without DKC1_LINUX_VULKAN. */
bool Dkc1LinuxPresenterVulkanLikelyAvailable(void);

#endif
