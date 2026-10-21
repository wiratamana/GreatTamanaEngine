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

constexpr int kSunLightSampleCount = 128;

// sin(0.5 degrees): the sun fades in over this height above the horizon.
constexpr float kSunHorizonFadeSin = 0.0087265f;

// smoothstep(0, edge, x)
float SmoothStepFromZero(float edge, float x) noexcept
{
    const float t = std::clamp(x / edge, 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

float RelativeLuminance(Vec3 c) noexcept { return 0.2126f * c.x + 0.7152f * c.y + 0.0722f * c.z; }

bool IsUsableSunLightInput(const AtmosphereParametersGpu& params, float heightKm, float sunUpDot) noexcept
{
    return std::isfinite(heightKm) && std::isfinite(sunUpDot) && std::isfinite(params.planetRadiusKm)
        && std::isfinite(params.atmosphereThicknessKm) && params.planetRadiusKm > 0.0f
        && params.atmosphereThicknessKm > 0.0f;
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

Vec3 ComputeSunTransmittance(
    const AtmosphereParametersGpu& params, float heightKm, float sunUpDot, int sampleCount) noexcept
{
    if (sampleCount <= 0 || !IsUsableSunLightInput(params, heightKm, sunUpDot)) {
        return Vec3::One();
    }
    const double radius = params.planetRadiusKm;
    const double thickness = params.atmosphereThicknessKm;
    const double top = radius + thickness;
    const double r = radius + std::clamp(static_cast<double>(heightKm), 0.0, thickness);
    const double up = std::clamp(static_cast<double>(sunUpDot), -1.0, 1.0);
    const double along = r * up;

    // A downward ray whose closest approach to the planet center lies inside the planet hits the ground.
    if (up < 0.0 && r * r * (1.0 - up * up) < radius * radius) {
        return Vec3::Zero();
    }
    const double discriminant = along * along + (top * top - r * r);
    if (!(discriminant > 0.0)) {
        return Vec3::One();
    }
    const double length = -along + std::sqrt(discriminant);
    if (!(length > 0.0)) {
        return Vec3::One();
    }

    Vec3 opticalDepth = Vec3::Zero();
    const double count = static_cast<double>(sampleCount);
    for (int i = 0; i < sampleCount; ++i) {
        const double a = static_cast<double>(i) / count;
        const double b = static_cast<double>(i + 1) / count;
        const double t0 = length * a * a; // Quadratic spacing: dense where the air is thick.
        const double t1 = length * b * b;
        const double tm = 0.5 * (t0 + t1);
        const double height = std::sqrt(std::max(r * r + 2.0 * along * tm + tm * tm, 0.0)) - radius;
        const Vec3 extinction =
            ComputeExtinctionCoefficientAtHeight(params, static_cast<float>(std::max(height, 0.0)));
        opticalDepth += extinction * static_cast<float>(t1 - t0);
    }
    return Vec3(std::exp(-opticalDepth.x), std::exp(-opticalDepth.y), std::exp(-opticalDepth.z));
}

SunLightModulation ComputeAtmosphereSunLight(
    const AtmosphereParametersGpu& params, float heightKm, float sunUpDot) noexcept
{
    SunLightModulation light;
    if (!IsUsableSunLightInput(params, heightKm, sunUpDot)) {
        return light;
    }
    const Vec3 overhead = ComputeSunTransmittance(params, heightKm, 1.0f, kSunLightSampleCount);
    const Vec3 sun = ComputeSunTransmittance(params, heightKm, sunUpDot, kSunLightSampleCount);
    const float fade = SmoothStepFromZero(kSunHorizonFadeSin, sunUpDot);
    const auto relative = [fade](float value, float reference) {
        return std::clamp(value / std::max(reference, 1e-6f), 0.0f, 1.0f) * fade;
    };
    const Vec3 tint(relative(sun.x, overhead.x), relative(sun.y, overhead.y), relative(sun.z, overhead.z));
    const float scale = std::clamp(RelativeLuminance(tint), 0.0f, 1.0f);
    if (!std::isfinite(tint.x) || !std::isfinite(tint.y) || !std::isfinite(tint.z) || !std::isfinite(scale)) {
        return light;
    }
    light.sunTint = tint;
    light.ambientScale = scale;
    light.active = true;
    return light;
}

float ReciprocalScaleHeightFromKm(float scaleHeightKm) noexcept
{
    return 1.0f / std::max(scaleHeightKm, kMinScaleHeightKm);
}

} // namespace gte
