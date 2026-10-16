#include "AtmosphereParameters.h"

namespace gte {

AtmosphereParametersGpu MakeDefaultEarthAtmosphereParameters()
{
    // The default member initializers already hold the Earth-like values.
    return AtmosphereParametersGpu{};
}

float AtmosphereRadiusKm(const AtmosphereParametersGpu& params) noexcept
{
    return params.planetRadiusKm + params.atmosphereThicknessKm;
}

Vec3 WorldPositionToAtmosphereSpaceKm(Vec3 worldPosition, float worldUnitsPerKm) noexcept
{
    if (worldUnitsPerKm <= 0.0f) {
        return Vec3::Zero();
    }
    return worldPosition / worldUnitsPerKm;
}

} // namespace gte
