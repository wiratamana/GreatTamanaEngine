#version 450

// Primitive-shape fragment shader: directional light and shadow receiver mask, tinted by the vertex color.

#include "DirectionalLightingAndReceiverMask.glsl"

layout(set = 1, binding = 0) uniform sampler2D sceneOcclusionMap;

layout(location = 0) in vec3 inWorldNormal;
layout(location = 1) in vec3 inWorldPos;
layout(location = 2) in vec3 fragColor;

layout(location = 0) out vec4 outColor;

void main()
{
    vec3 shaded = ApplyDirectionalLightAndReceiverMask(fragColor, inWorldNormal, inWorldPos, sceneOcclusionMap);
    outColor = vec4(shaded, 1.0);
}
