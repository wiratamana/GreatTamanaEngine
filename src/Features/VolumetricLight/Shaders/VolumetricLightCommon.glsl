#ifndef VOLUMETRIC_LIGHT_COMMON_GLSL
#define VOLUMETRIC_LIGHT_COMMON_GLSL

// Shared helpers of the VolumetricLight shaders. Distances are ray distances from the eye, in world units.
#include "VolumetricFroxelMath.glsl"

const float kPi = 3.14159265359;
const float kSkyDepth = 0.999999;

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

// Cheap per-pixel, per-frame noise in [0,1).
float InterleavedGradientNoise(vec2 p, uint frame)
{
    p += 5.588238 * float(frame & 63u);
    return fract(52.9829189 * fract(dot(p, vec2(0.06711056, 0.00583715))));
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

#endif
