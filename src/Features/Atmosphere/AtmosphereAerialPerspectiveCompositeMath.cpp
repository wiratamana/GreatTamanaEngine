#include "AtmosphereAerialPerspectiveCompositeMath.h"

#include <algorithm>

namespace gte {

bool IsClearDepth(float rawDepth) noexcept
{
    return rawDepth >= kAerialPerspectiveFarPlaneDepthThreshold;
}

Vec3 ComputeAerialPerspectiveCompositeColor(float rawDepth, const Vec3& sceneColorRgb, const Vec3& sampledAerialRgb,
    float sampledAerialA, const Vec3& shaftDeltaRgb, float strength, float skyIntensity) noexcept
{
    if (IsClearDepth(rawDepth)) {
        const Vec3 shaft = shaftDeltaRgb * (strength * skyIntensity);
        if (shaft.x == 0.0f && shaft.y == 0.0f && shaft.z == 0.0f) {
            return sceneColorRgb;
        }
        return Vec3(std::max(sceneColorRgb.x + shaft.x, 0.0f), std::max(sceneColorRgb.y + shaft.y, 0.0f),
            std::max(sceneColorRgb.z + shaft.z, 0.0f));
    }

    const Vec3 inScattering = sampledAerialRgb * strength;
    const float transmittance = 1.0f + (sampledAerialA - 1.0f) * strength; // mix(1.0, a, strength)
    return sceneColorRgb * transmittance + inScattering;
}

} // namespace gte
