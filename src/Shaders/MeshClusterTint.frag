#version 450

// Cluster debug fragment shader: base color from a hash of the cluster index, lit like Mesh.frag.

#include "DirectionalLighting.glsl"

layout(location = 0) in vec3 inWorldNormal;
layout(location = 1) in vec3 inWorldPos;
layout(location = 2) flat in uint inClusterId;

layout(location = 0) out vec4 outColor;

// PCG-style integer hash mapped to a mid-brightness color.
vec3 ClusterTint(uint id)
{
    uint h = id * 747796405u + 2891336453u;
    h = ((h >> ((h >> 28u) + 4u)) ^ h) * 277803737u;
    h = (h >> 22u) ^ h;
    vec3 channels = vec3(float(h & 255u), float((h >> 8u) & 255u), float((h >> 16u) & 255u)) / 255.0;
    return channels * 0.7 + 0.2;
}

void main()
{
    vec3 baseColor = ClusterTint(inClusterId);
    outColor = vec4(ApplyDirectionalLightShadowed(baseColor, inWorldNormal, inWorldPos), 1.0);
}
