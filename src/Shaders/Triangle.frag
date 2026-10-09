#version 450

// Fragment shader half of the primitive-shape draw path (see Triangle.vert).
// Real per-pixel lighting: samples the actual scene directional light +
// shadow map via the shared ApplyDirectionalLightAndReceiverMask() function
// (src/Shaders/DirectionalLightingAndReceiverMask.glsl), using the
// interpolated per-vertex color as the base tint.

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
