#ifndef VOLUMETRIC_LIGHT_COMMON_GLSL
#define VOLUMETRIC_LIGHT_COMMON_GLSL

// Shared helpers of the VolumetricLight shaders. Distances are ray distances from the eye, in world units.
#include "VolumetricFroxelMath.glsl"
#include "InterleavedGradientNoise.glsl"
#include "SceneDepthConstants.glsl"

const float kPi = 3.14159265359;

bool HasNonFinite(vec4 v)
{
    return any(isnan(v)) || any(isinf(v));
}

// Unit world ray through a froxel column. uv in 0..1; Vulkan clip depth runs 0 (near) to 1 (far).
vec3 FroxelRayDirection(vec2 uv, mat4 invViewProjection)
{
    vec2 ndc = uv * 2.0 - 1.0;
    vec4 nearWorld = invViewProjection * vec4(ndc, 0.0, 1.0);
    vec4 farWorld = invViewProjection * vec4(ndc, 1.0, 1.0);
    return normalize(farWorld.xyz / farWorld.w - nearWorld.xyz / nearWorld.w);
}

float HenyeyGreenstein(float cosTheta, float g)
{
    float g2 = g * g;
    float denom = 1.0 + g2 - 2.0 * g * cosTheta;
    return (1.0 - g2) / (4.0 * kPi * pow(max(denom, 1e-4), 1.5));
}

vec3 SrgbToLinear(vec3 c)
{
    return mix(c / 12.92, pow((c + 0.055) / 1.055, vec3(2.4)), step(vec3(0.04045), c));
}

vec3 LinearToSrgb(vec3 c)
{
    c = clamp(c, vec3(0.0), vec3(1.0));
    return mix(c * 12.92, 1.055 * pow(c, vec3(1.0 / 2.4)) - 0.055, step(vec3(0.0031308), c));
}

float Hash13(vec3 p)
{
    p = fract(p * 0.1031);
    p += dot(p, p.zyx + 31.32);
    return fract((p.x + p.y) * p.z);
}

float ValueNoise3(vec3 p)
{
    vec3 i = floor(p);
    vec3 f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    float n000 = Hash13(i);
    float n100 = Hash13(i + vec3(1.0, 0.0, 0.0));
    float n010 = Hash13(i + vec3(0.0, 1.0, 0.0));
    float n110 = Hash13(i + vec3(1.0, 1.0, 0.0));
    float n001 = Hash13(i + vec3(0.0, 0.0, 1.0));
    float n101 = Hash13(i + vec3(1.0, 0.0, 1.0));
    float n011 = Hash13(i + vec3(0.0, 1.0, 1.0));
    float n111 = Hash13(i + vec3(1.0, 1.0, 1.0));
    return mix(mix(mix(n000, n100, f.x), mix(n010, n110, f.x), f.y),
               mix(mix(n001, n101, f.x), mix(n011, n111, f.x), f.y), f.z);
}

const float kMinSkyTransmittance = 0.85;

// Caps plain-fog sky opacity so near fog cannot darken or blow out the sky.
vec4 ClampSkyPlainFog(vec4 fog)
{
    if (HasNonFinite(fog)) {
        return vec4(0.0, 0.0, 0.0, 1.0);
    }
    fog.rgb = max(fog.rgb, vec3(0.0));
    fog.a = clamp(fog.a, 0.0, 1.0);
    if (fog.a < kMinSkyTransmittance) {
        float scale = (1.0 - kMinSkyTransmittance) / max(1.0 - fog.a, 1e-5);
        fog.rgb *= scale;
        fog.a = kMinSkyTransmittance;
    }
    return fog;
}

// Appends mist beyond fogRange on sky pixels while keeping transmittance above kMinSkyTransmittance.
vec4 CombineSkyMistFog(vec4 plainFog, vec4 fogEnd, vec4 fogFar)
{
    float endT = clamp(fogEnd.a, 0.0, 1.0);
    float farT = clamp(fogFar.a, 0.0, 1.0);
    float unattenuate = smoothstep(0.05, 0.2, endT) / max(endT, 0.05);
    vec3 mistIn = max(fogFar.rgb - fogEnd.rgb, vec3(0.0)) * unattenuate;
    float mistT = endT > 0.05 ? clamp(farT / endT, 0.0, 1.0) : 1.0;
    return vec4(plainFog.rgb + plainFog.a * mistIn, max(plainFog.a * mistT, kMinSkyTransmittance));
}

#endif
