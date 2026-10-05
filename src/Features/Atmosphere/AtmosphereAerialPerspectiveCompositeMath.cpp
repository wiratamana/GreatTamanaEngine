#include "AtmosphereAerialPerspectiveCompositeMath.h"

namespace gte {

bool ShouldBypassAerialPerspectiveComposite(float rawDepth) noexcept
{
    return rawDepth >= kAerialPerspectiveFarPlaneDepthThreshold;
}

Vec3 ComputeAerialPerspectiveCompositeColor(float rawDepth, const Vec3& sceneColorRgb,
    const Vec3& sampledAerialRgb, float sampledAerialA, float strength) noexcept
{
    if (ShouldBypassAerialPerspectiveComposite(rawDepth)) {
        return sceneColorRgb;
    }

    const Vec3 inScattering = sampledAerialRgb * strength;
    const float transmittance = 1.0f + (sampledAerialA - 1.0f) * strength; // mix(1.0, a, strength)
    return sceneColorRgb * transmittance + inScattering;
}

} // namespace gte
