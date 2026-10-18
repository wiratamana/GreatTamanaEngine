#pragma once

#include "../../Game/Lighting/DirectionalLightResolver.h"
#include "../../Math/Vec3.h"

#include <cmath>
#include <cstdint>
#include <type_traits>

namespace gte {

// Feature module name, render-feature scope owner and log category.
inline constexpr const char* kSceneLightingFeatureName = "SceneLighting";

// Scene Services binding 8 (std140, 32 bytes). Member names and order match
// SceneGlobalUniformBlock in DirectionalLighting.glsl.
struct SceneLightingUniformData {
    float sunDirectionAndValid[4]; // xyz = toward sun, w = 1 once written
    float sunIlluminanceAndPad[4]; // xyz = color * intensity
};
static_assert(sizeof(SceneLightingUniformData) == 32);
static_assert(std::is_trivially_copyable_v<SceneLightingUniformData>);

// Packs the resolved sun; a non-finite direction becomes straight up.
inline SceneLightingUniformData PackSceneLightingUniformData(const ResolvedDirectionalLight& sun) noexcept
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
    return data;
}

} // namespace gte
