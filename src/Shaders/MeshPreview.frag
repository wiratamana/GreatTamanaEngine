#version 450

// Mesh preview fragment shader: directional light on a clay base color.

#include "DirectionalLighting.glsl"

layout(location = 0) in vec3 inWorldNormal;

layout(location = 0) out vec4 outColor;

void main()
{
    const vec3 baseColor = vec3(0.72, 0.73, 0.76); // Neutral light grey "clay" look.
    outColor = vec4(ApplyDirectionalLight(baseColor, inWorldNormal), 1.0);
}
