#version 450
/* Vulkan port of the dkc1_flat pass (macos_graphics.metal / gl_shaders.h).
 * Mode 0/1 = nearest/plain sample; mode 2 (sharp bilinear) reproduces the
 * same texel-snapping used by the GL and Metal backends. The CRT/
 * reconstruct/bloom passes are not ported to Vulkan yet -- see
 * linux_vk_graphics.h. */
layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 result;

layout(set = 0, binding = 0) uniform sampler2D source;

layout(push_constant) uniform Push {
    vec2 source_size;
    vec2 output_size;
    int mode;
} pc;

void main() {
    if (pc.mode == 0 || pc.mode == 1) {
        result = vec4(texture(source, v_uv).rgb, 1.0);
        return;
    }
    vec2 size = pc.source_size;
    vec2 scale = max(pc.output_size / size, vec2(1.0));
    vec2 texel = v_uv * size - 0.5;
    vec2 base = floor(texel);
    vec2 fraction = fract(texel);
    vec2 adjusted = clamp((fraction - (0.5 - 0.5 / scale)) * scale, 0.0, 1.0);
    result = vec4(texture(source, (base + adjusted + 0.5) / size).rgb, 1.0);
}
