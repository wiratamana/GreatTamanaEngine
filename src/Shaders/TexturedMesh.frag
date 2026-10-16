#version 450

// Textured mesh fragment shader: diffuse texture (set 0) lit by the scene directional light and shadow map (set 1).

#include "DirectionalLightingAndReceiverMask.glsl"

layout(set = 0, binding = 0) uniform sampler2D uDiffuseTexture;
layout(set = 1, binding = 0) uniform sampler2D sceneOcclusionMap;

layout(location = 0) in vec3 inWorldNormal;
layout(location = 1) in vec2 inUv;
layout(location = 2) in vec3 inWorldPos;

layout(location = 0) out vec4 outColor;

void main()
{
    vec4 texColor = texture(uDiffuseTexture, inUv);
    vec3 shaded = ApplyDirectionalLightAndReceiverMask(texColor.rgb, inWorldNormal, inWorldPos, sceneOcclusionMap);
    outColor = vec4(shaded, texColor.a);
}
