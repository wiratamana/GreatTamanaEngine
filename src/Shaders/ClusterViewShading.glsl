#ifndef CLUSTER_VIEW_SHADING_GLSL
#define CLUSTER_VIEW_SHADING_GLSL

#include "ClusterTint.glsl"

// Two-sided light: the viewer draws both faces.
vec3 ViewLight(vec3 baseColor, vec3 normal)
{
    float facing = abs(dot(normalize(normal), normalize(vec3(0.4, 0.8, -0.5))));
    return baseColor * (0.35 + 0.65 * facing);
}

// Linear color to the display value written into an 8-bit target.
vec3 ToDisplay(vec3 linearColor)
{
    return pow(clamp(linearColor, 0.0, 1.0), vec3(1.0 / 2.2));
}

#endif
