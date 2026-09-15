#version 450
/* Fullscreen quad via a hardcoded triangle strip, no vertex buffer -- same
 * trick the GL/Metal backends use. NOTE: Vulkan's NDC has +Y pointing down
 * (opposite of GL). If the presented image comes out vertically flipped on
 * real hardware, flip v_uv.y here (`v_uv = vec2(t[gl_VertexIndex].x, 1.0 -
 * t[gl_VertexIndex].y);`) -- this is the one thing that could not be
 * verified without a real display attached. */
layout(location = 0) out vec2 v_uv;

void main() {
    vec2 p[4] = vec2[](vec2(-1,-1), vec2(-1,1), vec2(1,-1), vec2(1,1));
    vec2 t[4] = vec2[](vec2(0,0), vec2(0,1), vec2(1,0), vec2(1,1));
    v_uv = t[gl_VertexIndex];
    gl_Position = vec4(p[gl_VertexIndex], 0.0, 1.0);
}
