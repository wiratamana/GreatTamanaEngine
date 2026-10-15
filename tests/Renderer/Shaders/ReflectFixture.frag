#version 450

layout(set = 0, binding = 0) uniform sampler2D sourceColor;
layout(set = 0, binding = 1) uniform sampler2D sourceMask;

layout(push_constant) uniform PushBlock {
    float strength;
} pc;

layout(location = 0) in vec2 vUv;
layout(location = 0) out vec4 outColor;

void main()
{
    outColor = texture(sourceColor, vUv) * mix(1.0, texture(sourceMask, vUv).r, pc.strength);
}
