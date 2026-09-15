/* Vulkan 1.0 backend for the Linux desktop host. See linux_vk_graphics.h
 * for current scope (single present pass; CRT/reconstruct/bloom passes
 * are a documented follow-up, not yet ported from the GL/Metal backends).
 *
 * Only immutable completed host pixels are read here; no guest state is
 * touched, matching the read-only contract the other backends follow.
 */
#include "linux_vk_graphics.h"
#include <vulkan/vulkan.h>
#include <SDL_vulkan.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "vk_shader_vert.h" /* generated: g_dkc1_present_vert */
#include "vk_shader_frag.h" /* generated: g_dkc1_present_frag */

#define DKC1_VK_FRAMES_IN_FLIGHT 2
#define DKC1_VK_CHECK(expr) do { VkResult _r = (expr); if (_r != VK_SUCCESS) { \
    fprintf(stderr, "[linux-vk-graphics] %s failed: %d (%s:%d)\n", #expr, (int)_r, __FILE__, __LINE__); \
    goto fail; } } while (0)

typedef struct Frame {
  VkCommandBuffer cmd;
  VkSemaphore image_available, render_finished;
  VkFence in_flight;
  /* Per-frame staging buffer: a single shared buffer let the CPU overwrite
   * frame N-1's still-pending source pixels (FIFO present doesn't block
   * the CPU, and the fence wait only reaches back to frame N-2), producing
   * torn frames. One buffer per frame-in-flight slot removes that race. */
  VkBuffer staging_buffer;
  VkDeviceMemory staging_memory;
  void *staging_mapped;
  VkDeviceSize staging_size;
} Frame;

typedef struct PushConsts {
  float source_w, source_h;
  float output_w, output_h;
  int32_t mode;
} PushConsts;

static SDL_Window *s_window;
static VkInstance s_instance;
static VkSurfaceKHR s_surface;
static VkPhysicalDevice s_phys;
static VkDevice s_device;
static uint32_t s_queue_family = UINT32_MAX;
static VkQueue s_queue;
static VkSwapchainKHR s_swapchain;
static VkFormat s_swap_format;
static VkExtent2D s_swap_extent;
static VkImage *s_swap_images;
static VkImageView *s_swap_views;
static VkFramebuffer *s_swap_fbs;
static uint32_t s_swap_count;
static VkRenderPass s_render_pass;
static VkPipelineLayout s_pipeline_layout;
static VkPipeline s_pipeline;
static VkDescriptorSetLayout s_desc_layout;
static VkDescriptorPool s_desc_pool;
static VkDescriptorSet s_desc_set;
static VkCommandPool s_cmd_pool;
static Frame s_frames[DKC1_VK_FRAMES_IN_FLIGHT];
static uint32_t s_frame_index;

static VkImage s_input_image;
static VkDeviceMemory s_input_memory;
static VkImageView s_input_view;
static VkSampler s_sampler; /* nearest */
static VkSampler s_sampler_linear;
static VkSampler s_desc_sampler; /* whichever is currently bound in the descriptor */
static int s_input_w, s_input_h;
static bool s_input_ready;
/* Staging buffers now live per-frame inside Frame (see above) instead of
 * as one shared global -- see the comment on Frame's staging fields. */

static int s_ready;

static uint32_t FindMemoryType(uint32_t type_bits, VkMemoryPropertyFlags props) {
  VkPhysicalDeviceMemoryProperties mem_props;
  vkGetPhysicalDeviceMemoryProperties(s_phys, &mem_props);
  for (uint32_t i = 0; i < mem_props.memoryTypeCount; i++)
    if ((type_bits & (1u << i)) &&
        (mem_props.memoryTypes[i].propertyFlags & props) == props)
      return i;
  return UINT32_MAX;
}

static VkShaderModule MakeShader(const uint32_t *code, size_t size) {
  VkShaderModuleCreateInfo info = {.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
                                     .codeSize = size, .pCode = code};
  VkShaderModule module = VK_NULL_HANDLE;
  vkCreateShaderModule(s_device, &info, NULL, &module);
  return module;
}

static void DestroySwapchainObjects(void) {
  if (s_swap_fbs)
    for (uint32_t i = 0; i < s_swap_count; i++)
      if (s_swap_fbs[i]) vkDestroyFramebuffer(s_device, s_swap_fbs[i], NULL);
  if (s_swap_views)
    for (uint32_t i = 0; i < s_swap_count; i++)
      if (s_swap_views[i]) vkDestroyImageView(s_device, s_swap_views[i], NULL);
  free(s_swap_fbs); s_swap_fbs = NULL;
  free(s_swap_views); s_swap_views = NULL;
  free(s_swap_images); s_swap_images = NULL;
  if (s_swapchain) { vkDestroySwapchainKHR(s_device, s_swapchain, NULL); s_swapchain = VK_NULL_HANDLE; }
}

static bool CreateSwapchain(void) {
  VkSurfaceCapabilitiesKHR caps;
  vkGetPhysicalDeviceSurfaceCapabilitiesKHR(s_phys, s_surface, &caps);

  uint32_t format_count = 0;
  vkGetPhysicalDeviceSurfaceFormatsKHR(s_phys, s_surface, &format_count, NULL);
  if (!format_count) return false;
  VkSurfaceFormatKHR *formats = malloc(sizeof(VkSurfaceFormatKHR) * format_count);
  vkGetPhysicalDeviceSurfaceFormatsKHR(s_phys, s_surface, &format_count, formats);
  VkSurfaceFormatKHR chosen = formats[0];
  for (uint32_t i = 0; i < format_count; i++)
    if (formats[i].format == VK_FORMAT_B8G8R8A8_UNORM &&
        formats[i].colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
      chosen = formats[i];
      break;
    }
  free(formats);
  s_swap_format = chosen.format;

  int draw_w, draw_h;
  SDL_Vulkan_GetDrawableSize(s_window, &draw_w, &draw_h);
  s_swap_extent.width = caps.currentExtent.width != UINT32_MAX
                             ? caps.currentExtent.width : (uint32_t)draw_w;
  s_swap_extent.height = caps.currentExtent.height != UINT32_MAX
                              ? caps.currentExtent.height : (uint32_t)draw_h;
  if (s_swap_extent.width < 1) s_swap_extent.width = 1;
  if (s_swap_extent.height < 1) s_swap_extent.height = 1;

  uint32_t image_count = caps.minImageCount + 1;
  if (caps.maxImageCount > 0 && image_count > caps.maxImageCount)
    image_count = caps.maxImageCount;

  /* Pick a composite-alpha bit the surface actually supports rather than
   * assuming OPAQUE is available -- some compositors (Wayland especially)
   * only advertise PRE_MULTIPLIED or INHERIT, and requesting an
   * unsupported bit fails swapchain creation outright. */
  static const VkCompositeAlphaFlagBitsKHR kAlphaPreference[] = {
    VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
    VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR,
    VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR,
    VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR,
  };
  VkCompositeAlphaFlagBitsKHR composite_alpha = 0;
  for (size_t i = 0; i < sizeof kAlphaPreference / sizeof *kAlphaPreference; i++)
    if (caps.supportedCompositeAlpha & kAlphaPreference[i]) {
      composite_alpha = kAlphaPreference[i];
      break;
    }
  if (!composite_alpha) return false;

  VkSwapchainCreateInfoKHR info = {
    .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
    .surface = s_surface, .minImageCount = image_count,
    .imageFormat = s_swap_format, .imageColorSpace = chosen.colorSpace,
    .imageExtent = s_swap_extent, .imageArrayLayers = 1,
    .imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
    .imageSharingMode = VK_SHARING_MODE_EXCLUSIVE,
    .preTransform = caps.currentTransform,
    .compositeAlpha = composite_alpha,
    .presentMode = VK_PRESENT_MODE_FIFO_KHR, /* vsync; matches the other
        backends' "one clean producer" cadence rather than racing ahead. */
    .clipped = VK_TRUE,
  };
  if (vkCreateSwapchainKHR(s_device, &info, NULL, &s_swapchain) != VK_SUCCESS)
    return false;

  vkGetSwapchainImagesKHR(s_device, s_swapchain, &s_swap_count, NULL);
  s_swap_images = malloc(sizeof(VkImage) * s_swap_count);
  vkGetSwapchainImagesKHR(s_device, s_swapchain, &s_swap_count, s_swap_images);

  s_swap_views = calloc(s_swap_count, sizeof(VkImageView));
  s_swap_fbs = calloc(s_swap_count, sizeof(VkFramebuffer));
  for (uint32_t i = 0; i < s_swap_count; i++) {
    VkImageViewCreateInfo vi = {
      .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
      .image = s_swap_images[i], .viewType = VK_IMAGE_VIEW_TYPE_2D,
      .format = s_swap_format,
      .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1},
    };
    if (vkCreateImageView(s_device, &vi, NULL, &s_swap_views[i]) != VK_SUCCESS)
      return false;
    VkFramebufferCreateInfo fi = {
      .sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
      .renderPass = s_render_pass, .attachmentCount = 1,
      .pAttachments = &s_swap_views[i], .width = s_swap_extent.width,
      .height = s_swap_extent.height, .layers = 1,
    };
    if (vkCreateFramebuffer(s_device, &fi, NULL, &s_swap_fbs[i]) != VK_SUCCESS)
      return false;
  }
  return true;
}

static bool RecreateSwapchain(void) {
  vkDeviceWaitIdle(s_device);
  DestroySwapchainObjects();
  return CreateSwapchain();
}

bool Dkc1LinuxVkGraphicsInit(SDL_Window *window) {
  s_window = window;
  VkImageView *dummy_views = NULL; (void)dummy_views;

  unsigned ext_count = 0;
  if (!SDL_Vulkan_GetInstanceExtensions(window, &ext_count, NULL)) return false;
  const char **exts = malloc(sizeof(char *) * ext_count);
  if (!SDL_Vulkan_GetInstanceExtensions(window, &ext_count, exts)) { free(exts); return false; }

  VkApplicationInfo app_info = {.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
      .pApplicationName = "DKC1Recomp", .apiVersion = VK_API_VERSION_1_0};
  VkInstanceCreateInfo inst_info = {.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
      .pApplicationInfo = &app_info,
      .enabledExtensionCount = ext_count, .ppEnabledExtensionNames = exts};
  VkResult r = vkCreateInstance(&inst_info, NULL, &s_instance);
  free(exts);
  if (r != VK_SUCCESS) {
    fprintf(stderr, "[linux-vk-graphics] vkCreateInstance failed: %d\n", (int)r);
    return false;
  }

  if (!SDL_Vulkan_CreateSurface(window, s_instance, &s_surface)) {
    fprintf(stderr, "[linux-vk-graphics] SDL_Vulkan_CreateSurface: %s\n", SDL_GetError());
    goto fail;
  }

  uint32_t dev_count = 0;
  vkEnumeratePhysicalDevices(s_instance, &dev_count, NULL);
  if (!dev_count) { fprintf(stderr, "[linux-vk-graphics] no Vulkan-capable device\n"); goto fail; }
  VkPhysicalDevice *devices = malloc(sizeof(VkPhysicalDevice) * dev_count);
  vkEnumeratePhysicalDevices(s_instance, &dev_count, devices);

  for (uint32_t d = 0; d < dev_count && s_queue_family == UINT32_MAX; d++) {
    uint32_t qf_count = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(devices[d], &qf_count, NULL);
    VkQueueFamilyProperties *qfs = malloc(sizeof(VkQueueFamilyProperties) * qf_count);
    vkGetPhysicalDeviceQueueFamilyProperties(devices[d], &qf_count, qfs);
    for (uint32_t q = 0; q < qf_count; q++) {
      VkBool32 present = VK_FALSE;
      vkGetPhysicalDeviceSurfaceSupportKHR(devices[d], q, s_surface, &present);
      if ((qfs[q].queueFlags & VK_QUEUE_GRAPHICS_BIT) && present) {
        s_phys = devices[d]; s_queue_family = q; break;
      }
    }
    free(qfs);
  }
  free(devices);
  if (s_queue_family == UINT32_MAX) {
    fprintf(stderr, "[linux-vk-graphics] no queue family with graphics+present\n");
    goto fail;
  }

  float priority = 1.0f;
  VkDeviceQueueCreateInfo queue_info = {.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
      .queueFamilyIndex = s_queue_family, .queueCount = 1, .pQueuePriorities = &priority};
  const char *device_exts[] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};
  VkDeviceCreateInfo dev_info = {.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
      .queueCreateInfoCount = 1, .pQueueCreateInfos = &queue_info,
      .enabledExtensionCount = 1, .ppEnabledExtensionNames = device_exts};
  DKC1_VK_CHECK(vkCreateDevice(s_phys, &dev_info, NULL, &s_device));
  vkGetDeviceQueue(s_device, s_queue_family, 0, &s_queue);

  VkAttachmentDescription color_attachment = {
    .format = VK_FORMAT_UNDEFINED, /* patched below once we know it */
    .samples = VK_SAMPLE_COUNT_1_BIT,
    .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR, .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
    .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
    .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
    .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
    .finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
  };
  /* Probe the format the swapchain will actually pick before creating the
   * render pass, since VkAttachmentDescription.format must match. */
  {
    uint32_t format_count = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(s_phys, s_surface, &format_count, NULL);
    VkSurfaceFormatKHR *formats = malloc(sizeof(VkSurfaceFormatKHR) * format_count);
    vkGetPhysicalDeviceSurfaceFormatsKHR(s_phys, s_surface, &format_count, formats);
    color_attachment.format = formats[0].format;
    for (uint32_t i = 0; i < format_count; i++)
      if (formats[i].format == VK_FORMAT_B8G8R8A8_UNORM &&
          formats[i].colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
        color_attachment.format = formats[i].format;
    free(formats);
  }
  VkAttachmentReference color_ref = {0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
  VkSubpassDescription subpass = {.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
      .colorAttachmentCount = 1, .pColorAttachments = &color_ref};
  VkSubpassDependency dependency = {
    .srcSubpass = VK_SUBPASS_EXTERNAL, .dstSubpass = 0,
    .srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
    .dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
    .dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
  };
  VkRenderPassCreateInfo rp_info = {.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
      .attachmentCount = 1, .pAttachments = &color_attachment,
      .subpassCount = 1, .pSubpasses = &subpass,
      .dependencyCount = 1, .pDependencies = &dependency};
  DKC1_VK_CHECK(vkCreateRenderPass(s_device, &rp_info, NULL, &s_render_pass));

  if (!CreateSwapchain()) goto fail;

  VkDescriptorSetLayoutBinding sampler_binding = {
    .binding = 0, .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
    .descriptorCount = 1, .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT};
  VkDescriptorSetLayoutCreateInfo dsl_info = {.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
      .bindingCount = 1, .pBindings = &sampler_binding};
  DKC1_VK_CHECK(vkCreateDescriptorSetLayout(s_device, &dsl_info, NULL, &s_desc_layout));

  VkPushConstantRange push_range = {.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
      .offset = 0, .size = sizeof(PushConsts)};
  VkPipelineLayoutCreateInfo pl_info = {.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
      .setLayoutCount = 1, .pSetLayouts = &s_desc_layout,
      .pushConstantRangeCount = 1, .pPushConstantRanges = &push_range};
  DKC1_VK_CHECK(vkCreatePipelineLayout(s_device, &pl_info, NULL, &s_pipeline_layout));

  {
    VkShaderModule vert = MakeShader(g_dkc1_present_vert, sizeof(g_dkc1_present_vert));
    VkShaderModule frag = MakeShader(g_dkc1_present_frag, sizeof(g_dkc1_present_frag));
    if (!vert || !frag) { fprintf(stderr, "[linux-vk-graphics] shader module creation failed\n"); goto fail; }
    VkPipelineShaderStageCreateInfo stages[2] = {
      {.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
       .stage = VK_SHADER_STAGE_VERTEX_BIT, .module = vert, .pName = "main"},
      {.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
       .stage = VK_SHADER_STAGE_FRAGMENT_BIT, .module = frag, .pName = "main"},
    };
    VkPipelineVertexInputStateCreateInfo vi_state = {.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    VkPipelineInputAssemblyStateCreateInfo ia_state = {.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
        .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP};
    VkPipelineViewportStateCreateInfo vp_state = {.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
        .viewportCount = 1, .scissorCount = 1};
    VkPipelineRasterizationStateCreateInfo rs_state = {.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
        .polygonMode = VK_POLYGON_MODE_FILL, .cullMode = VK_CULL_MODE_NONE,
        .frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE, .lineWidth = 1.0f};
    VkPipelineMultisampleStateCreateInfo ms_state = {.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
        .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT};
    VkPipelineColorBlendAttachmentState blend_attachment = {.colorWriteMask =
        VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT};
    VkPipelineColorBlendStateCreateInfo cb_state = {.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
        .attachmentCount = 1, .pAttachments = &blend_attachment};
    VkDynamicState dyn_states[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dyn_state = {.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
        .dynamicStateCount = 2, .pDynamicStates = dyn_states};
    VkGraphicsPipelineCreateInfo gp_info = {.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
        .stageCount = 2, .pStages = stages, .pVertexInputState = &vi_state,
        .pInputAssemblyState = &ia_state, .pViewportState = &vp_state,
        .pRasterizationState = &rs_state, .pMultisampleState = &ms_state,
        .pColorBlendState = &cb_state, .pDynamicState = &dyn_state,
        .layout = s_pipeline_layout, .renderPass = s_render_pass, .subpass = 0};
    VkResult pr = vkCreateGraphicsPipelines(s_device, VK_NULL_HANDLE, 1, &gp_info, NULL, &s_pipeline);
    vkDestroyShaderModule(s_device, vert, NULL);
    vkDestroyShaderModule(s_device, frag, NULL);
    if (pr != VK_SUCCESS) { fprintf(stderr, "[linux-vk-graphics] pipeline creation failed: %d\n", (int)pr); goto fail; }
  }

  VkCommandPoolCreateInfo cp_info = {.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
      .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT, .queueFamilyIndex = s_queue_family};
  DKC1_VK_CHECK(vkCreateCommandPool(s_device, &cp_info, NULL, &s_cmd_pool));

  VkDescriptorPoolSize pool_size = {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1};
  VkDescriptorPoolCreateInfo dp_info = {.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
      .maxSets = 1, .poolSizeCount = 1, .pPoolSizes = &pool_size};
  DKC1_VK_CHECK(vkCreateDescriptorPool(s_device, &dp_info, NULL, &s_desc_pool));
  VkDescriptorSetAllocateInfo ds_alloc = {.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
      .descriptorPool = s_desc_pool, .descriptorSetCount = 1, .pSetLayouts = &s_desc_layout};
  DKC1_VK_CHECK(vkAllocateDescriptorSets(s_device, &ds_alloc, &s_desc_set));

  VkSamplerCreateInfo samp_info = {.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
      .magFilter = VK_FILTER_NEAREST, .minFilter = VK_FILTER_NEAREST,
      .addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
      .addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
      .addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE};
  DKC1_VK_CHECK(vkCreateSampler(s_device, &samp_info, NULL, &s_sampler));
  /* Bilinear/sharp-bilinear/reconstruct modes all need actual hardware
   * linear filtering underneath -- the sharp-bilinear shader math only
   * sharpens an otherwise-linear sample, it doesn't substitute for one. A
   * nearest sampler made every non-Nearest upscaler mode look identical
   * to Nearest. */
  VkSamplerCreateInfo samp_info_linear = samp_info;
  samp_info_linear.magFilter = VK_FILTER_LINEAR;
  samp_info_linear.minFilter = VK_FILTER_LINEAR;
  DKC1_VK_CHECK(vkCreateSampler(s_device, &samp_info_linear, NULL, &s_sampler_linear));

  for (int i = 0; i < DKC1_VK_FRAMES_IN_FLIGHT; i++) {
    VkCommandBufferAllocateInfo cb_alloc = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = s_cmd_pool, .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY, .commandBufferCount = 1};
    DKC1_VK_CHECK(vkAllocateCommandBuffers(s_device, &cb_alloc, &s_frames[i].cmd));
    VkSemaphoreCreateInfo sem_info = {.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    DKC1_VK_CHECK(vkCreateSemaphore(s_device, &sem_info, NULL, &s_frames[i].image_available));
    DKC1_VK_CHECK(vkCreateSemaphore(s_device, &sem_info, NULL, &s_frames[i].render_finished));
    VkFenceCreateInfo fence_info = {.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO, .flags = VK_FENCE_CREATE_SIGNALED_BIT};
    DKC1_VK_CHECK(vkCreateFence(s_device, &fence_info, NULL, &s_frames[i].in_flight));
  }

  {
    VkPhysicalDeviceProperties props;
    vkGetPhysicalDeviceProperties(s_phys, &props);
    fprintf(stderr, "[linux-vk-graphics] Vulkan device: %s; present pass ready\n", props.deviceName);
  }
  s_ready = 1;
  return true;

fail:
  Dkc1LinuxVkGraphicsClose();
  return false;
}

static bool EnsureInputImage(int w, int h) {
  if (s_input_ready && s_input_w == w && s_input_h == h) return true;
  s_input_ready = false;
  if (s_input_image) {
    if (s_input_view) vkDestroyImageView(s_device, s_input_view, NULL);
    vkDestroyImage(s_device, s_input_image, NULL);
    if (s_input_memory) vkFreeMemory(s_device, s_input_memory, NULL);
    s_input_image = VK_NULL_HANDLE; s_input_view = VK_NULL_HANDLE;
    s_input_memory = VK_NULL_HANDLE;
  }
  s_input_w = w; s_input_h = h;
  VkImageCreateInfo img_info = {.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
      .imageType = VK_IMAGE_TYPE_2D, .format = VK_FORMAT_B8G8R8A8_UNORM,
      .extent = {(uint32_t)w, (uint32_t)h, 1}, .mipLevels = 1, .arrayLayers = 1,
      .samples = VK_SAMPLE_COUNT_1_BIT, .tiling = VK_IMAGE_TILING_OPTIMAL,
      .usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
      .sharingMode = VK_SHARING_MODE_EXCLUSIVE, .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED};
  if (vkCreateImage(s_device, &img_info, NULL, &s_input_image) != VK_SUCCESS) {
    s_input_image = VK_NULL_HANDLE;
    return false;
  }
  VkMemoryRequirements req;
  vkGetImageMemoryRequirements(s_device, s_input_image, &req);
  VkMemoryAllocateInfo alloc = {.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
      .allocationSize = req.size,
      .memoryTypeIndex = FindMemoryType(req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)};
  if (vkAllocateMemory(s_device, &alloc, NULL, &s_input_memory) != VK_SUCCESS) {
    vkDestroyImage(s_device, s_input_image, NULL);
    s_input_image = VK_NULL_HANDLE; s_input_memory = VK_NULL_HANDLE;
    return false;
  }
  vkBindImageMemory(s_device, s_input_image, s_input_memory, 0);
  VkImageViewCreateInfo view_info = {.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
      .image = s_input_image, .viewType = VK_IMAGE_VIEW_TYPE_2D, .format = VK_FORMAT_B8G8R8A8_UNORM,
      .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1}};
  if (vkCreateImageView(s_device, &view_info, NULL, &s_input_view) != VK_SUCCESS) {
    vkDestroyImage(s_device, s_input_image, NULL);
    vkFreeMemory(s_device, s_input_memory, NULL);
    s_input_image = VK_NULL_HANDLE; s_input_view = VK_NULL_HANDLE;
    s_input_memory = VK_NULL_HANDLE;
    return false;
  }
  s_desc_sampler = VK_NULL_HANDLE; /* force UpdateDescriptorSampler to (re)write */
  s_input_ready = true;
  return true;
}

/* (Re)points the descriptor set's sampler at `sampler` only when it
 * actually changed (upscaler mode flipped between nearest/linear, or the
 * image view was just recreated), not every frame. */
static void UpdateDescriptorSampler(VkSampler sampler) {
  if (sampler == s_desc_sampler) return;
  VkDescriptorImageInfo desc_img = {.sampler = sampler, .imageView = s_input_view,
      .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
  VkWriteDescriptorSet write = {.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
      .dstSet = s_desc_set, .dstBinding = 0, .descriptorCount = 1,
      .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, .pImageInfo = &desc_img};
  vkUpdateDescriptorSets(s_device, 1, &write, 0, NULL);
  s_desc_sampler = sampler;
}

static bool EnsureStaging(Frame *frame, VkDeviceSize needed) {
  if (frame->staging_buffer && frame->staging_size >= needed) return true;
  if (frame->staging_buffer) {
    vkUnmapMemory(s_device, frame->staging_memory);
    vkDestroyBuffer(s_device, frame->staging_buffer, NULL);
    vkFreeMemory(s_device, frame->staging_memory, NULL);
    frame->staging_buffer = VK_NULL_HANDLE;
    frame->staging_memory = VK_NULL_HANDLE;
    frame->staging_size = 0;
  }
  VkBufferCreateInfo buf_info = {.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
      .size = needed, .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT, .sharingMode = VK_SHARING_MODE_EXCLUSIVE};
  if (vkCreateBuffer(s_device, &buf_info, NULL, &frame->staging_buffer) != VK_SUCCESS) {
    frame->staging_buffer = VK_NULL_HANDLE;
    return false;
  }
  VkMemoryRequirements req;
  vkGetBufferMemoryRequirements(s_device, frame->staging_buffer, &req);
  VkMemoryAllocateInfo alloc = {.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
      .allocationSize = req.size,
      .memoryTypeIndex = FindMemoryType(req.memoryTypeBits,
          VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)};
  if (vkAllocateMemory(s_device, &alloc, NULL, &frame->staging_memory) != VK_SUCCESS) {
    vkDestroyBuffer(s_device, frame->staging_buffer, NULL);
    frame->staging_buffer = VK_NULL_HANDLE; frame->staging_memory = VK_NULL_HANDLE;
    return false;
  }
  vkBindBufferMemory(s_device, frame->staging_buffer, frame->staging_memory, 0);
  vkMapMemory(s_device, frame->staging_memory, 0, needed, 0, &frame->staging_mapped);
  frame->staging_size = needed;
  return true;
}

void Dkc1LinuxVkGraphicsDraw(const uint32_t *pixels, int w, int h,
                             int display_width, const Dkc1GraphicsSettings *settings) {
  if (!s_ready) return;
  Frame *frame = &s_frames[s_frame_index];
  vkWaitForFences(s_device, 1, &frame->in_flight, VK_TRUE, UINT64_MAX);

  uint32_t image_index = 0;
  VkResult acquire = vkAcquireNextImageKHR(s_device, s_swapchain, UINT64_MAX,
      frame->image_available, VK_NULL_HANDLE, &image_index);
  if (acquire == VK_ERROR_OUT_OF_DATE_KHR) { RecreateSwapchain(); return; }
  if (acquire != VK_SUCCESS && acquire != VK_SUBOPTIMAL_KHR) return;

  VkDeviceSize bytes = (VkDeviceSize)w * h * 4;
  if (!EnsureInputImage(w, h) || !EnsureStaging(frame, bytes)) return;
  memcpy(frame->staging_mapped, pixels, (size_t)bytes);

  /* mode 0 = nearest passthrough, mode 2 = sharp-bilinear/reconstruct (the
   * shader's own sharpening math needs real linear filtering underneath
   * to have anything to sharpen); mode 1 (plain Bilinear) also needs the
   * linear sampler, not the nearest one. */
  int mode = settings->upscaler == kDkc1UpscalerReconstruct ||
             settings->upscaler == kDkc1UpscalerSharpBilinear ? 2 : settings->upscaler;
  VkSampler desired_sampler = mode == 0 ? s_sampler : s_sampler_linear;
  if (desired_sampler != s_desc_sampler) {
    /* Rare (only on a settings change, not every frame): the descriptor
     * set is shared across all frames-in-flight, so rewriting it while a
     * different slot's command buffer might still be mid-flight referencing
     * the old binding needs the GPU fully idle first. A per-frame
     * descriptor set would avoid this stall entirely but isn't worth the
     * extra complexity for something that only happens when the user
     * changes the upscaler mode in the settings dialog. */
    vkDeviceWaitIdle(s_device);
    UpdateDescriptorSampler(desired_sampler);
  }

  vkResetFences(s_device, 1, &frame->in_flight);
  vkResetCommandBuffer(frame->cmd, 0);
  VkCommandBufferBeginInfo begin = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
  vkBeginCommandBuffer(frame->cmd, &begin);

  VkImageMemoryBarrier to_dst = {.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
      .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED, .newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
      .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED, .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
      .image = s_input_image, .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1},
      .dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT};
  vkCmdPipelineBarrier(frame->cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
      0, 0, NULL, 0, NULL, 1, &to_dst);
  VkBufferImageCopy copy = {.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1},
      .imageExtent = {(uint32_t)w, (uint32_t)h, 1}};
  vkCmdCopyBufferToImage(frame->cmd, frame->staging_buffer, s_input_image,
      VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
  VkImageMemoryBarrier to_read = to_dst;
  to_read.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
  to_read.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
  to_read.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
  to_read.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
  vkCmdPipelineBarrier(frame->cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
      0, 0, NULL, 0, NULL, 1, &to_read);

  VkClearValue clear = {.color = {{0, 0, 0, 1}}};
  VkRenderPassBeginInfo rp_begin = {.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
      .renderPass = s_render_pass, .framebuffer = s_swap_fbs[image_index],
      .renderArea = {{0, 0}, s_swap_extent}, .clearValueCount = 1, .pClearValues = &clear};
  vkCmdBeginRenderPass(frame->cmd, &rp_begin, VK_SUBPASS_CONTENTS_INLINE);
  vkCmdBindPipeline(frame->cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, s_pipeline);

  int ow = (int)s_swap_extent.width, oh = (int)s_swap_extent.height;
  int vw = ow, vh = ow * h / display_width;
  if (vh > oh) { vh = oh; vw = oh * display_width / h; }
  int x = (ow - vw) / 2, y = (oh - vh) / 2;
  VkViewport viewport = {(float)x, (float)y, (float)vw, (float)vh, 0.0f, 1.0f};
  VkRect2D scissor = {{0, 0}, s_swap_extent};
  vkCmdSetViewport(frame->cmd, 0, 1, &viewport);
  vkCmdSetScissor(frame->cmd, 0, 1, &scissor);

  vkCmdBindDescriptorSets(frame->cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, s_pipeline_layout,
      0, 1, &s_desc_set, 0, NULL);
  PushConsts pc = {(float)w, (float)h, (float)vw, (float)vh, mode};
  vkCmdPushConstants(frame->cmd, s_pipeline_layout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof pc, &pc);
  vkCmdDraw(frame->cmd, 4, 1, 0, 0);
  vkCmdEndRenderPass(frame->cmd);
  vkEndCommandBuffer(frame->cmd);

  VkPipelineStageFlags wait_stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
  VkSubmitInfo submit = {.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
      .waitSemaphoreCount = 1, .pWaitSemaphores = &frame->image_available, .pWaitDstStageMask = &wait_stage,
      .commandBufferCount = 1, .pCommandBuffers = &frame->cmd,
      .signalSemaphoreCount = 1, .pSignalSemaphores = &frame->render_finished};
  if (vkQueueSubmit(s_queue, 1, &submit, frame->in_flight) != VK_SUCCESS) {
    /* frame->in_flight was already reset above and a failed submit never
     * signals it -- the *next* call would hang forever in
     * vkWaitForFences(..., UINT64_MAX) on this same frame-in-flight slot.
     * Submit failures here are essentially always fatal (VK_ERROR_DEVICE_LOST
     * and friends aren't meaningfully recoverable in place), so stop using
     * this backend for good rather than risk that hang; every future Draw
     * call no-ops at the `if (!s_ready) return;` guard instead of ever
     * reaching that wait again. */
    fprintf(stderr, "[linux-vk-graphics] vkQueueSubmit failed; disabling Vulkan backend\n");
    s_ready = 0;
    return;
  }

  VkPresentInfoKHR present = {.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
      .waitSemaphoreCount = 1, .pWaitSemaphores = &frame->render_finished,
      .swapchainCount = 1, .pSwapchains = &s_swapchain, .pImageIndices = &image_index};
  VkResult present_result = vkQueuePresentKHR(s_queue, &present);
  if (present_result == VK_ERROR_OUT_OF_DATE_KHR || present_result == VK_SUBOPTIMAL_KHR)
    RecreateSwapchain();

  s_frame_index = (s_frame_index + 1) % DKC1_VK_FRAMES_IN_FLIGHT;
}

void Dkc1LinuxVkGraphicsSwap(void) {
  /* Presentation already happens inside Draw (vkQueuePresentKHR); Vulkan
   * has no separate "swap the window" step the way GL does. */
}

void Dkc1LinuxVkGraphicsWaitIdle(void) {
  if (s_device) vkDeviceWaitIdle(s_device);
}

void Dkc1LinuxVkGraphicsClose(void) {
  if (s_device) vkDeviceWaitIdle(s_device);
  for (int i = 0; i < DKC1_VK_FRAMES_IN_FLIGHT; i++) {
    if (s_frames[i].in_flight) vkDestroyFence(s_device, s_frames[i].in_flight, NULL);
    if (s_frames[i].image_available) vkDestroySemaphore(s_device, s_frames[i].image_available, NULL);
    if (s_frames[i].render_finished) vkDestroySemaphore(s_device, s_frames[i].render_finished, NULL);
    if (s_frames[i].staging_buffer) {
      vkUnmapMemory(s_device, s_frames[i].staging_memory);
      vkDestroyBuffer(s_device, s_frames[i].staging_buffer, NULL);
      vkFreeMemory(s_device, s_frames[i].staging_memory, NULL);
    }
  }
  memset(s_frames, 0, sizeof s_frames);
  if (s_sampler) { vkDestroySampler(s_device, s_sampler, NULL); s_sampler = VK_NULL_HANDLE; }
  if (s_sampler_linear) { vkDestroySampler(s_device, s_sampler_linear, NULL); s_sampler_linear = VK_NULL_HANDLE; }
  s_desc_sampler = VK_NULL_HANDLE;
  if (s_input_view) { vkDestroyImageView(s_device, s_input_view, NULL); s_input_view = VK_NULL_HANDLE; }
  if (s_input_image) { vkDestroyImage(s_device, s_input_image, NULL); s_input_image = VK_NULL_HANDLE; }
  if (s_input_memory) { vkFreeMemory(s_device, s_input_memory, NULL); s_input_memory = VK_NULL_HANDLE; }
  if (s_desc_pool) { vkDestroyDescriptorPool(s_device, s_desc_pool, NULL); s_desc_pool = VK_NULL_HANDLE; }
  if (s_desc_layout) { vkDestroyDescriptorSetLayout(s_device, s_desc_layout, NULL); s_desc_layout = VK_NULL_HANDLE; }
  if (s_cmd_pool) { vkDestroyCommandPool(s_device, s_cmd_pool, NULL); s_cmd_pool = VK_NULL_HANDLE; }
  if (s_pipeline) { vkDestroyPipeline(s_device, s_pipeline, NULL); s_pipeline = VK_NULL_HANDLE; }
  if (s_pipeline_layout) { vkDestroyPipelineLayout(s_device, s_pipeline_layout, NULL); s_pipeline_layout = VK_NULL_HANDLE; }
  DestroySwapchainObjects();
  if (s_render_pass) { vkDestroyRenderPass(s_device, s_render_pass, NULL); s_render_pass = VK_NULL_HANDLE; }
  if (s_device) { vkDestroyDevice(s_device, NULL); s_device = VK_NULL_HANDLE; }
  if (s_surface) { vkDestroySurfaceKHR(s_instance, s_surface, NULL); s_surface = VK_NULL_HANDLE; }
  if (s_instance) { vkDestroyInstance(s_instance, NULL); s_instance = VK_NULL_HANDLE; }
  s_phys = VK_NULL_HANDLE; s_queue_family = UINT32_MAX; s_ready = 0;
  s_input_w = s_input_h = 0; s_input_ready = false;
}
