#version 450

// Fullscreen triangle. Declares sourceColor at set 0 binding 0 too, so the same slot appears in two stages.
layout(set = 0, binding = 0) uniform sampler2D sourceColor;

layout(location = 0) out vec2 vUv;

void main()
{
    vUv = vec2(float((gl_VertexIndex << 1) & 2), float(gl_VertexIndex & 2));
    const float fade = texture(sourceColor, vec2(0.5)).a * 0.0;
    gl_Position = vec4(vUv * 2.0 - 1.0 + fade, 0.0, 1.0);
}
