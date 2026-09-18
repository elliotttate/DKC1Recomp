#import "macos_graphics.h"
#import <Foundation/Foundation.h>
#include "dkc1_hd_sprites.h"
#include "dkc1_video.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { Flat, Reconstruct, Lines, Beam, Down, Blur, Compose, Finish, PassCount };
@implementation Dkc1MetalGraphics {
  id<MTLDevice> _device;
  id<MTLRenderPipelineState> _pipelines[PassCount];
  id<MTLTexture> _inputs[3], _finished[3], _lines, _beam, _glow[2], _halo[2];
  NSLock *_inputLock;
  BOOL _busy[3];
  id<MTLTexture> _cachedOutput;
  uint32_t *_cachedPixels;
  size_t _cachedSize;
  Dkc1GraphicsSettings _cachedSettings;
  MTLViewport _cachedViewport;
  int _cachedWidth, _cachedHeight;
  BOOL _cacheValid;
}
- (instancetype)initWithDevice:(id<MTLDevice>)device
                    shaderPath:(NSString *)path error:(NSError **)error {
  if (!(self=[super init])) return nil;
  _device=[device retain]; _inputLock=[[NSLock alloc] init];
  if (!path) path=[[NSBundle mainBundle] pathForResource:@"macos_graphics" ofType:@"metal"];
  NSString *source=path ? [NSString stringWithContentsOfFile:path
      encoding:NSUTF8StringEncoding error:error] : nil;
  if (!source) { [self release]; return nil; }
  MTLCompileOptions *options=[[[MTLCompileOptions alloc] init] autorelease];
  options.fastMathEnabled=NO;
  id<MTLLibrary> library=[device newLibraryWithSource:source options:options error:error];
  if (!library) { [self release]; return nil; }
  id<MTLFunction> vertex=[library newFunctionWithName:@"dkc1_vertex"];
  NSArray *names=@[@"flat",@"reconstruct",@"lines",@"beam",@"down",@"blur",@"compose",@"finish"];
  BOOL ok=YES;
  for (int i=0;i<PassCount;i++) {
    id<MTLFunction> fragment=[library newFunctionWithName:
        [@"dkc1_" stringByAppendingString:names[i]]];
    MTLRenderPipelineDescriptor *d=[[MTLRenderPipelineDescriptor alloc] init];
    d.vertexFunction=vertex; d.fragmentFunction=fragment;
    d.colorAttachments[0].pixelFormat=(i==Flat || i==Reconstruct || i==Compose || i==Finish)
        ? MTLPixelFormatBGRA8Unorm : MTLPixelFormatRGBA16Float;
    _pipelines[i]=[device newRenderPipelineStateWithDescriptor:d error:error];
    [d release]; [fragment release];
    if (!_pipelines[i]) { ok=NO; break; }
  }
  [vertex release]; [library release];
  if (!ok) { [self release]; return nil; }
  return self;
}
- (id<MTLTexture>)texture:(int)width height:(int)height format:(MTLPixelFormat)format
                  shared:(BOOL)shared {
  MTLTextureDescriptor *d=[MTLTextureDescriptor texture2DDescriptorWithPixelFormat:format
      width:MAX(width,1) height:MAX(height,1) mipmapped:NO];
  d.storageMode=shared ? MTLStorageModeShared : MTLStorageModePrivate;
  d.usage=MTLTextureUsageShaderRead | (shared ? 0 : MTLTextureUsageRenderTarget);
  return [_device newTextureWithDescriptor:d];
}
- (BOOL)ensure:(id<MTLTexture> *)texture width:(int)w height:(int)h {
  if (*texture && (*texture).width==(NSUInteger)w && (*texture).height==(NSUInteger)h) return YES;
  [*texture release];
  *texture=[self texture:w height:h format:MTLPixelFormatRGBA16Float shared:NO];
  return *texture!=nil;
}
- (void)pass:(int)index source:(id<MTLTexture>)source target:(id<MTLTexture>)target
    viewport:(MTLViewport)viewport parameters:(const float *)params
      buffer:(id<MTLCommandBuffer>)buffer {
  MTLRenderPassDescriptor *pass=[MTLRenderPassDescriptor renderPassDescriptor];
  pass.colorAttachments[0].texture=target;
  pass.colorAttachments[0].loadAction=MTLLoadActionClear;
  pass.colorAttachments[0].storeAction=MTLStoreActionStore;
  pass.colorAttachments[0].clearColor=MTLClearColorMake(0,0,0,1);
  id<MTLRenderCommandEncoder> e=[buffer renderCommandEncoderWithDescriptor:pass];
  [e setRenderPipelineState:_pipelines[index]];
  [e setViewport:viewport]; [e setFragmentTexture:source atIndex:0];
  if (index==Compose) {
    [e setFragmentTexture:_glow[0] atIndex:1];
    [e setFragmentTexture:_halo[0] atIndex:2];
  }
  [e setFragmentBytes:params length:27*sizeof(float) atIndex:0];
  [e drawPrimitives:MTLPrimitiveTypeTriangleStrip vertexStart:0 vertexCount:4];
  [e endEncoding];
}
- (void)pass:(int)index source:(id<MTLTexture>)source target:(id<MTLTexture>)target
  parameters:(float *)p buffer:(id<MTLCommandBuffer>)buffer {
  p[0]=source.width; p[1]=source.height;
  [self pass:index source:source target:target
      viewport:(MTLViewport){0,0,target.width,target.height,0,1} parameters:p buffer:buffer];
}
- (BOOL)encodePixels:(const uint32_t *)pixels width:(int)w height:(int)h
             target:(id<MTLTexture>)target viewport:(MTLViewport)v
           settings:(Dkc1GraphicsSettings)s commandBuffer:(id<MTLCommandBuffer>)buffer {
  if (!pixels || w<1 || h<1 || !target || !buffer) return NO;
  Dkc1GraphicsClamp(&s);
  const BOOL cacheEffect=s.display==kDkc1DisplayCrt || s.upscaler==kDkc1UpscalerReconstruct;
  const size_t bytes=(size_t)w*h*4;
  const MTLViewport finalViewport=v;
  id<MTLTexture> finalTarget=target;
  if (cacheEffect) {
    BOOL hit=_cacheValid && _cachedWidth==w && _cachedHeight==h &&
        _cachedViewport.width==v.width && _cachedViewport.height==v.height &&
        _cachedViewport.originX==v.originX && _cachedViewport.originY==v.originY &&
        memcmp(&_cachedSettings,&s,sizeof s)==0 && _cachedSize==bytes &&
        memcmp(_cachedPixels,pixels,bytes)==0;
    if (hit) {
      float copy[27]={_cachedOutput.width,_cachedOutput.height,v.width,v.height,0};
      [self pass:Flat source:_cachedOutput target:target viewport:v parameters:copy buffer:buffer];
      return YES;
    }
    _cacheValid=NO;
    if (!_cachedOutput || _cachedOutput.width!=(NSUInteger)v.width || _cachedOutput.height!=(NSUInteger)v.height) {
      [_cachedOutput release];
      _cachedOutput=[self texture:v.width height:v.height format:MTLPixelFormatBGRA8Unorm shared:NO];
    }
    if (!_cachedOutput) return NO;
    if (_cachedSize!=bytes) {
      uint32_t *copy=realloc(_cachedPixels,bytes);
      if (!copy) return NO;
      _cachedPixels=copy; _cachedSize=bytes;
    }
    target=_cachedOutput;
    v=(MTLViewport){0,0,finalViewport.width,finalViewport.height,0,1};
  }
  // A texture being sampled by an earlier command buffer is never overwritten.
  int slot=-1; [_inputLock lock];
  for (int i=0;i<3;i++) if (!_busy[i]) { slot=i; _busy[i]=YES; break; }
  [_inputLock unlock];
  if (slot<0) return NO;
  if (!_inputs[slot] || _inputs[slot].width!=(NSUInteger)w || _inputs[slot].height!=(NSUInteger)h) {
    [_inputs[slot] release];
    _inputs[slot]=[self texture:w height:h format:MTLPixelFormatBGRA8Unorm shared:YES];
  }
  if (!_inputs[slot]) {
    [_inputLock lock]; _busy[slot]=NO; [_inputLock unlock]; return NO;
  }
  [_inputs[slot] replaceRegion:MTLRegionMake2D(0,0,w,h) mipmapLevel:0
      withBytes:pixels bytesPerRow:(NSUInteger)w*4];
  [buffer addCompletedHandler:^(id<MTLCommandBuffer> completed) {
    (void)completed;
    [_inputLock lock]; _busy[slot]=NO; [_inputLock unlock];
  }];
  id<MTLTexture> source=_inputs[slot];
  /* Fixed-room HD plates use the CPU scene compositor and arrive here as an
   * already composed 4x frame.  The GPU scene path applies this finish before
   * encodeTexture; apply the identical pass only to 4x CPU uploads so neither
   * native frames nor GPU-composed HD frames are processed twice. */
  if (s.hd_finish>0 && h==kDkc1VideoHeight*kDkc1HdScale) {
    if (!_finished[slot] || _finished[slot].width!=(NSUInteger)w ||
        _finished[slot].height!=(NSUInteger)h) {
      [_finished[slot] release];
      _finished[slot]=[self texture:w height:h format:MTLPixelFormatBGRA8Unorm shared:NO];
    }
    if (!_finished[slot]) return NO;
    float finish[27]={w,h,w,h,0,s.hd_finish/100.f};
    [self pass:Finish source:source target:_finished[slot]
        viewport:(MTLViewport){0,0,w,h,0,1} parameters:finish buffer:buffer];
    source=_finished[slot];
  }
  if (![self encodeSource:source target:target viewport:v settings:s
      originX:(cacheEffect?finalViewport.originX:0) originY:(cacheEffect?finalViewport.originY:0)
      commandBuffer:buffer]) return NO;
  if (cacheEffect) {
    memcpy(_cachedPixels,pixels,bytes);_cachedSettings=s;
    _cachedViewport=finalViewport;_cachedWidth=w;_cachedHeight=h;_cacheValid=YES;
    float copy[27]={target.width,target.height,finalViewport.width,finalViewport.height,0};
    [self pass:Flat source:target target:finalTarget viewport:finalViewport parameters:copy buffer:buffer];
  }
  return YES;
}
- (BOOL)encodeSource:(id<MTLTexture>)source target:(id<MTLTexture>)target
    viewport:(MTLViewport)v settings:(Dkc1GraphicsSettings)s
    originX:(float)originX originY:(float)originY commandBuffer:(id<MTLCommandBuffer>)buffer {
  int w=(int)source.width,h=(int)source.height;
  float p[27]={w,h,v.width,v.height,s.reconstruct_mode,
               s.strength/100.f,s.softness/100.f,s.shading/100.f};
  p[25]=originX;
  p[26]=originY;
  Dkc1CrtFrameParams c;
  BOOL crt=s.display==kDkc1DisplayCrt && Dkc1CrtDerive(&s.crt,v.width,v.height,w,h,&c);
  if (crt) {
    int vw=v.width,vh=v.height,gw=MAX(vw/4,1),gh=MAX(vh/4,1),hw=MAX(vw/16,1),hh=MAX(vh/16,1);
    crt=[self ensure:&_lines width:vw height:h] && [self ensure:&_beam width:vw height:vh];
    for (int i=0;i<2 && crt;i++) crt=[self ensure:&_glow[i] width:gw height:gh] && [self ensure:&_halo[i] width:hw height:hh];
    if (!crt) fprintf(stderr,"[graphics] CRT target allocation failed; using flat frame\n");
  }
  if (crt) {
    p[8]=c.sigma_h; p[9]=c.sigma_dark; p[10]=c.sigma_bright; p[11]=c.beam_fade;
    p[14]=c.glow; p[15]=c.halation; p[16]=c.curvature_x; p[17]=c.curvature_y;
    p[18]=c.corner_radius; p[19]=c.vignette;
    p[20]=c.mask==kDkc1CrtMaskNone ? 0 : c.mask==kDkc1CrtMaskSlot ? 2 : 1;
    p[21]=c.mask_pitch; p[22]=c.mask_strength; p[23]=c.mask_gain; p[24]=c.knee;
    [self pass:Lines source:source target:_lines parameters:p buffer:buffer];
    [self pass:Beam source:_lines target:_beam parameters:p buffer:buffer];
    for (int i=0;i<2;i++) {
      id<MTLTexture> source=i ? _glow[0] : _beam;
      id<MTLTexture> a=i ? _halo[0] : _glow[0], b=i ? _halo[1] : _glow[1];
      [self pass:Down source:source target:a parameters:p buffer:buffer];
      p[12]=1; p[13]=0; [self pass:Blur source:a target:b parameters:p buffer:buffer];
      p[12]=0; p[13]=1; [self pass:Blur source:b target:a parameters:p buffer:buffer];
    }
    [self pass:Compose source:_beam target:target viewport:v parameters:p buffer:buffer];
  } else {
    if (s.upscaler!=kDkc1UpscalerReconstruct) p[4]=s.upscaler;
    [self pass:s.upscaler==kDkc1UpscalerReconstruct ? Reconstruct : Flat
        source:source target:target viewport:v parameters:p buffer:buffer];
  }
  return YES;
}
- (BOOL)encodeTexture:(id<MTLTexture>)source target:(id<MTLTexture>)target
    viewport:(MTLViewport)viewport settings:(Dkc1GraphicsSettings)settings
    commandBuffer:(id<MTLCommandBuffer>)buffer {
  if(!source || !target || !buffer)return NO;
  Dkc1GraphicsClamp(&settings);
  const BOOL effect=settings.display==kDkc1DisplayCrt || settings.upscaler==kDkc1UpscalerReconstruct;
  if(effect) {
    // Match the established CPU-input path's origin-zero effect raster exactly.
    // Direct rendering at an inset viewport changes interpolation rounding.
    _cacheValid=NO;
    if(!_cachedOutput || _cachedOutput.width!=(NSUInteger)viewport.width ||
       _cachedOutput.height!=(NSUInteger)viewport.height) {
      [_cachedOutput release];
      _cachedOutput=[self texture:viewport.width height:viewport.height format:MTLPixelFormatBGRA8Unorm shared:NO];
    }
    if(!_cachedOutput)return NO;
    if(![self encodeSource:source target:_cachedOutput
        viewport:(MTLViewport){0,0,viewport.width,viewport.height,0,1} settings:settings
        originX:viewport.originX originY:viewport.originY commandBuffer:buffer])return NO;
    float copy[27]={_cachedOutput.width,_cachedOutput.height,viewport.width,viewport.height,0};
    [self pass:Flat source:_cachedOutput target:target viewport:viewport parameters:copy buffer:buffer];
    return YES;
  }
  return [self encodeSource:source target:target viewport:viewport settings:settings
      originX:0 originY:0 commandBuffer:buffer];
}
- (void)dealloc {
  for (int i=0;i<PassCount;i++) [_pipelines[i] release];
  for (int i=0;i<3;i++) { [_inputs[i] release]; [_finished[i] release]; }
  for (int i=0;i<2;i++) { [_glow[i] release]; [_halo[i] release]; }
  [_cachedOutput release]; free(_cachedPixels);
  [_lines release]; [_beam release]; [_inputLock release]; [_device release];
  [super dealloc];
}
@end
