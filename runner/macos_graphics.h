#import <Metal/Metal.h>
#include "desktop_graphics.h"

/* Used only by the display thread. Inputs remain immutable until the GPU
 * completes; intermediate textures are ordered on its single command queue. */
@interface Dkc1MetalGraphics : NSObject
- (instancetype)initWithDevice:(id<MTLDevice>)device
                    shaderPath:(NSString *)path error:(NSError **)error;
- (BOOL)encodePixels:(const uint32_t *)pixels width:(int)width height:(int)height
             target:(id<MTLTexture>)target viewport:(MTLViewport)viewport
           settings:(Dkc1GraphicsSettings)settings
      commandBuffer:(id<MTLCommandBuffer>)commandBuffer;
- (BOOL)encodeTexture:(id<MTLTexture>)source target:(id<MTLTexture>)target
            viewport:(MTLViewport)viewport settings:(Dkc1GraphicsSettings)settings
       commandBuffer:(id<MTLCommandBuffer>)commandBuffer;
@end
