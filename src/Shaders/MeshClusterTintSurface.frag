#version 450

// Cluster debug fragment shader for the uv and tangent vertex layouts: base color from the cluster index or the draw phase.

#include "ClusterTint.glsl"
#include "DirectionalLighting.glsl"

layout(location = 0) in vec3 inWorldNormal;
layout(location = 2) in vec3 inWorldPos;
layout(location = 5) flat in uint inClusterId;
layout(location = 6) flat in uint inDrawTag;

layout(location = 0) out vec4 outColor;

void main()
{
    vec3 baseColor = ClusterDebugTint(inClusterId, inDrawTag);
    outColor = vec4(ApplyDirectionalLightShadowed(baseColor, inWorldNormal, inWorldPos), 1.0);
}
