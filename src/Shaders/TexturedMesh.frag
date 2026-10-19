#version 450

// Textured mesh fragment shader: diffuse texture (set 0) lit by the scene directional light.

#include "DirectionalLighting.glsl"

layout(set = 0, binding = 0) uniform sampler2D uDiffuseTexture;

layout(location = 0) in vec3 inWorldNormal;
layout(location = 1) in vec2 inUv;

layout(location = 0) out vec4 outColor;

void main()
{
    vec4 texColor = texture(uDiffuseTexture, inUv); // SRGB format: already linear here.
    outColor = vec4(ApplyDirectionalLight(texColor.rgb, inWorldNormal), texColor.a);
}
