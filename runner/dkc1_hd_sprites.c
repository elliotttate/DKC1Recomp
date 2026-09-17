/* Experimental host-only texture replacement. Guest memory and native
 * renderBuffer are never written. Art is matched to canonical OAM pixels.
 * Missing, clipped, occluded, faded and unsupported frames fail closed. */
#include "dkc1_hd_sprites.h"
#include "dkc1_hd_scene.h"
#include "dkc1_wram_gen.h"
#include "snes/ppu.h"
#include "sha256.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { MAX_SIDE=128, MAX_PACK=512, HEIGHT=224, WIDTH=342 };
typedef struct Asset { char key[65]; int w,h; uint32_t *pixels; } Asset;
static Asset assets[MAX_PACK];
static unsigned asset_count;
static bool initialized, enabled, candidate, visible;
static const char *pack_dir, *export_dir;
static FILE *trace;
static unsigned long frame;
static Asset *selected;
static uint32_t sprite[MAX_SIDE*MAX_SIDE], canonical[MAX_SIDE*MAX_SIDE];
static uint32_t clean[WIDTH*HEIGHT], capture[WIDTH*HEIGHT];
static uint32_t accepted_native[WIDTH*HEIGHT];
static uint32_t output[WIDTH*HEIGHT*kDkc1HdScale*kDkc1HdScale];
static Ppu *scratch;
static int x0,y0,sw,sh,frame_width,first,count;
static bool flip;
static unsigned animation,pose;
static char frame_key[65];
static void Init(void) {
  if(initialized) return;
  initialized=true;
  const char *on=getenv("DKC1_HD_SPRITES");
  enabled=on && !strcmp(on,"1");
  pack_dir=getenv("DKC1_HD_PACK"); export_dir=getenv("DKC1_HD_EXPORT");
  const char *trace_path=getenv("DKC1_HD_TRACE");
  if(trace_path) trace=fopen(trace_path,"w");
}
bool Dkc1HdEnabled(void) { Init(); return enabled; }
void Dkc1HdToggle(void) { Init(); enabled=!enabled; visible=false;
  fprintf(stderr,"[hd-sprites] %s\n",enabled?"on":"off"); }
static int OamX(const Ppu *p,int n) {
  int x=(p->oam[n*2]&255)|(((p->highOam[n/4]>>((n%4)*2))&1)<<8);
  return x>=256 ? x-512 : x;
}
static int Size(const Ppu *p,int n) {
  static const int sizes[8][2]={{8,16},{8,32},{8,64},{16,32},
                               {16,64},{32,64},{16,32},{16,32}};
  return sizes[PPU_objSize(p)&7][(p->highOam[n/4]>>((n%4)*2+1))&1];
}
static uint32_t Color(uint16_t c) {
  unsigned r=c&31,g=(c>>5)&31,b=(c>>10)&31;
  return 0xff000000u|((r<<3)|(r>>2))<<16|((g<<3)|(g>>2))<<8|(b<<3)|(b>>2);
}
static bool Match(const Ppu *p,int n,int ax,unsigned props) {
  int x=OamX(p,n)-ax;
  return (p->oam[n*2]>>8)!=240 && abs(x)<80 &&
      (((p->oam[n*2+1]>>8)^props)&0x7e)==0;
}
static Asset *Lookup(void) {
  for(unsigned i=0;i<asset_count;i++)
    if(!strcmp(frame_key,assets[i].key)) return assets[i].w==sw && assets[i].h==sh ? &assets[i] : NULL;
  if(asset_count==MAX_PACK) return NULL;
  Asset *a=&assets[asset_count++]; strcpy(a->key,frame_key); a->w=sw; a->h=sh;
  if(export_dir) {
    char path[2048]; snprintf(path,sizeof path,"%s/%s.pam",export_dir,frame_key);
    FILE *f=fopen(path,"wb");
    if(f) {
      fprintf(f,"P7\nWIDTH %d\nHEIGHT %d\nDEPTH 4\nMAXVAL 255\nTUPLTYPE RGB_ALPHA\nENDHDR\n",sw,sh);
      for(int i=0;i<sw*sh;i++) { uint32_t c=canonical[i];
        unsigned char rgba[]={c>>16,c>>8,c,c>>24}; fwrite(rgba,1,4,f); }
      fclose(f);
      snprintf(path,sizeof path,"%s/%s.json",export_dir,frame_key);
      f=fopen(path,"w"); if(f) { fprintf(f,"{\"key\":\"%s\",\"width\":%d,\"height\":%d,\"animation\":%u,\"pose\":%u}\n",frame_key,sw,sh,animation,pose); fclose(f); }
    }
  }
  if(!pack_dir) return a;
  char path[2048]; snprintf(path,sizeof path,"%s/%s.dkhd",pack_dir,frame_key);
  FILE *f=fopen(path,"rb"); if(!f) return a;
  unsigned char header[12];
  if(fread(header,1,12,f)!=12 || memcmp(header,"DKHDv001",8) ||
      (header[8]|header[9]<<8)!=sw*4 || (header[10]|header[11]<<8)!=sh*4) {
    fclose(f); return a;
  }
  size_t bytes=(size_t)sw*sh*16*4;
  a->pixels=malloc(bytes);
  if(a->pixels && (fread(a->pixels,1,bytes,f)!=bytes || fgetc(f)!=EOF)) {
    free(a->pixels); a->pixels=NULL;
  }
  fclose(f);
  return a;
}
void Dkc1HdPrepare(Ppu *p,const uint8_t *w,int bias) {
  Init(); candidate=false; visible=false; selected=NULL;
  if(Dkc1HdScenePrepare(p,w,bias))return;
  if((!enabled && !export_dir) || !p || !w) return;
  /* This first art pack is validated only in normal Jungle Hijinxs play. */
  if(Dkc1WramU16(w,0x003e)!=0x16 ||
     Dkc1WramU16(w,DKC1_WRAM_Player_CurrentKongLo)!=1 ||
     PPU_mode(p)!=1 || PPU_forcedBlank(p) || PPU_brightness(p)!=15 ||
     PPU_mosaicSize(p)>1 || !(p->screenEnabled[0]&16)) return;
  uint32_t slot=0;
  for(uint32_t i=DKC1_ACTOR_SLOT_FIRST;i<=DKC1_ACTOR_SLOT_LAST;i+=DKC1_ACTOR_SLOT_STEP)
    if(Dkc1WramU16(w,DKC1_WRAM_NorSpr_SpriteIDLo+i)==1) {slot=i;break;}
  if(!slot) return;
  int ax=(int16_t)(Dkc1Actor_SprXPos(w,slot)-Dkc1WramU16(w,DKC1_WRAM_CameraX));
  unsigned props=Dkc1Actor_SprFlags(w,slot)>>8;
  first=0;count=0;
  for(int n=0;n<128;) {
    if(!Match(p,n,ax,props)) {n++;continue;}
    int start=n;while(n<128 && Match(p,n,ax,props)) n++;
    if(n-start>count) {first=start;count=n-start;}
  }
  if(count<2 || count>32) return;
  int minx=999,miny=999,maxx=-999,maxy=-999;
  for(int n=first;n<first+count;n++) {
    int x=OamX(p,n),y=p->oam[n*2]>>8,s=Size(p,n);
    if(x<minx)minx=x;if(y<miny)miny=y;
    if(x+s>maxx)maxx=x+s;if(y+s>maxy)maxy=y+s;
  }
  int w0=maxx-minx,h0=maxy-miny;
  if(w0<=0 || h0<=0 || w0>MAX_SIDE || h0>MAX_SIDE) return;
  memset(sprite,0,sizeof sprite);
  /* Same SNES tile address / bitplane / lower-OAM-first rules as ppu.c. */
  for(int n=first+count-1;n>=first;n--) {
    unsigned a=p->oam[n*2+1];int sz=Size(p,n);
    int ox=OamX(p,n)-minx,oy=(p->oam[n*2]>>8)-miny;
    int base=(a&0x100)?PPU_objTileAdr2(p):PPU_objTileAdr1(p);
    for(int y=0;y<sz;y++)for(int x=0;x<sz;x++) {
      int tx=(a&0x4000)?sz-1-x:x,ty=(a&0x8000)?sz-1-y:y;
      int tile=(((a&255)/16+(ty/8))*16)|(((a&15)+(tx/8))&15);
      int addr=(base+tile*16+(ty&7))&0x7fff,shift=7-(tx&7);
      uint32_t bits=((uint32_t)p->vram[addr]|(uint32_t)p->vram[(addr+8)&0x7fff]<<16)>>shift;
      unsigned ix=(bits&1)|((bits>>7)&2)|((bits>>14)&4)|((bits>>21)&8);
      if(ix) sprite[(oy+y)*w0+ox+x]=Color(p->cgram[128+((a>>9)&7)*16+ix]);
    }
  }
  int lx=w0,ly=h0,hx=-1,hy=-1;
  for(int y=0;y<h0;y++)for(int x=0;x<w0;x++)if(sprite[y*w0+x]) {
    if(x<lx)lx=x;if(y<ly)ly=y;if(x>hx)hx=x;if(y>hy)hy=y;
  }
  if(hx<0) return;
  sw=hx-lx+1;sh=hy-ly+1;flip=(props&0x40)!=0;
  for(int y=0;y<sh;y++)for(int x=0;x<sw;x++)
    canonical[y*sw+x]=sprite[(ly+y)*w0+lx+(flip?sw-1-x:x)];
  uint8_t hash[32];sha256_compute((const uint8_t *)canonical,(size_t)sw*sh*4,hash);
  for(int i=0;i<32;i++)snprintf(frame_key+i*2,3,"%02x",hash[i]);
  frame_width=(int)p->renderPitch/4;
  x0=minx+lx-bias+(frame_width-256)/2;y0=miny+ly;
  animation=Dkc1Actor_SprAnimID(w,slot);pose=Dkc1Actor_DisplayedPoseLo(w,slot);
  selected=Lookup();
  if(!enabled || !selected || !selected->pixels || frame_width>WIDTH ||
     x0<(frame_width-256)/2 || x0+sw>(frame_width+256)/2 || y0<0 || y0+sh>HEIGHT) return;
  if(!scratch) scratch=malloc(sizeof *scratch);
  candidate=scratch!=NULL;
}
void Dkc1HdCaptureLine(Ppu *p,int line) {
  Dkc1HdSceneCaptureLine(p,line);
  if(!candidate || line<y0+1 || line>y0+sh) return;
  /* Replay only the affected scanline on a private PPU copy. Do not call
   * Dkc1DrawPpuFrame/RtlRunFrame again, and do not modify live OAM or VRAM.
   * The exact HDMA register state has already been applied for this line. */
  memcpy(scratch,p,sizeof *scratch);
  PpuClearOverlayBindings(scratch);PpuClearOverlayCaptures(scratch);
  scratch->renderBuffer=(uint8_t *)clean;
  scratch->widescreenLineEnhancer=NULL;
  PpuBindOverlaySurface(scratch,kPpuOverlaySource_Obj,(uint8_t *)capture,p->renderPitch);
  PpuSetOverlayCapture(scratch,kPpuOverlaySource_Obj,x0-(frame_width-256)/2,y0,sw,sh,kPpuOverlayFlag_RemoveFromGame);
  PpuSetOverlayOamRange(scratch,(uint8_t)first,(uint8_t)count);
  ppu_runLine(scratch,line);
}
static void Finish(Ppu *p) {
  if(!candidate) return;
  /* Full visibility is required. Color math, object overlap and foreground
   * occlusion reject the replacement instead of painting over those layers. */
  const uint32_t *native=(const uint32_t *)p->renderBuffer;
  for(int y=0;y<sh;y++)for(int x=0;x<sw;x++) {
    uint32_t c=canonical[y*sw+(flip?sw-1-x:x)];
    const size_t at=(y0+y)*frame_width+x0+x;
    if(c && (((native[at]^c)&0xffffffu) || capture[at]!=c)) return;
    if(!c && ((native[at]^clean[at])&0xffffffu)) return;
  }
  memcpy(accepted_native,native,(size_t)frame_width*HEIGHT*4);
  visible=true;
}
void Dkc1HdFinish(Ppu *p) {
  Dkc1HdSceneFinish(p);
  Finish(p);
  if(trace) {
    fprintf(trace,"{\"frame\":%lu,\"candidate\":%d,\"replaced\":%d,\"key\":\"%s\",\"box\":[%d,%d,%d,%d],\"flip\":%d}\n",frame,candidate,visible,selected?frame_key:"",x0,y0,sw,sh,flip);
    fflush(trace);
  }
  frame++;
}
const uint32_t *Dkc1HdPresent(const uint32_t *native,int w,int h,int *scale) {
  Init();*scale=1;
  if(!enabled || w<256 || w>WIDTH || h!=HEIGHT) return native;
  const uint32_t *scene=Dkc1HdScenePresent(native,w,h);
  if(scene){*scale=kDkc1HdScale;return scene;}
  const int s=kDkc1HdScale,ow=w*s;
  for(int y=0;y<h;y++)for(int x=0;x<w;x++) {
    uint32_t c=native[y*w+x];
    for(int dy=0;dy<s;dy++)for(int dx=0;dx<s;dx++)output[(y*s+dy)*ow+x*s+dx]=c;
  }
  if(visible && selected && selected->pixels && w==frame_width &&
      !memcmp(native,accepted_native,(size_t)w*h*4)) {
    for(int y=0;y<sh*s;y++)for(int x=0;x<sw*s;x++) {
      uint32_t c=selected->pixels[y*sw*s+(flip?sw*s-1-x:x)];
      uint32_t b=clean[(y0+y/s)*w+x0+x/s];unsigned a=c>>24;
      unsigned r=(((c>>16)&255)*a+((b>>16)&255)*(255-a)+127)/255;
      unsigned g=(((c>>8)&255)*a+((b>>8)&255)*(255-a)+127)/255;
      unsigned bl=((c&255)*a+(b&255)*(255-a)+127)/255;
      output[(y0*s+y)*ow+x0*s+x]=0xff000000u|r<<16|g<<8|bl;
    }
  }
  *scale=s;return output;
}
