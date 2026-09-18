#ifndef DKC1_HD_GPU_TYPES_H
#define DKC1_HD_GPU_TYPES_H
/* Shared C/Metal ABI. Scalar fields only; no platform pointers or vector padding. */
#ifdef __METAL_VERSION__
#include <metal_stdlib>
using namespace metal;
typedef uint HdUint;
typedef ushort HdUshort;
#else
#include <stdint.h>
typedef uint32_t HdUint;
typedef uint16_t HdUshort;
#endif
enum { HdWidth=342,HdHeight=224,HdScale=4,HdAtlas=512,HdObjects=128 };
typedef struct HdGpuMaterial { HdUint offset,width,height,pad; } HdGpuMaterial;
typedef struct HdGpuObject {
  int x,y,width,height,priority;
  HdUint offset,material,valid,art_offset,math_exempt;
} HdGpuObject;
typedef struct HdGpuFrame {
  HdUint width,object_count,sequence,object_pixels;
  HdUint bg_width[3],bg_height[3];
  HdUint bg_material[3][256],bg_valid[3][256];
  HdUint bg_fallback_material[3][256];
  HdUint bg_connected[3][256],world_palette[256];
  HdUint bg_pixels[3][HdAtlas*HdAtlas];
  HdUshort bg_z[3][HdAtlas*HdAtlas];
  HdUshort scroll_x[3][HdHeight],scroll_y[3][HdHeight];
  HdUint main_enable[HdHeight],math_flags[HdHeight],fixed_color[HdHeight];
  HdUint backdrop[HdHeight],high_backdrop[HdHeight*HdScale];
  HdUint palette[HdHeight][256];
  HdUint native[HdWidth*HdHeight];
  HdGpuObject objects[HdObjects];
  /* 1-15 INIDISP brightness. 0 means full (legacy zeroed packets). */
  HdUint brightness;
  /* Object original pixels follow the fixed header in the same buffer. */
} HdGpuFrame;
#endif
