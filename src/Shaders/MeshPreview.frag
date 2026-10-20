#version 450

// Mesh preview fragment shader: clay base color, display-encoded for an 8-bit target.

#include "DirectionalLighting.glsl"

layout(location = 0) in vec3 inWorldNormal;

layout(location = 0) out vec4 outColor;

void main()
{
    const vec3 baseColor = SrgbToLinear(vec3(0.72, 0.73, 0.76)); // Neutral light grey "clay" look.
    outColor = vec4(ApplyDirectionalLightForDisplay(baseColor, inWorldNormal), 1.0);
}
