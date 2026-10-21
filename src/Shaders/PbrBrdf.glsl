// BRDF terms for the PBR mesh shaders: GGX distribution, Smith visibility, Fresnel, environment fit.
#ifndef PBR_BRDF_GLSL
#define PBR_BRDF_GLSL

const float kPi = 3.14159265;
const float kMinPerceptualRoughness = 0.045;
const float kDielectricF0 = 0.04;
const float kMaxSceneRadiance = 8192.0;   // Scene color is RGBA16_Float (max 65504); keep headroom.

// GGX normal distribution. alpha = perceptual roughness squared.
float DistributionGgx(float nDotH, float alpha)
{
    float a2 = alpha * alpha;
    float d = nDotH * nDotH * (a2 - 1.0) + 1.0;
    return a2 / (kPi * d * d);
}

// Height-correlated Smith visibility: G / (4 NdotL NdotV).
float VisibilitySmithGgxCorrelated(float nDotL, float nDotV, float alpha)
{
    float a2 = alpha * alpha;
    float gv = nDotL * sqrt(nDotV * nDotV * (1.0 - a2) + a2);
    float gl = nDotV * sqrt(nDotL * nDotL * (1.0 - a2) + a2);
    return 0.5 / max(gv + gl, 1e-5);
}

vec3 FresnelSchlick(vec3 f0, float vDotH)
{
    return f0 + (1.0 - f0) * pow(clamp(1.0 - vDotH, 0.0, 1.0), 5.0);
}

// Analytic environment BRDF (split-sum fit); returns the F0 scale and bias already applied.
vec3 EnvBrdfApprox(vec3 f0, float roughness, float nDotV)
{
    const vec4 c0 = vec4(-1.0, -0.0275, -0.572, 0.022);
    const vec4 c1 = vec4(1.0, 0.0425, 1.04, -0.04);
    vec4 r = roughness * c0 + c1;
    float a004 = min(r.x * r.x, exp2(-9.28 * nDotV)) * r.x + r.y;
    vec2 ab = vec2(-1.04, 1.04) * a004 + r.zw;
    return f0 * ab.x + ab.y;
}

// Fades ambient specular where baked occlusion is strong.
float SpecularOcclusion(float nDotV, float occlusion, float roughness)
{
    return clamp(pow(nDotV + occlusion, exp2(-16.0 * roughness - 1.0)) - 1.0 + occlusion, 0.0, 1.0);
}

#endif // PBR_BRDF_GLSL
