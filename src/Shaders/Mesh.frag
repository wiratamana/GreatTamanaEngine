#version 450

// Imported-mesh fragment shader: directional light and shadow receiver mask on a flat neutral base color.

#include "DirectionalLightingAndReceiverMask.glsl"

layout(set = 1, binding = 0) uniform sampler2D sceneOcclusionMap;

layout(location = 0) in vec3 inWorldNormal;
layout(location = 1) in vec3 inWorldPos;

layout(location = 0) out vec4 outColor;

void main()
{
    const vec3 baseColor = vec3(0.72, 0.73, 0.76); // Neutral grey; meshes carry no material.
    outColor = vec4(ApplyDirectionalLightAndReceiverMask(baseColor, inWorldNormal, inWorldPos, sceneOcclusionMap), 1.0);
}
