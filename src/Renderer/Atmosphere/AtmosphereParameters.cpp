#include "AtmosphereParameters.h"

namespace gte {

AtmosphereParametersGpu MakeDefaultEarthAtmosphereParameters()
{
    // AtmosphereParametersGpu's own default member initializers (see
    // AtmosphereTypes.h) already carry every value transcribed from the
    // cloned reference implementation, so a plain value-initialized instance
    // is already exactly right - this function is the one explicit,
    // discoverable call site future phases should call rather than relying
    // on that default-construction implicitly.
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
