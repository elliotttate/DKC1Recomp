/* Real preload/cache ownership and no-gameplay-I/O contract under ASan/UBSan. */
#include "../runner/dkc1_hd_scene.c"
#include <assert.h>

int main(int argc,char **argv) {
  /* Cave opt-in identity and uniform empty/inverted color-window semantics. */
  uint8_t scene[0x20000]={0};assert(!HdSceneEligible(scene));
  scene[0x3e]=0x16;assert(HdSceneEligible(scene));
  scene[0x3e]=6;scene[0x30]=9;scene[0x32]=1;scene[0xd5]=0xda;
  scene[0x1b12]=0xbf;scene[0x1b24]=0x69;scene[0x1b26]=0x6c;
  assert(HdSceneEligible(scene));scene[0x30]=10;assert(!HdSceneEligible(scene));scene[0x30]=9;
  scene[0x1b26]=0x6d;assert(!HdSceneEligible(scene));
  scene[0x3e]=8;scene[0x30]=scene[0x32]=0;scene[0xd5]=0xd9;
  scene[0x1b11]=0xc0;scene[0x1b12]=0xa3;scene[0x1b24]=0;scene[0x1b26]=0x14;
  assert(HdSceneEligible(scene));scene[0x3e]=6;assert(HdSceneEligible(scene) && !HdCaveSceneEligible(scene));scene[0xd5]=0xda;assert(!HdSceneEligible(scene));
  memset(scene,0,sizeof scene);
  scene[0x3e]=0x47;scene[0x30]=0x2d;scene[0x32]=1;scene[0xd5]=0xda;
  scene[0x1b12]=0xbf;scene[0x1b24]=0xb0;scene[0x1b26]=0xb0;
  assert(HdHoardSceneEligible(scene) && HdSceneEligible(scene) &&
         !HdCaveSceneEligible(scene) && HdCaveMathEligible(scene));
  scene[0x30]=0x2e;assert(!HdSceneEligible(scene));scene[0x30]=0x2d;
  scene[0x1b26]=0xb1;assert(!HdHoardSceneEligible(scene)&&!HdSceneEligible(scene));
  memset(scene,0,sizeof scene);
  scene[0x3e]=0x5c;scene[0x30]=0x40;scene[0x32]=0x0d;scene[0xd5]=0xe3;
  scene[0x1b12]=0x02;
  assert(HdTreehouseSceneEligible(scene) && HdSceneEligible(scene) &&
         !HdCaveSceneEligible(scene) && !HdCaveMathEligible(scene));
  scene[0x30]=0x41;assert(!HdTreehouseSceneEligible(scene)&&!HdSceneEligible(scene));
  {Object hud={.y=10},world={.y=64},sign={.x=101,.y=142,.width=53,.height=60};
   fixed_scene=FIXED_SCENE_HOARD;frame_width=342;
   assert(FixedHoardHudObject(&hud,true));
   assert(!FixedHoardHudObject(&hud,false) && !FixedHoardHudObject(&world,true));
   assert(FixedHoardSignObject(&sign,true));
   assert(!FixedHoardSignObject(&sign,false));sign.width=52;
   assert(!FixedHoardSignObject(&sign,true));
   fixed_scene=FIXED_SCENE_TREEHOUSE;assert(!FixedHoardHudObject(&hud,true));
   fixed_scene=FIXED_SCENE_NONE;}
  {Ppu order={0};
   assert(OamFirstSlot(&order)==0 && OamRank(&order,0)==0 && OamRank(&order,127)==127);
   order.oamaddh=0x80;order.oamaddl=0xfc;
   assert(OamFirstSlot(&order)==126 && OamRank(&order,126)==0 &&
          OamRank(&order,127)==1 && OamRank(&order,0)==2 && OamRank(&order,125)==127);}
  fixed_scene=FIXED_SCENE_HOARD;
  assert(OamComponentsCompatible(0x7440,0x31c2,16,8,true));
  assert(OamComponentsCompatible(0xb449,0x31c3,16,8,true));
  assert(OamComponentsCompatible(0x7440,0x31c0,16,8,true));
  assert(OamComponentsCompatible(0x7440,0x31c6,16,8,true));
  assert(!OamComponentsCompatible(0x7440,0x31bf,16,8,true));
  assert(!OamComponentsCompatible(0x7440,0x31c7,16,8,true));
  assert(!OamComponentsCompatible(0x7440,0x31c2,16,8,false));
  assert(!OamComponentsCompatible(0x7240,0x31c2,16,8,true));
  fixed_scene=FIXED_SCENE_NONE;
  assert(!OamComponentsCompatible(0x7440,0x31c2,16,8,true));
  assert(OamComponentsCompatible(0x1800,0x1800,16,16,false));
  frame_width=342;hoard_margin_mirror=true;
  {int sx,sdx;HoardBgSample(43,1,&sx,&sdx);assert(sx==43&&sdx==1);
   HoardBgSample(42,1,&sx,&sdx);assert(sx==43+Dkc1LockedInteriorSourceX(-1)&&sdx==2);
   HoardBgSample(43+256,1,&sx,&sdx);assert(sx==43+255&&sdx==1);
   hoard_margin_mirror=false;HoardBgSample(0,1,&sx,&sdx);assert(sx==0&&sdx==1);}
  Ppu window={0};window.cgwsel=0x10;window.cgadsub=0x93;
  assert(HdUniformColorWindow(&window) && HdMathFlags(&window)==0);
  window.windowsel=0x300000;window.window1left=255;window.window1right=254;
  assert(HdUniformColorWindow(&window) && HdMathFlags(&window)==0x93);
  window.windowsel=0x200000;assert(HdMathFlags(&window)==0);
  window.window1left=0;window.window1right=255;assert(!HdUniformColorWindow(&window));
  frame_brightness=8;assert(ApplyBrightness(0xffffffff)==0xff888888);
  frame_brightness=15;assert(ApplyBrightness(0xff7f7f7f)==0xff7f7f7f);
  math_flags[0]=0x93;fixed_color[0]=0x0842;
  assert(Shade(0xff808080,0xff808080,6,0)==0xff808080); /* OBJ palettes 0..3 */
  assert(Shade(0xff808080,0xff808080,4,0)!=0xff808080); /* OBJ palettes 4..7 */
  assert(argc==3);pack_dir=argv[1];preload_requested=true;
  if(!strcmp(argv[2],"invalid")) {
    assert(!PreloadResidentPack());assert(!resident_count && !resident_materials);
    puts("HD preload rejected invalid pack");return 0;
  }
  assert(PreloadResidentPack());
  if(!strncmp(argv[2],"world",5)) {
    if(!strcmp(argv[2],"world-invalid")) {
      assert(!LoadConnectedWorld());for(int l=0;l<3;l++)assert(!world_layers[l].chunks);
      FreeResidentPack();return 0;
    }
    assert(LoadConnectedWorld());connected_world_active=true;frame_width=256;
    uint32_t pixels[32*32];uint16_t indices[32*32]={0};unsigned mask;for(unsigned i=0;i<32*32;i++)pixels[i]=0xff010203;
    world_camera_x=512;world_camera_y=256;
    Material *m=ConnectedChunk(0,0,0,512,256,pixels,indices,&mask);
    assert(m==&world_layers[0].chunks[8*17+16] && m->hd_borrowed);
    /* A partial wide-left chunk belongs to the previous ring, even when the
     * native camera has crossed the 512px boundary. Never alias it forward. */
    frame_width=342;
    assert(ConnectedChunk(0,15,0,512,256,pixels,indices,&mask)==
           &world_layers[0].chunks[8*17+15]);
    assert(mask==0xffffu);
    assert(ConnectedChunk(0,0,0,512,256,pixels,indices,&mask)==m);
    world_camera_x=544;
    assert(ConnectedChunk(0,15,0,512,256,pixels,indices,&mask)==
           &world_layers[0].chunks[8*17+15]);
    world_camera_x=0;
    assert(!ConnectedChunk(0,15,0,512,256,pixels,indices,&mask));
    world_camera_x=512;frame_width=256;
    cave_world_layers[0]=world_layers[0];active_world_layers=cave_world_layers;
    active_world_origin_x=0x6900;world_camera_x=0x6b00;
    assert(ConnectedChunk(0,8,0,512,256,pixels,indices,&mask)==m);
    active_world_layers=world_layers;active_world_origin_x=0;world_camera_x=512;
    memset(cave_world_layers,0,sizeof cave_world_layers); /* borrowed fixture */
    pixels[42]=0;assert(ConnectedChunk(0,0,0,512,256,pixels,indices,&mask) && mask==0xfffdu);pixels[42]=0xff010203;
    Background *bg=&backgrounds[0];bg->width=bg->height=32;bg->chunks[0]=m;
    bg->connected[0]=true;bg->connected_mask[0]=0xfffdu;
    Material *fallback=&materials[0];fallback->hd=resident_materials[0].pixels;
    bg->fallback_chunks[0]=fallback;material_count=1;
    assert(BackgroundMaterial(bg,0,0)==m);
    assert(BackgroundMaterial(bg,8,0)==fallback && BackgroundHd(bg,8,0));
    BeginMaterialFrame();assert(fallback->pinned);
    bg->fallback_chunks[0]=NULL;assert(!BackgroundHd(bg,8,0));
    assert(BackgroundMaterial(bg,0,0)==m); /* Missing neighbor cannot demote it. */
    memset(bg,0,sizeof *bg);material_count=0;fallback->hd=NULL;
    pixels[42]=0xff000000;assert(ConnectedChunk(0,0,0,512,256,pixels,indices,&mask) && mask==0xffffu);pixels[42]=0xff010203;
    world_camera_x=1024;assert(!ConnectedChunk(0,0,0,512,256,pixels,indices,&mask));
    assert(ConnectedChunk(1,0,0,256,256,pixels,indices,&mask));
    connected_world_active=false;assert(!ConnectedChunk(1,0,0,256,256,pixels,indices,&mask));
    assert(material_file_reads==3);FreeResidentPack();
    puts("Connected world: ring wrap, exact source rejection, bounds, scene isolation, no gameplay reads");return 0;
  }
  if(!strncmp(argv[2],"center",6)) {
    exact_centers_enabled=true;
    if(!strcmp(argv[2],"center-invalid")) {
      assert(!LoadCenterAliases());assert(!center_alias_count);FreeResidentPack();return 0;
    }
    assert(resident_count==3 && LoadCenterAliases() && center_alias_count==1);
    uint32_t pixels[32*32],context[48*48];
    for(unsigned i=0;i<32*32;i++)pixels[i]=0xff010203;
    for(unsigned i=0;i<48*48;i++)context[i]=0xff8899aa;
    for(int y=0;y<32;y++)memcpy(context+(y+8)*48+8,pixels+y*32,32*4);
    /* The same bitmap is not an allowed object fallback. */
    Material *object=GetMaterial(pixels,32,32,"object");assert(object && !object->hd);
    Material *bg=GetMaterialKey(pixels,32,32,"background",context,sizeof context);
    assert(bg && bg->hd_borrowed && bg->hd && bg->hd[0]==0xff345678);
    assert(center_alias_hits==1 && material_file_reads==3);
    /* Different neighboring source pixels keep the center identical. */
    context[0]^=1;
    Material *alias=GetMaterialKey(pixels,32,32,"background",context,sizeof context);
    assert(alias && alias->hd==bg->hd && alias->resident_index==bg->resident_index);
    /* One different center pixel must fail closed. */
    pixels[0]^=1;context[8*48+8]^=1;
    Material *miss=GetMaterialKey(pixels,32,32,"background",context,sizeof context);
    assert(miss && !miss->hd && center_alias_hits==2 && material_file_reads==3);
    for(unsigned i=0;i<material_count;i++)free(materials[i].original);
    FreeResidentPack();puts("Exact centers: context aliases, one-pixel rejection, object isolation, zero gameplay reads");return 0;
  }
  if(!strncmp(argv[2],"silhouette",10)) {
    if(!strcmp(argv[2],"silhouette-invalid")) {
      assert(!LoadObjectSilhouettes());assert(!object_silhouette_count);FreeResidentPack();return 0;
    }
    assert(LoadObjectSilhouettes() && object_silhouette_count==1);
    uint32_t source[4]={0xff112233,0,0xff112233,0xff112233};
    uint32_t live[4]={0xffaabbcc,0,0xff445566,0xff778899};
    uint32_t background[4]={0xff010101,0,0xff020202,0xff030303};
    /* Same authored mask, different live CGRAM colors, must reuse HD art. */
    Material *hit=GetMaterial(live,2,2,"object");
    assert(hit && hit->hd_borrowed && hit->hd && hit->hd[0]==0xff345678);
    assert(hit->art_original && hit->art_original[0]==source[0] && !hit->art_original[1] &&
           hit->art_original[2]==source[2] && hit->art_original[3]==source[3]);
    assert(object_silhouette_hits==1 && material_file_reads==3);
    /* Background lookups cannot use the object silhouette table. */
    Material *bg=GetMaterial(background,2,2,"background");assert(bg && !bg->hd);
    live[1]=0xff000001;
    Material *miss=GetMaterial(live,2,2,"object");
    assert(miss && !miss->hd && object_silhouette_hits==1 && material_file_reads==3);
    /* Exact source colors still win through the ordinary resident key. */
    Material *exact=GetMaterial(source,2,2,"object");
    assert(exact && exact->hd==hit->hd && object_silhouette_hits==1);
    for(unsigned i=0;i<material_count;i++){free(materials[i].original);free(materials[i].art_original);}
    FreeResidentPack();puts("Object silhouettes: exact mask reuse, one-pixel rejection, background isolation, zero gameplay reads");return 0;
  }
  assert(resident_count==2 && resident_bytes==128 && material_file_reads==2);
  uint32_t a=0xff000001,b=0xff000002;
  Material *m=GetMaterial(&a,1,1,"object");
  assert(m && m->hd && m->hd_borrowed && m->hd[0]==0xff123456);
  uint32_t *resident=m->hd;
  /* Remove the source files: hits, misses and eviction must never reread them. */
  for(unsigned i=0;i<resident_count;i++) {
    char path[2048];snprintf(path,sizeof path,"%s/%s.dkhd",pack_dir,resident_materials[i].key);
    assert(!remove(path));
  }
  for(unsigned i=2;i<=MAX_MATERIALS;i++) {
    uint32_t pixel=0xff000000u|i;
    assert(GetMaterial(&pixel,1,1,"object"));
  }
  BeginMaterialFrame();
  uint32_t miss=0xfff00000;
  assert(GetMaterial(&miss,1,1,"object"));
  assert(material_evictions==1 && resident[0]==0xff123456);
  BeginMaterialFrame();m=GetMaterial(&a,1,1,"object");
  assert(m && m->hd==resident && m->hd_borrowed);
  m=GetMaterial(&b,1,1,"object");assert(m && m->hd[0]==0xffabcdef);
  assert(material_file_reads==2);
  for(unsigned i=0;i<material_count;i++) {
    free(materials[i].original);if(!materials[i].hd_borrowed)free(materials[i].hd);
  }
  FreeResidentPack();
  puts("HD preload passed: resident ownership, eviction, misses and zero gameplay reads");
  return 0;
}
