#version 450

// Sky background: samples the view's sky-view LUT along the per-pixel camera ray, outputs linear radiance.
// The ray comes from unprojecting the near/far NDC points through invViewProjection. The sun disk is added on top.

layout(push_constant) uniform PushConstants {
    mat4 invViewProjection;
    float eyeHeightKm;
    float planetRadiusKm;
    float skyIntensity;
    float _pad0;
    vec4 sunDirectionAndCosRadius; // xyz toward the sun, w cosine of the disk's angular radius.
    vec4 sunDiskRadiance;          // rgb radiance of the disk, zero when the sun is down.
} pc;

layout(binding = 0) uniform sampler2D skyViewLut;

#include "AtmosphereCommon.glsl"

layout(location = 0) in vec2 inNdcXY;

layout(location = 0) out vec4 outColor;

const float kDiskInnerFraction = 0.8;

void main()
{
    vec4 nearH = pc.invViewProjection * vec4(inNdcXY, 0.0, 1.0);
    vec4 farH = pc.invViewProjection * vec4(inNdcXY, 1.0, 1.0);
    if (abs(nearH.w) < 1e-6 || abs(farH.w) < 1e-6) {
        discard;
    }
    vec3 nearPoint = nearH.xyz / nearH.w;
    vec3 farPoint = farH.xyz / farH.w;
    vec3 viewDirection = normalize(farPoint - nearPoint);

    float viewHeightKm = max(pc.eyeHeightKm, pc.planetRadiusKm);
    ivec2 lutSize = textureSize(skyViewLut, 0);
    vec2 uv = ViewDirectionToSkyViewLutUv(viewDirection, lutSize, viewHeightKm, pc.planetRadiusKm);

    vec3 radiance = max(texture(skyViewLut, uv).rgb, vec3(0.0)) * pc.skyIntensity;
    // A soft rim over the last part of the radius keeps the disk edge from aliasing.
    float cosOuter = pc.sunDirectionAndCosRadius.w;
    float cosInner = 1.0 - (1.0 - cosOuter) * kDiskInnerFraction;
    float disk = smoothstep(cosOuter, cosInner, dot(viewDirection, pc.sunDirectionAndCosRadius.xyz));
    radiance += pc.sunDiskRadiance.rgb * disk;
    // Linear scene radiance; a large sun-disk value must stay finite in 16-bit float.
    outColor = vec4(min(radiance, vec3(60000.0)), 1.0);
}
