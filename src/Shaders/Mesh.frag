#version 450

// Imported-mesh fragment shader: outputs linear scene color, lit on a flat neutral base color.

#include "DirectionalLighting.glsl"

layout(location = 0) in vec3 inWorldNormal;
layout(location = 1) in vec3 inWorldPos;

layout(location = 0) out vec4 outColor;

void main()
{
    const vec3 baseColor = SrgbToLinear(vec3(0.72, 0.73, 0.76)); // Neutral grey; meshes carry no material.
    outColor = vec4(ApplyDirectionalLightShadowed(baseColor, inWorldNormal, inWorldPos), 1.0);
}
