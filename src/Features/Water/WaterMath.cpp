#include "WaterMath.h"

#include "../../Core/FeatureKit/ScreenSpaceView.h"

#include <algorithm>
#include <cmath>

namespace gte {

namespace {

constexpr double kTwoPiD = 6.283185307179586476925286766559;
constexpr double kGravity = 9.81;
constexpr double kTimeWrapSeconds = 3600.0;
constexpr float kWaveRatio = 0.62f;          // Wavelength ratio of two neighbouring waves.
constexpr float kAmplitudeExponent = 0.6f;   // Amplitude follows wavelength to this power.
constexpr float kMinWavelength = 0.8f;
constexpr double kRippleWrapMetres = 1024.0; // Ripple lattice period in metres: integer in layer units.

Vec3 SanitizeColor(const Vec3& c, const Vec3& fallback) noexcept
{
    return Vec3(fx::SanitizeFinite(c.x, 0.0f, 4.0f, fallback.x), fx::SanitizeFinite(c.y, 0.0f, 4.0f, fallback.y),
        fx::SanitizeFinite(c.z, 0.0f, 4.0f, fallback.z));
}

// Mean of ((1 + sin) / 2)^e over one period, so the profile has zero average.
double ProfileMean(double e) noexcept
{
    return std::exp(std::lgamma(e + 0.5) - std::lgamma(e + 1.0)) / std::sqrt(kTwoPiD * 0.5);
}

double Fract(double v) noexcept
{
    return v - std::floor(v);
}

} // namespace

WaterSettings SanitizeWaterSettings(const WaterSettings& settings) noexcept
{
    const WaterSettings d{};
    WaterSettings s = settings;
    const auto f = [](float v, float lo, float hi, float fallback) { return fx::SanitizeFinite(v, lo, hi, fallback); };

    s.seaLevel = f(settings.seaLevel, -10000.0f, 10000.0f, d.seaLevel);
    s.waveCount = std::clamp<std::uint32_t>(settings.waveCount, 1u, kWaterMaxWaves);
    s.waveHeight = f(settings.waveHeight, 0.0f, 30.0f, d.waveHeight);
    s.choppiness = f(settings.choppiness, 0.0f, 1.0f, d.choppiness);
    s.waveSpeed = f(settings.waveSpeed, 0.0f, 4.0f, d.waveSpeed);
    s.windDirectionDeg = f(settings.windDirectionDeg, -3600.0f, 3600.0f, d.windDirectionDeg);
    s.windSpread = f(settings.windSpread, 0.0f, 90.0f, d.windSpread);
    s.baseWavelength = f(settings.baseWavelength, 4.0f, 400.0f, d.baseWavelength);
    s.rippleStrength = f(settings.rippleStrength, 0.0f, 2.0f, d.rippleStrength);
    s.rippleScale = f(settings.rippleScale, 0.25f, 4.0f, d.rippleScale);
    s.waveLodScale = f(settings.waveLodScale, 0.25f, 4.0f, d.waveLodScale);

    s.shallowColor = SanitizeColor(settings.shallowColor, d.shallowColor);
    s.deepColor = SanitizeColor(settings.deepColor, d.deepColor);
    s.foamColor = SanitizeColor(settings.foamColor, d.foamColor);
    s.clarity = f(settings.clarity, 0.5f, 200.0f, d.clarity);
    s.absorption = f(settings.absorption, 0.0f, 8.0f, d.absorption);
    s.refractionStrength = f(settings.refractionStrength, 0.0f, 0.3f, d.refractionStrength);

    s.roughness = f(settings.roughness, 0.02f, 0.6f, d.roughness);
    s.sunSpecular = f(settings.sunSpecular, 0.0f, 8.0f, d.sunSpecular);
    s.reflectionStrength = f(settings.reflectionStrength, 0.0f, 2.0f, d.reflectionStrength);
    s.shadowStrength = f(settings.shadowStrength, 0.0f, 1.0f, d.shadowStrength);

    s.crestFoam = f(settings.crestFoam, 0.0f, 2.0f, d.crestFoam);
    s.shoreFoam = f(settings.shoreFoam, 0.0f, 2.0f, d.shoreFoam);
    s.shoreWidth = f(settings.shoreWidth, 0.1f, 30.0f, d.shoreWidth);

    s.ssrSteps = std::clamp<std::uint32_t>(settings.ssrSteps, 4u, 48u);
    s.ssrMaxDistance = f(settings.ssrMaxDistance, 10.0f, 5000.0f, d.ssrMaxDistance);

    s.wetBand = f(settings.wetBand, 0.1f, 20.0f, d.wetBand);
    s.wetStrength = f(settings.wetStrength, 0.0f, 1.0f, d.wetStrength);
    s.wetFadeDistance = f(settings.wetFadeDistance, 100.0f, 50000.0f, d.wetFadeDistance);

    s.maxDistance = f(settings.maxDistance, 100.0f, 1.0e6f, d.maxDistance);

    s.debugView = static_cast<std::uint8_t>(settings.debugView) < kWaterDebugViewCount ? settings.debugView
                                                                                       : WaterDebugView::Off;
    return s;
}

WaterWaveTable BuildWaterWaves(const WaterSettings& safe, double eyeX, double eyeZ, double timeSeconds) noexcept
{
    WaterWaveTable table{};
    const std::uint32_t count = std::min(safe.waveCount, kWaterMaxWaves);
    const double wind = static_cast<double>(safe.windDirectionDeg) * kTwoPiD / 360.0;
    const double spread = static_cast<double>(safe.windSpread) * kTwoPiD / 360.0;
    const double exponent = 1.0 + 3.0 * static_cast<double>(safe.choppiness);
    const double mean = ProfileMean(exponent);

    double weightSum = 0.0;
    std::array<double, kWaterMaxWaves> wavelength{};
    std::array<double, kWaterMaxWaves> weight{};
    for (std::uint32_t i = 0; i < count; ++i) {
        wavelength[i] = std::max(static_cast<double>(safe.baseWavelength) * std::pow(kWaveRatio, static_cast<double>(i)),
            static_cast<double>(kMinWavelength));
        weight[i] = std::pow(wavelength[i] / wavelength[0], static_cast<double>(kAmplitudeExponent));
        weightSum += weight[i];
    }

    for (std::uint32_t i = 0; i < count; ++i) {
        // Golden-ratio sequences give a fixed, well spread direction and phase per wave index.
        const double r = i == 0 ? 0.0 : Fract(0.61803398875 * static_cast<double>(i) + 0.3) * 2.0 - 1.0;
        const double angle = wind + spread * r;
        const double dx = std::cos(angle);
        const double dz = std::sin(angle);
        const double k = kTwoPiD / wavelength[i];
        const double omega = std::sqrt(kGravity * k) * static_cast<double>(safe.waveSpeed);
        const double phi = kTwoPiD * Fract(0.7548776662 * static_cast<double>(i + 1));
        const double along = k * (dx * eyeX + dz * eyeZ);
        const double offset = std::fmod(along - omega * timeSeconds + phi, kTwoPiD);

        WaterWaveGpu& w = table[i];
        w.dirAndK[0] = static_cast<float>(dx);
        w.dirAndK[1] = static_cast<float>(dz);
        w.dirAndK[2] = static_cast<float>(k);
        w.dirAndK[3] = static_cast<float>(exponent);
        w.ampPhase[0] = static_cast<float>(static_cast<double>(safe.waveHeight) * weight[i] / weightSum);
        w.ampPhase[1] = static_cast<float>(omega);
        w.ampPhase[2] = static_cast<float>(offset);
        w.ampPhase[3] = static_cast<float>(mean);
    }
    return table;
}

float WaterWaveHeight(const WaterWaveTable& waves, std::uint32_t count, float relX, float relZ) noexcept
{
    float h = 0.0f;
    for (std::uint32_t i = 0; i < std::min(count, kWaterMaxWaves); ++i) {
        const WaterWaveGpu& w = waves[i];
        const float theta = w.dirAndK[2] * (w.dirAndK[0] * relX + w.dirAndK[1] * relZ) + w.ampPhase[2];
        const float s = 0.5f + 0.5f * std::sin(theta);
        h += w.ampPhase[0] * (std::pow(s, w.dirAndK[3]) - w.ampPhase[3]);
    }
    return h;
}

WaterShadeParamsGpu MakeWaterShadeParams(const WaterSettings& safe, const WaterFrameInputs& in) noexcept
{
    WaterShadeParamsGpu p;
    p.invViewProjection = in.invViewProjection;
    p.viewProjection = in.viewProjection;

    const double wrappedTime = std::fmod(in.timeSeconds, kTimeWrapSeconds);
    fx::PackVec3(p.eyeTime, in.eye, static_cast<float>(wrappedTime));
    p.viewInfo[0] = in.pixelScale;
    p.viewInfo[1] = in.jitterNdc.x;
    p.viewInfo[2] = in.jitterNdc.y;
    p.viewInfo[3] = safe.seaLevel;
    fx::PackVec3(p.sunDirection, in.sunDirection, safe.sunSpecular);
    fx::PackVec3(p.sunRadiance, in.sunRadiance, in.skyIntensity);
    p.skyInfo[0] = in.eyeHeightKm;
    p.skyInfo[1] = in.planetRadiusKm;
    p.skyInfo[2] = in.skyValid ? 1.0f : 0.0f;
    p.skyInfo[3] = in.ambientScale;
    fx::PackVec3(p.shallowColor, safe.shallowColor, safe.clarity);
    fx::PackVec3(p.deepColor, safe.deepColor, safe.refractionStrength);
    fx::PackVec3(p.foamColor, safe.foamColor, safe.crestFoam);
    p.surfaceInfo[0] = safe.shoreFoam;
    p.surfaceInfo[1] = safe.shoreWidth;
    p.surfaceInfo[2] = safe.roughness;
    p.surfaceInfo[3] = safe.reflectionStrength;
    p.rippleInfo[0] = safe.rippleStrength;
    p.rippleInfo[1] = safe.waveLodScale;
    p.rippleInfo[2] = safe.absorption;
    p.rippleInfo[3] = safe.maxDistance;
    p.wetInfo[0] = safe.wetBand;
    p.wetInfo[1] = safe.wetStrength;
    p.wetInfo[2] = safe.wetFadeDistance;
    p.wetInfo[3] = safe.ssrMaxDistance;

    p.counts[0] = safe.waveCount;
    p.counts[1] = static_cast<std::uint32_t>(safe.debugView);
    p.counts[2] = safe.ssrSteps;
    p.counts[3] = (safe.ssr ? kWaterFlagSsr : 0u) | (safe.wetShore ? kWaterFlagWet : 0u) | (in.skyValid ? kWaterFlagSky : 0u);

    // Ripple layers drift in different directions; scales are multiples of 1/8 so the lattice wraps with the eye.
    struct LayerSource {
        double scale;
        double velocityX;
        double velocityZ;
    };
    constexpr LayerSource kLayers[2] = { { 0.875, 0.45, 0.30 }, { 2.25, -0.35, 0.55 } };
    for (std::size_t i = 0; i < 2; ++i) {
        const double scale = std::max(std::round(kLayers[i].scale * static_cast<double>(safe.rippleScale) * 8.0) / 8.0, 0.125);
        const double period = scale * kRippleWrapMetres;
        const double ox = std::fmod((static_cast<double>(in.eye.x) + kLayers[i].velocityX * in.timeSeconds) * scale, period);
        const double oz = std::fmod((static_cast<double>(in.eye.z) + kLayers[i].velocityZ * in.timeSeconds) * scale, period);
        p.ripple[i].scaleOffsetPeriod[0] = static_cast<float>(scale);
        p.ripple[i].scaleOffsetPeriod[1] = static_cast<float>(ox < 0.0 ? ox + period : ox);
        p.ripple[i].scaleOffsetPeriod[2] = static_cast<float>(oz < 0.0 ? oz + period : oz);
        p.ripple[i].scaleOffsetPeriod[3] = static_cast<float>(period);
    }

    const WaterWaveTable waves = BuildWaterWaves(safe, static_cast<double>(in.eye.x), static_cast<double>(in.eye.z), in.timeSeconds);
    for (std::size_t i = 0; i < kWaterMaxWaves; ++i) {
        p.waves[i] = waves[i];
    }
    return p;
}

} // namespace gte
