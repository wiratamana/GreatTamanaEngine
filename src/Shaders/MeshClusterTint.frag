#version 450

// Cluster debug fragment shader: base color from a hash of the cluster index, lit like Mesh.frag.

#include "ClusterTint.glsl"
#include "DirectionalLighting.glsl"

layout(location = 0) in vec3 inWorldNormal;
layout(location = 1) in vec3 inWorldPos;
layout(location = 2) flat in uint inClusterId;

layout(location = 0) out vec4 outColor;

void main()
{
    vec3 baseColor = ClusterTint(inClusterId);
    outColor = vec4(ApplyDirectionalLightShadowed(baseColor, inWorldNormal, inWorldPos), 1.0);
}
