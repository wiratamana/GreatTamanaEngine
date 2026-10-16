#include "AtmosphereMath.h"
#include "AtmosphereParameters.h"

#include <algorithm>
#include <cmath>

namespace gte {

namespace {

// Far intersection of the ray with the sphere of radius `radiusKm` at the origin; `direction` must be normalized.
// For an origin inside the sphere this is the distance to exit it. Negative when the ray never reaches the sphere.
float DistanceToSphereBoundary(Vec3 originKm, Vec3 direction, float radiusKm) noexcept
{
    const float b = 2.0f * Dot(originKm, direction);
    const float c = Dot(originKm, originKm) - radiusKm * radiusKm;
    const float discriminant = b * b - 4.0f * c;
    if (discriminant < 0.0f) {
        return -1.0f;
    }
    const float sqrtDiscriminant = std::sqrt(discriminant);
    return (-b + sqrtDiscriminant) * 0.5f;
}

} // namespace

float RayleighDensityAtHeight(const AtmosphereParametersGpu& params, float heightKm) noexcept
{
    return std::exp(-heightKm * params.rayleighDensityExpScale);
}

float MieDensityAtHeight(const AtmosphereParametersGpu& params, float heightKm) noexcept
{
    return std::exp(-heightKm * params.mieDensityExpScale);
}

float OzoneDensityAtHeight(const AtmosphereParametersGpu& params, float heightKm) noexcept
{
    if (params.ozoneTentHalfWidthKm <= 0.0f) {
        return 0.0f;
    }
    const float distanceFromCenter = std::abs(heightKm - params.ozoneTentCenterKm);
    return std::max(0.0f, 1.0f - distanceFromCenter / params.ozoneTentHalfWidthKm);
}

Vec3 ComputeExtinctionCoefficientAtHeight(const AtmosphereParametersGpu& params, float heightKm) noexcept
{
    const float rayleighDensity = RayleighDensityAtHeight(params, heightKm);
    const float mieDensity = MieDensityAtHeight(params, heightKm);
    const float ozoneDensity = OzoneDensityAtHeight(params, heightKm);

    return params.rayleighScattering * rayleighDensity
        + (params.mieScattering + params.mieAbsorption) * mieDensity
        + params.ozoneAbsorption * ozoneDensity;
}

Vec3 ComputeOpticalDepthToTopOfAtmosphere(
    const AtmosphereParametersGpu& params, Vec3 positionKm, Vec3 direction, int sampleCount) noexcept
{
    if (sampleCount <= 0) {
        return Vec3::Zero();
    }

    const Vec3 normalizedDirection = Normalize(direction);
    if (normalizedDirection == Vec3::Zero()) {
        // Normalize() degrades a zero-length input to Zero(); there is no direction to march along.
        return Vec3::Zero();
    }

    const float atmosphereRadiusKm = AtmosphereRadiusKm(params);
    const float pathLengthKm = DistanceToSphereBoundary(positionKm, normalizedDirection, atmosphereRadiusKm);
    if (pathLengthKm <= 0.0f) {
        return Vec3::Zero();
    }

    const float stepLengthKm = pathLengthKm / static_cast<float>(sampleCount);
    const Vec3 step = normalizedDirection * stepLengthKm;

    Vec3 opticalDepth = Vec3::Zero();
    Vec3 currentPositionKm = positionKm;

    for (int i = 0; i < sampleCount; ++i) {
        // Advance, then sample: the same per-step order as the transmittance shader.
        currentPositionKm += step;

        const float heightKm = std::max(Length(currentPositionKm) - params.planetRadiusKm, 0.0f);
        opticalDepth += ComputeExtinctionCoefficientAtHeight(params, heightKm) * stepLengthKm;
    }

    return opticalDepth;
}

Vec3 ComputeTransmittanceToTopOfAtmosphere(
    const AtmosphereParametersGpu& params, Vec3 positionKm, Vec3 direction, int sampleCount) noexcept
{
    const Vec3 opticalDepth = ComputeOpticalDepthToTopOfAtmosphere(params, positionKm, direction, sampleCount);
    return Vec3(std::exp(-opticalDepth.x), std::exp(-opticalDepth.y), std::exp(-opticalDepth.z));
}

float RayleighPhaseFunction(float cosTheta) noexcept
{
    constexpr float kPi = 3.14159265358979323846f;
    return 3.0f / (16.0f * kPi) * (1.0f + cosTheta * cosTheta);
}

float CornetteShanksMiePhaseFunction(float g, float cosTheta) noexcept
{
    constexpr float kPi = 3.14159265358979323846f;
    const float numerator = 3.0f / (8.0f * kPi) * (1.0f - g * g) * (1.0f + cosTheta * cosTheta);
    const float denominator = (2.0f + g * g) * std::pow(1.0f + g * g - 2.0f * g * cosTheta, 1.5f);
    return numerator / denominator;
}

void TransmittanceLutUvToHeightZenith(
    const AtmosphereParametersGpu& params, float lutGridU, float lutGridV, float& outHeightKm, float& outUpDot) noexcept
{
    const float clampedU = std::clamp(lutGridU, 0.0f, 1.0f);
    outHeightKm = clampedU * params.atmosphereThicknessKm; // mix(0, thickness, u)
    outUpDot = std::max(lutGridV * 2.0f - 1.0f, -0.999f);
}

float ReciprocalScaleHeightFromKm(float scaleHeightKm) noexcept
{
    return 1.0f / std::max(scaleHeightKm, kMinScaleHeightKm);
}

} // namespace gte
