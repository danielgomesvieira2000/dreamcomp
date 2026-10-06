// Modifier volumes (dreamcomp): the depth the volume triangles are tested at must be computed
// exactly as geometry.frag writes it, or the stencil marks the wrong side of every surface.
#version 450

layout(push_constant) uniform Push {
    vec2 scale;
    vec2 offset;
    int mode;
    float alpha;
} push;

layout(location = 0) in float v_inv_w;

layout(location = 0) out vec4 o_colour;

void main() {
    o_colour = vec4(0.0, 0.0, 0.0, push.alpha);
    const float w = 100000.0 * v_inv_w;
    gl_FragDepth = log2(1.0 + max(w, -0.999999)) / 34.0;
}
