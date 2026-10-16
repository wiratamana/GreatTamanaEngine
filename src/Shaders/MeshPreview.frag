#version 450

// Mesh preview fragment shader: directional light on a clay base color; set 1 holds a dummy white occlusion map (no shadow).

#include "DirectionalLightingAndReceiverMask.glsl"

layout(set = 1, binding = 0) uniform sampler2D sceneOcclusionMap;

layout(location = 0) in vec3 inWorldNormal;
layout(location = 1) in vec3 inWorldPos;

layout(location = 0) out vec4 outColor;

void main()
{
    const vec3 baseColor = vec3(0.72, 0.73, 0.76); // Neutral light grey "clay" look.
    outColor = vec4(ApplyDirectionalLightAndReceiverMask(baseColor, inWorldNormal, inWorldPos, sceneOcclusionMap), 1.0);
}
