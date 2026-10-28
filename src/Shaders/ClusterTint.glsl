#ifndef CLUSTER_TINT_GLSL
#define CLUSTER_TINT_GLSL

#include "ClusterCullConstants.glsl"

// PCG-style integer hash of a cluster index mapped to a mid-brightness color.
vec3 ClusterTint(uint id)
{
    uint h = id * 747796405u + 2891336453u;
    h = ((h >> ((h >> 28u) + 4u)) ^ h) * 277803737u;
    h = (h >> 22u) ^ h;
    vec3 channels = vec3(float(h & 255u), float((h >> 8u) & 255u), float((h >> 16u) & 255u)) / 255.0;
    return channels * 0.7 + 0.2;
}

// Debug color of a draw: the phase color when the tag asks for the phase view, else the cluster hash color.
// First phase red, second phase green, a single-phase draw gray.
vec3 ClusterDebugTint(uint clusterId, uint drawTag)
{
    if ((drawTag & kClusterDrawTagPhaseView) == 0u) {
        return ClusterTint(clusterId);
    }
    uint phase = drawTag & kClusterDrawTagPhaseMask;
    if (phase == kClusterDrawTagFirst) {
        return vec3(0.9, 0.2, 0.2);
    }
    return phase == kClusterDrawTagSecond ? vec3(0.2, 0.9, 0.2) : vec3(0.6);
}

#endif
