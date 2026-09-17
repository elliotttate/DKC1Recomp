#include "dkc1_shadow_window.h"
#include <assert.h>
#include <stdio.h>
int main(void) {
  assert(Dkc1ShadowWindowContains(0,5120,1107,5120,1107,5120,43,32768,4096));
  assert(!Dkc1ShadowWindowContains(0,5120,1108,5117,1108,5117,43,32768,4096));
  assert(Dkc1ShadowWindowContains(0,4864,1108,5117,1108,5117,43,32768,4096));
  assert(!Dkc1ShadowWindowContains(0,0,0,3864,0,3864,43,32768,4096));
  assert(!Dkc1ShadowWindowContains(0,0,32461,0,0,0,43,32768,4096));
  assert(!Dkc1ShadowWindowContains(512,256,512,256,511,256,43,32768,4096));
  assert(!Dkc1ShadowWindowContains(0,0,UINT32_MAX,UINT32_MAX,0,0,43,32768,4096));
  assert(Dkc1ShadowWindowContains(0x9400,0x100,0x9af9,0x112,
                                  0x9af9,0x112,43,32768,4096));
  puts("shadow window tests passed");
}
