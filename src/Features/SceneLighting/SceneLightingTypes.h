#pragma once

#include "../../Game/Lighting/DirectionalLightResolver.h"
#include "../../Game/Lighting/SunShadowReceiver.h"
#include "../../Math/Mat4.h"
#include "../../Math/Vec3.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <type_traits>

namespace gte {

// Feature module name, render-feature scope owner and log category.
inline constexpr const char* kSceneLightingFeatureName = "SceneLighting";

// Tunable lighting values; flat floats so the debug endpoint can reflect them.
// Ambient is added to the sun term, not scaled by it.
struct SceneLightingSettings {
    float skyAmbientR = 1.26f, skyAmbientG = 1.31f, skyAmbientB = 1.40f;
    float groundAmbientR = 0.66f, groundAmbientG = 0.57f, groundAmbientB = 0.46f;
    float ambientIntensity = 0.35f;
};

// Clamps every value; a non-finite value becomes its default.
inline SceneLightingSettings SanitizeSceneLightingSettings(const SceneLightingSettings& in) noexcept
{
    const SceneLightingSettings d{};
    const auto clamp = [](float v, float fallback, float lo, float hi) {
        return std::isfinite(v) ? std::min(std::max(v, lo), hi) : fallback;
    };
    SceneLightingSettings s;
    s.skyAmbientR = clamp(in.skyAmbientR, d.skyAmbientR, 0.0f, 4.0f);
    s.skyAmbientG = clamp(in.skyAmbientG, d.skyAmbientG, 0.0f, 4.0f);
    s.skyAmbientB = clamp(in.skyAmbientB, d.skyAmbientB, 0.0f, 4.0f);
    s.groundAmbientR = clamp(in.groundAmbientR, d.groundAmbientR, 0.0f, 4.0f);
    s.groundAmbientG = clamp(in.groundAmbientG, d.groundAmbientG, 0.0f, 4.0f);
    s.groundAmbientB = clamp(in.groundAmbientB, d.groundAmbientB, 0.0f, 4.0f);
    s.ambientIntensity = clamp(in.ambientIntensity, d.ambientIntensity, 0.0f, 8.0f);
    return s;
}

// Scene Services binding 8 (std140, 448 bytes). Member names and order match
// SceneGlobalUniformBlock in DirectionalLighting.glsl.
struct SceneLightingUniformData {
    float sunDirectionAndValid[4];         // xyz = toward sun, w = 1 once written
    float sunIlluminance[4];               // xyz = color * intensity, w = padding
    float skyAmbientAndIntensity[4];       // rgb = sky ambient, w = ambient intensity
    float groundAmbientAndMaterialDebugView[4]; // rgb = ground ambient, w = material debug view
    float shadowParams[4];                 // x cascade count (0 = none), y strength, z 1 / map size, w filter radius (texels)
    float shadowBias[4];                   // x const bias, y normal offset, z slope bias (texels), w filter samples
    float cameraPositionAndFadeStart[4];   // xyz eye, w fade start
    float cameraForwardAndFadeEnd[4];      // xyz forward, w fade end
    float cascadeSplitFar[4];              // View depth where cascade i ends
    float cascadeBlendWidth[4];            // Blend band at the end of cascade i
    float cascadeTexelWorld[4];            // World size of one texel
    float cascadeDepthRange[4];            // Ortho depth span, world units
    Mat4 cascadeViewProjection0;           // World to cascade clip
    Mat4 cascadeViewProjection1;
    Mat4 cascadeViewProjection2;
    Mat4 cascadeViewProjection3;
};
static_assert(sizeof(SceneLightingUniformData) == 448);
static_assert(std::is_trivially_copyable_v<SceneLightingUniformData>);

// Packs the sun, settings and optional cascade data; a non-finite direction becomes up.
// No receiver, or zero material strength, leaves every shadow field at zero.
inline SceneLightingUniformData PackSceneLightingUniformData(const ResolvedDirectionalLight& sun,
    const SceneLightingSettings& settings = {}, const SunShadowReceiver* receiver = nullptr) noexcept
{
    const bool finite = std::isfinite(sun.directionTowardSun.x) && std::isfinite(sun.directionTowardSun.y)
        && std::isfinite(sun.directionTowardSun.z);
    const Vec3 direction = finite ? sun.directionTowardSun : Vec3::Up();

    SceneLightingUniformData data{};
    data.sunDirectionAndValid[0] = direction.x;
    data.sunDirectionAndValid[1] = direction.y;
    data.sunDirectionAndValid[2] = direction.z;
    data.sunDirectionAndValid[3] = 1.0f;
    data.sunIlluminance[0] = sun.sunIlluminance.x;
    data.sunIlluminance[1] = sun.sunIlluminance.y;
    data.sunIlluminance[2] = sun.sunIlluminance.z;
    data.sunIlluminance[3] = 0.0f;
    data.skyAmbientAndIntensity[0] = settings.skyAmbientR;
    data.skyAmbientAndIntensity[1] = settings.skyAmbientG;
    data.skyAmbientAndIntensity[2] = settings.skyAmbientB;
    data.skyAmbientAndIntensity[3] = settings.ambientIntensity;
    data.groundAmbientAndMaterialDebugView[0] = settings.groundAmbientR;
    data.groundAmbientAndMaterialDebugView[1] = settings.groundAmbientG;
    data.groundAmbientAndMaterialDebugView[2] = settings.groundAmbientB;
    data.groundAmbientAndMaterialDebugView[3] = 0.0f;

    if (receiver == nullptr || !(receiver->materialStrength > 0.0f) || receiver->cascadeCount == 0) {
        return data;
    }

    const std::uint32_t count = std::min(receiver->cascadeCount, kSunShadowReceiverMaxCascades);
    data.shadowParams[0] = static_cast<float>(count);
    data.shadowParams[1] = receiver->materialStrength;
    data.shadowParams[2] = receiver->resolution > 0 ? 1.0f / static_cast<float>(receiver->resolution) : 0.0f;
    data.shadowParams[3] = receiver->filterRadiusTexels;
    data.shadowBias[0] = receiver->constBiasTexels;
    data.shadowBias[1] = receiver->normalOffsetTexels;
    data.shadowBias[2] = receiver->slopeBiasTexels;
    data.shadowBias[3] = static_cast<float>(receiver->filterSamples);
    data.cameraPositionAndFadeStart[0] = receiver->cameraPosition.x;
    data.cameraPositionAndFadeStart[1] = receiver->cameraPosition.y;
    data.cameraPositionAndFadeStart[2] = receiver->cameraPosition.z;
    data.cameraPositionAndFadeStart[3] = receiver->fadeStart;
    data.cameraForwardAndFadeEnd[0] = receiver->cameraForward.x;
    data.cameraForwardAndFadeEnd[1] = receiver->cameraForward.y;
    data.cameraForwardAndFadeEnd[2] = receiver->cameraForward.z;
    data.cameraForwardAndFadeEnd[3] = receiver->shadowDistance;
    for (std::uint32_t c = 0; c < kSunShadowReceiverMaxCascades; ++c) {
        data.cascadeSplitFar[c] = receiver->splitFar[c];
        data.cascadeBlendWidth[c] = receiver->blendWidth[c];
        data.cascadeTexelWorld[c] = receiver->texelWorld[c];
        data.cascadeDepthRange[c] = receiver->depthRange[c];
    }
    data.cascadeViewProjection0 = receiver->viewProjection[0];
    data.cascadeViewProjection1 = receiver->viewProjection[1];
    data.cascadeViewProjection2 = receiver->viewProjection[2];
    data.cascadeViewProjection3 = receiver->viewProjection[3];
    return data;
}

} // namespace gte
