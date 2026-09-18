/* Full-scene HD experiment. All material data is derived from the current
 * verified cartridge's PPU state. This module never writes guest memory. */
#include "dkc1_hd_scene.h"
#include "dkc1_hd_sprites.h"
#include "dkc1_edge_policy.h"
#include "dkc1_wram_gen.h"
#include "snes/ppu.h"
#include "snes/ws_shadow.h"
#include "sha256.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef __APPLE__
#include <CommonCrypto/CommonDigest.h>
#include <dispatch/dispatch.h>
#endif

enum { SCALE=4, WIDTH=342, HEIGHT=224, ATLAS=512, CHUNK=32, CONTEXT=8, GROUPS=128,
       MAX_MATERIALS=4096 };
typedef struct Material {
  char key[65];
  int width,height;
  uint32_t *original,*hd,*art_original;
  uint64_t last_use;
  bool pinned,hd_borrowed;
  uint32_t resident_index;
} Material;
typedef struct ResidentMaterial {
  char key[65];
  int width,height;
  uint32_t *pixels,*base;
} ResidentMaterial;
typedef struct CenterAlias { char key[65]; unsigned resident; } CenterAlias;
static CenterAlias *center_aliases,*object_silhouettes;
static unsigned center_alias_count,object_silhouette_count;
static uint64_t center_alias_hits,object_silhouette_hits;
static bool exact_centers_enabled;
typedef struct WorldLayer { unsigned width,height; Material *chunks; } WorldLayer;
static WorldLayer world_layers[3],cave_world_layers[3];
static WorldLayer *active_world_layers=world_layers;
static int active_world_origin_x;
static bool connected_world_enabled,connected_world_active;
static bool hoard_margin_mirror;
typedef enum FixedScene {
  FIXED_SCENE_NONE,
  FIXED_SCENE_HOARD,
  FIXED_SCENE_TREEHOUSE
} FixedScene;
static FixedScene fixed_scene;
static uint32_t *hoard_fixed_plate,*treehouse_fixed_plate;
static uint64_t connected_world_hits;
static uint32_t world_palette[256],cave_world_palette[256];
static uint32_t *active_world_palette=world_palette;
static const char *missing_export;
static FILE *coverage_trace;
static unsigned long coverage_frame;
static unsigned coverage_camera_x,coverage_camera_y,coverage_entrance;
static int world_camera_x,world_camera_y;
static void TraceCoverage(void);
typedef struct Background {
  int width,height;
  uint32_t pixels[ATLAS*ATLAS];
  uint16_t z[ATLAS*ATLAS];
  Material *chunks[16*16];
  Material *fallback_chunks[16*16];
  uint16_t connected_mask[16*16];
  bool connected[16*16];
  uint8_t digest[32];
  bool bound_connected;
  int bound_x,bound_y;
} Background;
typedef struct Object {
  int x,y,width,height,first,priority;
  bool math_exempt;
  Material *material;
} Object;
static Material materials[MAX_MATERIALS];
static unsigned material_count;
static uint64_t material_clock,material_evictions,material_failures;
static ResidentMaterial *resident_materials;
static unsigned resident_count;
static uint64_t resident_bytes,material_file_reads;
static bool preload_requested,preload_attempted;
static Background backgrounds[3];
static Object objects[GROUPS];
static unsigned object_count;
static bool initialized,active,scene_enabled;
static const char *export_dir,*pack_dir;
static int frame_width,presentation_bias;
static uint32_t original[WIDTH*HEIGHT],output[WIDTH*HEIGHT*SCALE*SCALE];
static uint16_t main_z[WIDTH*HEIGHT],sub_z[WIDTH*HEIGHT];
static uint16_t scroll_x[3][HEIGHT],scroll_y[3][HEIGHT];
static uint8_t main_enable[HEIGHT],sub_enable[HEIGHT],math_flags[HEIGHT],math_select[HEIGHT];
static uint16_t fixed_color[HEIGHT];
static uint32_t backdrop[HEIGHT];
static uint32_t high_backdrop[HEIGHT*SCALE];
static uint32_t palette[HEIGHT][256];
static FILE *scene_trace;
static FILE *audit_trace;
static bool ready;
static unsigned long frame_number;
static int frame_brightness=15;
static uint32_t Pixel(int x,int y,int dx,int dy,bool high,unsigned *hits,unsigned *misses);
static void HdHash(const void *data,size_t bytes,uint8_t digest[32]) {
#ifdef __APPLE__
  CC_SHA256(data,(CC_LONG)bytes,digest);
#else
  sha256_compute(data,bytes,digest);
#endif
}
static uint32_t FullColor(uint16_t c) {
  unsigned r=c&31,g=(c>>5)&31,b=(c>>10)&31;
  return 0xff000000u | ((r<<3)|(r>>2))<<16 |
      ((g<<3)|(g>>2))<<8 | (b<<3)|(b>>2);
}
static uint32_t ApplyBrightness(uint32_t color) {
  unsigned b=(unsigned)frame_brightness;if(b>=15)return color;
  uint32_t result=color&0xff000000u;
  for(int ch=0;ch<3;ch++){int shift=ch*8;result|=(((color>>shift)&255)*b/15u)<<shift;}
  return result;
}
static void WritePam(const char *path,const uint32_t *pixels,int w,int h) {
  FILE *f=fopen(path,"wb");if(!f)return;
  fprintf(f,"P7\nWIDTH %d\nHEIGHT %d\nDEPTH 4\nMAXVAL 255\nTUPLTYPE RGB_ALPHA\nENDHDR\n",w,h);
  for(int i=0;i<w*h;i++) {uint32_t c=pixels[i];uint8_t b[]={c>>16,c>>8,c,c>>24};fwrite(b,1,4,f);}
  fclose(f);
}
static void PinMaterial(Material *m) {
  if(m){m->pinned=true;m->last_use=++material_clock;}
}
static void BeginMaterialFrame(void) {
  for(unsigned i=0;i<material_count;i++)materials[i].pinned=false;
  /* Unchanged atlases skip decoding and keep their pointers. Protect those
   * before any layer can replace cache entries. Old object pointers are no
   * longer used: DecodeObjects rebuilds them before synchronous presentation.
   * Even retaining primary/fallback old and new atlas chunks uses at most 3072 slots,
   * plus 128 objects, below the fixed 4096-slot limit. */
  for(int l=0;l<3;l++) {
    Background *bg=&backgrounds[l];
    for(int y=0;y<bg->height/CHUNK;y++)for(int x=0;x<bg->width/CHUNK;x++) {
      PinMaterial(bg->chunks[y*16+x]);
      PinMaterial(bg->fallback_chunks[y*16+x]);
    }
  }
}
/* Read eagerly into owned memory: mmap alone would defer page faults until play.
 * The immutable resident pack owns HD rasters independently of the small cache
 * of decoded cartridge materials, so cache eviction cannot unload the art. */
static uint32_t *ReadHdRaster(const char *path,int expected_w,int expected_h,
                              int *width,int *height) {
  material_file_reads++;
  FILE *f=fopen(path,"rb");if(!f)return NULL;
  uint8_t header[12];uint32_t *pixels=NULL;
  if(fread(header,1,12,f)==12 && !memcmp(header,"DKHDv001",8)) {
    unsigned w=header[8]|header[9]<<8,h=header[10]|header[11]<<8;
    if(w && h && w<=WIDTH*SCALE && h<=HEIGHT*SCALE &&
       !(w%SCALE) && !(h%SCALE) &&
       (!expected_w || w==(unsigned)expected_w*SCALE) &&
       (!expected_h || h==(unsigned)expected_h*SCALE)) {
      size_t bytes=(size_t)w*h*4;
      pixels=malloc(bytes);
      if(pixels && (fread(pixels,1,bytes,f)!=bytes || fgetc(f)!=EOF)) {
        free(pixels);pixels=NULL;
      }
      if(pixels){if(width)*width=(int)w/SCALE;if(height)*height=(int)h/SCALE;}
    }
  }
  fclose(f);return pixels;
}
static void FreeWorldLayers(WorldLayer *layers) {
  for(int l=0;l<3;l++) {
    WorldLayer *w=&layers[l];
    if(w->chunks)for(unsigned i=0;i<w->width*w->height;i++)free(w->chunks[i].original);
    free(w->chunks);memset(w,0,sizeof *w);
  }
}
static void FreeConnectedWorld(void) {
  FreeWorldLayers(world_layers);FreeWorldLayers(cave_world_layers);
}
static void FreeResidentPack(void) {
  FreeConnectedWorld();
  for(unsigned i=0;i<resident_count;i++){free(resident_materials[i].pixels);free(resident_materials[i].base);}
  free(resident_materials);resident_materials=NULL;resident_count=0;resident_bytes=0;
  free(center_aliases);center_aliases=NULL;center_alias_count=0;
  free(object_silhouettes);object_silhouettes=NULL;object_silhouette_count=0;
  free(hoard_fixed_plate);hoard_fixed_plate=NULL;
  free(treehouse_fixed_plate);treehouse_fixed_plate=NULL;
}
/* Fixed plates are optional, exact-size pack companions. They are eagerly
 * owned like the resident material pack and never consulted during gameplay. */
static uint32_t *LoadFixedPlate(const char *name) {
  char path[2048];
  if(!pack_dir)return NULL;
  snprintf(path,sizeof path,"%s/%s",pack_dir,name);
  FILE *f=fopen(path,"rb");if(!f)return NULL;
  const size_t bytes=(size_t)WIDTH*SCALE*HEIGHT*SCALE*4;
  uint32_t *pixels=malloc(bytes);
  bool ok=pixels && fread(pixels,1,bytes,f)==bytes && fgetc(f)==EOF;
  fclose(f);
  if(!ok){free(pixels);return NULL;}
  return pixels;
}
static void LoadFixedPlates(void) {
  free(hoard_fixed_plate);free(treehouse_fixed_plate);
  hoard_fixed_plate=LoadFixedPlate("fixed-hoard-wide.bgra");
  treehouse_fixed_plate=LoadFixedPlate("fixed-treehouse-wide.bgra");
}
static bool PreloadResidentPack(void) {
  char path[2048],line[128];unsigned count=0;char extra;
  if(!pack_dir)return false;
  snprintf(path,sizeof path,"%s/preload.txt",pack_dir);
  FILE *f=fopen(path,"rb");if(!f)return false;
  if(!fgets(line,sizeof line,f) ||
     sscanf(line,"DKHPv001 %u %c",&count,&extra)!=1 || !count || count>65536) {
    fclose(f);return false;
  }
  resident_materials=calloc(count,sizeof *resident_materials);
  if(!resident_materials){fclose(f);return false;}
  resident_count=count;
  bool ok=true;
  for(unsigned i=0;i<count;i++) {
    ResidentMaterial *r=&resident_materials[i];
    if(!fgets(line,sizeof line,f) || strlen(line)!=65 || line[64]!='\n') {ok=false;break;}
    line[64]=0;
    if(strspn(line,"0123456789abcdef")!=64 ||
       (i && strcmp(resident_materials[i-1].key,line)>=0)) {ok=false;break;}
    memcpy(r->key,line,65);
    snprintf(path,sizeof path,"%s/%s.dkhd",pack_dir,r->key);
    r->pixels=ReadHdRaster(path,0,0,&r->width,&r->height);
    if(!r->pixels){ok=false;break;}
    resident_bytes+=(uint64_t)r->width*r->height*SCALE*SCALE*4;
  }
  if(fgetc(f)!=EOF)ok=false;
  fclose(f);
  if(!ok)FreeResidentPack();
  return ok;
}
static ResidentMaterial *FindResidentMaterial(const char *key) {
  unsigned lo=0,hi=resident_count;
  while(lo<hi) {
    unsigned mid=lo+(hi-lo)/2;int cmp=strcmp(key,resident_materials[mid].key);
    if(!cmp)return &resident_materials[mid];
    if(cmp<0)hi=mid;else lo=mid+1;
  }
  return NULL;
}
/* The optional index maps a byte-exact 32x32 source center to existing resident
 * art. Neighboring ring-buffer changes may change the 48x48 context key while
 * the actual rendered source center remains identical. No approximate match,
 * cartridge writes, new texture allocation, or gameplay file reads are used. */
static bool LoadCenterAliases(void) {
  char path[2048],line[160],extra;unsigned count;
  if(!exact_centers_enabled || !resident_count)return false;
  snprintf(path,sizeof path,"%s/background-centers.txt",pack_dir);
  FILE *f=fopen(path,"rb");if(!f)return false;
  if(!fgets(line,sizeof line,f) || sscanf(line,"DKHCv001 %u %c",&count,&extra)!=1 ||
     !count || count>65536){fclose(f);return false;}
  CenterAlias *aliases=calloc(count,sizeof *aliases);if(!aliases){fclose(f);return false;}
  bool ok=true;
  for(unsigned i=0;i<count;i++) {
    if(!fgets(line,sizeof line,f) || strlen(line)!=130 || line[64]!=' ' || line[129]!='\n') {ok=false;break;}
    line[64]=0;line[129]=0;
    if(strspn(line,"0123456789abcdef")!=64 || strspn(line+65,"0123456789abcdef")!=64 ||
       (i && strcmp(aliases[i-1].key,line)>=0)){ok=false;break;}
    ResidentMaterial *r=FindResidentMaterial(line+65);
    if(!r || r->width!=CHUNK || r->height!=CHUNK){ok=false;break;}
    memcpy(aliases[i].key,line,65);aliases[i].resident=(unsigned)(r-resident_materials);
  }
  if(fgetc(f)!=EOF)ok=false;fclose(f);
  if(!ok){free(aliases);return false;}
  free(center_aliases);center_aliases=aliases;center_alias_count=count;return true;
}
static ResidentMaterial *FindCenterAlias(const uint32_t *pixels) {
  if(!center_alias_count)return NULL;
  uint8_t digest[32];char key[65];HdHash(pixels,CHUNK*CHUNK*4,digest);
  for(int i=0;i<32;i++)snprintf(key+i*2,3,"%02x",digest[i]);
  unsigned lo=0,hi=center_alias_count;
  while(lo<hi){unsigned mid=lo+(hi-lo)/2;int cmp=strcmp(key,center_aliases[mid].key);
    if(!cmp){center_alias_hits++;return &resident_materials[center_aliases[mid].resident];}
    if(cmp<0)hi=mid;else lo=mid+1;
  }
  return NULL;
}
/* Optional object index: exact native silhouette plus size, not colors.
 * Live CGRAM animation changes the BGRA key while the authored mask stays
 * identical. Background center aliases remain isolated from this table. */
static void ObjectSilhouetteKey(const uint32_t *pixels,int w,int h,char key[65]) {
  size_t bits=(size_t)w*(size_t)h,bytes=4+(bits+7)/8;uint8_t digest[32];
  uint8_t *packed=malloc(bytes);if(!packed){memset(key,'0',64);key[64]=0;return;}
  packed[0]=(uint8_t)w;packed[1]=(uint8_t)(w>>8);packed[2]=(uint8_t)h;packed[3]=(uint8_t)(h>>8);
  memset(packed+4,0,bytes-4);
  for(size_t i=0;i<bits;i++)if(pixels[i])packed[4+i/8]|=(uint8_t)(1u<<(i&7));
  HdHash(packed,bytes,digest);free(packed);
  for(int i=0;i<32;i++)snprintf(key+i*2,3,"%02x",digest[i]);
}
static bool LoadObjectSilhouettes(void) {
  char path[2048],line[160],extra;unsigned count;
  if(!resident_count)return false;
  snprintf(path,sizeof path,"%s/object-silhouettes.txt",pack_dir);
  FILE *f=fopen(path,"rb");if(!f)return false;
  if(!fgets(line,sizeof line,f) || sscanf(line,"DKHOs001 %u %c",&count,&extra)!=1 ||
     !count || count>65536){fclose(f);return false;}
  CenterAlias *aliases=calloc(count,sizeof *aliases);if(!aliases){fclose(f);return false;}
  bool ok=true;
  for(unsigned i=0;i<count;i++) {
    if(!fgets(line,sizeof line,f) || strlen(line)!=130 || line[64]!=' ' || line[129]!='\n'){ok=false;break;}
    line[64]=0;line[129]=0;
    if(strspn(line,"0123456789abcdef")!=64 || strspn(line+65,"0123456789abcdef")!=64 ||
       (i && strcmp(aliases[i-1].key,line)>=0)){ok=false;break;}
    ResidentMaterial *r=FindResidentMaterial(line+65);
    if(!r || r->width<1 || r->height<1 || r->width>128 || r->height>128){ok=false;break;}
    memcpy(aliases[i].key,line,65);aliases[i].resident=(unsigned)(r-resident_materials);
  }
  if(fgetc(f)!=EOF)ok=false;fclose(f);
  if(!ok){free(aliases);return false;}
  free(object_silhouettes);object_silhouettes=aliases;object_silhouette_count=count;
  snprintf(path,sizeof path,"%s/object-bases.bin",pack_dir);
  f=fopen(path,"rb");if(!f)return true;
  uint8_t header[12];ok=fread(header,1,12,f)==12 && !memcmp(header,"DKHDb001",8);
  unsigned bases=ok?header[8]|header[9]<<8|header[10]<<16|header[11]<<24:0;
  char previous[65]="";
  if(!bases || bases>65536)ok=false;
  for(unsigned i=0;ok && i<bases;i++) {
    char key[65];uint8_t dim[4];
    if(fread(key,1,64,f)!=64 || fread(dim,1,4,f)!=4){ok=false;break;}
    key[64]=0;unsigned w=dim[0]|dim[1]<<8,h=dim[2]|dim[3]<<8;
    if(strspn(key,"0123456789abcdef")!=64 || (i && strcmp(previous,key)>=0) ||
       !w || !h || w>128 || h>128){ok=false;break;}
    size_t bytes=(size_t)w*h*4;uint32_t *pixels=malloc(bytes);
    ResidentMaterial *r=FindResidentMaterial(key);
    if(!pixels || !r || r->width!=(int)w || r->height!=(int)h ||
       fread(pixels,1,bytes,f)!=bytes){free(pixels);ok=false;break;}
    free(r->base);r->base=pixels;memcpy(previous,key,65);
  }
  if(ok && fgetc(f)!=EOF)ok=false;fclose(f);
  if(!ok){for(unsigned i=0;i<resident_count;i++){free(resident_materials[i].base);resident_materials[i].base=NULL;}
    free(object_silhouettes);object_silhouettes=NULL;object_silhouette_count=0;return false;}
  return true;
}
static ResidentMaterial *FindObjectSilhouette(const uint32_t *pixels,int w,int h) {
  if(!object_silhouette_count || w<1 || h<1 || w>128 || h>128)return NULL;
  char key[65];ObjectSilhouetteKey(pixels,w,h,key);
  unsigned lo=0,hi=object_silhouette_count;
  while(lo<hi){unsigned mid=lo+(hi-lo)/2;int cmp=strcmp(key,object_silhouettes[mid].key);
    if(!cmp){object_silhouette_hits++;return &resident_materials[object_silhouettes[mid].resident];}
    if(cmp<0)hi=mid;else lo=mid+1;
  }
  return NULL;
}
static Material *GetMaterialKey(const uint32_t *pixels,int w,int h,const char *kind,
                               const void *identity,size_t identity_bytes) {
  uint8_t digest[32];char key[65];
  HdHash(identity,identity_bytes,digest);
  for(int i=0;i<32;i++)snprintf(key+i*2,3,"%02x",digest[i]);
  for(unsigned i=0;i<material_count;i++)if(!strcmp(key,materials[i].key)) {
    Material *m=&materials[i];
    if(m->width!=w || m->height!=h){material_failures++;return NULL;}
    PinMaterial(m);return m;
  }
  Material *m=NULL;
  if(material_count<MAX_MATERIALS)m=&materials[material_count];
  else for(unsigned i=0;i<material_count;i++)
    if(!materials[i].pinned && (!m || materials[i].last_use<m->last_use))m=&materials[i];
  if(!m){material_failures++;return NULL;}
  /* Allocate before replacing a valid entry. A failed allocation must not
   * leave a cached key whose original raster is NULL. */
  uint32_t *copy=malloc((size_t)w*h*4);
  if(!copy){material_failures++;return NULL;}
  memcpy(copy,pixels,(size_t)w*h*4);
  if(material_count<MAX_MATERIALS)material_count++;
  else {free(m->original);free(m->art_original);if(!m->hd_borrowed)free(m->hd);material_evictions++;}
  memset(m,0,sizeof *m);strcpy(m->key,key);m->width=w;m->height=h;
  m->original=copy;PinMaterial(m);
  char path[2048];
  if(export_dir) {
    snprintf(path,sizeof path,"%s/%s.pam",export_dir,key);WritePam(path,pixels,w,h);
    snprintf(path,sizeof path,"%s/%s.json",export_dir,key);FILE *f=fopen(path,"w");
    if(f){fprintf(f,"{\"key\":\"%s\",\"width\":%d,\"height\":%d,\"kind\":\"%s\"}\n",key,w,h,kind);fclose(f);}
  }
  if(preload_requested) {
    ResidentMaterial *r=FindResidentMaterial(key);
    if(!r && w==CHUNK && h==CHUNK && !strcmp(kind,"background"))r=FindCenterAlias(pixels);
    if(!r && !strcmp(kind,"object"))r=FindObjectSilhouette(pixels,w,h);
    if(r && r->width==w && r->height==h){
      m->hd=r->pixels;m->hd_borrowed=true;m->resident_index=(uint32_t)(r-resident_materials);
      if(r->base){size_t bytes=(size_t)w*h*4;uint32_t *base=malloc(bytes);
        if(base){memcpy(base,r->base,bytes);m->art_original=base;}}
    }
  } else if(pack_dir) {
    snprintf(path,sizeof path,"%s/%s.dkhd",pack_dir,key);
    m->hd=ReadHdRaster(path,w,h,NULL,NULL);
  }
  if(!m->hd && missing_export) {
    snprintf(path,sizeof path,"%s/%s.pam",missing_export,key);WritePam(path,pixels,w,h);
    if(identity_bytes==48*48*4 && !strcmp(kind,"background")) {
      snprintf(path,sizeof path,"%s/%s-context.pam",missing_export,key);WritePam(path,identity,48,48);
    }
    snprintf(path,sizeof path,"%s/%s.json",missing_export,key);FILE *f=fopen(path,"w");
    if(f){fprintf(f,"{\"key\":\"%s\",\"kind\":\"%s\",\"width\":%d,\"height\":%d}\n",key,kind,w,h);fclose(f);}
  }
  return m;
}
static Material *GetMaterial(const uint32_t *pixels,int w,int h,const char *kind) {
  return GetMaterialKey(pixels,w,h,kind,pixels,(size_t)w*h*4);
}
/* Private pack source registration: each grid cell owns an exact native raster
 * and borrows one resident HD raster. No world coordinate alone permits art. */
static bool LoadWorldFile(const char *name,WorldLayer *layers,uint32_t *colors) {
  char path[2048];snprintf(path,sizeof path,"%s/%s",pack_dir,name);
  FILE *f=fopen(path,"rb");if(!f)return false;
  uint8_t header[32];bool ok=fread(header,1,sizeof header,f)==sizeof header && !memcmp(header,"DKHWv002",8);
  if(ok)ok=fread(colors,1,256*4,f)==256*4;
  for(int l=0;ok && l<3;l++) {
    WorldLayer *w=&layers[l];const uint8_t *p=header+8+l*8;
    w->width=p[0]|p[1]<<8|p[2]<<16|(uint32_t)p[3]<<24;
    w->height=p[4]|p[5]<<8|p[6]<<16|(uint32_t)p[7]<<24;
    if(!w->width || !w->height || w->width>168 || w->height>16){ok=false;break;}
    w->chunks=calloc((size_t)w->width*w->height,sizeof *w->chunks);if(!w->chunks){ok=false;break;}
    for(unsigned i=0;ok && i<w->width*w->height;i++) {
      Material *m=&w->chunks[i];m->original=malloc(CHUNK*CHUNK*4);
      if(!m->original || fread(m->key,1,64,f)!=64 || strspn(m->key,"0123456789abcdef")!=64 ||
         fread(m->original,1,CHUNK*CHUNK*4,f)!=CHUNK*CHUNK*4){ok=false;break;}
      ResidentMaterial *r=FindResidentMaterial(m->key);
      if(!r || r->width!=CHUNK || r->height!=CHUNK){ok=false;break;}
      m->width=m->height=CHUNK;m->hd=r->pixels;m->hd_borrowed=true;m->resident_index=(uint32_t)(r-resident_materials);
    }
  }
  if(fgetc(f)!=EOF)ok=false;fclose(f);if(!ok)FreeWorldLayers(layers);return ok;
}
static bool LoadConnectedWorld(void) {
  return LoadWorldFile("connected-world.bin",world_layers,world_palette);
}
static Material *ConnectedChunk(int layer,int cx,int cy,int ring_w,int ring_h,const uint32_t *pixels,const uint16_t *indices,unsigned *mask) {
  *mask=0;
  WorldLayer *w=&active_world_layers[layer];if(!connected_world_active || !w->chunks)return NULL;
  int x=cx*CHUNK,y=cy*CHUNK;
  if(layer==0) {
    // Anchor the ring lift to the visible left edge, including its partial
    // chunk. The native camera can already be in the next ring period while
    // the wide left margin still uses the previous one.
    int left=world_camera_x-(frame_width-256)/2;
    int base_x=left-(left&(ring_w-1));
    int base_y=world_camera_y-(world_camera_y&(ring_h-1));
    x+=base_x;y+=base_y;
    while(x+CHUNK<=left)x+=ring_w;
    while(x>left+ring_w)x-=ring_w;
    while(y+CHUNK<=world_camera_y)y+=ring_h;
    x-=active_world_origin_x;
  }
  if(x<0 || y<0 || x/CHUNK>=(int)w->width || y/CHUNK>=(int)w->height)return NULL;
  Material *m=&w->chunks[(y/CHUNK)*w->width+x/CHUNK];
  // Streaming updates 8px tiles independently. Verify each one separately so
  // unseen stale neighbors cannot demote the visible part of a 32px HD cell.
  // Palette animation changes color, not identity: compare in the pack's
  // canonical palette, then apply the live palette during composition.
  for(int sy=0;sy<4;sy++)for(int sx=0;sx<4;sx++) {
    bool exact=true;
    for(int y=sy*8;y<sy*8+8;y++)for(int x=sx*8;x<sx*8+8;x++) {
      int at=y*CHUNK+x;uint32_t canonical=pixels[at]?active_world_palette[indices[at]&255]:0;
      if(m->original[at]!=canonical)exact=false;
    }
    if(exact)*mask|=1u<<(sy*4+sx);
  }
  if(!*mask)return NULL;
  connected_world_hits++;return m;
}
static void DecodeBackground(Ppu *p,int layer) {
  Background *bg=&backgrounds[layer];
  bg->width=PPU_bgTilemapWider(p,layer)?512:256;
  bg->height=PPU_bgTilemapHigher(p,layer)?512:256;
  const int words=layer==2?8:16,palette_shift=layer==2?8:6;
  const int map_base=PPU_bgTilemapAdr(p,layer),char_base=PPU_bgTileAdr(p,layer);
  const unsigned zlo[]={0x8000,0x7100,PPU_bg3priority(p)?0x1200:0x1200};
  const unsigned zhi[]={0xc000,0xb100,PPU_bg3priority(p)?0xf200:0x3200};
  for(int ty=0;ty<bg->height/8;ty++)for(int tx=0;tx<bg->width/8;tx++) {
    int map=map_base+((ty&31)*32)+(tx&31);
    if(ty&32)map+=PPU_bgTilemapWider(p,layer)?0x800:0x400;
    if(tx&32)map+=0x400;
    uint16_t tile=p->vram[map&0x7fff];
    if(WsShadowLayerActive(layer) && frame_width>256) {
      int hs=p->hScroll[layer]+presentation_bias;
      int screen_x=(tx*8-hs)&(bg->width-1);
      if(screen_x>=(frame_width+256)/2)screen_x-=bg->width;
      int screen_y=(ty*8-p->vScroll[layer])&(bg->height-1);
      if(screen_y>HEIGHT)screen_y-=bg->height;
      if(screen_y<1 && screen_y+8>1)screen_y=1;
      if(screen_x<WsShadowNativeLeft(layer) || screen_x+8>WsShadowNativeRight(layer)) {
        int wx=WsShadowPresentWorldX(layer,screen_x,(uint16_t)hs);
        unsigned wrapped_y=(unsigned)(p->vScroll[layer]+screen_y);
        unsigned wy=WsShadowWorldY(layer)+((wrapped_y-WsShadowScrollY(layer))&0x3ff);
        uint16_t shadow;
        if(wx>=0 && WsShadowLookupWorldTile(layer,(unsigned)wx>>3,wy>>3,&shadow))tile=shadow;
      }
    }
    unsigned pal=(tile&0x1c00)>>palette_shift;
    for(int y=0;y<8;y++)for(int x=0;x<8;x++) {
      int sy=(tile&0x8000)?7-y:y,sx=(tile&0x4000)?x:7-x;
      int a=(char_base+(tile&1023)*words+sy)&0x7fff;
      uint32_t bits=p->vram[a];if(words==16)bits|=(uint32_t)p->vram[(a+8)&0x7fff]<<16;
      bits>>=sx;unsigned index=(bits&1)|((bits>>7)&2)|((bits>>14)&4)|((bits>>21)&8);
      size_t at=(ty*8+y)*bg->width+tx*8+x;
      bg->pixels[at]=index?FullColor(p->cgram[pal+index]):0;
      bg->z[at]=((tile&0x2000)?zhi[layer]:zlo[layer])+pal+index;
    }
  }
  uint8_t atlas_digest[32];
  HdHash(bg->pixels,(size_t)bg->width*bg->height*4,atlas_digest);
  int bind_x=layer==0?(world_camera_x-(frame_width-256)/2)>>5:0;
  int bind_y=layer==0?world_camera_y>>5:0;
  if(!memcmp(atlas_digest,bg->digest,32) && bg->bound_connected==connected_world_active &&
     (!connected_world_active || (bg->bound_x==bind_x && bg->bound_y==bind_y)))return;
  memcpy(bg->digest,atlas_digest,32);
  bg->bound_connected=connected_world_active;bg->bound_x=bind_x;bg->bound_y=bind_y;
  uint32_t chunk[CHUNK*CHUNK],context[(CHUNK+CONTEXT*2)*(CHUNK+CONTEXT*2)];uint16_t indices[CHUNK*CHUNK];
  for(int cy=0;cy<bg->height/CHUNK;cy++)for(int cx=0;cx<bg->width/CHUNK;cx++) {
    for(int y=0;y<CHUNK;y++)memcpy(chunk+y*CHUNK,bg->pixels+(cy*CHUNK+y)*bg->width+cx*CHUNK,CHUNK*4);
    for(int y=0;y<CHUNK;y++)memcpy(indices+y*CHUNK,bg->z+(cy*CHUNK+y)*bg->width+cx*CHUNK,CHUNK*2);
    for(int y=0;y<CHUNK+CONTEXT*2;y++)for(int x=0;x<CHUNK+CONTEXT*2;x++)
      context[y*(CHUNK+CONTEXT*2)+x]=bg->pixels[
          ((cy*CHUNK+y-CONTEXT)&(bg->height-1))*bg->width+
          ((cx*CHUNK+x-CONTEXT)&(bg->width-1))];
    unsigned mask;Material *connected=ConnectedChunk(layer,cx,cy,bg->width,bg->height,chunk,indices,&mask);
    Material *fallback=NULL;
    if(connected_world_active && (!connected || mask!=0xffffu)) {
      uint32_t canonical[CHUNK*CHUNK];
      for(int i=0;i<CHUNK*CHUNK;i++)canonical[i]=chunk[i]?active_world_palette[indices[i]&255]:0;
      // An exact captured center can represent a mixed streaming boundary.
      // It may fill unverified subtiles, but must not replace already verified
      // world art: identical native tiles can have different generated detail
      // at different map positions. Whole-cell replacement causes visible pops.
      uint8_t identity[8+sizeof canonical];memcpy(identity,"DKCWv002",8);memcpy(identity+8,canonical,sizeof canonical);
      Material *alias=GetMaterialKey(canonical,CHUNK,CHUNK,"background",identity,sizeof identity);
      if(alias && alias->hd) {
        if(connected)fallback=alias;
        else {connected=alias;mask=0xffffu;}
      }
    }
    bg->connected[cy*16+cx]=connected!=NULL;bg->connected_mask[cy*16+cx]=(uint16_t)mask;
    bg->fallback_chunks[cy*16+cx]=fallback;
    bg->chunks[cy*16+cx]=connected?connected:GetMaterialKey(chunk,CHUNK,CHUNK,"background",context,sizeof context);
  }
  /* Export whole atlases with surrounding context for seam-free restoration.
   * Chunks are keyed locally, but their HD art is cropped from these atlases. */
  if(export_dir) {
    uint8_t digest[32];char key[65],path[2048];
    memcpy(digest,atlas_digest,32);
    for(int i=0;i<32;i++)snprintf(key+i*2,3,"%02x",digest[i]);
    static char previous[3][65];
    if(strcmp(previous[layer],key)) {
      strcpy(previous[layer],key);
      snprintf(path,sizeof path,"%s/atlas-%d-%s.pam",export_dir,layer,key);WritePam(path,bg->pixels,bg->width,bg->height);
    }
  }
}
static int OamX(const Ppu *p,int n) {
  int x=(p->oam[n*2]&255)|(((p->highOam[n/4]>>((n%4)*2))&1)<<8);
  return x>=256+p->extraRightCur?x-512:x;
}
static int OamSize(const Ppu *p,int n) {
  static const int sizes[8][2]={{8,16},{8,32},{8,64},{16,32},{16,64},{32,64},{16,32},{16,32}};
  return sizes[PPU_objSize(p)&7][(p->highOam[n/4]>>((n%4)*2+1))&1];
}
static int OamFirstSlot(const Ppu *p) {
  /* OAMADDL is a byte address. With priority rotation enabled, the aligned
   * sprite containing that address is evaluated first and wins equal-priority
   * overlaps; otherwise slot zero remains first. */
  return PPU_objPriority(p)?((p->oamaddl&0xfe)>>1):0;
}
static int OamRank(const Ppu *p,int slot) {
  return (slot-OamFirstSlot(p)+128)&127;
}
static bool HoardPrankImpactWord(unsigned word) {
  /* Neutral 8x8 impact tiles used by Diddy's hat-stomp gag. Flips do not
   * change their identity, but palette, priority, name-select, and tile do. */
  word&=0x3fffu;
  return word>=0x31c0u && word<=0x31c6u;
}
static bool HoardPrankDiddyWord(unsigned word) {
  return ((word>>9)&7u)==2u && ((word>>12)&3u)==3u;
}
static bool OamComponentsCompatible(unsigned a,unsigned b,int as,int bs,
                                    bool boxes_overlap) {
  (void)as;(void)bs;
  unsigned different=a^b;
  if(different&0x3800u)return false; /* priority or color-math class */
  if(!(different&0x0600u))return true; /* same OBJ palette */
  /* Diddy's Hoard-room hat stomp interleaves two neutral impact tiles among
   * his palette-2 OAM slots. Group only that exact source-backed pair so the
   * native priority can be reconstructed without joining unrelated actors. */
  return fixed_scene==FIXED_SCENE_HOARD && boxes_overlap &&
      ((HoardPrankImpactWord(a) && HoardPrankDiddyWord(b)) ||
       (HoardPrankImpactWord(b) && HoardPrankDiddyWord(a)));
}
static void PaletteObjectAlias(Material *m,const uint32_t *canonical) {
  if(!m || m->hd || !connected_world_active || !active_world_layers[0].chunks)return;
  size_t bytes=(size_t)m->width*m->height*4;uint8_t digest[32];char key[65];HdHash(canonical,bytes,digest);
  for(int i=0;i<32;i++)snprintf(key+i*2,3,"%02x",digest[i]);
  ResidentMaterial *r=FindResidentMaterial(key);
  if(r && r->width==m->width && r->height==m->height) {
    m->art_original=malloc(bytes);if(!m->art_original)return;
    memcpy(m->art_original,canonical,bytes);m->hd=r->pixels;m->hd_borrowed=true;m->resident_index=(uint32_t)(r-resident_materials);
  } else if(missing_export) {
    char path[2048];snprintf(path,sizeof path,"%s/object-canonical-%s.pam",missing_export,key);WritePam(path,canonical,m->width,m->height);
    snprintf(path,sizeof path,"%s/object-canonical-%s.json",missing_export,key);FILE *f=fopen(path,"w");
    if(f){fprintf(f,"{\"key\":\"%s\",\"kind\":\"object-canonical\",\"width\":%d,\"height\":%d}\n",key,m->width,m->height);fclose(f);}
  }
}
static void DecodeObjects(Ppu *p) {
  /* Build components from touching OAM pieces sharing palette and priority.
   * Pose identity comes from the decoded complete raster, never a slot ID. */
  const int first_slot=OamFirstSlot(p);
  int parent[128],xs[128],ys[128],sizes[128];bool valid[128];
  for(int n=0;n<128;n++) {
    parent[n]=n;xs[n]=OamX(p,n)-presentation_bias;ys[n]=p->oam[n*2]>>8;sizes[n]=OamSize(p,n);
    if(ys[n]+sizes[n]>256)ys[n]-=256;
    valid[n]=ys[n]<224 && ys[n]+sizes[n]>0 && xs[n]+sizes[n]>-(frame_width-256)/2 && xs[n]<(frame_width+256)/2;
  }
  for(int a=0;a<128;a++)if(valid[a])for(int b=a+1;b<128;b++)if(valid[b]) {
    bool overlap=xs[a]<xs[b]+sizes[b] && xs[b]<xs[a]+sizes[a] &&
        ys[a]<ys[b]+sizes[b] && ys[b]<ys[a]+sizes[a];
    if(!OamComponentsCompatible(p->oam[a*2+1],p->oam[b*2+1],
                                sizes[a],sizes[b],overlap))continue;
    if(xs[a]>xs[b]+sizes[b] || xs[b]>xs[a]+sizes[a] || ys[a]>ys[b]+sizes[b] || ys[b]>ys[a]+sizes[a])continue;
    int ra=a,rb=b;while(parent[ra]!=ra)ra=parent[ra];while(parent[rb]!=rb)rb=parent[rb];parent[rb]=ra;
  }
  for(int n=0;n<128;n++){int r=n;while(parent[r]!=r)r=parent[r];parent[n]=r;}
  object_count=0;
  static uint32_t pixels[128*128],trimmed[128*128],canonical[128*128],canonical_trimmed[128*128];
  for(int root=0;root<128;root++)if(valid[root] && parent[root]==root) {
    int x0=999,y0=999,x1=-999,y1=-999,first=128;
    for(int n=0;n<128;n++)if(valid[n] && parent[n]==root){
      if(xs[n]<x0)x0=xs[n];if(ys[n]<y0)y0=ys[n];
      if(xs[n]+sizes[n]>x1)x1=xs[n]+sizes[n];if(ys[n]+sizes[n]>y1)y1=ys[n]+sizes[n];
      int rank=OamRank(p,n);if(rank<first)first=rank;
    }
    int w=x1-x0,h=y1-y0;if(w<1||h<1||w>128||h>128)continue;
    memset(pixels,0,(size_t)w*h*4);
    memset(canonical,0,(size_t)w*h*4);
    /* Match ppu_evaluateSprites: accepted sprites are fetched in reverse
     * evaluation order, so the priority-rotation first slot is painted last. */
    for(int rank=127;rank>=0;rank--){int n=(first_slot+rank)&127;if(valid[n] && parent[n]==root) {
      unsigned a=p->oam[n*2+1];int sz=sizes[n];int base=(a&256)?PPU_objTileAdr2(p):PPU_objTileAdr1(p);
      for(int y=0;y<sz;y++)for(int x=0;x<sz;x++) {
        int tx=(a&0x4000)?sz-1-x:x,ty=(a&0x8000)?sz-1-y:y;
        int tile=(((a&255)/16+ty/8)*16)|(((a&15)+tx/8)&15),addr=(base+tile*16+(ty&7))&0x7fff;
        uint32_t bits=((uint32_t)p->vram[addr]|(uint32_t)p->vram[(addr+8)&0x7fff]<<16)>>(7-(tx&7));
        unsigned ix=(bits&1)|((bits>>7)&2)|((bits>>14)&4)|((bits>>21)&8);
        if(ix){unsigned at=(ys[n]-y0+y)*w+xs[n]-x0+x,index=128+((a>>9)&7)*16+ix;
          pixels[at]=FullColor(p->cgram[index]);canonical[at]=active_world_palette[index];}
      }
    }}
    int lx=w,ly=h,hx=-1,hy=-1;
    for(int y=0;y<h;y++)for(int x=0;x<w;x++)if(pixels[y*w+x]){if(x<lx)lx=x;if(y<ly)ly=y;if(x>hx)hx=x;if(y>hy)hy=y;}
    if(hx<0)continue;int tw=hx-lx+1,th=hy-ly+1;
    for(int y=0;y<th;y++)memcpy(trimmed+y*tw,pixels+(y+ly)*w+lx,tw*4);
    for(int y=0;y<th;y++)memcpy(canonical_trimmed+y*tw,canonical+(y+ly)*w+lx,tw*4);
    Object *o=&objects[object_count++];o->x=x0+lx+(frame_width-256)/2;o->y=y0+ly;o->width=tw;o->height=th;o->first=first;
    o->priority=((p->oam[first*2+1]>>12)&3)*4+2;
    o->math_exempt=!(p->oam[first*2+1]&0x0800);
    o->material=GetMaterial(trimmed,tw,th,"object");
    PaletteObjectAlias(o->material,canonical_trimmed);
  }
  for(unsigned i=1;i<object_count;i++)for(unsigned j=i;j>0 && objects[j].first<objects[j-1].first;j--) {
    Object tmp=objects[j];objects[j]=objects[j-1];objects[j-1]=tmp;
  }
}
/* Explicit HD presentation capabilities; never change cartridge streaming. */
static bool HdCaveSceneEligible(const uint8_t *w) {
  return Dkc1WramU16(w,0x003e)==0x06 && Dkc1WramU16(w,0x0030)==0x09 &&
      Dkc1WramU16(w,0x0032)==1 && w[0xd5]==0xda && Dkc1WramU16(w,0xd3)==0 &&
      Dkc1WramU16(w,0x1b11)==0xbf00 && Dkc1WramU16(w,0x1b23)==0x6900 &&
      Dkc1WramU16(w,0x1b25)==0x6c00;
}
static bool HdHoardSceneEligible(const uint8_t *w) {
  return Dkc1HdHoardSceneEligible(w);
}
static bool HdTreehouseSceneEligible(const uint8_t *w) {
  return Dkc1HdTreehouseSceneEligible(w);
}
static bool HdCaveMathEligible(const uint8_t *w) {
  /* Bonus 1 and the Banana Hoard share CGWSEL=$10 / CGADSUB=$93 with an
   * inverted empty color window. The hoard is not the $6900 cave world. */
  return HdCaveSceneEligible(w) || HdHoardSceneEligible(w);
}
static bool HdSceneEligible(const uint8_t *w) {
  if(Dkc1WramU16(w,0x003e)==0x16)return true;
  /* Bonus exit selects another entrance into the same proven Jungle map. */
  if(Dkc1WramU16(w,0x0030)==0 && Dkc1WramU16(w,0x0032)==0 &&
      w[0xd5]==0xd9 && Dkc1WramU16(w,0xd3)==0 && Dkc1WramU16(w,0x1b11)==0xa3c0 &&
      Dkc1WramU16(w,0x1b23)==0 && Dkc1WramU16(w,0x1b25)==5120)return true;
  return HdCaveSceneEligible(w) || HdHoardSceneEligible(w) ||
      HdTreehouseSceneEligible(w);
}
/* Empty color-window spans are uniform. Inverting an enabled empty window
 * covers the whole line, matching PpuWindows_CalcWithExtra. */
static bool HdUniformColorWindow(const Ppu *p) {
  unsigned flags=GET_WINDOW_FLAGS(p,5);
  bool window1=(flags&2) && p->window1left<=p->window1right;
  bool window2=(flags&8) && p->window2left<=p->window2right;
  return PPU_preventMathMode(p)==1 && !window1 && !window2;
}
static uint8_t HdMathFlags(const Ppu *p) {
  if(HdUniformColorWindow(p)) {
    unsigned flags=GET_WINDOW_FLAGS(p,5);
    bool inside=(flags&3)==3 || (flags&12)==12;
    return inside?p->cgadsub:0;
  }
  return p->cgadsub;
}
bool Dkc1HdScenePrepare(Ppu *p,const uint8_t *w,int bias) {
  if(!initialized){initialized=true;const char *on=getenv("DKC1_HD_SCENE");scene_enabled=on&&!strcmp(on,"1");export_dir=getenv("DKC1_HD_SCENE_EXPORT");pack_dir=getenv("DKC1_HD_SCENE_PACK");const char *preload=getenv("DKC1_HD_SCENE_PRELOAD");preload_requested=preload&&!strcmp(preload,"1");const char *trace=getenv("DKC1_HD_SCENE_TRACE");if(trace)scene_trace=fopen(trace,"w");}
  static bool audit_initialized;
  if(!audit_initialized){audit_initialized=true;const char *audit=getenv("DKC1_HD_SCENE_AUDIT");if(audit)audit_trace=fopen(audit,"w");
    const char *centers=getenv("DKC1_HD_EXACT_CENTERS");exact_centers_enabled=centers&&!strcmp(centers,"1");
    const char *world=getenv("DKC1_HD_CONNECTED_WORLD");connected_world_enabled=world&&!strcmp(world,"1");
    missing_export=getenv("DKC1_HD_MISSING_EXPORT");const char *coverage=getenv("DKC1_HD_COVERAGE_TRACE");if(coverage)coverage_trace=fopen(coverage,"w");}
  coverage_frame++;coverage_entrance=Dkc1WramU16(w,0x003e);coverage_camera_x=Dkc1WramU16(w,0x088b);coverage_camera_y=Dkc1WramU16(w,0x0895);
  bool cave_scene=HdCaveSceneEligible(w);
  bool cave_math=HdCaveMathEligible(w);
  bool treehouse_scene=HdTreehouseSceneEligible(w);
  hoard_margin_mirror=false;
  fixed_scene=FIXED_SCENE_NONE;
  if(scene_enabled && preload_requested && !preload_attempted) {
    preload_attempted=true;
    if(PreloadResidentPack()) {
      fprintf(stderr,"[hd-scene] preloaded %u materials (%llu bytes); gameplay texture reads disabled\n",resident_count,(unsigned long long)resident_bytes);
      if(exact_centers_enabled){bool loaded=LoadCenterAliases();fprintf(stderr,"[hd-scene] exact center index %s (%u entries)\n",loaded?"loaded":"unavailable",center_alias_count);}
      {bool loaded=LoadObjectSilhouettes();fprintf(stderr,"[hd-scene] object silhouette index %s (%u entries)\n",loaded?"loaded":"unavailable",object_silhouette_count);}
      LoadFixedPlates();
      fprintf(stderr,"[hd-scene] fixed plates hoard=%s treehouse=%s\n",
          hoard_fixed_plate?"loaded":"unavailable",
          treehouse_fixed_plate?"loaded":"unavailable");
      if(connected_world_enabled) {
        fprintf(stderr,"[hd-scene] connected world %s\n",LoadConnectedWorld()?"loaded":"unavailable");
        fprintf(stderr,"[hd-scene] cave world %s\n",LoadWorldFile("connected-cave.bin",cave_world_layers,cave_world_palette)?"loaded":"unavailable");
      }
    }
    else fprintf(stderr,"[hd-scene] preload failed; using original pixels without gameplay texture reads\n");
  }
  active=false;ready=false;if(!scene_enabled&&!export_dir&&!audit_trace)return false;
  bool ordinary_composition=!PPU_addSubscreen(p) && !PPU_clipMode(p) &&
      !(p->cgadsub && PPU_preventMathMode(p) &&
      !(cave_math && HdUniformColorWindow(p)));
  bool fixed_composition=treehouse_scene && treehouse_fixed_plate;
  if(!HdSceneEligible(w) || PPU_mode(p)!=1 || PPU_brightness(p)==0 || PPU_forcedBlank(p) || PPU_mosaicSize(p)>1 || (p->bgmode&0x70) || p->screenWindowed[0] || (!ordinary_composition && !fixed_composition)) {
    if(coverage_trace){fprintf(coverage_trace,"{\"frame\":%lu,\"supported\":false,\"entrance\":%u,\"camera\":[%u,%u],\"reason\":\"scene_guard\",\"eligible_scene\":%s,\"bgmode\":%u,\"inidisp\":%u,\"mosaic\":%u,\"window_main\":%u,\"cgwsel\":%u,\"cgadsub\":%u}\n",coverage_frame,Dkc1WramU16(w,0x003e),coverage_camera_x,coverage_camera_y,HdSceneEligible(w)?"true":"false",p->bgmode,p->inidisp,p->mosaic,p->screenWindowed[0],p->cgwsel,p->cgadsub);fflush(coverage_trace);}
    return scene_enabled;
  }
  frame_width=(int)p->renderPitch/4;if(frame_width<256||frame_width>WIDTH)return scene_enabled;
  frame_brightness=PPU_brightness(p);
  presentation_bias=bias;
  hoard_margin_mirror=HdHoardSceneEligible(w);
  if(treehouse_scene && treehouse_fixed_plate)fixed_scene=FIXED_SCENE_TREEHOUSE;
  else if(hoard_margin_mirror && hoard_fixed_plate)fixed_scene=FIXED_SCENE_HOARD;
  if(active_world_layers!=(cave_scene?cave_world_layers:world_layers))
    for(int l=0;l<3;l++)memset(backgrounds[l].digest,0,sizeof backgrounds[l].digest);
  active_world_layers=cave_scene?cave_world_layers:world_layers;
  active_world_palette=cave_scene?cave_world_palette:world_palette;
  active_world_origin_x=cave_scene?0x6900:0;
  connected_world_active=!treehouse_scene && connected_world_enabled && active_world_layers[0].chunks &&
      (cave_scene || (w[0xd5]==0xd9 &&
      Dkc1WramU16(w,0xd3)==0 && Dkc1WramU16(w,0x1b11)==0xa3c0 &&
      Dkc1WramU16(w,0x1b23)==0 && Dkc1WramU16(w,0x1b25)==5120));
  /* WRAM's next camera can lead the PPU by one VBlank. Lift the actual
   * presentation scroll into that camera's neighborhood before choosing cells. */
  int rw=PPU_bgTilemapWider(p,0)?512:256,rh=PPU_bgTilemapHigher(p,0)?512:256;
  world_camera_x=(int)coverage_camera_x+(((int)p->hScroll[0]-(int)coverage_camera_x+rw/2)&(rw-1))-rw/2+bias;
  world_camera_y=(int)coverage_camera_y+(((int)p->vScroll[0]-(int)coverage_camera_y+rh/2)&(rh-1))-rh/2;
  BeginMaterialFrame();
  if(!treehouse_scene)for(int l=0;l<3;l++)DecodeBackground(p,l);
  DecodeObjects(p);active=true;
  if(export_dir && frame_number==0){char path[2048];snprintf(path,sizeof path,"%s/scene.json",export_dir);FILE*f=fopen(path,"w");if(f){fprintf(f,"{\"bgsc\":[%u,%u,%u],\"bgTileAdr\":%u,\"main\":%u,\"sub\":%u,\"cgadsub\":%u,\"cgwsel\":%u,\"objects\":%u}\n",p->bgXsc[0],p->bgXsc[1],p->bgXsc[2],p->bgTileAdr,p->screenEnabled[0],p->screenEnabled[1],p->cgadsub,p->cgwsel,object_count);fclose(f);}
    /* Atomic source companion for this exact atlas export. Read-only PPU/WRAM
     * data permits offline ROM-map coverage instead of relying on one route. */
    const char *names[]={"source-vram.bin","source-cgram.bin","source-wram.bin"};
    const void *data[]={p->vram,p->cgram,w};const size_t sizes[]={0x10000,0x200,0x20000};
    for(int i=0;i<3;i++){snprintf(path,sizeof path,"%s/%s",export_dir,names[i]);f=fopen(path,"wb");if(f){fwrite(data[i],1,sizes[i],f);fclose(f);}}
  }
  frame_number++;return scene_enabled;
}
void Dkc1HdSceneCaptureLine(Ppu *p,int line) {
  if(!active||line<1||line>HEIGHT)return;
  /* HDMA may change the window registers after Prepare. A spatial color
   * window still requires stock output rather than an unverified HD mask. */
  if(fixed_scene!=FIXED_SCENE_TREEHOUSE && p->cgadsub &&
     PPU_preventMathMode(p) && !HdUniformColorWindow(p)){active=false;return;}
  const int y=line-1,extra=(frame_width-256)/2;
  for(int l=0;l<3;l++){scroll_x[l][y]=p->hScroll[l];scroll_y[l][y]=p->vScroll[l];}
  memcpy(main_z+y*frame_width,p->bgBuffers[0].data+kPpuExtraLeftRight-extra,frame_width*2);
  memcpy(sub_z+y*frame_width,p->bgBuffers[1].data+kPpuExtraLeftRight-extra,frame_width*2);
  main_enable[y]=p->screenEnabled[0];sub_enable[y]=p->screenEnabled[1];math_flags[y]=HdMathFlags(p);math_select[y]=p->cgwsel;fixed_color[y]=p->fixedColor;backdrop[y]=FullColor(p->cgram[0]);
  for(int i=0;i<256;i++)palette[y][i]=FullColor(p->cgram[i]);
}
void Dkc1HdSceneFinish(Ppu *p) {
  if(active){memcpy(original,p->renderBuffer,(size_t)frame_width*HEIGHT*4);ready=true;
    /* Restore the HDMA sky as a continuous gradient in the HD presentation.
     * The native palette and the exact low-resolution oracle stay unchanged. */
    uint32_t smooth[HEIGHT];
    for(int y=0;y<HEIGHT;y++) {
      smooth[y]=0xff000000u;
      for(int ch=0;ch<3;ch++) {
        unsigned value=0;
        for(int dy=-2;dy<=2;dy++){int sy=y+dy;if(sy<0)sy=0;if(sy>=HEIGHT)sy=HEIGHT-1;value+=((backdrop[sy]>>(ch*8))&255)*(3-abs(dy));}
        smooth[y]|=((value+4)/9)<<(ch*8);
      }
    }
    for(int y=0;y<HEIGHT*SCALE;y++) {
      int position=y*64-96;if(position<0)position=0;
      int lo=position>>8,hi=lo+1,fraction=position&255;if(hi>=HEIGHT)hi=HEIGHT-1;
      high_backdrop[y]=0xff000000u;
      for(int ch=0;ch<3;ch++) {
        unsigned a=(smooth[lo]>>(ch*8))&255,b=(smooth[hi]>>(ch*8))&255;
        high_backdrop[y]|=((a*(256-fraction)+b*fraction+128)>>8)<<(ch*8);
      }
    }
    if(audit_trace){
      unsigned hits=0,misses=0,mismatch=0;
      for(int y=0;y<HEIGHT;y++)for(int x=0;x<frame_width;x++)
        if((Pixel(x,y,0,0,false,&hits,&misses)^original[y*frame_width+x])&0xffffffu) {
          if(mismatch<8 && getenv("DKC1_HD_SCENE_DEBUG"))fprintf(stderr,"hd-audit frame=%lu x=%d y=%d native=%06x decoded=%06x z=%04x\n",frame_number,x,y,original[y*frame_width+x]&0xffffff,Pixel(x,y,0,0,false,&hits,&misses)&0xffffff,main_z[y*frame_width+x]);
          mismatch++;
        }
      fprintf(audit_trace,"{\"frame\":%lu,\"mismatch_pixels\":%u}\n",frame_number,mismatch);fflush(audit_trace);
    }
    if(coverage_trace)TraceCoverage();
  }
}

typedef struct Fragment { uint32_t color,low; int priority,layer; } Fragment;
static uint32_t Blend(uint32_t front,uint32_t back) {
  unsigned a=front>>24;if(a==255)return front;if(!a)return back;
  if(!(back>>24))return front;
  unsigned b_alpha=(back>>24)*(255-a),denom=a*255+b_alpha;
  uint32_t result=((denom+127)/255)<<24;
  for(int ch=0;ch<3;ch++) {
    int shift=ch*8;
    result|=((((front>>shift)&255)*a*255+((back>>shift)&255)*b_alpha+denom/2)/denom)<<shift;
  }
  return result;
}
static uint32_t Shade(uint32_t color,uint32_t low,int layer,int y) {
  if(layer>=6 || !(math_flags[y]&(1u<<layer)))return color;
  uint32_t result=color&0xff000000u;
  for(int c=0;c<3;c++) {
    int shift=c*8,raw=(low>>shift)&255,value=(color>>shift)&255;
    int fix=(fixed_color[y]>>(c*5))&31;
    /* PPU words are RGB; host words are BGRA. */
    fix=(fixed_color[y]>>((2-c)*5))&31;
    int v5=raw>>3;int exact=(math_flags[y]&128)?v5-fix:v5+fix;
    if(exact<0)exact=0;if(math_flags[y]&64)exact/=2;if(exact>31)exact=31;
    exact=(exact<<3)|(exact>>2);
    int correction=value-raw;if(math_flags[y]&64)correction/=2;
    int out=exact+correction;if(out<0)out=0;if(out>255)out=255;
    result|=(uint32_t)out<<shift;
  }
  return result;
}
static uint32_t MaterialPixel(Material *m,int x,int y,int dx,int dy,bool high) {
  if(!m || x<0 || y<0 || x>=m->width || y>=m->height)return 0;
  if(high && m->hd)return m->hd[(y*SCALE+dy)*(m->width*SCALE)+x*SCALE+dx];
  return m->original[y*m->width+x];
}
static Material *BackgroundMaterial(const Background *bg,int tx,int ty) {
  unsigned chunk=(ty/CHUNK)*16+tx/CHUNK;Material *m=bg->chunks[chunk];
  if(bg->connected[chunk] && !(bg->connected_mask[chunk]&(1u<<((ty%32/8)*4+tx%32/8))))
    m=bg->fallback_chunks[chunk];
  return m;
}
static bool BackgroundHd(const Background *bg,int tx,int ty) {
  Material *m=BackgroundMaterial(bg,tx,ty);return m && m->hd;
}
/* Map a widescreen column onto the authored 256 when the Banana Hoard
 * widens. Left wall reflects; the right opening continues its last column.
 * Objects stay in screen space. */
static void HoardBgSample(int x,int dx,int *sample_x,int *sample_dx) {
  *sample_x=x;*sample_dx=dx;
  if(!hoard_margin_mirror)return;
  int extra=(frame_width-256)/2;
  int native=x-extra;
  int source=Dkc1LockedInteriorSourceX(native);
  if(source==native)return;
  *sample_x=extra+source;
  if(Dkc1LockedInteriorSourceFlipped(native))*sample_dx=SCALE-1-dx;
}
static uint32_t FixedPlatePixel(int x,int y,int dx,int dy) {
  const uint32_t *plate=fixed_scene==FIXED_SCENE_HOARD?hoard_fixed_plate:
      fixed_scene==FIXED_SCENE_TREEHOUSE?treehouse_fixed_plate:NULL;
  if(!plate)return 0;
  int wide_x=x*SCALE+dx+(WIDTH-frame_width)*SCALE/2;
  return plate[(y*SCALE+dy)*(WIDTH*SCALE)+wide_x];
}
static bool FixedHoardHudObject(const Object *o,bool high) {
  return high && fixed_scene==FIXED_SCENE_HOARD && o->y<64;
}
static bool FixedHoardSignObject(const Object *o,bool high) {
  return high && fixed_scene==FIXED_SCENE_HOARD && o->width==53 &&
      o->height==60 && o->y>=128 && o->x<frame_width/2;
}
static uint32_t Pixel(int x,int y,int dx,int dy,bool high,unsigned *hits,unsigned *misses) {
  if(fixed_scene==FIXED_SCENE_TREEHOUSE && !high)
    return original[y*frame_width+x];
  Fragment f[4];int count=0;
  uint32_t plate=high?FixedPlatePixel(x,y,dx,dy):0;
  int bg_x=x,bg_dx=dx;HoardBgSample(x,dx,&bg_x,&bg_dx);
  for(int l=0;l<3 && fixed_scene!=FIXED_SCENE_TREEHOUSE;l++)if(main_enable[y]&(1<<l)) {
    Background *bg=&backgrounds[l];
    int tx=(bg_x-(frame_width-256)/2+scroll_x[l][y])&(bg->width-1);
    int ty=(y+1+scroll_y[l][y])&(bg->height-1);
    size_t at=(size_t)ty*bg->width+tx;
    Material *m=BackgroundMaterial(bg,tx,ty);
    bool hd=high && BackgroundHd(bg,tx,ty);
    uint32_t c=hd?MaterialPixel(m,tx%CHUNK,ty%CHUNK,bg_dx,dy,true):(bg->chunks[(ty/CHUNK)*16+tx/CHUNK]?bg->pixels[at]:0);
    if(high && dx==0 && dy==0 && bg->pixels[at]) {if(hd)(*hits)++;else (*misses)++;}
    uint32_t low=bg->pixels[at];
    if(low) {
      if(hd && bg->connected[(ty/CHUNK)*16+tx/CHUNK])low=active_world_palette[bg->z[at]&255];
      uint32_t current=palette[y][bg->z[at]&255],corrected=c&0xff000000u;
      for(int ch=0;ch<3;ch++){int shift=ch*8;int v=((c>>shift)&255)+((current>>shift)&255)-((low>>shift)&255);if(v<0)v=0;if(v>255)v=255;corrected|=(uint32_t)v<<shift;}
      c=corrected;low=current;
    }
    if(c>>24)f[count++]=(Fragment){c,low,bg->z[at]>>12,l};
  }
  uint32_t object=0,object_low=0,hud=0,hud_low=0;
  int priority=0,object_layer=4,hud_layer=6;
  /* Components are ordered by their lowest OAM slot. Reverse compositing
   * preserves the SNES's lower-slot-wins rule before BG priority is applied. */
  for(int i=(int)object_count-1;i>=0;i--) {
    Object *o=&objects[i];int ox=x-o->x,oy=y-o->y;
    if(ox<0||oy<0||ox>=o->width||oy>=o->height)continue;
    /* The fixed Hoard plate contains the separately authored HD sign.  Do
     * not paint the original 53x60 OAM raster back over it. */
    if(FixedHoardSignObject(o,high))continue;
    uint32_t c=MaterialPixel(o->material,ox,oy,dx,dy,high);
    uint32_t low=MaterialPixel(o->material,ox,oy,0,0,false);
    if(high && low && o->material && o->material->art_original) {
      uint32_t base=o->material->art_original[oy*o->width+ox],adjusted=c&0xff000000u;
      for(int ch=0;ch<3;ch++){int shift=ch*8,v=((c>>shift)&255)+((low>>shift)&255)-((base>>shift)&255);if(v<0)v=0;if(v>255)v=255;adjusted|=(uint32_t)v<<shift;}c=adjusted;
    }
    if(c>>24) {
      /* The Hoard foreground legitimately wins over these top-of-screen OAM
       * components in the stock priority graph.  Its HD presentation plate
       * would otherwise leave only the few pixels crossing the plate edge.
       * Keep the live HUD above the presentation-only room art while leaving
       * the low-resolution reconstruction byte-exact. */
      if(FixedHoardHudObject(o,high)) {
        hud=Blend(c,hud);hud_low=low;hud_layer=o->math_exempt?6:4;
      } else {
        object=Blend(c,object);object_low=low;priority=o->priority;
        object_layer=fixed_scene==FIXED_SCENE_TREEHOUSE?6:(o->math_exempt?6:4);
      }
    }
    if(high && dx==0 && dy==0 && low){if(o->material && o->material->hd)(*hits)++;else (*misses)++;}
  }
  bool plate_replaces_background=(plate>>24) && fixed_scene!=FIXED_SCENE_NONE;
  if((object>>24) && !plate_replaces_background)
    f[count++]=(Fragment){object,object_low,priority,object_layer};
  for(int a=1;a<count;a++)for(int b=a;b>0 && f[b].priority<f[b-1].priority;b--){Fragment t=f[b];f[b]=f[b-1];f[b-1]=t;}
  uint32_t color=fixed_scene==FIXED_SCENE_TREEHOUSE && high?
      (plate>>24?plate:original[y*frame_width+x]):
      (high?high_backdrop[y*SCALE+dy]:backdrop[y]);
  for(int i=0;i<count;i++)color=Blend(Shade(f[i].color,f[i].low,f[i].layer,y),color);
  if(fixed_scene==FIXED_SCENE_HOARD && plate>>24)color=Blend(plate,color);
  if(plate_replaces_background && object>>24)
    color=Blend(Shade(object,object_low,object_layer,y),color);
  if(hud>>24)color=Blend(Shade(hud,hud_low,hud_layer,y),color);
  return ApplyBrightness(color);
}
static void TraceCoverage(void) {
  unsigned samples[4]={0},missing[4]={0},visible[4]={0},visible_missing[4]={0},mismatch=0;
  for(int y=0;y<HEIGHT;y++)for(int x=0;x<frame_width;x++) {
    unsigned hit=0,miss=0;
    if((Pixel(x,y,0,0,false,&hit,&miss)^original[y*frame_width+x])&0xffffffu){mismatch++;continue;}
    int top=-1,priority=-1;bool top_missing=false;
    if(fixed_scene==FIXED_SCENE_TREEHOUSE) {
      samples[0]++;top=0;priority=0;
    }
    for(int l=0;l<3 && fixed_scene!=FIXED_SCENE_TREEHOUSE;l++)if(main_enable[y]&(1<<l)) {
      Background *bg=&backgrounds[l];
      int cover_x=x,cover_dx=0;HoardBgSample(x,0,&cover_x,&cover_dx);
      int tx=(cover_x-(frame_width-256)/2+scroll_x[l][y])&(bg->width-1);
      int ty=(y+1+scroll_y[l][y])&(bg->height-1);size_t at=(size_t)ty*bg->width+tx;
      if(!bg->pixels[at])continue;
      bool absent=!BackgroundHd(bg,tx,ty);
      samples[l]++;missing[l]+=absent;
      int z=bg->z[at]>>12;if(z>=priority){top=l;priority=z;top_missing=absent;}
    }
    Object *front=NULL;
    for(unsigned i=0;i<object_count;i++) {
      Object *o=&objects[i];int ox=x-o->x,oy=y-o->y;
      if(MaterialPixel(o->material,ox,oy,0,0,false)) {
        samples[3]++;missing[3]+=!o->material||!o->material->hd;
        if(!front)front=o;
      }
    }
    if(front && front->priority>=priority){top=3;top_missing=!front->material||!front->material->hd;}
    if(top>=0){visible[top]++;visible_missing[top]+=top_missing;}
  }
  const char *plate=fixed_scene==FIXED_SCENE_HOARD?"hoard":
      fixed_scene==FIXED_SCENE_TREEHOUSE?"treehouse":"none";
  fprintf(coverage_trace,"{\"frame\":%lu,\"hd_frame\":%lu,\"supported\":true,\"entrance\":%u,\"camera\":[%u,%u],\"fixed_plate\":\"%s\",\"native_mismatch_pixels\":%u,\"source_samples\":[%u,%u,%u,%u],\"missing_samples\":[%u,%u,%u,%u],\"visible_pixels\":[%u,%u,%u,%u],\"visible_missing\":[%u,%u,%u,%u],\"center_alias_hits\":%llu,\"object_silhouette_hits\":%llu}\n",
    coverage_frame,frame_number,coverage_entrance,coverage_camera_x,coverage_camera_y,plate,mismatch,
    samples[0],samples[1],samples[2],samples[3],missing[0],missing[1],missing[2],missing[3],
    visible[0],visible[1],visible[2],visible[3],visible_missing[0],visible_missing[1],visible_missing[2],visible_missing[3],(unsigned long long)center_alias_hits,(unsigned long long)object_silhouette_hits);
  fflush(coverage_trace);
}
typedef struct RenderStats {
  unsigned mismatch,hits,misses;int left,right,top,bottom;
  uint8_t padding[36];
} RenderStats;
typedef struct RenderJob { const uint32_t *native;int width;RenderStats stats[16]; } RenderJob;
static void RenderBand(void *context,size_t band) {
  RenderJob *job=context;int width=job->width;
  RenderStats *s=&job->stats[band];s->left=width;s->top=HEIGHT;
  for(int y=(int)band*14;y<(int)(band+1)*14;y++)for(int x=0;x<width;x++) {
    const bool exact=((Pixel(x,y,0,0,false,&s->hits,&s->misses)^job->native[y*width+x])&0xffffffu)==0;
    if(!exact){s->mismatch++;if(x<s->left)s->left=x;if(x>s->right)s->right=x;if(y<s->top)s->top=y;if(y>s->bottom)s->bottom=y;}
    for(int dy=0;dy<SCALE;dy++)for(int dx=0;dx<SCALE;dx++)
      output[(y*SCALE+dy)*(width*SCALE)+x*SCALE+dx]=
          exact?Pixel(x,y,dx,dy,true,&s->hits,&s->misses):job->native[y*width+x];
  }
}
const uint32_t *Dkc1HdScenePresent(const uint32_t *native,int width,int height) {
  if(!scene_enabled||!active||!ready||width!=frame_width||height!=HEIGHT ||
      memcmp(native,original,(size_t)width*height*4))return NULL;
  static unsigned long output_frame;
  if(output_frame==frame_number)return output;
  output_frame=frame_number;
  RenderJob job={.native=native,.width=width};
  /* Workers read one immutable completed frame and write disjoint bands.
   * No guest execution, PPU calls, palette changes, or asset mutation occurs. */
#ifdef __APPLE__
  dispatch_apply_f(16,dispatch_get_global_queue(QOS_CLASS_USER_INTERACTIVE,0),&job,RenderBand);
#else
  for(size_t band=0;band<16;band++)RenderBand(&job,band);
#endif
  unsigned mismatch=0,hits=0,misses=0;int left=width,right=0,top=height,bottom=0;
  for(int i=0;i<16;i++){
    RenderStats *s=&job.stats[i];mismatch+=s->mismatch;hits+=s->hits;misses+=s->misses;
    if(s->mismatch){if(s->left<left)left=s->left;if(s->right>right)right=s->right;if(s->top<top)top=s->top;if(s->bottom>bottom)bottom=s->bottom;}
  }
  if(scene_trace){const char *plate=fixed_scene==FIXED_SCENE_HOARD?"hoard":fixed_scene==FIXED_SCENE_TREEHOUSE?"treehouse":"none";fprintf(scene_trace,"{\"frame\":%lu,\"fixed_plate\":\"%s\",\"mismatch_pixels\":%u,\"mismatch_bbox\":[%d,%d,%d,%d],\"material_hits\":%u,\"material_misses\":%u,\"objects\":%u,\"cache_entries\":%u,\"cache_evictions\":%llu,\"cache_failures\":%llu,\"resident_materials\":%u,\"resident_bytes\":%llu,\"material_file_reads\":%llu}\n",frame_number,plate,mismatch,left,top,right,bottom,hits,misses,object_count,material_count,(unsigned long long)material_evictions,(unsigned long long)material_failures,resident_count,(unsigned long long)resident_bytes,(unsigned long long)material_file_reads);fflush(scene_trace);}
  return output;
}

unsigned Dkc1HdSceneResidentCount(void) { return resident_count; }
uint64_t Dkc1HdSceneResidentBytes(void) { return resident_bytes; }
const uint32_t *Dkc1HdSceneResidentPixels(unsigned index,int *width,int *height) {
  if(index>=resident_count)return NULL;
  *width=resident_materials[index].width*SCALE;
  *height=resident_materials[index].height*SCALE;
  return resident_materials[index].pixels;
}
size_t Dkc1HdSceneGpuFrameSize(const uint32_t *native,int width,int height) {
  if(fixed_scene!=FIXED_SCENE_NONE || !Dkc1HdEnabled() || !resident_count || !scene_enabled || !active || !ready ||
     width!=frame_width || height!=HEIGHT || !native ||
     memcmp(native,original,(size_t)width*height*4))return 0;
  size_t pixels=0;
  for(unsigned i=0;i<object_count;i++) {
    Material *m=objects[i].material;
    if(m)pixels+=(size_t)m->width*m->height*(m->art_original?2:1);
  }
  return sizeof(HdGpuFrame)+(pixels ? pixels : 1)*4;
}
bool Dkc1HdSceneCopyGpuFrame(void *destination,size_t size) {
  size_t required=Dkc1HdSceneGpuFrameSize(original,frame_width,HEIGHT);
  if(!required || !destination || size<required)return false;
  HdGpuFrame *f=destination;
  f->width=frame_width;f->object_count=object_count;f->sequence=(uint32_t)frame_number;
  f->brightness=(uint32_t)frame_brightness;
  for(int l=0;l<3;l++) {
    Background *bg=&backgrounds[l];size_t pixels=(size_t)bg->width*bg->height;
    f->bg_width[l]=bg->width;f->bg_height[l]=bg->height;
    memcpy(f->bg_pixels[l],bg->pixels,pixels*4);memcpy(f->bg_z[l],bg->z,pixels*2);
    for(int cy=0;cy<bg->height/CHUNK;cy++)for(int cx=0;cx<bg->width/CHUNK;cx++) {
      int at=cy*16+cx;Material *m=bg->chunks[at];
      f->bg_valid[l][at]=m!=NULL;
      f->bg_material[l][at]=m && m->hd_borrowed?m->resident_index:UINT32_MAX;
      Material *fallback=bg->fallback_chunks[at];
      f->bg_fallback_material[l][at]=fallback && fallback->hd_borrowed?fallback->resident_index:UINT32_MAX;
      f->bg_connected[l][at]=bg->connected[at]?(0x10000u|bg->connected_mask[at]):0;
    }
  }
  memcpy(f->scroll_x,scroll_x,sizeof scroll_x);memcpy(f->scroll_y,scroll_y,sizeof scroll_y);
  memcpy(f->backdrop,backdrop,sizeof backdrop);memcpy(f->high_backdrop,high_backdrop,sizeof high_backdrop);
  memcpy(f->palette,palette,sizeof palette);
  memcpy(f->world_palette,active_world_palette,256*4);
  memcpy(f->native,original,(size_t)frame_width*HEIGHT*4);
  for(int y=0;y<HEIGHT;y++) {
    f->main_enable[y]=main_enable[y];f->math_flags[y]=math_flags[y];f->fixed_color[y]=fixed_color[y];
  }
  uint32_t *low=(uint32_t *)(f+1);unsigned offset=0;
  for(unsigned i=0;i<object_count;i++) {
    Object *o=&objects[i];Material *m=o->material;
    f->objects[i]=(HdGpuObject){o->x,o->y,o->width,o->height,o->priority,offset,
      m && m->hd_borrowed?m->resident_index:UINT32_MAX,m!=NULL,UINT32_MAX,o->math_exempt};
    if(m){size_t n=(size_t)m->width*m->height;memcpy(low+offset,m->original,n*4);offset+=(unsigned)n;}
    if(m && m->art_original){size_t n=(size_t)m->width*m->height;f->objects[i].art_offset=offset;memcpy(low+offset,m->art_original,n*4);offset+=(unsigned)n;}
  }
  f->object_pixels=offset;return true;
}
