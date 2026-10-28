#version 450

// Cluster debug fragment shader: base color from the cluster index or the draw phase, lit like Mesh.frag.

#include "ClusterTint.glsl"
#include "DirectionalLighting.glsl"

layout(location = 0) in vec3 inWorldNormal;
layout(location = 1) in vec3 inWorldPos;
layout(location = 2) flat in uint inClusterId;
layout(location = 3) flat in uint inDrawTag;

layout(location = 0) out vec4 outColor;

void main()
{
    vec3 baseColor = ClusterDebugTint(inClusterId, inDrawTag);
    outColor = vec4(ApplyDirectionalLightShadowed(baseColor, inWorldNormal, inWorldPos), 1.0);
}
