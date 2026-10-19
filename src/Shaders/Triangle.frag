#version 450

// Primitive-shape fragment shader: directional light tinted by the vertex color.

#include "DirectionalLighting.glsl"

layout(location = 0) in vec3 inWorldNormal;
layout(location = 1) in vec3 fragColor;

layout(location = 0) out vec4 outColor;

void main()
{
    outColor = vec4(ApplyDirectionalLight(SrgbToLinear(fragColor), inWorldNormal), 1.0); // Vertex colors are sRGB.
}
