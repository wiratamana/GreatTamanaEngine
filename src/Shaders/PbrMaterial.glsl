// Shared PBR surface setup for the lit mesh fragment shaders: samples the material set and shades.
#ifndef PBR_MATERIAL_GLSL
#define PBR_MATERIAL_GLSL

#include "DirectionalLighting.glsl"
#include "MaterialParams.glsl"
#include "PbrBrdf.glsl"

layout(set = 0, binding = 0) uniform sampler2D uBaseColorTexture;
layout(set = 0, binding = 2) uniform sampler2D uNormalTexture;
layout(set = 0, binding = 3) uniform sampler2D uMetallicRoughnessTexture;
layout(set = 0, binding = 4) uniform sampler2D uOcclusionTexture;

const float kSpecularSunScale = kDirectScale * kPi;   // The Lambert term has no 1/pi; keep one energy scale.
const float kHorizonFadeScale = 8.0;

// The fragment-stage inputs of one lit mesh fragment.
struct PbrFragmentInput {
    vec3 worldNormal;    // Interpolated, not normalized.
    vec4 worldTangent;   // xyz interpolated tangent, w handedness.
    vec3 worldPosition;
    vec2 uv;
    float windingSign;   // +1, or -1 when the model matrix mirrors.
    bool frontFacing;
};

struct PbrSurface {
    vec3 baseColor;
    float metallic;
    float perceptualRoughness;
    float occlusion;
    vec3 normal;           // Shading normal: world space, unit length, facing the viewer side.
    vec3 geometricNormal;  // Interpolated vertex normal: unit length, facing the viewer side.
};

vec4 SampleBaseColor(vec2 uv)
{
    return texture(uBaseColorTexture, uv) * uMaterial.baseColorFactor;   // SRGB format: already linear here.
}

vec3 DebugColor(float value) { return SrgbToLinear(vec3(clamp(value, 0.0, 1.0))); }

vec3 SafeNormalize(vec3 v, vec3 fallback)
{
    float lengthSquared = dot(v, v);
    return lengthSquared > 1e-12 ? v * inversesqrt(lengthSquared) : fallback;
}

// Linear HDR radiance: sun (GGX + Lambert, cascade shadows) plus hemisphere ambient, limited to kMaxSceneRadiance.
vec3 ShadePbrLinear(PbrSurface s, vec3 worldPosition)
{
    vec3 n = s.normal;
    vec3 v = SafeNormalize(SceneEyePosition() - worldPosition, n);
    float nDotV = max(dot(n, v), 1e-4);
    float roughness = clamp(s.perceptualRoughness, kMinPerceptualRoughness, 1.0);
    float alpha = roughness * roughness;
    vec3 f0 = mix(vec3(kDielectricF0), s.baseColor, s.metallic);
    vec3 diffuseColor = s.baseColor * (1.0 - s.metallic);

    vec3 sunDirection = SceneSunDirection();
    float nDotL = max(dot(n, sunDirection), 0.0);
    float geometricNDotL = dot(s.geometricNormal, sunDirection);
    float horizon = clamp(geometricNDotL * kHorizonFadeScale, 0.0, 1.0);
    float shadow = (SceneSunWritten() && nDotL > 0.0 && horizon > 0.0)
        ? SunShadowAmount(worldPosition, s.geometricNormal, geometricNDotL) : 0.0;
    float sunVisibility = 1.0 - uSceneLighting.shadowParams.y * shadow;
    vec3 sunTerm = SceneSunIlluminance() * (nDotL * sunVisibility * horizon);

    vec3 h = SafeNormalize(sunDirection + v, n);
    float nDotH = clamp(dot(n, h), 0.0, 1.0);
    float vDotH = clamp(dot(v, h), 0.0, 1.0);
    vec3 specularBrdf = FresnelSchlick(f0, vDotH)
        * (DistributionGgx(nDotH, alpha) * VisibilitySmithGgxCorrelated(nDotL, nDotV, alpha));
    vec3 direct = diffuseColor * (sunTerm * kDirectScale) + specularBrdf * (sunTerm * kSpecularSunScale);

    vec3 reflected = reflect(-v, n);
    vec3 specularAmbient = SceneHemisphereAmbient(reflected) * EnvBrdfApprox(f0, roughness, nDotV)
        * SpecularOcclusion(nDotV, s.occlusion, roughness);
    vec3 diffuseAmbient = diffuseColor * SceneHemisphereAmbient(n) * s.occlusion;
    return min(diffuseAmbient + specularAmbient + direct, vec3(kMaxSceneRadiance));
}

// Shades one fragment. `base` is the factor-multiplied base color.
vec3 ShadePbrMaterial(vec3 base, PbrFragmentInput f)
{
    float side = (f.frontFacing ? 1.0 : -1.0) * f.windingSign;
    vec3 nf = SafeNormalize(f.worldNormal, vec3(0.0, 1.0, 0.0));
    vec3 tangentRaw = f.worldTangent.xyz - nf * dot(nf, f.worldTangent.xyz);
    bool hasTangent = dot(tangentRaw, tangentRaw) > 1e-12;
    vec3 tf = hasTangent ? normalize(tangentRaw) : vec3(0.0);
    vec3 bf = cross(nf, tf) * f.worldTangent.w;

    vec3 nTs = texture(uNormalTexture, f.uv).xyz * 2.0 - 1.0;
    nTs.xy *= uMaterial.params.w;
    vec3 shadingNormal = hasTangent ? SafeNormalize(tf * nTs.x + bf * nTs.y + nf * nTs.z, nf) : nf;
    shadingNormal *= side;     // Back faces: the whole frame is negated.

    vec4 mr = texture(uMetallicRoughnessTexture, f.uv);
    float roughness = clamp(uMaterial.params.z * mr.g, 0.0, 1.0);
    float metallic = clamp(uMaterial.params.y * mr.b, 0.0, 1.0);
    float occlusion = 1.0 + uMaterial.pbr.x * (texture(uOcclusionTexture, f.uv).r - 1.0);

    int debugView = SceneMaterialDebugView();
    if (debugView == 1) { return SrgbToLinear(shadingNormal * 0.5 + 0.5); }
    if (debugView == 2) { return DebugColor(roughness); }
    if (debugView == 3) { return DebugColor(metallic); }
    if (debugView == 4) { return DebugColor(occlusion); }
    if (debugView == 5) { return SrgbToLinear(tf * side * 0.5 + 0.5); }
    if (debugView == 6) { return f.worldTangent.w > 0.0 ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0); }
    if (debugView == 7) { return side > 0.0 ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0); }
    if (debugView == 8) { return base; }

    PbrSurface surface;
    surface.baseColor = base;
    surface.metallic = metallic;
    surface.perceptualRoughness = roughness;
    surface.occlusion = occlusion;
    surface.normal = shadingNormal;
    surface.geometricNormal = nf * side;
    return ShadePbrLinear(surface, f.worldPosition);
}

#endif // PBR_MATERIAL_GLSL
