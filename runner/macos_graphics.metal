// DKC2 Reconstruct and CRT algorithms translated from GLSL to Metal.
// See docs/GRAPHICS_OPTIONS_PORT.md for source identity and validation.
#include <metal_stdlib>
using namespace metal;
struct VertexOutput { float4 position [[position]]; float2 uv; };
constexpr sampler pointSampler(coord::normalized,address::clamp_to_edge,filter::nearest);
constexpr sampler linearSampler(coord::normalized,address::clamp_to_edge,filter::linear);
vertex VertexOutput dkc1_vertex(uint id [[vertex_id]]) {
  const float2 positions[]={float2(-1,1),float2(-1,-1),float2(1,1),float2(1,-1)};
  const float2 coords[]={float2(0,0),float2(0,1),float2(1,0),float2(1,1)};
  VertexOutput out; out.position=float4(positions[id],0,1); out.uv=coords[id]; return out;
}
// p: source xy, output xy, mode, strength, softness, shading,
// sigmaH/dark/bright/beamFade, blur direction xy, glow/halo,
// curvature xy, corner/vignette, mask/pitch/strength/gain/knee, output origin xy.
fragment float4 dkc1_flat(VertexOutput in [[stage_in]], texture2d<float> source [[texture(0)]], constant float *u [[buffer(0)]]) {
  if (int(u[4])==0) return float4(source.sample(pointSampler,in.uv).rgb,1);
  if (int(u[4])==1) return float4(source.sample(linearSampler,in.uv).rgb,1);
  float2 size=float2(u[0],u[1]),scale=max(float2(u[2],u[3])/size,float2(1));
  float2 texel=in.uv*size-0.5,base=floor(texel),fraction=fract(texel);
  float2 adjusted=clamp((fraction-(0.5-0.5/scale))*scale,0.0,1.0);
  return float4(source.sample(linearSampler,(base+adjusted+0.5)/size).rgb,1);
}

float finish_luma(float3 c) { return dot(c,float3(.2126,.7152,.0722)); }
float finish_curve(float x) {
  x=clamp(x,0.0f,1.0f);
  if(x<.18)return x*(.17/.18);
  if(x<.50)return .17+(x-.18)*(.31/.32);
  if(x<.78)return .48+(x-.50)*(.25/.28);
  return .73+(x-.78)*(.21/.22);
}
uint finish_hash(uint2 p,uint seed) {
  uint v=p.x*0x9e3779b9u^p.y*0x85ebca6bu^seed;
  v^=v>>16;v*=0x7feb352du;v^=v>>15;v*=0x846ca68bu;v^=v>>16;
  return v;
}
float3 grounded_finish(float3 color,uint2 p,float strength) {
  float luma=finish_luma(color);
  float3 subdued=mix(float3(luma),color,.92);
  float3 graded=float3(finish_curve(subdued.r),finish_curve(subdued.g),
                       finish_curve(subdued.b));
  float a=float(finish_hash(p,0x1234u)&0xffffu)/65535.0;
  float b=float(finish_hash(p,0x9abcu)&0xffffu)/65535.0;
  float level=clamp(strength,0.0f,1.0f)*3.0;
  return clamp(color+(graded-color)*level+(a-b)*.010*level,0.0f,1.0f);
}
fragment float4 dkc1_finish(VertexOutput in [[stage_in]],
    texture2d<float> source [[texture(0)]],constant float *u [[buffer(0)]]) {
  uint2 p=uint2(clamp(floor(in.uv*float2(u[0],u[1])),float2(0),
                       float2(u[0]-1,u[1]-1)));
  float4 result=source.sample(pointSampler,in.uv);
  result.rgb=grounded_finish(result.rgb,p,u[5]);
  return result;
}

float3 tx(float2 t, texture2d<float> source, float2 source_size) { return source.sample(pointSampler, (t + 0.5) / source_size).rgb; }
float df(float3 a, float3 b) {
  float3 d = abs(a - b);
  return dot(d, float3(0.299, 0.587, 0.114)) * 2.0 +
         abs((a.r - b.r) - (a.b - b.b)) * 0.5;
}
bool eq(float3 a, float3 b) { return df(a, b) < 0.004; }
float3 reconstruct_decode(float3 c, float3 n, float3 s, float3 w, float3 e,
            float3 nw, float3 ne, float3 sw, float3 se, int mode) {
  if (mode < 1) return c;
  if (eq(c, nw) && eq(c, ne) && eq(c, sw) && eq(c, se) &&
      eq(n, s) && eq(n, e) && eq(n, w) && !eq(c, n))
    return mix(c, n, 0.5);
  if (eq(c, n) && eq(c, s) && eq(e, ne) && eq(e, se) &&
      eq(w, nw) && eq(w, sw) && eq(e, w) && !eq(c, e))
    return mix(c, e, 0.5);
  if (eq(c, e) && eq(c, w) && eq(n, ne) && eq(n, nw) &&
      eq(s, se) && eq(s, sw) && eq(n, s) && !eq(c, n))
    return mix(c, n, 0.5);
  return c;
}
fragment float4 dkc1_reconstruct(VertexOutput in [[stage_in]], texture2d<float> source [[texture(0)]], constant float *u [[buffer(0)]]) {
  float2 uv=in.uv,source_size=float2(u[0],u[1]),output_size=float2(u[2],u[3]);
  float2 lines_size=source_size,target_size=output_size,direction=float2(u[12],u[13]),curvature=float2(u[16],u[17]);
  int mode=int(u[4]),mask_kind=int(u[20]);
  float strength=u[5],softness=u[6],shading=u[7],sigma_h=u[8],sigma_dark=u[9],sigma_bright=u[10],beam_fade=u[11];
  float glow_amount=u[14],halo_amount=u[15],corner_radius=u[18],vignette=u[19],mask_pitch=u[21],mask_strength=u[22],mask_gain=u[23],knee=u[24];

  float2 pos = uv * source_size;
  float2 t = floor(pos);
  float2 fp = pos - t;
  float2 dir = float2(fp.x < 0.5 ? -1.0 : 1.0, fp.y < 0.5 ? -1.0 : 1.0);
  float2 f = abs(fp - 0.5) + 0.5;
  float2 scale = max(output_size / source_size, float2(1.0));
  /* softness widens every transition from one output pixel to three. */
  float band = 1.0 + 2.0 * softness;
  float aa = min(scale.x, scale.y) / band;
  float2 dx = float2(dir.x, 0.0);
  float2 dy = float2(0.0, dir.y);
  float3 A1 = tx(t - dx - dy - dy, source, source_size), B1 = tx(t - dy - dy, source, source_size), C1 = tx(t + dx - dy - dy, source, source_size);
  float3 A0 = tx(t - dx - dx - dy, source, source_size), A = tx(t - dx - dy, source, source_size), B = tx(t - dy, source, source_size), C = tx(t + dx - dy, source, source_size), C4 = tx(t + dx + dx - dy, source, source_size);
  float3 D0 = tx(t - dx - dx, source, source_size), D = tx(t - dx, source, source_size), E = tx(t, source, source_size), F = tx(t + dx, source, source_size), F4 = tx(t + dx + dx, source, source_size);
  float3 G0 = tx(t - dx - dx + dy, source, source_size), G = tx(t - dx + dy, source, source_size), H = tx(t + dy, source, source_size), I = tx(t + dx + dy, source, source_size), I4 = tx(t + dx + dx + dy, source, source_size);
  float3 G5 = tx(t - dx + dy + dy, source, source_size), H5 = tx(t + dy + dy, source, source_size), I5 = tx(t + dx + dy + dy, source, source_size);
  float3 e = reconstruct_decode(E, B, H, D, F, A, C, G, I, mode);
  float3 fc = reconstruct_decode(F, C, I, E, F4, B, C4, H, I4, mode);
  float3 hc = reconstruct_decode(H, E, H5, G, I, D, F, G5, I5, mode);
  float3 ic = reconstruct_decode(I, F, I5, H, I4, E, F4, H5, I5, mode);
  /* f runs from the texel center (0.5) to its edge (1.0); blend half
     way to the neighbor over the last output pixel before the edge,
     and the neighbor's fragments continue the other half. */
  float2 adj = 0.5 * clamp((f - (1.0 - 0.5 * band / scale)) * scale / band,
                         0.0, 1.0);
  float3 base = mix(mix(e, fc, adj.x), mix(hc, ic, adj.x), adj.y);
  /* Smooth shading: where the neighbors are close in color (a shading
     band of the pre-rendered art, not an outline), interpolate them
     into a gradient instead of flat steps. */
  if (shading > 0.0) {
    float2 g = f - 0.5;
    float3 bil = mix(mix(e, fc, g.x), mix(hc, ic, g.x), g.y);
    float sim = max(max(df(e, fc), df(e, hc)), df(e, ic));
    float w = shading * (1.0 - smoothstep(0.03, 0.14, sim));
    base = mix(base, bil, w);
  }
  if (mode < 2) { return float4(base, 1.0); }
  float wd1 = df(E, C) + df(E, G) + df(I, H5) + df(I, F4) + 4.0 * df(H, F);
  float wd2 = df(H, D) + df(H, I5) + df(F, I4) + df(F, B) + 4.0 * df(E, I);
  bool edr = wd1 < wd2 && !eq(E, H) && !eq(E, F) && !(eq(E, I) && eq(H, F));
  if (!edr) { return float4(base, 1.0); }
  float3 nc = (df(E, F) <= df(E, H)) ? fc : hc;
  float cov = clamp((f.x + f.y - 1.5) * aa + 0.5, 0.0, 1.0);
  if (mode >= 3) {
    bool left = 2.0 * df(F, G) <= df(H, C) && !eq(E, G) && !eq(D, G);
    bool up = df(F, G) >= 2.0 * df(H, C) && !eq(E, C) && !eq(B, C);
    if (left) cov = max(cov, clamp((2.0 * f.x + f.y - 2.0) * aa * 0.75 + 0.5, 0.0, 1.0));
    if (up) cov = max(cov, clamp((f.x + 2.0 * f.y - 2.0) * aa * 0.75 + 0.5, 0.0, 1.0));
    if (mode >= 4) {
      bool left3 = left && 4.0 * df(F, G) <= df(H, C) && !eq(E, G0) && !eq(D0, G0);
      bool up3 = up && df(F, G) >= 4.0 * df(H, C) && !eq(E, C1) && !eq(B1, C1);
      if (left3) cov = max(cov, clamp((3.0 * f.x + f.y - 2.5) * aa * 0.6 + 0.5, 0.0, 1.0));
      if (up3) cov = max(cov, clamp((f.x + 3.0 * f.y - 2.5) * aa * 0.6 + 0.5, 0.0, 1.0));
    }
  }
  return float4(mix(base, nc, cov * strength), 1.0);
}

float3 srgb_decode(float3 c) {
  float3 lo = c / 12.92;
  float3 hi = pow((c + 0.055) / 1.055, float3(2.4));
  return mix(lo, hi, step(float3(0.04045), c));
}
fragment float4 dkc1_lines(VertexOutput in [[stage_in]], texture2d<float> source [[texture(0)]], constant float *u [[buffer(0)]]) {
  float2 uv=in.uv,source_size=float2(u[0],u[1]),output_size=float2(u[2],u[3]);
  float2 lines_size=source_size,target_size=output_size,direction=float2(u[12],u[13]),curvature=float2(u[16],u[17]);
  int mode=int(u[4]),mask_kind=int(u[20]);
  float strength=u[5],softness=u[6],shading=u[7],sigma_h=u[8],sigma_dark=u[9],sigma_bright=u[10],beam_fade=u[11];
  float glow_amount=u[14],halo_amount=u[15],corner_radius=u[18],vignette=u[19],mask_pitch=u[21],mask_strength=u[22],mask_gain=u[23],knee=u[24];

  float sx = uv.x * source_size.x;
  float row = (floor(uv.y * source_size.y) + 0.5) / source_size.y;
  float c = floor(sx);
  float inv = -0.5 / (sigma_h * sigma_h);
  float3 acc = float3(0.0);
  float wsum = 0.0;
  for (int k = -2; k <= 2; k++) {
    float tx = c + float(k);
    float d = tx + 0.5 - sx;
    float w = exp(d * d * inv);
    float3 s = source.sample(pointSampler, float2((tx + 0.5) / source_size.x, row)).rgb;
    acc += w * srgb_decode(s);
    wsum += w;
  }
  return float4(acc / wsum, 1.0);
}

float3 line_at(float i, float2 uv, texture2d<float> lines, float2 lines_size) {
  if (i < 0.0 || i >= lines_size.y) return float3(0.0);
  return lines.sample(pointSampler, float2(uv.x, (i + 0.5) / lines_size.y)).rgb;
}
fragment float4 dkc1_beam(VertexOutput in [[stage_in]], texture2d<float> lines [[texture(0)]], constant float *u [[buffer(0)]]) {
  float2 uv=in.uv,source_size=float2(u[0],u[1]),output_size=float2(u[2],u[3]);
  float2 lines_size=source_size,target_size=output_size,direction=float2(u[12],u[13]),curvature=float2(u[16],u[17]);
  int mode=int(u[4]),mask_kind=int(u[20]);
  float strength=u[5],softness=u[6],shading=u[7],sigma_h=u[8],sigma_dark=u[9],sigma_bright=u[10],beam_fade=u[11];
  float glow_amount=u[14],halo_amount=u[15],corner_radius=u[18],vignette=u[19],mask_pitch=u[21],mask_strength=u[22],mask_gain=u[23],knee=u[24];

  float y = uv.y * lines_size.y;
  float i0 = floor(y - 0.5);
  float3 acc = float3(0.0);
  for (int k = -1; k <= 2; k++) {
    float i = i0 + float(k);
    float d = y - (i + 0.5);
    float3 L = clamp(line_at(i, uv, lines, lines_size), 0.0, 1.0);
    float3 s = mix(float3(sigma_dark), float3(sigma_bright), sqrt(L));
    acc += L * exp(-d * d / (2.0 * s * s)) / (s * 2.5066283);
  }
  float3 flat = line_at(clamp(floor(y), 0.0, lines_size.y - 1.0), uv, lines, lines_size);
  return float4(mix(flat, acc, beam_fade), 1.0);
}

fragment float4 dkc1_down(VertexOutput in [[stage_in]], texture2d<float> source [[texture(0)]], constant float *u [[buffer(0)]]) {
  float2 uv=in.uv,source_size=float2(u[0],u[1]),output_size=float2(u[2],u[3]);
  float2 lines_size=source_size,target_size=output_size,direction=float2(u[12],u[13]),curvature=float2(u[16],u[17]);
  int mode=int(u[4]),mask_kind=int(u[20]);
  float strength=u[5],softness=u[6],shading=u[7],sigma_h=u[8],sigma_dark=u[9],sigma_bright=u[10],beam_fade=u[11];
  float glow_amount=u[14],halo_amount=u[15],corner_radius=u[18],vignette=u[19],mask_pitch=u[21],mask_strength=u[22],mask_gain=u[23],knee=u[24];

  float2 t = 1.0 / source_size;
  float3 c = source.sample(linearSampler, uv + float2(-t.x, -t.y)).rgb +
           source.sample(linearSampler, uv + float2(t.x, -t.y)).rgb +
           source.sample(linearSampler, uv + float2(-t.x, t.y)).rgb +
           source.sample(linearSampler, uv + float2(t.x, t.y)).rgb;
  return float4(c * 0.25, 1.0);
}

fragment float4 dkc1_blur(VertexOutput in [[stage_in]], texture2d<float> source [[texture(0)]], constant float *u [[buffer(0)]]) {
  float2 uv=in.uv,source_size=float2(u[0],u[1]),output_size=float2(u[2],u[3]);
  float2 lines_size=source_size,target_size=output_size,direction=float2(u[12],u[13]),curvature=float2(u[16],u[17]);
  int mode=int(u[4]),mask_kind=int(u[20]);
  float strength=u[5],softness=u[6],shading=u[7],sigma_h=u[8],sigma_dark=u[9],sigma_bright=u[10],beam_fade=u[11];
  float glow_amount=u[14],halo_amount=u[15],corner_radius=u[18],vignette=u[19],mask_pitch=u[21],mask_strength=u[22],mask_gain=u[23],knee=u[24];

  float2 step = direction / source_size;
  float3 acc = source.sample(linearSampler, uv).rgb;
  float wsum = 1.0;
  for (int k = 1; k <= 4; k++) {
    float w = exp(-float(k * k) / 8.0);
    acc += w * (source.sample(linearSampler, uv + step * float(k)).rgb +
                source.sample(linearSampler, uv - step * float(k)).rgb);
    wsum += 2.0 * w;
  }
  return float4(acc / wsum, 1.0);
}

float3 encode(float3 c) {
  c = clamp(c, 0.0, 1.0);
  float3 lo = c * 12.92;
  float3 hi = 1.055 * pow(c, float3(1.0 / 2.4)) - 0.055;
  return mix(lo, hi, step(float3(0.0031308), c));
}
float3 soft_knee(float3 x, float knee) {
  float3 t = max(x - knee, 0.0) / (1.0 - knee);
  float3 e = exp(-2.0 * t);
  float3 bent = knee + (1.0 - knee) * (1.0 - e) / (1.0 + e);
  return mix(x, bent, step(float3(knee), x));
}
float ign(float2 p) {
  return fract(52.9829189 * fract(0.06711056 * p.x + 0.00583715 * p.y));
}
fragment float4 dkc1_compose(VertexOutput in [[stage_in]], texture2d<float> image [[texture(0)]], texture2d<float> glow [[texture(1)]], texture2d<float> halo [[texture(2)]], constant float *u [[buffer(0)]]) {
  float2 uv=in.uv,source_size=float2(u[0],u[1]),output_size=float2(u[2],u[3]);
  float2 lines_size=source_size,target_size=output_size,direction=float2(u[12],u[13]),curvature=float2(u[16],u[17]);
  int mode=int(u[4]),mask_kind=int(u[20]);
  float strength=u[5],softness=u[6],shading=u[7],sigma_h=u[8],sigma_dark=u[9],sigma_bright=u[10],beam_fade=u[11];
  float glow_amount=u[14],halo_amount=u[15],corner_radius=u[18],vignette=u[19],mask_pitch=u[21],mask_strength=u[22],mask_gain=u[23],knee=u[24];

  float4 fragCoord=in.position+float4(u[25],u[26],0,0);
  float2 p = uv * 2.0 - 1.0;
  float2 q = p;
  q.x = p.x * (1.0 + curvature.x * p.y * p.y);
  q.y = p.y * (1.0 + curvature.y * p.x * p.x);
  q *= 1.0 + max(curvature.x, curvature.y);
  float2 s = (q + 1.0) * 0.5;
  float2 px = s * target_size;
  float2 e = min(px, target_size - px);
  float radius = corner_radius * target_size.y;
  float2 cr = max(float2(radius) - e, 0.0);
  float inside = radius - length(cr);
  if (radius <= 0.0) inside = min(e.x, e.y);
  float edge = smoothstep(-0.5, 0.5, inside);
  float2 sc = clamp(s, 0.0, 1.0);
  float3 c = image.sample(linearSampler, sc).rgb * (1.0 - glow_amount - halo_amount) +
           glow.sample(linearSampler, sc).rgb * glow_amount +
           halo.sample(linearSampler, sc).rgb * halo_amount;
  if (mask_kind > 0) {
    float col = floor(fragCoord.x);
    float stripe = floor(fmod(col, mask_pitch) * 3.0 / mask_pitch);
    float3 sel = float3(stripe == 0.0 ? 1.0 : 0.0, stripe == 1.0 ? 1.0 : 0.0,
                    stripe == 2.0 ? 1.0 : 0.0);
    float3 pass = mix(float3(1.0), sel, mask_strength);
    if (mask_kind == 2) {
      float group = floor(col / mask_pitch);
      float shift = fmod(group, 2.0) * mask_pitch * 0.5;
      float yy = fmod(floor(fragCoord.y) + shift, mask_pitch);
      if (yy < 1.0) pass *= 1.0 - mask_strength;
    }
    c = c * mask_gain * pass;
  }
  c = soft_knee(c, knee);
  float vig = 1.0 - vignette * smoothstep(0.5, 1.5, length(p));
  c *= edge * vig;
  float3 out_c = encode(c);
  float noise = ign(fragCoord.xy) - ign(fragCoord.xy + float2(17.0, 41.0));
  out_c += noise * (0.5 / 255.0);
  return float4(out_c, 1.0);
}
