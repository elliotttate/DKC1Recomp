#include "linux_presenter.h"
#ifdef DKC1_LINUX_VULKAN
#include "linux_vk_graphics.h"
#include <vulkan/vulkan.h>
#endif
#include "linux_gl_graphics.h"
#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { kBackendNone = 0, kBackendVulkan = 1, kBackendOpenGl = 2 };
static int s_backend = kBackendNone;
static Dkc1GraphicsSettings s_graphics;
static int s_active = 1;

static int EnvDisabled(const char *name) {
  const char *v = getenv(name);
  return v && v[0] && strcmp(v, "0") != 0;
}

int Dkc1LinuxPresenterStart(SDL_Window *window, double preferred_hz,
                            Dkc1MacFullscreenScaling scaling, int fullscreen) {
  (void)preferred_hz; (void)scaling; (void)fullscreen;
  s_backend = kBackendNone;

#ifdef DKC1_LINUX_VULKAN
  if (!EnvDisabled("DKC1_DISABLE_VULKAN") && Dkc1LinuxVkGraphicsInit(window)) {
    s_backend = kBackendVulkan;
    return 1;
  }
  fprintf(stderr, "[linux-presenter] Vulkan unavailable or disabled; trying OpenGL\n");
#else
  fprintf(stderr, "[linux-presenter] built without Vulkan support; trying OpenGL\n");
#endif

  if (!EnvDisabled("DKC1_DISABLE_OPENGL") && Dkc1LinuxGlGraphicsInit(window)) {
    s_backend = kBackendOpenGl;
    return 1;
  }

  fprintf(stderr,
          "[linux-presenter] no accelerated backend available; falling back "
          "to plain SDL_Renderer (no CRT/upscale shader effects)\n");
  return 0;
}

int Dkc1LinuxPresenterActiveBackend(void) { return s_backend; }

bool Dkc1LinuxPresenterVulkanLikelyAvailable(void) {
#ifdef DKC1_LINUX_VULKAN
  if (EnvDisabled("DKC1_DISABLE_VULKAN")) return false;
  VkApplicationInfo app_info = {.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
      .pApplicationName = "DKC1Recomp", .apiVersion = VK_API_VERSION_1_0};
  VkInstanceCreateInfo inst_info = {.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
      .pApplicationInfo = &app_info};
  VkInstance instance = VK_NULL_HANDLE;
  if (vkCreateInstance(&inst_info, NULL, &instance) != VK_SUCCESS) return false;
  uint32_t device_count = 0;
  vkEnumeratePhysicalDevices(instance, &device_count, NULL);
  vkDestroyInstance(instance, NULL);
  return device_count > 0;
#else
  return false;
#endif
}

void Dkc1LinuxPresenterQueueFrame(const uint32_t *pixels, int width, int height,
                                  int presentation_width) {
  if (!s_active) return;
  switch (s_backend) {
#ifdef DKC1_LINUX_VULKAN
    case kBackendVulkan:
      Dkc1LinuxVkGraphicsDraw(pixels, width, height, presentation_width, &s_graphics);
      break;
#endif
    case kBackendOpenGl:
      Dkc1LinuxGlGraphicsDraw(pixels, width, height, presentation_width, &s_graphics);
      Dkc1LinuxGlGraphicsSwap();
      break;
    default:
      break;
  }
}

void Dkc1LinuxPresenterSetGeometry(int presentation_width, int fullscreen) {
  (void)presentation_width; (void)fullscreen;
  /* Both backends re-derive viewport geometry every draw from the
   * window's current drawable size; nothing to precompute here. */
}

void Dkc1LinuxPresenterSetScaling(Dkc1MacFullscreenScaling scaling) {
  (void)scaling; /* Scaling mode lives in s_graphics.upscaler already. */
}

void Dkc1LinuxPresenterSetActive(int active) { s_active = active; }

void Dkc1LinuxPresenterFlush(void) {
  /* Both backends draw synchronously from QueueFrame's perspective, but
   * the GPU work they submitted may still be in flight (Vulkan pipelines
   * DKC1_VK_FRAMES_IN_FLIGHT frames deep). A rewind/quickload jump wants
   * the *next* QueueFrame to be the first thing visible, not a leftover
   * frame from just before the jump still working its way through the
   * queue -- so actually wait for the GPU to go idle here rather than
   * treating this as a no-op. */
#ifdef DKC1_LINUX_VULKAN
  if (s_backend == kBackendVulkan) Dkc1LinuxVkGraphicsWaitIdle();
#endif
}

void Dkc1LinuxPresenterStop(void) {
  switch (s_backend) {
#ifdef DKC1_LINUX_VULKAN
    case kBackendVulkan: Dkc1LinuxVkGraphicsClose(); break;
#endif
    case kBackendOpenGl: Dkc1LinuxGlGraphicsClose(); break;
    default: break;
  }
  s_backend = kBackendNone;
}

void Dkc1LinuxPresenterSetGraphics(const Dkc1GraphicsSettings *settings) {
  s_graphics = *settings;
}
