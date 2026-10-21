#pragma once

#include "../../Math/Vec3.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace gte {

// Owner and name of the channel that publishes the sun light factors, once per view.
inline constexpr const char* kSunLightModulationOwner = "Atmosphere";
inline constexpr const char* kSunLightModulationChannel = "SunLight";

// Priority of the publisher. Same-stage readers must use a larger number.
inline constexpr std::int32_t kSunLightModulationPriority = 5;

// Factors for light consumers that do not march the air themselves.
// Both are 1 for an overhead sun and 0 for a sun below the horizon.
struct SunLightModulation {
    bool active = false;          // False: the consumer keeps its own lighting.
    Vec3 sunTint = Vec3::One();   // Per channel factor on the sun illuminance.
    float ambientScale = 1.0f;    // Factor on every ambient light.
};

// A record one view applied, with the frame it was applied in.
struct SunLightModulationSnapshot {
    SunLightModulation light;
    std::uint64_t frameIndex = 0;
};

// The record a consumer may apply. Inactive or non-finite input gives the inactive record.
// Finite values are clamped to 0..1.
inline SunLightModulation SanitizeSunLightModulation(const SunLightModulation& in) noexcept
{
    const bool finite = std::isfinite(in.sunTint.x) && std::isfinite(in.sunTint.y)
        && std::isfinite(in.sunTint.z) && std::isfinite(in.ambientScale);
    if (!in.active || !finite) {
        return SunLightModulation{};
    }
    SunLightModulation out;
    out.active = true;
    out.sunTint = Vec3(std::clamp(in.sunTint.x, 0.0f, 1.0f), std::clamp(in.sunTint.y, 0.0f, 1.0f),
        std::clamp(in.sunTint.z, 0.0f, 1.0f));
    out.ambientScale = std::clamp(in.ambientScale, 0.0f, 1.0f);
    return out;
}

} // namespace gte
