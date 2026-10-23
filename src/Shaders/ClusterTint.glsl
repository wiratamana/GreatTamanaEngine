#ifndef CLUSTER_TINT_GLSL
#define CLUSTER_TINT_GLSL

// PCG-style integer hash of a cluster index mapped to a mid-brightness color.
vec3 ClusterTint(uint id)
{
    uint h = id * 747796405u + 2891336453u;
    h = ((h >> ((h >> 28u) + 4u)) ^ h) * 277803737u;
    h = (h >> 22u) ^ h;
    vec3 channels = vec3(float(h & 255u), float((h >> 8u) & 255u), float((h >> 16u) & 255u)) / 255.0;
    return channels * 0.7 + 0.2;
}

#endif
