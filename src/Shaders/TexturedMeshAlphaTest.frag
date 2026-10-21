#version 450

// Textured mesh fragment shader with alpha test (below the material cutoff is discarded); outputs linear scene color.

#include "DirectionalLighting.glsl"
#include "MaterialParams.glsl"

layout(set = 0, binding = 0) uniform sampler2D uDiffuseTexture;

layout(location = 0) in vec3 inWorldNormal;
layout(location = 1) in vec2 inUv;
layout(location = 2) in vec3 inWorldPos;

layout(location = 0) out vec4 outColor;

void main()
{
    vec4 color = texture(uDiffuseTexture, inUv) * uMaterial.baseColorFactor; // SRGB format: already linear here.
    if (color.a < uMaterial.params.x) {
        discard;
    }
    vec3 n = gl_FrontFacing ? inWorldNormal : -inWorldNormal; // Double-sided materials light the visible face.
    outColor = vec4(ApplyDirectionalLightShadowed(color.rgb, n, inWorldPos), 1.0);
}
