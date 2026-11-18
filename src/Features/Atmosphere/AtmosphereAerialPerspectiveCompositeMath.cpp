#include "AtmosphereAerialPerspectiveCompositeMath.h"

#include <algorithm>

namespace gte {

bool IsClearDepth(float rawDepth) noexcept
{
    return rawDepth >= kAerialPerspectiveFarPlaneDepthThreshold;
}

Vec3 ComputeAerialPerspectiveCompositeColor(float rawDepth, const Vec3& sceneColorRgb, const Vec3& sampledAerialRgb,
    float sampledAerialA, const Vec3& shaftDeltaRgb, float strength, float skyIntensity, float aerialSkyMatch,
    const Vec3& horizonSkyRgb) noexcept
{
    if (IsClearDepth(rawDepth)) {
        const Vec3 shaft = shaftDeltaRgb * (strength * skyIntensity);
        if (shaft.x == 0.0f && shaft.y == 0.0f && shaft.z == 0.0f) {
            return sceneColorRgb;
        }
        return Vec3(std::max(sceneColorRgb.x + shaft.x, 0.0f), std::max(sceneColorRgb.y + shaft.y, 0.0f),
            std::max(sceneColorRgb.z + shaft.z, 0.0f));
    }

    const float transmittance = std::clamp(1.0f + (sampledAerialA - 1.0f) * strength, 0.0f, 1.0f);
    const float skyMatch = std::clamp(aerialSkyMatch, 0.0f, 1.0f);
    const float skyGain = std::max(1.0f + (skyIntensity - 1.0f) * skyMatch, 0.0f);
    const Vec3 attenuatedScene = sceneColorRgb * transmittance;
    const Vec3 skyCeiling(
        std::max(horizonSkyRgb.x, 0.0f), std::max(horizonSkyRgb.y, 0.0f), std::max(horizonSkyRgb.z, 0.0f));
    // Clamp in-scatter so far terrain approaches the horizon sky without exceeding it or clipping to white.
    const Vec3 maxInScattering(std::max(skyCeiling.x - attenuatedScene.x, 0.0f),
        std::max(skyCeiling.y - attenuatedScene.y, 0.0f), std::max(skyCeiling.z - attenuatedScene.z, 0.0f));
    const Vec3 rawInScattering = sampledAerialRgb * (strength * skyGain);
    const Vec3 inScattering(std::clamp(rawInScattering.x, 0.0f, maxInScattering.x),
        std::clamp(rawInScattering.y, 0.0f, maxInScattering.y),
        std::clamp(rawInScattering.z, 0.0f, maxInScattering.z));
    const Vec3 color = attenuatedScene + inScattering;
    return Vec3(std::clamp(color.x, 0.0f, kAerialPerspectiveMaxOutput),
        std::clamp(color.y, 0.0f, kAerialPerspectiveMaxOutput),
        std::clamp(color.z, 0.0f, kAerialPerspectiveMaxOutput));
}

} // namespace gte
