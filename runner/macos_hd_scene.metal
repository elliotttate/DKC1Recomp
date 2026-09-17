// dkc1_hd_gpu_types.h is prepended by the host when compiling this source.
struct Fragment { uint color,low; int priority,layer; };
uint hd_blend(uint front,uint back) {
  uint a=front>>24;if(a==255)return front;if(!a)return back;
  if(!(back>>24))return front;
  uint ba=(back>>24)*(255-a),denom=a*255+ba,result=((denom+127)/255)<<24;
  for(int ch=0;ch<3;ch++) {
    int shift=ch*8;
    result|=((((front>>shift)&255)*a*255+((back>>shift)&255)*ba+denom/2)/denom)<<shift;
  }
  return result;
}
uint hd_shade(uint color,uint low,int layer,int y,device const HdGpuFrame &f) {
  if(layer>=6 || !(f.math_flags[y]&(1u<<layer)))return color;
  uint result=color&0xff000000u;
  for(int c=0;c<3;c++) {
    int shift=c*8,raw=(low>>shift)&255,value=(color>>shift)&255;
    int fix=(f.fixed_color[y]>>((2-c)*5))&31;
    int v5=raw>>3,exact=(f.math_flags[y]&128)?v5-fix:v5+fix;
    if(exact<0)exact=0;if(f.math_flags[y]&64)exact/=2;if(exact>31)exact=31;
    exact=(exact<<3)|(exact>>2);
    int correction=value-raw;if(f.math_flags[y]&64)correction/=2;
    result|=uint(clamp(exact+correction,0,255))<<shift;
  }
  return result;
}
uint hd_texel(uint material,int x,int y,int dx,int dy,
              device const uint *art,device const HdGpuMaterial *materials) {
  HdGpuMaterial m=materials[material];
  return art[m.offset+(y*4+dy)*m.width+x*4+dx];
}
uint hd_pixel(int x,int y,int dx,int dy,bool high,device const HdGpuFrame &f,
              device const uint *art,device const HdGpuMaterial *materials,
              device const uint *object_pixels) {
  Fragment fragments[4];int count=0;
  for(int l=0;l<3;l++)if(f.main_enable[y]&(1<<l)) {
    int tx=(x-(int(f.width)-256)/2+int(f.scroll_x[l][y]))&(int(f.bg_width[l])-1);
    int ty=(y+1+int(f.scroll_y[l][y]))&(int(f.bg_height[l])-1);
    uint at=ty*f.bg_width[l]+tx,chunk=(ty/32)*16+tx/32;
    uint low=f.bg_pixels[l][at],material=f.bg_material[l][chunk];
    uint connected=f.bg_connected[l][chunk];
    if(connected && !(connected&(1u<<((ty%32/8)*4+tx%32/8))))material=f.bg_fallback_material[l][chunk];
    bool hd=high && material!=0xffffffffu;
    uint color=f.bg_valid[l][chunk] ? low : 0;
    if(hd)color=hd_texel(material,tx%32,ty%32,dx,dy,art,materials);
    if(low) {
      if(hd && connected)low=f.world_palette[f.bg_z[l][at]&255];
      uint current=f.palette[y][f.bg_z[l][at]&255],corrected=color&0xff000000u;
      for(int ch=0;ch<3;ch++) {
        int shift=ch*8,v=int((color>>shift)&255)+int((current>>shift)&255)-int((low>>shift)&255);
        corrected|=uint(clamp(v,0,255))<<shift;
      }
      color=corrected;low=current;
    }
    if(color>>24)fragments[count++]={color,low,int(f.bg_z[l][at]>>12),l};
  }
  uint object=0,object_low=0;int priority=0,object_layer=4;
  for(int i=int(f.object_count)-1;i>=0;i--) {
    HdGpuObject o=f.objects[i];int ox=x-o.x,oy=y-o.y;
    if(!o.valid || ox<0 || oy<0 || ox>=o.width || oy>=o.height)continue;
    uint low=object_pixels[o.offset+oy*o.width+ox],color=low;
    if(high && o.material!=0xffffffffu)color=hd_texel(o.material,ox,oy,dx,dy,art,materials);
    if(high && low && o.art_offset!=0xffffffffu) {
      uint base=object_pixels[o.art_offset+oy*o.width+ox],adjusted=color&0xff000000u;
      for(int ch=0;ch<3;ch++){int shift=ch*8,v=int((color>>shift)&255)+int((low>>shift)&255)-int((base>>shift)&255);adjusted|=uint(clamp(v,0,255))<<shift;}color=adjusted;
    }
    if(color>>24){object=hd_blend(color,object);object_low=low;priority=o.priority;object_layer=o.math_exempt?6:4;}
  }
  if(object>>24)fragments[count++]={object,object_low,priority,object_layer};
  for(int a=1;a<count;a++)for(int b=a;b>0 && fragments[b].priority<fragments[b-1].priority;b--) {
    Fragment t=fragments[b];fragments[b]=fragments[b-1];fragments[b-1]=t;
  }
  uint color=high?f.high_backdrop[y*4+dy]:f.backdrop[y];
  for(int i=0;i<count;i++)color=hd_blend(hd_shade(fragments[i].color,fragments[i].low,fragments[i].layer,y,f),color);
  uint b=f.brightness;if(b==0||b>15)b=15;
  if(b!=15){uint result=color&0xff000000u;
    for(int ch=0;ch<3;ch++){int shift=ch*8;result|=(((color>>shift)&255)*b/15)<<shift;}
    color=result;}
  return color;
}
kernel void hd_oracle(device const HdGpuFrame &f [[buffer(0)]],
    device const uint *art [[buffer(1)]],device const HdGpuMaterial *materials [[buffer(2)]],
    device const uint *objects [[buffer(3)]],device uint *exact [[buffer(4)]],
    constant float &polish [[buffer(5)]],
    uint2 p [[thread_position_in_grid]]) {
  if(p.x>=f.width || p.y>=HdHeight)return;
  uint at=p.y*f.width+p.x;
  exact[at]=((hd_pixel(p.x,p.y,0,0,false,f,art,materials,objects)^f.native[at])&0xffffffu)==0;
  if(polish>0) {
    // Conservative protection includes every sprite/HUD rectangle plus a
    // native-pixel gutter. Never soften an unverified original fallback.
    bool protect=false;
    for(uint i=0;i<f.object_count;i++) {
      HdGpuObject o=f.objects[i];
      if(o.valid && int(p.x)>=o.x-1 && int(p.x)<o.x+o.width+1 &&
         int(p.y)>=o.y-1 && int(p.y)<o.y+o.height+1)protect=true;
    }
    for(int l=0;l<3;l++)if(f.main_enable[p.y]&(1<<l)) {
      int x=(int(p.x)-(int(f.width)-256)/2+int(f.scroll_x[l][p.y]))&(int(f.bg_width[l])-1);
      int y=(int(p.y)+1+int(f.scroll_y[l][p.y]))&(int(f.bg_height[l])-1);
      uint chunk=(y/32)*16+x/32;
      uint connected=f.bg_connected[l][chunk];
      uint material=f.bg_material[l][chunk];
      if(connected && !(connected&(1u<<((y%32/8)*4+x%32/8))))material=f.bg_fallback_material[l][chunk];
      bool present=material!=0xffffffffu;
      if((f.bg_pixels[l][y*f.bg_width[l]+x]>>24) && !present)protect=true;
    }
    if(protect)exact[at]|=2;
  }
}
kernel void hd_compose(device const HdGpuFrame &f [[buffer(0)]],
    device const uint *art [[buffer(1)]],device const HdGpuMaterial *materials [[buffer(2)]],
    device const uint *objects [[buffer(3)]],device const uint *exact [[buffer(4)]],
    texture2d<float,access::write> target [[texture(0)]],uint2 p [[thread_position_in_grid]]) {
  if(p.x>=f.width*4 || p.y>=HdHeight*4)return;
  uint at=(p.y/4)*f.width+p.x/4;
  uint c=(exact[at]&1)?hd_pixel(p.x/4,p.y/4,p.x%4,p.y%4,true,f,art,materials,objects):f.native[at];
  target.write(float4((c>>16)&255,(c>>8)&255,c&255,(c>>24)&255)/255.0f,p);
}

float hd_luma(float3 c) { return dot(c,float3(.2126,.7152,.0722)); }
float hd_finish_curve(float x) {
  x=clamp(x,0.0f,1.0f);
  if(x<.18)return x*(.17/.18);
  if(x<.50)return .17+(x-.18)*(.31/.32);
  if(x<.78)return .48+(x-.50)*(.25/.28);
  return .73+(x-.78)*(.21/.22);
}
uint hd_finish_hash(uint2 p,uint seed) {
  uint v=p.x*0x9e3779b9u^p.y*0x85ebca6bu^seed;
  v^=v>>16;v*=0x7feb352du;v^=v>>15;v*=0x846ca68bu;v^=v>>16;
  return v;
}
float3 hd_grounded_finish(float3 color,uint2 p,float strength) {
  float luma=hd_luma(color);
  float3 subdued=mix(float3(luma),color,.92);
  float3 graded=float3(hd_finish_curve(subdued.r),hd_finish_curve(subdued.g),
                       hd_finish_curve(subdued.b));
  float a=float(hd_finish_hash(p,0x1234u)&0xffffu)/65535.0;
  float b=float(hd_finish_hash(p,0x9abcu)&0xffffu)/65535.0;
  // One third now matches the original finish. Full scale deliberately
  // extrapolates that grade and grain to 3x for an obvious live comparison.
  float level=clamp(strength,0.0f,1.0f)*3.0;
  return clamp(color+(graded-color)*level+(a-b)*.010*level,0.0f,1.0f);
}
kernel void hd_polish(device const HdGpuFrame &f [[buffer(0)]],
    device const uint *exact [[buffer(4)]],constant float &strength [[buffer(5)]],
    constant float &finish [[buffer(6)]],
    texture2d<float,access::sample> source [[texture(0)]],
    texture2d<float,access::write> target [[texture(1)]],uint2 p [[thread_position_in_grid]]) {
  if(p.x>=f.width*4 || p.y>=HdHeight*4)return;
  constexpr sampler linear(coord::pixel,address::clamp_to_edge,filter::linear);
  float2 uv=float2(p)+.5;float4 result=source.sample(linear,uv);
  if(strength>0) {
    bool safe=true;
    // The stencil and all filtered taps remain inside these native cells.
    for(int y=-1;y<=1;y++)for(int x=-1;x<=1;x++) {
      uint nx=uint(clamp(int(p.x/4)+x,0,int(f.width)-1));
      uint ny=uint(clamp(int(p.y/4)+y,0,HdHeight-1));
      if(exact[ny*f.width+nx]!=1)safe=false;
    }
    if(safe) {
      float nw=hd_luma(source.sample(linear,uv+float2(-1,-1)).rgb);
      float ne=hd_luma(source.sample(linear,uv+float2(1,-1)).rgb);
      float sw=hd_luma(source.sample(linear,uv+float2(-1,1)).rgb);
      float se=hd_luma(source.sample(linear,uv+float2(1,1)).rgb);
      float c=hd_luma(result.rgb),lo=min(c,min(min(nw,ne),min(sw,se))),hi=max(c,max(max(nw,ne),max(sw,se)));
      float2 tangent=float2(-(nw+ne-sw-se),nw+sw-ne-se);
      float magnitude=max(abs(tangent.x),abs(tangent.y));
      // Leave low-contrast texture and ambiguous corners alone. A short spatial
      // footprint avoids temporal ghosts and broad blur across leaf interiors.
      if(hi-lo>=max(.045,hi*.18) && magnitude>=.03) {
        float2 step=tangent/magnitude;
        float4 filtered=(source.sample(linear,uv-step*.75)+source.sample(linear,uv+step*.75))*.5;
        float luma=hd_luma(filtered.rgb);
        if(luma>=lo && luma<=hi)
          result.rgb=mix(result.rgb,filtered.rgb,clamp(strength,0.0f,1.0f)*.65);
      }
    }
  }
  if(finish>0)result.rgb=hd_grounded_finish(result.rgb,p,finish);
  target.write(result,p);
}
