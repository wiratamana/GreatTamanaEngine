#pragma once

#include "../../Game/Lighting/DirectionalLightResolver.h"
#include "../../Math/Vec3.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <type_traits>

namespace gte {

// Feature module name, render-feature scope owner and log category.
inline constexpr const char* kSceneLightingFeatureName = "SceneLighting";

// Tunable lighting values; flat floats so the debug endpoint can reflect them.
struct SceneLightingSettings {
    float skyAmbientR = 0.42f, skyAmbientG = 0.46f, skyAmbientB = 0.55f;
    float groundAmbientR = 0.22f, groundAmbientG = 0.20f, groundAmbientB = 0.18f;
    float ambientIntensity = 1.0f;
    float exposure = 1.0f;
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
    s.exposure = clamp(in.exposure, d.exposure, 0.01f, 16.0f);
    return s;
}

// Scene Services binding 8 (std140, 64 bytes). Member names and order match
// SceneGlobalUniformBlock in DirectionalLighting.glsl.
struct SceneLightingUniformData {
    float sunDirectionAndValid[4];     // xyz = toward sun, w = 1 once written
    float sunIlluminanceAndPad[4];     // xyz = color * intensity
    float skyAmbientAndIntensity[4];   // rgb = sky ambient, w = ambient intensity
    float groundAmbientAndExposure[4]; // rgb = ground ambient, w = exposure
};
static_assert(sizeof(SceneLightingUniformData) == 64);
static_assert(std::is_trivially_copyable_v<SceneLightingUniformData>);

// Packs the resolved sun and the settings; a non-finite direction becomes straight up.
inline SceneLightingUniformData PackSceneLightingUniformData(
    const ResolvedDirectionalLight& sun, const SceneLightingSettings& settings = {}) noexcept
{
    const bool finite = std::isfinite(sun.directionTowardSun.x) && std::isfinite(sun.directionTowardSun.y)
        && std::isfinite(sun.directionTowardSun.z);
    const Vec3 direction = finite ? sun.directionTowardSun : Vec3::Up();

    SceneLightingUniformData data{};
    data.sunDirectionAndValid[0] = direction.x;
    data.sunDirectionAndValid[1] = direction.y;
    data.sunDirectionAndValid[2] = direction.z;
    data.sunDirectionAndValid[3] = 1.0f;
    data.sunIlluminanceAndPad[0] = sun.sunIlluminance.x;
    data.sunIlluminanceAndPad[1] = sun.sunIlluminance.y;
    data.sunIlluminanceAndPad[2] = sun.sunIlluminance.z;
    data.skyAmbientAndIntensity[0] = settings.skyAmbientR;
    data.skyAmbientAndIntensity[1] = settings.skyAmbientG;
    data.skyAmbientAndIntensity[2] = settings.skyAmbientB;
    data.skyAmbientAndIntensity[3] = settings.ambientIntensity;
    data.groundAmbientAndExposure[0] = settings.groundAmbientR;
    data.groundAmbientAndExposure[1] = settings.groundAmbientG;
    data.groundAmbientAndExposure[2] = settings.groundAmbientB;
    data.groundAmbientAndExposure[3] = settings.exposure;
    return data;
}

} // namespace gte
