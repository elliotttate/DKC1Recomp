/* Synthetic edge cases plus real CPU/GPU equality; no cartridge assets needed. */
#include "../runner/dkc1_hd_scene.c"
#import "macos_hd_scene.h"
#include <assert.h>
bool Dkc1HdEnabled(void) { return true; }

int main(int argc,char **argv) {
  @autoreleasepool {
    assert(argc==2);
    id<MTLDevice> device=MTLCreateSystemDefaultDevice();if(!device)return 77;
    scene_enabled=active=ready=true;
    resident_count=4;resident_materials=calloc(resident_count,sizeof *resident_materials);
    for(unsigned i=0;i<resident_count;i++) {
      Material *m=&materials[i];m->width=m->height=32;m->hd_borrowed=true;m->resident_index=i;
      m->original=malloc(32*32*4);m->hd=malloc(128*128*4);
      for(int p=0;p<32*32;p++)m->original[p]=((p+i)%7 ? 0xff000000u : 0) | ((p*137+i*1731)&0xffffff);
      for(int y=0;y<128;y++)for(int x=0;x<128;x++) {
        uint32_t c=m->original[(y/4)*32+x/4];
        m->hd[y*128+x]=(c&0xffffff)^((x%4)*0x030405+(y%4)*0x050403);
        m->hd[y*128+x]|=((c>>24) ? (unsigned[]){255,128,1,0}[(x+y)%4] : 0)<<24;
      }
      resident_materials[i]=(ResidentMaterial){.width=32,.height=32,.pixels=m->hd};
      resident_bytes+=128*128*4;
    }
    for(int l=0;l<3;l++) {
      Background *b=&backgrounds[l];b->width=b->height=32;b->chunks[0]=&materials[l];
      memcpy(b->pixels,materials[l].original,32*32*4);
      for(int p=0;p<32*32;p++)b->z[p]=((l+1)<<12)|(p%256);
    }
    NSError *error=nil;
    Dkc1HdMetalRenderer *renderer=[[Dkc1HdMetalRenderer alloc] initWithDevice:device shaderPath:@(argv[1]) error:&error];
    if(!renderer){fprintf(stderr,"%s\n",error.description.UTF8String);return 1;}
    id<MTLCommandQueue> queue=[device newCommandQueue];
    unsigned validations=0;
    for(int aspect=0;aspect<2;aspect++)for(int scenario=0;scenario<8;scenario++) {
      Dkc1HdMetalSetPolish((scenario&1)?70:0);
      for(int l=0;l<3;l++) {
        backgrounds[l].connected[0]=scenario==2 || scenario==3;
        backgrounds[l].connected_mask[0]=0x5a5a;
        backgrounds[l].fallback_chunks[0]=scenario==3?&materials[(l+1)%3]:NULL;
      }
      for(int i=0;i<256;i++)world_palette[i]=0xff000000u|((i*5173)&0xffffff);
      frame_width=aspect?342:256;frame_number++;
      materials[3].hd=scenario==6?NULL:resident_materials[3].pixels;
      materials[3].hd_borrowed=scenario!=6;
      free(materials[3].art_original);materials[3].art_original=NULL;
      if(scenario==3) {
        materials[3].art_original=malloc(32*32*4);
        for(int i=0;i<32*32;i++)materials[3].art_original[i]=materials[3].original[i]^0x102010;
      }
      object_count=scenario==7?0:4;
      for(unsigned i=0;i<object_count;i++)objects[i]=(Object){.x=100+(int)i*7,.y=70+(int)i*5,.width=32,.height=32,.priority=i%2?2:4,.math_exempt=(scenario==1 || scenario==3) && (i&1),.material=&materials[3]};
      backgrounds[2].chunks[0]=scenario==6?NULL:&materials[2];
      for(int y=0;y<HEIGHT;y++) {
        main_enable[y]=(scenario==5 ? 3 : 7);
        math_flags[y]=(scenario&3)*64 | (scenario<4?31:0);
        fixed_color[y]=(y*197)&32767;backdrop[y]=0xff112233;
        for(int dy=0;dy<4;dy++)high_backdrop[y*4+dy]=0xff112233+dy*0x010101;
        for(int p=0;p<256;p++)palette[y][p]=0xff000000|((y*10573+p*2381)&0xffffff);
        for(int l=0;l<3;l++){scroll_x[l][y]=y+l*13;scroll_y[l][y]=y*2+l*11;}
        for(int x=0;x<frame_width;x++) {
          unsigned hit=0,miss=0;uint32_t c=Pixel(x,y,0,0,false,&hit,&miss);
          original[y*frame_width+x]=c^((x+y)%53==0?0x010101:0); /* oracle fallbacks */
        }
      }
      Dkc1HdMetalFrame *frames[8];
      for(int i=0;i<8;i++){frames[i]=[renderer captureNative:original width:frame_width height:HEIGHT];assert(frames[i]);}
      Dkc1HdMetalSetPolish(0); /* A later UI change cannot mutate queued frames. */
      assert(![renderer captureNative:original width:frame_width height:HEIGHT]);
      const uint32_t *reference=Dkc1HdScenePresent(original,frame_width,HEIGHT);assert(reference);
      /* Subsequent CPU mutation cannot change an already captured packet. */
      memset(original,0,sizeof original);
      for(int i=0;i<8;i++) {
        assert([renderer validateFrame:frames[i] reference:reference queue:queue]);validations++;
        [frames[i] release];
      }
    }
    [renderer release];[queue release];[device release];
    for(unsigned i=0;i<resident_count;i++){free(materials[i].original);free(materials[i].hd);free(materials[i].art_original);}
    free(resident_materials);
    printf("HD Metal passed: %u immutable CPU/GPU frames, transparency, priority, palette, color math, native fallback, pool exhaustion/reuse and both widths\n",validations);
    return 0;
  }
}
