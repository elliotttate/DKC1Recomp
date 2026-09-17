/* Spatial AA acceptance: silhouettes, protection, determinism and GPU cost. */
#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#include "dkc1_hd_gpu_types.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc,char **argv) {
 @autoreleasepool {
  assert(argc==3);id<MTLDevice> device=MTLCreateSystemDefaultDevice();if(!device)return 77;
  NSError *error=nil;NSString *types=[NSString stringWithContentsOfFile:@(argv[1]) encoding:NSUTF8StringEncoding error:&error];
  NSString *shader=[NSString stringWithContentsOfFile:@(argv[2]) encoding:NSUTF8StringEncoding error:&error];
  id<MTLLibrary> lib=[device newLibraryWithSource:[types stringByAppendingFormat:@"\n%@",shader] options:nil error:&error];
  if(!lib){fprintf(stderr,"%s\n",error.description.UTF8String);return 1;}
  id<MTLFunction> fn=[lib newFunctionWithName:@"hd_polish"];
  id<MTLComputePipelineState> pipeline=[device newComputePipelineStateWithFunction:fn error:&error];assert(pipeline);
  const unsigned w=1368,h=896,nw=w/4,nh=h/4;size_t bytes=w*h*4;
  uint32_t *source=malloc(bytes),*result=malloc(bytes),*repeat=malloc(bytes),*weak=malloc(bytes);
  for(unsigned y=0;y<h;y++)for(unsigned x=0;x<w;x++) {
   // Native four-pixel stairs, smooth flat regions and a protected HUD-like block.
   source[y*w+x]=x/4>y/4+50?0xffd0e0f0:0xff102030;
  }
  id<MTLBuffer> frame=[device newBufferWithLength:sizeof(HdGpuFrame) options:MTLResourceStorageModeShared];
  memset(frame.contents,0,frame.length);((HdGpuFrame *)frame.contents)->width=nw;
  id<MTLBuffer> mask=[device newBufferWithLength:nw*nh*4 options:MTLResourceStorageModeShared];
  uint32_t *m=mask.contents;for(unsigned i=0;i<nw*nh;i++)m[i]=1;
  for(unsigned y=90;y<110;y++)for(unsigned x=130;x<180;x++)m[y*nw+x]=3;
  for(unsigned y=150;y<165;y++)for(unsigned x=190;x<230;x++)m[y*nw+x]=0;
  MTLTextureDescriptor *d=[MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatBGRA8Unorm width:w height:h mipmapped:NO];
  d.storageMode=MTLStorageModeShared;d.usage=MTLTextureUsageShaderRead|MTLTextureUsageShaderWrite;
  id<MTLTexture> input=[device newTextureWithDescriptor:d],output=[device newTextureWithDescriptor:d];
  [input replaceRegion:MTLRegionMake2D(0,0,w,h) mipmapLevel:0 withBytes:source bytesPerRow:w*4];
  id<MTLCommandQueue> queue=[device newCommandQueue];double times[100];
  for(int run=0;run<104;run++) {
   float strength=run==0?0:1,finish=0;id<MTLCommandBuffer> cb=[queue commandBuffer];id<MTLComputeCommandEncoder> e=[cb computeCommandEncoder];
   [e setComputePipelineState:pipeline];[e setBuffer:frame offset:0 atIndex:0];[e setBuffer:mask offset:0 atIndex:4];[e setBytes:&strength length:sizeof strength atIndex:5];[e setBytes:&finish length:sizeof finish atIndex:6];
   [e setTexture:input atIndex:0];[e setTexture:output atIndex:1];
   [e dispatchThreadgroups:MTLSizeMake((w+7)/8,(h+7)/8,1) threadsPerThreadgroup:MTLSizeMake(8,8,1)];[e endEncoding];[cb commit];[cb waitUntilCompleted];assert(cb.status==MTLCommandBufferStatusCompleted);
   if(run>=4)times[run-4]=(cb.GPUEndTime-cb.GPUStartTime)*1000;
   if(run<3) {
    [output getBytes:result bytesPerRow:w*4 fromRegion:MTLRegionMake2D(0,0,w,h) mipmapLevel:0];
    if(!run)assert(!memcmp(result,source,bytes));
    if(run==1)memcpy(repeat,result,bytes);
    if(run==2)assert(!memcmp(repeat,result,bytes));
   }
  }
  unsigned changed=0,flat_changed=0,protected_changed=0;
  for(unsigned y=0;y<h;y++)for(unsigned x=0;x<w;x++) {
   uint32_t a=source[y*w+x],b=result[y*w+x];assert((a>>24)==(b>>24));
   if(a!=b){changed++;if(m[(y/4)*nw+x/4]!=1)protected_changed++;if(abs((int)x-(int)y-200)>12)flat_changed++;}
   for(int c=0;c<3;c++){unsigned v=(b>>(c*8))&255;assert(v>=((0xff102030>>(c*8))&255) && v<=((0xffd0e0f0>>(c*8))&255));}
  }
  assert(changed>1000 && !flat_changed && !protected_changed);
  unsigned finish_changed=0;
  for(int run=0;run<2;run++) {
   float strength=0,finish=1;id<MTLCommandBuffer> cb=[queue commandBuffer];id<MTLComputeCommandEncoder> e=[cb computeCommandEncoder];
   [e setComputePipelineState:pipeline];[e setBuffer:frame offset:0 atIndex:0];[e setBuffer:mask offset:0 atIndex:4];[e setBytes:&strength length:sizeof strength atIndex:5];[e setBytes:&finish length:sizeof finish atIndex:6];
   [e setTexture:input atIndex:0];[e setTexture:output atIndex:1];
   [e dispatchThreadgroups:MTLSizeMake((w+7)/8,(h+7)/8,1) threadsPerThreadgroup:MTLSizeMake(8,8,1)];[e endEncoding];[cb commit];[cb waitUntilCompleted];assert(cb.status==MTLCommandBufferStatusCompleted);
   [output getBytes:result bytesPerRow:w*4 fromRegion:MTLRegionMake2D(0,0,w,h) mipmapLevel:0];
   if(!run)memcpy(repeat,result,bytes);else assert(!memcmp(repeat,result,bytes));
  }
  for(unsigned i=0;i<w*h;i++){assert((source[i]>>24)==(result[i]>>24));finish_changed+=source[i]!=result[i];}
  assert(finish_changed>w*h*9/10);
  uint64_t strong_delta=0,weak_delta=0;
  for(unsigned i=0;i<w*h;i++)for(int c=0;c<3;c++)
    strong_delta+=abs((int)((source[i]>>(c*8))&255)-(int)((result[i]>>(c*8))&255));
  {
   float strength=0,finish=1.0f/3.0f;id<MTLCommandBuffer> cb=[queue commandBuffer];id<MTLComputeCommandEncoder> e=[cb computeCommandEncoder];
   [e setComputePipelineState:pipeline];[e setBuffer:frame offset:0 atIndex:0];[e setBuffer:mask offset:0 atIndex:4];[e setBytes:&strength length:sizeof strength atIndex:5];[e setBytes:&finish length:sizeof finish atIndex:6];
   [e setTexture:input atIndex:0];[e setTexture:output atIndex:1];
   [e dispatchThreadgroups:MTLSizeMake((w+7)/8,(h+7)/8,1) threadsPerThreadgroup:MTLSizeMake(8,8,1)];[e endEncoding];[cb commit];[cb waitUntilCompleted];assert(cb.status==MTLCommandBufferStatusCompleted);
   [output getBytes:weak bytesPerRow:w*4 fromRegion:MTLRegionMake2D(0,0,w,h) mipmapLevel:0];
  }
  for(unsigned i=0;i<w*h;i++)for(int c=0;c<3;c++)
    weak_delta+=abs((int)((source[i]>>(c*8))&255)-(int)((weak[i]>>(c*8))&255));
  assert(strong_delta>weak_delta*2);
  for(int i=0;i<100;i++)for(int j=i+1;j<100;j++)if(times[j]<times[i]){double t=times[i];times[i]=times[j];times[j]=t;}
  printf("HD polish: changed=%u flat_changed=%u protected_changed=%u finish_changed=%u finish_delta_33=%llu finish_delta_100=%llu deterministic=1 gpu_median_ms=%.4f gpu_p99_ms=%.4f\n",changed,flat_changed,protected_changed,finish_changed,(unsigned long long)weak_delta,(unsigned long long)strong_delta,times[50],times[98]);
  free(source);free(result);free(repeat);free(weak);[queue release];[input release];[output release];[mask release];[frame release];[pipeline release];[fn release];[lib release];[device release];
 }
 return 0;
}
