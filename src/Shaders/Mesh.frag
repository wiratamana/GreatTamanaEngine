#version 450

// Fragment shader half of the engine's shared "imported mesh" draw path (see
// Mesh.vert). Real per-pixel lighting: samples the actual scene directional
// light + shadow map via the shared ApplyDirectionalLightAndReceiverMask()
// function (src/Shaders/DirectionalLightingAndReceiverMask.glsl) - no
// textures/materials involved at all yet, a *.gta AssetType::Mesh payload
// carries no material/texture data (see TODO.md, "PMX material/texture
// import"), so a flat neutral-grey base color stands in until that lands.

#include "DirectionalLightingAndReceiverMask.glsl"

layout(set = 1, binding = 0) uniform sampler2D sceneOcclusionMap;

layout(location = 0) in vec3 inWorldNormal;
layout(location = 1) in vec3 inWorldPos;

layout(location = 0) out vec4 outColor;

void main()
{
    const vec3 baseColor = vec3(0.72, 0.73, 0.76); // Neutral grey - no material system yet.
    outColor = vec4(ApplyDirectionalLightAndReceiverMask(baseColor, inWorldNormal, inWorldPos, sceneOcclusionMap), 1.0);
}
