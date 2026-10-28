#version 450

// Cluster debug fragment shader for the uv and tangent vertex layouts: base color from a hash of the cluster index.

#include "ClusterTint.glsl"
#include "DirectionalLighting.glsl"

layout(location = 0) in vec3 inWorldNormal;
layout(location = 2) in vec3 inWorldPos;
layout(location = 5) flat in uint inClusterId;

layout(location = 0) out vec4 outColor;

void main()
{
    vec3 baseColor = ClusterTint(inClusterId);
    outColor = vec4(ApplyDirectionalLightShadowed(baseColor, inWorldNormal, inWorldPos), 1.0);
}
