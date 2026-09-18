#import "macos_graphics.h"
#import <Foundation/Foundation.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void WritePPM(const char *path,const uint32_t *pixels,int w,int h) {
  FILE *f=fopen(path,"wb"); if (!f) abort(); fprintf(f,"P6\n%d %d\n255\n",w,h);
  for (int i=0;i<w*h;i++) { unsigned char rgb[]={pixels[i]>>16,pixels[i]>>8,pixels[i]};fwrite(rgb,1,3,f); } fclose(f);
}
int main(int argc,char **argv) {
  @autoreleasepool {
    if (argc<2) return 2;
    id<MTLDevice> device=MTLCreateSystemDefaultDevice();
    if (!device) { fprintf(stderr,"Metal device unavailable\n");return 77; }
    NSError *error=nil;
    Dkc1MetalGraphics *g=[[Dkc1MetalGraphics alloc] initWithDevice:device shaderPath:@(argv[1]) error:&error];
    if (!g) { fprintf(stderr,"%s\n",error.description.UTF8String);return 1; }
    int w=64,h=64;
    uint32_t *pixels=malloc((size_t)w*h*4);
    for (int y=0;y<h;y++) for (int x=0;x<w;x++) {
      int r=(x*4)&255,green=(y*4)&255,b=((x/4+y/4)&1)*224;
      if (x<24 && y<24) r=green=b=((x+y)&1)*255;
      if (x>28 && x<y*2) r=green=b=232;
      pixels[y*w+x]=0xff000000u|(r<<16)|(green<<8)|b;
    }
    if (argc>3) {
      FILE *f=fopen(argv[3],"rb");int max;char magic[3];
      if (!f || fscanf(f,"%2s%d%d%d",magic,&w,&h,&max)!=4 || strcmp(magic,"P6") || max!=255) return 2;
      fgetc(f);free(pixels);pixels=malloc((size_t)w*h*4);
      for (int i=0;i<w*h;i++) { unsigned char rgb[3];if (fread(rgb,1,3,f)!=3)return 2;pixels[i]=0xff000000u|(rgb[0]<<16)|(rgb[1]<<8)|rgb[2]; }fclose(f);
    }
    id<MTLCommandQueue> queue=[device newCommandQueue];
    int failures=0;uint64_t hashes[12]={0};
    // Native transfer, nearest, bilinear, five Reconstruct modes, three tubes,
    // and Sharp Bilinear. All use the same immutable input pixels.
    for (int test=0;test<12;test++) {
      @autoreleasepool {
        int scale=getenv("DKC1_TEST_SCALE") ? atoi(getenv("DKC1_TEST_SCALE")) : 4;
        if (scale<1 || scale>16) return 2;
        int ow=test ? w*scale : w,oh=test ? h*scale : h;
        if (test && getenv("DKC1_TEST_PIXEL_ASPECT")) ow=w*scale*7/6;
        MTLTextureDescriptor *d=[MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatBGRA8Unorm width:ow height:oh mipmapped:NO];
        d.storageMode=MTLStorageModeShared;d.usage=MTLTextureUsageRenderTarget;
        id<MTLTexture> out=[device newTextureWithDescriptor:d];
        Dkc1GraphicsSettings s;Dkc1GraphicsDefault(&s);
        if (test==2) s.upscaler=kDkc1UpscalerBilinear;
        if (test>=3 && test<=7) {s.upscaler=kDkc1UpscalerReconstruct;s.reconstruct_mode=test-3;}
        if (test>=8 && test<=10) {s.display=kDkc1DisplayCrt;Dkc1CrtSettingsApplyPreset(&s.crt,test-8);}
        if (test==11) s.upscaler=kDkc1UpscalerSharpBilinear;
        id<MTLCommandBuffer> command=[queue commandBuffer];
        if (![g encodePixels:pixels width:w height:h target:out viewport:(MTLViewport){0,0,ow,oh,0,1} settings:s commandBuffer:command]) return 1;
        [command commit];[command waitUntilCompleted];
        if (command.status==MTLCommandBufferStatusError) {fprintf(stderr,"GPU error: %s\n",command.error.description.UTF8String);return 1;}
        uint32_t *data=malloc((size_t)ow*oh*4);[out getBytes:data bytesPerRow:ow*4 fromRegion:MTLRegionMake2D(0,0,ow,oh) mipmapLevel:0];
        // GPU-resident input must preserve every display/filter mode, including
        // a letterboxed viewport's CRT mask and dither phase.
        MTLTextureDescriptor *inputDescription=[MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatBGRA8Unorm width:w height:h mipmapped:NO];
        inputDescription.storageMode=MTLStorageModeShared;inputDescription.usage=MTLTextureUsageShaderRead;
        id<MTLTexture> input=[device newTextureWithDescriptor:inputDescription];
        [input replaceRegion:MTLRegionMake2D(0,0,w,h) mipmapLevel:0 withBytes:pixels bytesPerRow:w*4];
        id<MTLTexture> direct=[device newTextureWithDescriptor:d],reference=[device newTextureWithDescriptor:d];
        uint32_t *gpu=malloc((size_t)ow*oh*4),*cpu=malloc((size_t)ow*oh*4);
        for(int inset=0;inset<2;inset++) {
          MTLViewport v=inset?(MTLViewport){3,5,ow-8,oh-12,0,1}:(MTLViewport){0,0,ow,oh,0,1};
          id<MTLCommandBuffer> compare=[queue commandBuffer];
          if(![g encodePixels:pixels width:w height:h target:reference viewport:v settings:s commandBuffer:compare] ||
             ![g encodeTexture:input target:direct viewport:v settings:s commandBuffer:compare])return 1;
          [compare commit];[compare waitUntilCompleted];
          if(compare.status==MTLCommandBufferStatusError)return 1;
          [reference getBytes:cpu bytesPerRow:ow*4 fromRegion:MTLRegionMake2D(0,0,ow,oh) mipmapLevel:0];
          [direct getBytes:gpu bytesPerRow:ow*4 fromRegion:MTLRegionMake2D(0,0,ow,oh) mipmapLevel:0];
          if(memcmp(cpu,gpu,(size_t)ow*oh*4)){fprintf(stderr,"GPU texture input differs case=%d inset=%d\n",test,inset);failures++;}
        }
        free(gpu);free(cpu);[input release];[direct release];[reference release];
        uint64_t hash=1469598103934665603ull;
        for (int i=0;i<ow*oh;i++) {
          hash=(hash^(data[i]&0xffffffu))*1099511628211ull;
          if (test==0 && ((data[i]^pixels[i])&0xffffffu)) failures++;
        }
        hashes[test]=hash;printf("case=%d hash=%016llx gpu_ms=%.4f\n",test,(unsigned long long)hash,(command.GPUEndTime-command.GPUStartTime)*1000.0);
        if (argc>2 && argv[2][0]) {char path[4096];snprintf(path,sizeof path,"%s/case-%02d.ppm",argv[2],test);WritePPM(path,data,ow,oh);}
        if (test>=3 && test<=10) {
          // Repeated scanout must be identical, including CRT mask/dither phase.
          id<MTLCommandBuffer> repeat=[queue commandBuffer];
          if (![g encodePixels:pixels width:w height:h target:out viewport:(MTLViewport){0,0,ow,oh,0,1} settings:s commandBuffer:repeat]) return 1;
          [repeat commit];[repeat waitUntilCompleted];
          uint32_t *again=malloc((size_t)ow*oh*4);
          [out getBytes:again bytesPerRow:ow*4 fromRegion:MTLRegionMake2D(0,0,ow,oh) mipmapLevel:0];
          if (memcmp(data,again,(size_t)ow*oh*4)) failures++;
          printf("  repeat_gpu_ms=%.4f identical=%d\n",(repeat.GPUEndTime-repeat.GPUStartTime)*1000.0,!memcmp(data,again,(size_t)ow*oh*4));
          // Change pixels, settings, then viewport without discarding history;
          // compare each with a brand-new renderer (no reusable prior frame).
          uint32_t original=pixels[w*h/2];
          for (int change=0;change<3;change++) {
            if (change==0) pixels[w*h/2]^=0x00ffffff;
            if (change==1) { s.softness=17; s.crt.glow=23; }
            MTLViewport viewport=change==2 ? (MTLViewport){3,5,ow-8,oh-12,0,1} : (MTLViewport){0,0,ow,oh,0,1};
            Dkc1MetalGraphics *fresh=[[Dkc1MetalGraphics alloc] initWithDevice:device shaderPath:@(argv[1]) error:&error];
            if (!fresh) return 1;
            id<MTLTexture> reference=[device newTextureWithDescriptor:d];
            id<MTLCommandBuffer> changed=[queue commandBuffer];
            if (![g encodePixels:pixels width:w height:h target:out viewport:viewport settings:s commandBuffer:changed] ||
                ![fresh encodePixels:pixels width:w height:h target:reference viewport:viewport settings:s commandBuffer:changed]) return 1;
            [changed commit];[changed waitUntilCompleted];
            if (changed.status==MTLCommandBufferStatusError) return 1;
            [out getBytes:data bytesPerRow:ow*4 fromRegion:MTLRegionMake2D(0,0,ow,oh) mipmapLevel:0];
            [reference getBytes:again bytesPerRow:ow*4 fromRegion:MTLRegionMake2D(0,0,ow,oh) mipmapLevel:0];
            if (memcmp(data,again,(size_t)ow*oh*4)) {fprintf(stderr,"cache invalidation failed case=%d change=%d\n",test,change);failures++;}
            [reference release];[fresh release];
          }
          pixels[w*h/2]=original;free(again);
        }
        free(data);[out release];
      }
    }
    if (hashes[1]==hashes[2] || hashes[3]==hashes[4] || hashes[1]==hashes[8] || hashes[8]==hashes[9] || hashes[9]==hashes[10]) failures++;
    // Fixed-room HD plates are CPU-composited 4x frames. Grounded finish must
    // affect that upload path, retain the documented 33% < 100% relationship,
    // and stay out of encodeTexture because the GPU HD compositor has already
    // applied it there.
    {
      const int fw=64,fh=224*4;
      const size_t count=(size_t)fw*fh,bytes=count*4;
      uint32_t *source=malloc(bytes),*raw=malloc(bytes),*weak=malloc(bytes),
          *strong=malloc(bytes),*again=malloc(bytes),*direct=malloc(bytes);
      for(int y=0;y<fh;y++)for(int x=0;x<fw;x++) {
        int r=(x*255)/(fw-1),green=(y*255)/(fh-1),b=((x*11+y*7)&255);
        source[y*fw+x]=0xff000000u|(r<<16)|(green<<8)|b;
      }
      MTLTextureDescriptor *d=[MTLTextureDescriptor texture2DDescriptorWithPixelFormat:
          MTLPixelFormatBGRA8Unorm width:fw height:fh mipmapped:NO];
      d.storageMode=MTLStorageModeShared;d.usage=MTLTextureUsageRenderTarget;
      id<MTLTexture> outputs[4];for(int i=0;i<4;i++)outputs[i]=[device newTextureWithDescriptor:d];
      Dkc1GraphicsSettings settings;Dkc1GraphicsDefault(&settings);
      for(int i=0;i<4;i++) {
        settings.hd_finish=i==0?0:i==1?33:100;
        id<MTLCommandBuffer> command=[queue commandBuffer];
        if(![g encodePixels:source width:fw height:fh target:outputs[i]
            viewport:(MTLViewport){0,0,fw,fh,0,1} settings:settings commandBuffer:command])return 1;
        [command commit];[command waitUntilCompleted];if(command.status==MTLCommandBufferStatusError)return 1;
      }
      [outputs[0] getBytes:raw bytesPerRow:fw*4 fromRegion:MTLRegionMake2D(0,0,fw,fh) mipmapLevel:0];
      [outputs[1] getBytes:weak bytesPerRow:fw*4 fromRegion:MTLRegionMake2D(0,0,fw,fh) mipmapLevel:0];
      [outputs[2] getBytes:strong bytesPerRow:fw*4 fromRegion:MTLRegionMake2D(0,0,fw,fh) mipmapLevel:0];
      [outputs[3] getBytes:again bytesPerRow:fw*4 fromRegion:MTLRegionMake2D(0,0,fw,fh) mipmapLevel:0];
      unsigned changed=0;uint64_t weakDelta=0,strongDelta=0;
      for(size_t i=0;i<count;i++) {
        if((raw[i]^source[i])&0xffffffu)failures++;
        if((raw[i]^strong[i])&0xffffffu)changed++;
        for(int shift=0;shift<24;shift+=8) {
          int base=(raw[i]>>shift)&255;
          weakDelta+=llabs((int)((weak[i]>>shift)&255)-base);
          strongDelta+=llabs((int)((strong[i]>>shift)&255)-base);
        }
      }
      if(changed<=count*9/10 || weakDelta>=strongDelta || memcmp(strong,again,bytes)) {
        fprintf(stderr,"CPU HD finish failed changed=%u weak=%llu strong=%llu deterministic=%d\n",
            changed,(unsigned long long)weakDelta,(unsigned long long)strongDelta,!memcmp(strong,again,bytes));
        failures++;
      }
      MTLTextureDescriptor *inputDescription=[MTLTextureDescriptor texture2DDescriptorWithPixelFormat:
          MTLPixelFormatBGRA8Unorm width:fw height:fh mipmapped:NO];
      inputDescription.storageMode=MTLStorageModeShared;inputDescription.usage=MTLTextureUsageShaderRead;
      id<MTLTexture> input=[device newTextureWithDescriptor:inputDescription];
      [input replaceRegion:MTLRegionMake2D(0,0,fw,fh) mipmapLevel:0 withBytes:source bytesPerRow:fw*4];
      settings.hd_finish=100;id<MTLCommandBuffer> command=[queue commandBuffer];
      if(![g encodeTexture:input target:outputs[3] viewport:(MTLViewport){0,0,fw,fh,0,1}
          settings:settings commandBuffer:command])return 1;
      [command commit];[command waitUntilCompleted];if(command.status==MTLCommandBufferStatusError)return 1;
      [outputs[3] getBytes:direct bytesPerRow:fw*4 fromRegion:MTLRegionMake2D(0,0,fw,fh) mipmapLevel:0];
      if(memcmp(raw,direct,bytes)){fprintf(stderr,"GPU HD texture was double-finished\n");failures++;}
      printf("CPU HD finish: changed=%u weak_delta=%llu strong_delta=%llu deterministic=%d direct_raw=%d\n",
          changed,(unsigned long long)weakDelta,(unsigned long long)strongDelta,
          !memcmp(strong,again,bytes),!memcmp(raw,direct,bytes));
      [input release];for(int i=0;i<4;i++)[outputs[i] release];
      free(source);free(raw);free(weak);free(strong);free(again);free(direct);
    }
    free(pixels);[g release];[queue release];[device release];
    printf("Metal graphics: %s (%d mismatches)\n",failures ? "FAILED" : "passed",failures);return failures ? 1 : 0;
  }
}
