#ifndef DKC1_COOP_OAM_H
#define DKC1_COOP_OAM_H
#include <stdint.h>
#include <string.h>

/* Hardware byte representation only; visibility, page flushing and resource
 * ownership remain with each caller. Replace one pair, preserving its peers. */
static inline unsigned Dkc1CoopOamHigh(const uint8_t *high,unsigned i) {
  return (high[i>>2]>>((i&3u)*2u))&3u;
}
static inline void Dkc1CoopOamCopy(uint8_t *oam,uint8_t *high,unsigned i,
                                 const uint8_t *sprite,unsigned bits) {
  memcpy(oam+i*4u,sprite,4);
  unsigned shift=(i&3u)*2u;
  high[i>>2]=(uint8_t)((high[i>>2]&~(3u<<shift))|(bits<<shift));
}
static inline void Dkc1CoopOamWrite(uint8_t *oam,uint8_t *high,unsigned i,
    int x,unsigned y,uint16_t attributes,unsigned large) {
  uint8_t sprite[4]={(uint8_t)x,(uint8_t)y,(uint8_t)attributes,
                     (uint8_t)(attributes>>8)};
  Dkc1CoopOamCopy(oam,high,i,sprite,(large?2u:0u)|(((uint16_t)x>>8)&1u));
}
static inline void Dkc1CoopOamClear(uint8_t *oam,uint8_t *high) {
  memset(oam,0,512);memset(high,0,32);
  for(unsigned i=0;i<128;i++)oam[i*4u+1u]=0xf0;
}
/* The two native rope replay paths share this exact clipping convention.
 * Keep the raw hidden-Y test before wrap/translation and retain X9 as-is. */
static inline int Dkc1CoopOamReplay(const uint8_t *src,const uint8_t *high_src,
    uint8_t *out,uint8_t *high_out,int delta,int margin) {
  Dkc1CoopOamClear(out,high_out);
  int written=0;
  for(unsigned i=0;i<128;i++) {
    int sy=src[i*4u+1u];
    if(sy==0xf0)continue;
    unsigned high=Dkc1CoopOamHigh(high_src,i);
    int sx=src[i*4u]+((high&1u)?256:0);
    if(sy>=224)sy-=256;
    sy-=delta;
    if(sx < -margin-32 || sx>=256+margin+32 || sy < -32 || sy>=224)continue;
    Dkc1CoopOamWrite(out,high_out,(unsigned)written,sx,(unsigned)sy,
        (uint16_t)(src[i*4u+2u]|(uint16_t)src[i*4u+3u]<<8),high&2u);
    if(++written==128)break;
  }
  return written;
}
#endif
