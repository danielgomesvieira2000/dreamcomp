// Modifier volumes (dreamcomp): triangles that only touch the stencil buffer, and the full-screen
// quad that darkens what they marked. Same screen-space convention as geometry.vert: x and y in
// pixels, z is 1/w (larger is nearer).
#version 450

layout(push_constant) uniform Push {
    vec2 scale;
    vec2 offset;
    int mode;
    float alpha;  // the final quad's darkening: 1 - FPU_SHAD_SCALE / 256
} push;

layout(location = 0) in vec3 in_pos;

layout(location = 0) out float v_inv_w;

void main() {
    v_inv_w = in_pos.z;
    gl_Position = vec4(in_pos.xy * push.scale + push.offset, 0.0, 1.0);
}
