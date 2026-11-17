#include "WaterMath.h"

#include "../../Core/FeatureKit/ScreenSpaceView.h"

#include <algorithm>
#include <cmath>
#include <limits>

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

    s.crestFoam = f(settings.crestFoam, 0.0f, 2.0f, d.crestFoam);
    s.shoreFoam = f(settings.shoreFoam, 0.0f, 2.0f, d.shoreFoam);
    s.shoreWidth = f(settings.shoreWidth, 0.1f, 30.0f, d.shoreWidth);

    s.ssrSteps = std::clamp<std::uint32_t>(settings.ssrSteps, 4u, 48u);
    s.ssrMaxDistance = f(settings.ssrMaxDistance, 10.0f, 20000.0f, d.ssrMaxDistance);

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

bool WaterPointInPolygon(std::span<const WaterBodyPoint> polygon, Vec2 p) noexcept
{
    if (polygon.size() < 3) {
        return false;
    }
    bool inside = false;
    Vec2 prev = polygon.back().xz;
    for (const WaterBodyPoint& point : polygon) {
        const Vec2 cur = point.xz;
        if ((cur.y > p.y) != (prev.y > p.y)
            && p.x < (prev.x - cur.x) * (p.y - cur.y) / (prev.y - cur.y) + cur.x) {
            inside = !inside;
        }
        prev = cur;
    }
    return inside;
}

float WaterRiverQuery(std::span<const WaterBodyPoint> line, Vec2 p, float& level, float& halfWidth, Vec2& tangent) noexcept
{
    float best = std::numeric_limits<float>::max();
    level = 0.0f;
    halfWidth = 0.0f;
    tangent = Vec2(1.0f, 0.0f);
    for (std::size_t i = 0; i + 1 < line.size(); ++i) {
        const WaterBodyPoint& a = line[i];
        const WaterBodyPoint& b = line[i + 1];
        const Vec2 ab = b.xz - a.xz;
        const float l2 = Dot(ab, ab);
        const float u = l2 > 1e-6f ? std::clamp(Dot(p - a.xz, ab) / l2, 0.0f, 1.0f) : 0.0f;
        const Vec2 closest = a.xz + ab * u;
        const float width = a.halfWidth + (b.halfWidth - a.halfWidth) * u;
        const float signedDistance = Length(p - closest) - width;
        if (signedDistance < best) {
            best = signedDistance;
            level = a.level + (b.level - a.level) * u;
            halfWidth = width;
            tangent = l2 > 1e-6f ? ab * (1.0f / std::sqrt(l2)) : Vec2(1.0f, 0.0f);
        }
    }
    return best;
}

std::vector<WaterBodyPoint> WaterSampleSpline(std::span<const WaterBodyPoint> control, std::uint32_t perSegment)
{
    std::vector<WaterBodyPoint> out;
    if (control.size() < 2) {
        return out;
    }
    const std::uint32_t steps = std::max(perSegment, 1u);
    const auto at = [&](std::ptrdiff_t i) {
        return control[static_cast<std::size_t>(std::clamp<std::ptrdiff_t>(i, 0, static_cast<std::ptrdiff_t>(control.size()) - 1))];
    };
    const auto blend = [](float p0, float p1, float p2, float p3, float t) {
        const float t2 = t * t;
        const float t3 = t2 * t;
        return 0.5f * ((2.0f * p1) + (-p0 + p2) * t + (2.0f * p0 - 5.0f * p1 + 4.0f * p2 - p3) * t2
                          + (-p0 + 3.0f * p1 - 3.0f * p2 + p3) * t3);
    };
    for (std::size_t i = 0; i + 1 < control.size(); ++i) {
        const WaterBodyPoint p0 = at(static_cast<std::ptrdiff_t>(i) - 1);
        const WaterBodyPoint p1 = at(static_cast<std::ptrdiff_t>(i));
        const WaterBodyPoint p2 = at(static_cast<std::ptrdiff_t>(i) + 1);
        const WaterBodyPoint p3 = at(static_cast<std::ptrdiff_t>(i) + 2);
        for (std::uint32_t k = 0; k < steps; ++k) {
            const float t = static_cast<float>(k) / static_cast<float>(steps);
            WaterBodyPoint s;
            s.xz = Vec2(blend(p0.xz.x, p1.xz.x, p2.xz.x, p3.xz.x, t), blend(p0.xz.y, p1.xz.y, p2.xz.y, p3.xz.y, t));
            s.level = blend(p0.level, p1.level, p2.level, p3.level, t);
            s.halfWidth = std::max(blend(p0.halfWidth, p1.halfWidth, p2.halfWidth, p3.halfWidth, t), 0.0f);
            out.push_back(s);
        }
    }
    out.push_back(control.back());
    if (out.size() > kWaterMaxRiverPoints) {
        // Too many samples: keep an even subset, always including both ends.
        std::vector<WaterBodyPoint> reduced;
        reduced.reserve(kWaterMaxRiverPoints);
        for (std::uint32_t i = 0; i < kWaterMaxRiverPoints; ++i) {
            const std::size_t index = static_cast<std::size_t>(
                static_cast<double>(i) * static_cast<double>(out.size() - 1) / static_cast<double>(kWaterMaxRiverPoints - 1) + 0.5);
            reduced.push_back(out[std::min(index, out.size() - 1)]);
        }
        out = std::move(reduced);
    }
    return out;
}

WaterBodiesGpu PackWaterBodies(std::span<const WaterBodyDesc> bodies, float seaLevel, std::uint32_t& packedBodies) noexcept
{
    WaterBodiesGpu out;
    std::uint32_t bodyCount = 0;
    std::uint32_t pointCount = 0;
    const auto finite = [](float v) { return std::isfinite(v); };

    for (const WaterBodyDesc& body : bodies) {
        if (bodyCount >= kWaterMaxBodies || !finite(body.level) || !finite(body.waveScale) || !finite(body.flowSpeed)) {
            continue;
        }
        WaterBodyHeaderGpu header;
        header.info[0] = static_cast<float>(static_cast<std::uint8_t>(body.kind));
        header.info[1] = body.level;
        header.info[2] = std::clamp(body.waveScale, 0.0f, 2.0f);
        header.info[3] = static_cast<float>(bodyCount);
        header.shape[0] = std::clamp(body.flowSpeed, -20.0f, 20.0f);
        header.shape[1] = header.shape[2] = body.level;

        const auto copyPoints = [&](std::uint32_t minCount, std::uint32_t maxCount) -> bool {
            const std::uint32_t count = std::min<std::uint32_t>(static_cast<std::uint32_t>(body.points.size()), maxCount);
            if (count < minCount || pointCount + count > kWaterMaxPoints) {
                return false;
            }
            Vec2 lo(std::numeric_limits<float>::max(), std::numeric_limits<float>::max());
            Vec2 hi(std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest());
            float margin = 0.0f;
            float levelLo = std::numeric_limits<float>::max();
            float levelHi = std::numeric_limits<float>::lowest();
            for (std::uint32_t i = 0; i < count; ++i) {
                const WaterBodyPoint& p = body.points[i];
                if (!finite(p.xz.x) || !finite(p.xz.y) || !finite(p.level) || !finite(p.halfWidth)) {
                    return false;
                }
                float* dst = out.points[pointCount + i];
                dst[0] = p.xz.x;
                dst[1] = p.xz.y;
                dst[2] = p.level;
                dst[3] = std::max(p.halfWidth, 0.0f);
                lo = Vec2(std::min(lo.x, p.xz.x), std::min(lo.y, p.xz.y));
                hi = Vec2(std::max(hi.x, p.xz.x), std::max(hi.y, p.xz.y));
                margin = std::max(margin, dst[3]);
                levelLo = std::min(levelLo, p.level);
                levelHi = std::max(levelHi, p.level);
            }
            header.bounds[0] = lo.x - margin;
            header.bounds[1] = lo.y - margin;
            header.bounds[2] = hi.x + margin;
            header.bounds[3] = hi.y + margin;
            header.range[0] = pointCount;
            header.range[1] = count;
            header.shape[1] = levelLo;
            header.shape[2] = levelHi;
            pointCount += count;
            return true;
        };

        bool ok = true;
        switch (body.kind) {
        case WaterBodyKind::Ocean:
            header.bounds[0] = header.bounds[1] = std::numeric_limits<float>::lowest();
            header.bounds[2] = header.bounds[3] = std::numeric_limits<float>::max();
            break;
        case WaterBodyKind::LakeBox:
            ok = finite(body.boxMin.x) && finite(body.boxMin.y) && finite(body.boxMax.x) && finite(body.boxMax.y)
                && body.boxMax.x > body.boxMin.x && body.boxMax.y > body.boxMin.y;
            header.bounds[0] = body.boxMin.x;
            header.bounds[1] = body.boxMin.y;
            header.bounds[2] = body.boxMax.x;
            header.bounds[3] = body.boxMax.y;
            break;
        case WaterBodyKind::LakePolygon:
            ok = copyPoints(3, kWaterMaxLakePoints);
            break;
        case WaterBodyKind::River:
            ok = copyPoints(2, kWaterMaxRiverPoints);
            break;
        }
        if (ok) {
            out.headers[bodyCount++] = header;
        }
    }

    if (bodyCount == 0 && bodies.empty()) {
        WaterBodyHeaderGpu ocean;
        ocean.info[0] = static_cast<float>(static_cast<std::uint8_t>(WaterBodyKind::Ocean));
        ocean.info[1] = seaLevel;
        ocean.info[2] = 1.0f;
        ocean.bounds[0] = ocean.bounds[1] = std::numeric_limits<float>::lowest();
        ocean.bounds[2] = ocean.bounds[3] = std::numeric_limits<float>::max();
        out.headers[bodyCount++] = ocean;
    }
    out.counts[0] = bodyCount;
    packedBodies = bodyCount;
    return out;
}

WaterProbe WaterProbePoint(std::span<const WaterBodyDesc> bodies, const WaterSettings& safe, double timeSeconds, Vec2 xz) noexcept
{
    WaterProbe probe;
    float bestLevel = std::numeric_limits<float>::lowest();
    const auto consider = [&](int index, float level, float scale) {
        if (level > bestLevel) {
            bestLevel = level;
            probe.body = index;
            probe.level = level;
            probe.waveScale = scale;
        }
    };
    if (bodies.empty()) {
        consider(0, safe.seaLevel, 1.0f);
    }
    for (std::size_t i = 0; i < bodies.size(); ++i) {
        const WaterBodyDesc& b = bodies[i];
        const int index = static_cast<int>(i);
        switch (b.kind) {
        case WaterBodyKind::Ocean:
            consider(index, b.level, b.waveScale);
            break;
        case WaterBodyKind::LakeBox:
            if (xz.x >= b.boxMin.x && xz.x <= b.boxMax.x && xz.y >= b.boxMin.y && xz.y <= b.boxMax.y) {
                consider(index, b.level, b.waveScale);
            }
            break;
        case WaterBodyKind::LakePolygon:
            if (WaterPointInPolygon(b.points, xz)) {
                consider(index, b.level, b.waveScale);
            }
            break;
        case WaterBodyKind::River: {
            float level = 0.0f;
            float halfWidth = 0.0f;
            Vec2 tangent;
            if (WaterRiverQuery(b.points, xz, level, halfWidth, tangent) <= 0.0f) {
                consider(index, level, b.waveScale);
            }
            break;
        }
        }
    }
    probe.inWater = probe.body >= 0;
    if (probe.inWater) {
        const WaterWaveTable waves = BuildWaterWaves(safe, 0.0, 0.0, timeSeconds);
        probe.waveHeight = WaterWaveHeight(waves, safe.waveCount, xz.x, xz.y) * probe.waveScale;
        probe.surfaceY = probe.level + probe.waveHeight;
    }
    return probe;
}

bool WaterAnyBodyBelowEye(const WaterBodiesGpu& packed, float eyeY) noexcept
{
    for (std::uint32_t i = 0; i < std::min(packed.counts[0], kWaterMaxBodies); ++i) {
        if (packed.headers[i].info[1] < eyeY - 0.05f) {
            return true;
        }
    }
    return false;
}

} // namespace gte
