#ifndef DKC1_MACOS_HD_SCENE_H
#define DKC1_MACOS_HD_SCENE_H
#include <stdbool.h>
#include <stdint.h>
/* Default-off, synchronous CPU/GPU oracle for deterministic headless tools. */
bool Dkc1HdMetalValidateFrame(const uint32_t *native,int width,int height);
/* Spatial background edge cleanup, 0 disables it. Captured per immutable frame. */
void Dkc1HdMetalSetPolish(int strength);
/* Global filmic finish and stable luminance grain, 0 disables it. */
void Dkc1HdMetalSetFinish(int strength);
#ifdef __OBJC__
#import <Metal/Metal.h>
@class Dkc1HdMetalRenderer;
@interface Dkc1HdMetalFrame : NSObject {
@public
  Dkc1HdMetalRenderer *owner;
  unsigned slot,width,height,sequence;
  id<MTLBuffer> snapshot;
  id<MTLTexture> texture,rawTexture;
  float polish,finish;
  BOOL encoded;
}
@end
@interface Dkc1HdMetalRenderer : NSObject
- (instancetype)initWithDevice:(id<MTLDevice>)device shaderPath:(NSString *)path error:(NSError **)error;
- (Dkc1HdMetalFrame *)captureNative:(const uint32_t *)native width:(int)width height:(int)height;
- (BOOL)encodeFrame:(Dkc1HdMetalFrame *)frame commandBuffer:(id<MTLCommandBuffer>)buffer;
- (BOOL)validateFrame:(Dkc1HdMetalFrame *)frame reference:(const uint32_t *)reference
               queue:(id<MTLCommandQueue>)queue;
- (void)releaseSlot:(unsigned)slot;
@end
#endif
#endif
