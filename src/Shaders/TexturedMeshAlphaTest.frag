#version 450

// Textured mesh fragment shader with alpha test: texels below the cutoff are discarded.

#include "DirectionalLighting.glsl"

layout(set = 0, binding = 0) uniform sampler2D uDiffuseTexture;

layout(location = 0) in vec3 inWorldNormal;
layout(location = 1) in vec2 inUv;

layout(location = 0) out vec4 outColor;

// One cutoff for every masked material (the glTF default).
const float kAlphaCutoff = 0.5;

void main()
{
    vec4 texColor = texture(uDiffuseTexture, inUv); // SRGB format: already linear here.
    if (texColor.a < kAlphaCutoff) {
        discard;
    }
    outColor = vec4(ApplyDirectionalLight(texColor.rgb, inWorldNormal), 1.0);
}
