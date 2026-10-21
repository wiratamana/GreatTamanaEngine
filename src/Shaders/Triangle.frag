#version 450

// Primitive-shape fragment shader: vertex color tint, outputs linear scene color.

#include "DirectionalLighting.glsl"

layout(location = 0) in vec3 inWorldNormal;
layout(location = 1) in vec3 fragColor;
layout(location = 2) in vec3 inWorldPos;

layout(location = 0) out vec4 outColor;

void main()
{
    vec3 base = SrgbToLinear(fragColor); // Vertex colors are sRGB.
    outColor = vec4(ApplyDirectionalLightShadowed(base, inWorldNormal, inWorldPos), 1.0);
}
