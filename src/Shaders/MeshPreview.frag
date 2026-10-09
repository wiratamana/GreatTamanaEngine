#version 450

// Fragment shader half of the Inspector's Mesh Asset preview viewer (see
// MeshPreview.vert). Real per-pixel directional lighting via the shared
// ApplyDirectionalLightAndReceiverMask() function (same as Mesh.frag) - the
// preview has no real shadow map, so AssetPreviewMesh.cpp binds its own
// local, always-neutral 1x1 white dummy texture at set=1 binding=0 instead
// of a real one (lighting only, no shadow, is an acceptable reduced mode
// for a preview thumbnail).

#include "DirectionalLightingAndReceiverMask.glsl"

layout(set = 1, binding = 0) uniform sampler2D sceneOcclusionMap;

layout(location = 0) in vec3 inWorldNormal;
layout(location = 1) in vec3 inWorldPos;

layout(location = 0) out vec4 outColor;

void main()
{
    const vec3 baseColor = vec3(0.72, 0.73, 0.76); // Neutral light grey - Unity's own model-preview "clay" look.
    outColor = vec4(ApplyDirectionalLightAndReceiverMask(baseColor, inWorldNormal, inWorldPos, sceneOcclusionMap), 1.0);
}
