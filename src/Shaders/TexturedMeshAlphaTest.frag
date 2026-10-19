#version 450

// Textured mesh fragment shader with alpha test: fragments below the material's alpha cutoff are discarded.

#include "DirectionalLighting.glsl"
#include "MaterialParams.glsl"

layout(set = 0, binding = 0) uniform sampler2D uDiffuseTexture;

layout(location = 0) in vec3 inWorldNormal;
layout(location = 1) in vec2 inUv;

layout(location = 0) out vec4 outColor;

void main()
{
    vec4 color = texture(uDiffuseTexture, inUv) * uMaterial.baseColorFactor; // SRGB format: already linear here.
    if (color.a < uMaterial.params.x) {
        discard;
    }
    outColor = vec4(ApplyDirectionalLight(color.rgb, inWorldNormal), 1.0);
}
