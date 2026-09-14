#ifndef DKC1_LINUX_GL_GRAPHICS_H
#define DKC1_LINUX_GL_GRAPHICS_H
#include <SDL.h>
#include <stdint.h>
#include "desktop_graphics.h"

/* Desktop OpenGL 3.3 core implementation of the same 7-pass presentation
 * pipeline windows_graphics.c uses (reconstruct/lines/beam/down/blur/
 * compose), reusing the identical generated GLSL (gl_shaders.h, generated
 * from macos_graphics.metal by the same script Windows uses). This is the
 * OpenGL fallback behind the Vulkan-primary Linux presenter. */
bool Dkc1LinuxGlGraphicsInit(SDL_Window *window);
void Dkc1LinuxGlGraphicsDraw(const uint32_t *pixels, int w, int h,
                             int display_width,
                             const Dkc1GraphicsSettings *settings);
void Dkc1LinuxGlGraphicsSwap(void);
void Dkc1LinuxGlGraphicsClose(void);
int Dkc1LinuxGlGraphicsTest(void);

#endif
