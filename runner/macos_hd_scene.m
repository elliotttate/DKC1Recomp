#import "macos_hd_scene.h"
#include "dkc1_hd_scene.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdatomic.h>

static atomic_int s_polish=-1,s_finish=-1;
void Dkc1HdMetalSetPolish(int strength) {
  atomic_store(&s_polish,strength<0?0:strength>100?100:strength);
}
void Dkc1HdMetalSetFinish(int strength) {
  atomic_store(&s_finish,strength<0?0:strength>100?100:strength);
}
static float Polish(void) {
  int v=atomic_load(&s_polish);
  if(v<0){const char *e=getenv("DKC1_HD_POLISH");Dkc1HdMetalSetPolish(e?atoi(e):0);v=atomic_load(&s_polish);}
  return v/100.0f;
}
static float Finish(void) {
  int v=atomic_load(&s_finish);
  if(v<0){const char *e=getenv("DKC1_HD_FINISH");Dkc1HdMetalSetFinish(e?atoi(e):0);v=atomic_load(&s_finish);}
  return v/100.0f;
}

enum { HdGpuSlots=8 };
@implementation Dkc1HdMetalFrame
- (void)dealloc {
  [owner releaseSlot:slot];[owner release];[super dealloc];
}
@end

@implementation Dkc1HdMetalRenderer {
  id<MTLDevice> _device;
  id<MTLComputePipelineState> _oracle,_compose,_polish;
  id<MTLBuffer> _art,_materials,_snapshots[HdGpuSlots],_masks[HdGpuSlots];
  id<MTLTexture> _textures[HdGpuSlots],_polished[HdGpuSlots];
  BOOL _busy[HdGpuSlots],_failed;
  NSLock *_lock,*_logLock;
  FILE *_log;
}
- (instancetype)initWithDevice:(id<MTLDevice>)device shaderPath:(NSString *)path error:(NSError **)error {
  if(!(self=[super init]))return nil;
  _device=[device retain];_lock=[[NSLock alloc] init];_logLock=[[NSLock alloc] init];
  unsigned count=Dkc1HdSceneResidentCount();uint64_t bytes=Dkc1HdSceneResidentBytes();
  if(!count || !bytes || bytes>device.maxBufferLength || bytes/4>UINT32_MAX) {
    fprintf(stderr,"[hd-metal] resident pack unavailable or exceeds device buffer capacity\n");
    [self release];return nil;
  }
  if(!path) {
    const char *override=getenv("DKC1_HD_METAL_SHADER");
    path=override?[NSString stringWithUTF8String:override]:[[NSBundle mainBundle] pathForResource:@"macos_hd_scene" ofType:@"metal"];
  }
  NSString *header=path?[[path stringByDeletingLastPathComponent] stringByAppendingPathComponent:@"dkc1_hd_gpu_types.h"]:nil;
  NSString *types=header?[NSString stringWithContentsOfFile:header encoding:NSUTF8StringEncoding error:error]:nil;
  NSString *source=path?[NSString stringWithContentsOfFile:path encoding:NSUTF8StringEncoding error:error]:nil;
  if(!source || !types){[self release];return nil;}
  MTLCompileOptions *options=[[[MTLCompileOptions alloc] init] autorelease];
  options.fastMathEnabled=NO;
  id<MTLLibrary> library=[device newLibraryWithSource:[types stringByAppendingFormat:@"\n%@",source] options:options error:error];
  if(!library){[self release];return nil;}
  id<MTLFunction> oracle=[library newFunctionWithName:@"hd_oracle"],compose=[library newFunctionWithName:@"hd_compose"];
  _oracle=[device newComputePipelineStateWithFunction:oracle error:error];
  _compose=[device newComputePipelineStateWithFunction:compose error:error];
  id<MTLFunction> polish=[library newFunctionWithName:@"hd_polish"];
  _polish=[device newComputePipelineStateWithFunction:polish error:NULL];[polish release];
  [oracle release];[compose release];[library release];
  if(!_oracle || !_compose){[self release];return nil;}
  _art=[device newBufferWithLength:(NSUInteger)bytes options:MTLResourceStorageModeShared];
  _materials=[device newBufferWithLength:count*sizeof(HdGpuMaterial) options:MTLResourceStorageModeShared];
  if(!_art || !_materials){[self release];return nil;}
  uint32_t *pixels=_art.contents;HdGpuMaterial *materials=_materials.contents;uint32_t offset=0;
  for(unsigned i=0;i<count;i++) {
    int w,h;const uint32_t *data=Dkc1HdSceneResidentPixels(i,&w,&h);
    materials[i]=(HdGpuMaterial){offset,(uint32_t)w,(uint32_t)h,0};
    memcpy(pixels+offset,data,(size_t)w*h*4);offset+=(uint32_t)(w*h);
  }
  const char *log=getenv("DKC1_HD_METAL_TRACE");if(log)_log=fopen(log,"w");
  fprintf(stderr,"[hd-metal] uploaded %u resident materials (%llu bytes); device=%s; frame_header=%zu\n",count,(unsigned long long)bytes,device.name.UTF8String,sizeof(HdGpuFrame));
  return self;
}
- (void)releaseSlot:(unsigned)slot { [_lock lock];_busy[slot]=NO;[_lock unlock]; }
- (Dkc1HdMetalFrame *)captureNative:(const uint32_t *)native width:(int)width height:(int)height {
  [_lock lock];BOOL failed=_failed;[_lock unlock];if(failed)return nil;
  size_t bytes=Dkc1HdSceneGpuFrameSize(native,width,height);if(!bytes)return nil;
  int slot=-1;[_lock lock];
  for(int i=0;i<HdGpuSlots;i++)if(!_busy[i]){slot=i;_busy[i]=YES;break;}
  [_lock unlock];if(slot<0)return nil;
  if(!_snapshots[slot] || _snapshots[slot].length<bytes) {
    [_snapshots[slot] release];_snapshots[slot]=[_device newBufferWithLength:bytes options:MTLResourceStorageModeShared];
  }
  if(!_masks[slot])_masks[slot]=[_device newBufferWithLength:HdWidth*HdHeight*4 options:MTLResourceStorageModePrivate];
  if(!_textures[slot] || _textures[slot].width!=(NSUInteger)width*4) {
    [_textures[slot] release];
    MTLTextureDescriptor *d=[MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatBGRA8Unorm width:width*4 height:height*4 mipmapped:NO];
    d.storageMode=MTLStorageModePrivate;d.usage=MTLTextureUsageShaderWrite|MTLTextureUsageShaderRead;
    _textures[slot]=[_device newTextureWithDescriptor:d];
  }
  if(!_snapshots[slot] || !_masks[slot] || !_textures[slot] ||
     !Dkc1HdSceneCopyGpuFrame(_snapshots[slot].contents,bytes)) {
    [self releaseSlot:slot];return nil;
  }
  Dkc1HdMetalFrame *frame=[[Dkc1HdMetalFrame alloc] init];
  frame->owner=[self retain];frame->slot=slot;frame->snapshot=_snapshots[slot];frame->texture=_textures[slot];
  frame->rawTexture=frame->texture;frame->polish=_polish?Polish():0;frame->finish=_polish?Finish():0;
  if(frame->polish>0 || frame->finish>0) {
    if(!_polished[slot] || _polished[slot].width!=frame->texture.width) {
      [_polished[slot] release];
      MTLTextureDescriptor *d=[MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatBGRA8Unorm width:width*4 height:height*4 mipmapped:NO];
      d.storageMode=MTLStorageModePrivate;d.usage=MTLTextureUsageShaderWrite|MTLTextureUsageShaderRead;
      _polished[slot]=[_device newTextureWithDescriptor:d];
    }
    if(_polished[slot])frame->texture=_polished[slot];else frame->polish=0;
  }
  frame->width=width*4;frame->height=height*4;frame->sequence=((HdGpuFrame *)frame->snapshot.contents)->sequence;
  return frame;
}
- (BOOL)encodeFrame:(Dkc1HdMetalFrame *)frame commandBuffer:(id<MTLCommandBuffer>)buffer {
  if(!frame || !buffer)return NO;
  [_lock lock];BOOL failed=_failed;[_lock unlock];if(failed)return NO;
  if(frame->encoded)return YES;
  for(int pass=0;pass<2;pass++) {
    id<MTLComputeCommandEncoder> e=[buffer computeCommandEncoder];if(!e)return NO;
    [e setComputePipelineState:pass?_compose:_oracle];
    [e setBuffer:frame->snapshot offset:0 atIndex:0];[e setBuffer:_art offset:0 atIndex:1];
    [e setBuffer:_materials offset:0 atIndex:2];
    [e setBuffer:frame->snapshot offset:sizeof(HdGpuFrame) atIndex:3];
    [e setBuffer:_masks[frame->slot] offset:0 atIndex:4];
    [e setBytes:&frame->polish length:sizeof(float) atIndex:5];
    [e setBytes:&frame->finish length:sizeof(float) atIndex:6];
    if(pass)[e setTexture:frame->rawTexture atIndex:0];
    NSUInteger w=pass?frame->width:frame->width/4,h=pass?frame->height:frame->height/4;
    [e dispatchThreadgroups:MTLSizeMake((w+7)/8,(h+7)/8,1) threadsPerThreadgroup:MTLSizeMake(8,8,1)];
    [e endEncoding];
  }
  if(frame->polish>0 || frame->finish>0) {
    id<MTLComputeCommandEncoder> e=[buffer computeCommandEncoder];if(!e)return NO;
    [e setComputePipelineState:_polish];
    [e setBuffer:frame->snapshot offset:0 atIndex:0];
    [e setBuffer:_masks[frame->slot] offset:0 atIndex:4];
    [e setBytes:&frame->polish length:sizeof(float) atIndex:5];
    [e setBytes:&frame->finish length:sizeof(float) atIndex:6];
    [e setTexture:frame->rawTexture atIndex:0];[e setTexture:frame->texture atIndex:1];
    [e dispatchThreadgroups:MTLSizeMake((frame->width+7)/8,(frame->height+7)/8,1) threadsPerThreadgroup:MTLSizeMake(8,8,1)];
    [e endEncoding];
  }
  frame->encoded=YES;
  /* Keep the pooled snapshot/output immutable until this GPU submission ends. */
  [frame retain];
  [buffer addCompletedHandler:^(id<MTLCommandBuffer> completed) {
    if(completed.status==MTLCommandBufferStatusError) {
      [_lock lock];_failed=YES;[_lock unlock];
      fprintf(stderr,"[hd-metal] GPU command failed: %s\n",completed.error.localizedDescription.UTF8String);
    }
    if(_log) {
      [_logLock lock];fprintf(_log,"{\"frame\":%u,\"gpu_ms\":%.6f,\"gpu_error\":%d}\n",frame->sequence,(completed.GPUEndTime-completed.GPUStartTime)*1000.0,completed.status==MTLCommandBufferStatusError);fflush(_log);[_logLock unlock];
    }
    [frame release];
  }];
  return YES;
}
- (BOOL)validateFrame:(Dkc1HdMetalFrame *)frame reference:(const uint32_t *)reference queue:(id<MTLCommandQueue>)queue {
  if(!frame || !reference)return NO;
  id<MTLCommandBuffer> buffer=[queue commandBuffer];
  size_t pitch=((size_t)frame->width*4+255)&~(size_t)255;
  id<MTLBuffer> readback=[_device newBufferWithLength:pitch*frame->height options:MTLResourceStorageModeShared];
  if(!readback)return NO;
  if(![self encodeFrame:frame commandBuffer:buffer]){[readback release];[buffer commit];[buffer waitUntilCompleted];return NO;}
  id<MTLBlitCommandEncoder> blit=[buffer blitCommandEncoder];
  /* Intentional postprocessing differences must never weaken the raw oracle. */
  [blit copyFromTexture:frame->rawTexture sourceSlice:0 sourceLevel:0 sourceOrigin:MTLOriginMake(0,0,0)
      sourceSize:MTLSizeMake(frame->width,frame->height,1) toBuffer:readback destinationOffset:0
      destinationBytesPerRow:pitch destinationBytesPerImage:pitch*frame->height];
  [blit endEncoding];[buffer commit];[buffer waitUntilCompleted];
  unsigned mismatches=0,first=0;
  for(unsigned y=0;y<frame->height;y++)for(unsigned x=0;x<frame->width;x++) {
    uint32_t actual=((uint32_t *)((uint8_t *)readback.contents+y*pitch))[x];
    if(actual!=reference[y*frame->width+x]){if(!mismatches)first=y*frame->width+x;mismatches++;}
  }
  BOOL ok=!mismatches && buffer.status==MTLCommandBufferStatusCompleted;
  if(_log){[_logLock lock];fprintf(_log,"{\"frame\":%u,\"validated_pixels\":%u,\"mismatch_pixels\":%u,\"first_mismatch\":%u}\n",frame->sequence,frame->width*frame->height,mismatches,first);fflush(_log);[_logLock unlock];}
  if(!ok)fprintf(stderr,"[hd-metal] oracle failed frame=%u mismatches=%u first=%u\n",frame->sequence,mismatches,first);
  [readback release];return ok;
}
- (void)dealloc {
  if(_log)fclose(_log);
  for(int i=0;i<HdGpuSlots;i++){[_snapshots[i] release];[_masks[i] release];[_textures[i] release];[_polished[i] release];}
  [_art release];[_materials release];[_oracle release];[_compose release];[_polish release];[_device release];
  [_lock release];[_logLock release];[super dealloc];
}
@end

bool Dkc1HdMetalValidateFrame(const uint32_t *native,int width,int height) {
  if(!Dkc1HdSceneGpuFrameSize(native,width,height))return true;
  @autoreleasepool {
    static Dkc1HdMetalRenderer *renderer;static id<MTLCommandQueue> queue;
    if(!renderer) {
      id<MTLDevice> device=MTLCreateSystemDefaultDevice();NSError *error=nil;
      renderer=[[Dkc1HdMetalRenderer alloc] initWithDevice:device shaderPath:nil error:&error];
      queue=[device newCommandQueue];[device release];
      if(!renderer){fprintf(stderr,"[hd-metal] validation initialization failed: %s\n",error.localizedDescription.UTF8String ?: "unknown");return false;}
    }
    Dkc1HdMetalFrame *frame=[renderer captureNative:native width:width height:height];
    const uint32_t *reference=Dkc1HdScenePresent(native,width,height);
    BOOL ok=[renderer validateFrame:frame reference:reference queue:queue];[frame release];return ok;
  }
}
