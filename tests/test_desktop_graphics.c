#include "desktop_graphics.h"
#include "desktop_filter.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
int main(void) {
  Dkc1GraphicsSettings s;Dkc1GraphicsDefault(&s);
  assert(s.display==kDkc1DisplayFlat && s.upscaler==kDkc1UpscalerNearest);
  assert(s.reconstruct_mode==3 && s.strength==100 && s.softness==50 && s.shading==60);
  s.display=-5;s.upscaler=999;s.screen=99;s.reconstruct_mode=99;
  s.strength=-1;s.softness=101;s.shading=-8;s.window_scale=0;s.state_slot=999;
  s.aspect=-1;s.edge=999;s.volume=101;s.crt.mask=999;s.crt.curvature=-1;
  s.hd_polish=-1;s.hd_finish=999;
  Dkc1GraphicsClamp(&s);
  assert(s.display==0 && s.upscaler==3 && s.screen==3 && s.reconstruct_mode==4);
  assert(s.strength==0 && s.softness==100 && s.shading==0 && s.window_scale==1);
  assert(s.state_slot==4 && s.aspect==0 && s.edge==3 && s.volume==100 && s.crt.curvature==0);
  assert(s.hd_polish==0 && s.hd_finish==100);
  uint32_t source[]={0,0xffffff,0x808080,0x703010,0x103060,0x55cc55},saved[6],out[6];
  memcpy(saved,source,sizeof source); Dkc1DesktopColorFilter filter;
  assert(Dkc1DesktopColorFilterInit(&filter,0));
  assert(Dkc1DesktopColorFilterApply(&filter,(uint8_t*)source,(uint8_t*)out,6)==(uint8_t*)source);
  for(int profile=1;profile<4;profile++) {
    assert(Dkc1DesktopColorFilterInit(&filter,profile));
    assert(Dkc1DesktopColorFilterApply(&filter,(uint8_t*)source,(uint8_t*)out,6)==(uint8_t*)out);
    assert(memcmp(source,saved,sizeof source)==0 && memcmp(out,source,sizeof out)!=0);
  }
  Dkc1DesktopColorFilterDestroy(&filter);
  puts("desktop_graphics: passed");return 0;
}
