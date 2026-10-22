#include "AtmosphereParameters.h"

#include <algorithm>
#include <cmath>

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

namespace {

float Sanitize(float value, float lo, float hi, float fallback) noexcept
{
    return std::isfinite(value) ? std::clamp(value, lo, hi) : fallback;
}

Vec3 SanitizeColor(const Vec3& value, const Vec3& fallback) noexcept
{
    return Vec3(Sanitize(value.x, 0.0f, 1.0f, fallback.x), Sanitize(value.y, 0.0f, 1.0f, fallback.y),
        Sanitize(value.z, 0.0f, 1.0f, fallback.z));
}

} // namespace

AtmosphereSettings SanitizeAtmosphereSettings(const AtmosphereSettings& settings) noexcept
{
    const AtmosphereSettings d{};
    AtmosphereSettings s = settings;

    s.rayleighScattering = SanitizeColor(settings.rayleighScattering, d.rayleighScattering);
    s.rayleighScaleHeightKm = Sanitize(settings.rayleighScaleHeightKm, 0.01f, 100.0f, d.rayleighScaleHeightKm);
    s.mieScattering = SanitizeColor(settings.mieScattering, d.mieScattering);
    s.miePhaseG = Sanitize(settings.miePhaseG, -0.99f, 0.99f, d.miePhaseG);
    s.mieAbsorption = SanitizeColor(settings.mieAbsorption, d.mieAbsorption);
    s.mieScaleHeightKm = Sanitize(settings.mieScaleHeightKm, 0.01f, 100.0f, d.mieScaleHeightKm);
    s.ozoneAbsorption = SanitizeColor(settings.ozoneAbsorption, d.ozoneAbsorption);
    s.ozoneTentCenterKm = Sanitize(settings.ozoneTentCenterKm, 0.0f, 100.0f, d.ozoneTentCenterKm);
    s.groundAlbedo = SanitizeColor(settings.groundAlbedo, d.groundAlbedo);
    s.ozoneTentHalfWidthKm = Sanitize(settings.ozoneTentHalfWidthKm, 0.0f, 100.0f, d.ozoneTentHalfWidthKm);
    s.planetRadiusKm = Sanitize(settings.planetRadiusKm, 1.0f, 50000.0f, d.planetRadiusKm);
    s.atmosphereThicknessKm = Sanitize(settings.atmosphereThicknessKm, 1.0f, 500.0f, d.atmosphereThicknessKm);
    s.multiScatteringStrength = Sanitize(settings.multiScatteringStrength, 0.0f, 10.0f, d.multiScatteringStrength);
    s.aerialPerspectiveStrength = Sanitize(
        settings.aerialPerspectiveStrength, 0.0f, kMaxAerialPerspectiveStrength, d.aerialPerspectiveStrength);
    s.skyIntensity = Sanitize(settings.skyIntensity, 0.0f, 50.0f, d.skyIntensity);
    s.aerialPerspectiveMaxDistanceKm =
        Sanitize(settings.aerialPerspectiveMaxDistanceKm, 0.01f, 50.0f, d.aerialPerspectiveMaxDistanceKm);
    s.aerialPerspectiveDepthExponent =
        Sanitize(settings.aerialPerspectiveDepthExponent, 1.0f, 4.0f, d.aerialPerspectiveDepthExponent);
    s.aerialPerspectiveSamplesPerSlice = std::clamp(settings.aerialPerspectiveSamplesPerSlice, 1, 16);
    s.aerialPerspectiveScatteringExaggeration = Sanitize(
        settings.aerialPerspectiveScatteringExaggeration, 0.1f, 50.0f, d.aerialPerspectiveScatteringExaggeration);
    return s;
}

} // namespace gte
