#pragma once

// Scalar reference of the BRDF terms in Shaders/PbrBrdf.glsl (same formulas and constants).

#include <algorithm>
#include <cmath>

namespace gte::pbr {

inline constexpr float kPi = 3.14159265f;
inline constexpr float kMinPerceptualRoughness = 0.045f;
inline constexpr float kDielectricF0 = 0.04f;
inline constexpr float kMaxSceneRadiance = 8192.0f;

// GGX normal distribution. alpha = perceptual roughness squared.
inline float DistributionGgx(float nDotH, float alpha)
{
    const float a2 = alpha * alpha;
    const float d = nDotH * nDotH * (a2 - 1.0f) + 1.0f;
    return a2 / (kPi * d * d);
}

// Height-correlated Smith visibility: G / (4 NdotL NdotV).
inline float VisibilitySmithGgxCorrelated(float nDotL, float nDotV, float alpha)
{
    const float a2 = alpha * alpha;
    const float gv = nDotL * std::sqrt(nDotV * nDotV * (1.0f - a2) + a2);
    const float gl = nDotV * std::sqrt(nDotL * nDotL * (1.0f - a2) + a2);
    return 0.5f / std::max(gv + gl, 1e-5f);
}

inline float FresnelSchlick(float f0, float vDotH)
{
    return f0 + (1.0f - f0) * std::pow(std::clamp(1.0f - vDotH, 0.0f, 1.0f), 5.0f);
}

// Analytic environment BRDF (split-sum fit) with the F0 scale and bias applied.
inline float EnvBrdfApprox(float f0, float roughness, float nDotV)
{
    const float r0 = roughness * -1.0f + 1.0f;
    const float r1 = roughness * -0.0275f + 0.0425f;
    const float r2 = roughness * -0.572f + 1.04f;
    const float r3 = roughness * 0.022f + -0.04f;
    const float a004 = std::min(r0 * r0, std::exp2(-9.28f * nDotV)) * r0 + r1;
    const float scale = -1.04f * a004 + r2;
    const float bias = 1.04f * a004 + r3;
    return f0 * scale + bias;
}

// Fades ambient specular where baked occlusion is strong.
inline float SpecularOcclusion(float nDotV, float occlusion, float roughness)
{
    return std::clamp(std::pow(nDotV + occlusion, std::exp2(-16.0f * roughness - 1.0f)) - 1.0f + occlusion, 0.0f, 1.0f);
}

inline float ClampSceneRadiance(float value)
{
    return std::min(value, kMaxSceneRadiance);
}

} // namespace gte::pbr
