/* Exercise the actual compositor cache, including retained atlas pointers. */
#include "../runner/dkc1_hd_scene.c"
#include <assert.h>

static Material *Load(unsigned value) {
  uint32_t pixel=0xff000000u|value;
  return GetMaterial(&pixel,1,1,"object");
}
int main(void) {
  for(unsigned i=0;i<MAX_MATERIALS;i++)assert(Load(i));
  assert(material_count==MAX_MATERIALS);
  /* All were used by this frame: refusing is safer than invalidating one. */
  assert(!Load(MAX_MATERIALS));
  assert(material_failures==1 && material_evictions==0);

  Material *retained=&materials[0];
  backgrounds[0].width=backgrounds[0].height=CHUNK;
  backgrounds[0].chunks[0]=retained;
  /* Exercise freeing HD data as well as native data under sanitizers. */
  materials[1].hd=malloc(16*sizeof(uint32_t));
  BeginMaterialFrame();
  Material *replacement=Load(MAX_MATERIALS);
  assert(replacement==&materials[1]);
  assert(!replacement->hd && replacement->original[0]==(0xff000000u|MAX_MATERIALS));
  assert(retained->original[0]==0xff000000u);

  /* Several whole-cache turnovers: an unchanged atlas must remain valid,
   * as must every object already used in the current frame. */
  for(unsigned frame=0;frame<150;frame++) {
    BeginMaterialFrame();
    Material *current[128];
    for(unsigned i=0;i<128;i++)current[i]=Load(MAX_MATERIALS+1+frame*128+i);
    for(unsigned i=0;i<128;i++) {
      assert(current[i]);
      assert(current[i]->original[0]==(0xff000000u|(MAX_MATERIALS+1+frame*128+i)));
    }
    assert(backgrounds[0].chunks[0]==retained);
    assert(retained->original[0]==0xff000000u);
  }
  assert(material_count==MAX_MATERIALS);
  assert(material_evictions==19201 && material_failures==1);

  /* A cache hit must pin the entry even when it is not a retained BG. */
  BeginMaterialFrame();
  unsigned old=materials[2].original[0]&0xffffffu;
  Material *hit=Load(old);
  assert(hit==&materials[2] && hit->pinned);
  for(unsigned i=0;i<256;i++)assert(Load(100000+i));
  assert(hit->original[0]==(0xff000000u|old));
  for(unsigned i=0;i<material_count;i++){free(materials[i].original);free(materials[i].hd);}
  puts("HD material cache passed: full capacity, eviction, retained atlases, current objects, cache hits");
  return 0;
}
