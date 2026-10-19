#version 450

// Textured mesh fragment shader: base color texture times the material factor, lit by the scene directional light.

#include "DirectionalLighting.glsl"
#include "MaterialParams.glsl"

layout(set = 0, binding = 0) uniform sampler2D uDiffuseTexture;

layout(location = 0) in vec3 inWorldNormal;
layout(location = 1) in vec2 inUv;

layout(location = 0) out vec4 outColor;

void main()
{
    vec3 color = (texture(uDiffuseTexture, inUv) * uMaterial.baseColorFactor).rgb; // SRGB format: already linear here.
    outColor = vec4(ApplyDirectionalLight(color, inWorldNormal), 1.0); // Opaque pipeline: alpha is always 1.
}
