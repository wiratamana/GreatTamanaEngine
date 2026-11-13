#include "VolumetricCloudMath.h"

#include "../../Core/FeatureKit/ScreenSpaceView.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <numeric>
#include <utility>

namespace gte {

namespace {

constexpr float kMinLayerThicknessM = 50.0f;
constexpr float kMinStepM = 1e-4f;
// A camera standing on the ground looks into it, even with rounding at 6371 km.
constexpr float kGroundSlackM = 0.5f;
constexpr float kMinPhaseDenominator = 1e-4f;
// Radiance of the blue sky light, relative to the sun's luminance.
constexpr float kAmbientTint[3] = { 0.55f, 0.7f, 1.0f };

float ClampOrDefault(float value, float low, float high, float fallback) noexcept
{
    return std::isfinite(value) ? std::clamp(value, low, high) : fallback;
}

float Saturate(float v) noexcept
{
    return std::clamp(v, 0.0f, 1.0f);
}

float SmoothStep(float lo, float hi, float v) noexcept
{
    const float t = Saturate((v - lo) / std::max(hi - lo, kMinStepM));
    return t * t * (3.0f - 2.0f * t);
}

// Both roots of the ray against a sphere of altitude `heightM`, centred below the eye.
struct SphereRoots {
    bool hit = false;
    float tNear = 0.0f;
    float tFar = 0.0f;
};

// The discriminant is a product of two sums, so it keeps its digits for a radius of 6371 km.
SphereRoots IntersectShell(float cameraY, float dirY, float radiusM, float heightM) noexcept
{
    const float ro = radiusM + cameraY;
    const float s = std::sqrt(std::max(1.0f - dirY * dirY, 0.0f));
    const float inner = (heightM - cameraY) + ro * dirY * dirY / (1.0f + s);
    const float outer = radiusM + heightM + ro * s;
    const float disc = inner * outer;
    SphereRoots roots;
    if (disc < 0.0f) {
        return roots;
    }
    const float root = std::sqrt(disc);
    const float b = ro * dirY;
    roots.hit = true;
    roots.tNear = -b - root;
    roots.tFar = -b + root;
    return roots;
}

} // namespace

VolumetricCloudSettings SanitizeVolumetricCloudSettings(const VolumetricCloudSettings& candidate) noexcept
{
    const VolumetricCloudSettings defaults;
    VolumetricCloudSettings out = candidate;
    for (const VolumetricCloudFloatSetting& entry : kVolumetricCloudFloatSettings) {
        out.*(entry.member) = ClampOrDefault(candidate.*(entry.member), entry.low, entry.high, defaults.*(entry.member));
    }
    out.layerTopM = std::max(out.layerTopM, out.layerBaseM + kMinLayerThicknessM);
    out.channel = std::min(candidate.channel, kCloudDebugChannelRgb);
    if (static_cast<std::uint8_t>(candidate.debugView) > static_cast<std::uint8_t>(VolumetricCloudDebugView::BlueNoise)) {
        out.debugView = VolumetricCloudDebugView::Off;
    }
    return out;
}

VolumetricCloudSettings ApplyVolumetricCloudPreset(
    const VolumetricCloudSettings& current, VolumetricCloudPreset preset) noexcept
{
    const VolumetricCloudSettings defaults;
    VolumetricCloudSettings out = current;
    out.coverage = defaults.coverage;
    out.density = defaults.density;
    switch (preset) {
    case VolumetricCloudPreset::Clear:
        out.coverage = 0.4f;
        out.density = 1.3f;
        break;
    case VolumetricCloudPreset::Scattered:
        break;
    case VolumetricCloudPreset::Broken:
        out.coverage = 0.9f;
        break;
    case VolumetricCloudPreset::Overcast:
        out.coverage = 0.97f;
        out.density = 1.5f;
        break;
    }
    return SanitizeVolumetricCloudSettings(out);
}

VolumetricCloudSlicePush MakeVolumetricCloudSlicePush(const VolumetricCloudSettings& settings) noexcept
{
    return VolumetricCloudSlicePush{ settings.slice, settings.tiling, settings.channel, 0u };
}

float CloudRemap(float v, float lo0, float hi0, float lo1, float hi1) noexcept
{
    float width = hi0 - lo0;
    if (std::abs(width) < kMinStepM) {
        width = width < 0.0f ? -kMinStepM : kMinStepM;
    }
    return lo1 + (v - lo0) / width * (hi1 - lo1);
}

float CloudAltitudeM(float horizontalSquared, float y, float radiusM) noexcept
{
    const float s = horizontalSquared + y * (2.0f * radiusM + y);
    return s / (std::sqrt(std::max(s + radiusM * radiusM, 0.0f)) + radiusM);
}

float CloudHeightFraction(float altitudeM, float baseM, float topM) noexcept
{
    return Saturate((altitudeM - baseM) / std::max(topM - baseM, kMinStepM));
}

float CloudShapeGradient(float heightFraction, float peak, float baseRoundness, float topRoundness,
    float topFalloffStart) noexcept
{
    const float stop = Saturate(peak + topRoundness);
    const float bottom = Saturate(CloudRemap(heightFraction, 0.0f, baseRoundness, 0.0f, 1.0f));
    const float top = Saturate(CloudRemap(heightFraction, stop * topFalloffStart, stop, 1.0f, 0.0f));
    return bottom * top;
}

float CloudStretchShape(float shape, float low, float high) noexcept
{
    return Saturate((shape - low) / std::max(high - low, kMinStepM));
}

float CloudSkyVisibility(float upwardOpticalDepth, float strength, float floor) noexcept
{
    return floor + (1.0f - floor) * std::exp(-std::max(upwardOpticalDepth, 0.0f) * strength);
}

float CloudGroundShare(float heightFraction) noexcept
{
    const float low = 1.0f - Saturate(heightFraction);
    return low * low;
}

Vec3 CloudOctaveTint(float blueShift, const Vec3& fullShiftTint) noexcept
{
    return Vec3(1.0f, 1.0f, 1.0f) + (fullShiftTint - Vec3(1.0f, 1.0f, 1.0f)) * Saturate(blueShift);
}

float CloudDensityGradient(float heightFraction, float ramp, float topFade) noexcept
{
    const float bottom = heightFraction * Saturate(CloudRemap(heightFraction, 0.0f, ramp, 0.0f, 1.0f));
    const float top = Saturate(CloudRemap(heightFraction, topFade, 1.0f, 1.0f, 0.0f));
    return bottom * top;
}

CloudShellSegment ComputeCloudShellSegment(
    float cameraY, float dirY, float radiusM, float baseM, float topM, float maxDistanceM) noexcept
{
    CloudShellSegment segment;
    const float y = std::max(cameraY, 0.0f);
    const SphereRoots top = IntersectShell(y, dirY, radiusM, topM);
    const SphereRoots base = IntersectShell(y, dirY, radiusM, baseM);

    float start = 0.0f;
    float end = 0.0f;
    if (y < baseM) {
        // The base sphere also holds the ground: a ray that leaves it only far away passes the layer.
        const SphereRoots ground = IntersectShell(y, dirY, radiusM, 0.0f);
        if (dirY < 0.0f && ground.hit && ground.tNear > -kGroundSlackM) {
            return segment;
        }
        start = base.tFar;
        end = top.tFar;
    } else if (y < topM) {
        end = top.tFar;
        if (base.hit && base.tNear > 0.0f) {
            end = std::min(end, base.tNear);
        }
    } else {
        if (!top.hit || top.tFar <= 0.0f) {
            return segment;
        }
        start = std::max(top.tNear, 0.0f);
        end = (base.hit && base.tNear > 0.0f) ? base.tNear : top.tFar;
    }
    end = std::min(end, maxDistanceM);
    if (!(end > start)) {
        return segment;
    }
    segment.hit = true;
    segment.tStart = start;
    segment.tEnd = end;
    return segment;
}

void AdvanceCloudWind(double (&offset)[2], double (&step)[2], float speed, float directionDegrees, float dt,
    double periodM) noexcept
{
    const double radians = static_cast<double>(directionDegrees) * kPi / 180.0;
    const double distance = static_cast<double>(speed) * static_cast<double>(dt);
    // The lookups read the noise at position + offset, so the pattern moves along the direction when the offset moves against it.
    step[0] = -std::cos(radians) * distance;
    step[1] = -std::sin(radians) * distance;
    for (int axis = 0; axis < 2; ++axis) {
        offset[axis] += step[axis];
        if (periodM > 0.0) {
            offset[axis] = std::fmod(offset[axis], periodM);
            offset[axis] += offset[axis] < 0.0 ? periodM : 0.0;
        }
    }
}

double CloudWindPeriodM(const VolumetricCloudSettings& safe) noexcept
{
    constexpr std::uint64_t kMaxPeriodM = 100000000ull;
    std::uint64_t period = 1;
    for (const float tile : { safe.weatherScaleM, safe.weatherScaleM * kCloudFarWeatherTileScale, safe.shapeScaleM,
             safe.shapeScaleM * kCloudLargeShapeScale, safe.detailScaleM }) {
        const std::uint64_t metres = static_cast<std::uint64_t>(std::max(std::llround(tile), 1ll));
        period = period / std::gcd(period, metres) * metres;
        if (period > kMaxPeriodM) {
            return 0.0;
        }
    }
    return static_cast<double>(period);
}

float CloudHenyeyGreenstein(float cosTheta, float g) noexcept
{
    const float g2 = g * g;
    const float denominator = std::max(1.0f + g2 - 2.0f * g * cosTheta, kMinPhaseDenominator);
    return (1.0f - g2) / (4.0f * kPi * denominator * std::sqrt(denominator));
}

float CloudDraine(float cosTheta, float g, float alpha) noexcept
{
    const float shape = (1.0f + alpha * cosTheta * cosTheta) / (1.0f + alpha * (1.0f + 2.0f * g * g) / 3.0f);
    return CloudHenyeyGreenstein(cosTheta, g) * shape;
}

float CloudPhase(const VolumetricCloudSettings& safe, float cosTheta, float eccentricity) noexcept
{
    if (safe.phaseModel >= 0.5f) {
        const float hg = CloudHenyeyGreenstein(cosTheta, kCloudDraineHgG * eccentricity);
        const float draine = CloudDraine(cosTheta, kCloudDraineG * eccentricity, kCloudDraineAlpha);
        return (1.0f - kCloudDraineWeight) * hg + kCloudDraineWeight * draine;
    }
    const float forward = CloudHenyeyGreenstein(cosTheta, safe.forwardG * eccentricity);
    const float backward = CloudHenyeyGreenstein(cosTheta, safe.backwardG * eccentricity);
    return forward + (backward - forward) * safe.backwardMix;
}

float CloudBeer(float opticalDepth) noexcept
{
    return std::exp(-std::max(opticalDepth, 0.0f));
}

float CloudSunScatter(const VolumetricCloudSettings& safe, float cosTheta, float opticalDepth) noexcept
{
    float sum = 0.0f;
    float attenuation = 1.0f;
    float contribution = 1.0f;
    float eccentricity = 1.0f;
    const int octaves = static_cast<int>(std::lround(safe.msOctaves));
    for (int i = 0; i < octaves; ++i) {
        sum += contribution * CloudPhase(safe, cosTheta, eccentricity) * CloudBeer(opticalDepth * attenuation);
        attenuation *= safe.msAttenuation;
        contribution *= safe.msContribution;
        eccentricity *= safe.msEccentricity;
    }
    return sum + safe.silverIntensity * CloudHenyeyGreenstein(cosTheta, safe.silverG) * CloudBeer(opticalDepth);
}

float CloudStepOpacity(float extinction, float stepLength) noexcept
{
    return 1.0f - std::exp(-std::max(extinction, 0.0f) * std::max(stepLength, 0.0f));
}

void CloudAccumulateStep(float& light, float& transmittance, float source, float opacity) noexcept
{
    light += transmittance * source * opacity;
    transmittance *= 1.0f - opacity;
}

float CloudPlanetRadiusKm(const VolumetricCloudSettings& safe, const VolumetricCloudFrameFacts& facts) noexcept
{
    const bool shared = facts.atmosphereLit && std::isfinite(facts.atmospherePlanetRadiusKm) && facts.atmospherePlanetRadiusKm >= 1.0f;
    return shared ? facts.atmospherePlanetRadiusKm : safe.planetRadiusKm;
}

VolumetricCloudParamsGpu MakeVolumetricCloudParams(
    const VolumetricCloudSettings& safe, const VolumetricCloudFrameFacts& facts) noexcept
{
    VolumetricCloudParamsGpu p;
    p.invViewProjection = facts.invViewProjection;
    fx::PackVec3(p.eyeAndPlanetRadius, facts.eye, CloudPlanetRadiusKm(safe, facts) * 1000.0f);

    // A sun under the horizon lights nothing; the sky keeps a little ambient light.
    const float sunVisibility = SmoothStep(-0.05f, 0.15f, facts.sunDirection.y);
    fx::PackVec3(p.sunDirectionAndIntensity, facts.sunDirection, safe.sunIntensity);
    const Vec3 sun = facts.sunIlluminance * facts.sunTint;
    // With the atmosphere's tables the sun goes in untouched: they hold its colour and the planet's shadow.
    if (facts.atmosphereLit) {
        fx::PackVec3(p.sunColorAndAmbient, facts.sunIlluminance, safe.ambientIntensity);
    } else {
        fx::PackVec3(p.sunColorAndAmbient, sun * sunVisibility, safe.ambientIntensity);
    }

    const float luminance = 0.2126f * sun.x + 0.7152f * sun.y + 0.0722f * sun.z;
    const float ambient = luminance * facts.ambientScale * (0.15f + 0.85f * sunVisibility);
    fx::PackVec3(p.ambientColor, Vec3(kAmbientTint[0], kAmbientTint[1], kAmbientTint[2]) * ambient);

    p.layer[0] = safe.layerBaseM;
    p.layer[1] = safe.layerTopM;
    p.layer[2] = safe.maxDistanceM;
    p.layer[3] = safe.fadeStart;
    p.weather[0] = safe.weatherScaleM;
    p.weather[1] = facts.windOffset[0];
    p.weather[2] = facts.windOffset[1];
    p.weather[3] = safe.coverage;
    p.noise[0] = safe.shapeScaleM;
    p.noise[1] = safe.detailScaleM;
    p.noise[2] = safe.detailStrength;
    p.noise[3] = safe.detailFadeDistM;
    p.density[0] = safe.density;
    p.density[1] = safe.extinction;
    p.density[2] = safe.peakMin;
    p.density[3] = safe.jitter;
    p.heightShape[0] = safe.baseRoundness;
    p.heightShape[1] = safe.topRoundness;
    p.heightShape[2] = safe.densityRamp;
    p.heightShape[3] = safe.densityTopFade;
    p.march[0] = safe.stepBaseM;
    p.march[1] = safe.stepGrowth;
    p.march[2] = safe.coarseMultiplier;
    p.march[3] = safe.minTransmittance;
    p.counts[0] = static_cast<std::uint32_t>(std::lround(safe.maxSteps));
    p.counts[1] = static_cast<std::uint32_t>(facts.frameIndex & 0xFFFFFFFFu);
    p.counts[2] = static_cast<std::uint32_t>(std::lround(safe.emptyToCoarse));
    p.counts[3] = static_cast<std::uint32_t>(std::lround(safe.lightStride));
    p.skip[0] = safe.clearSkip;
    p.skip[1] = safe.topFalloffStart;
    p.skip[2] = safe.shapeLow;
    p.skip[3] = std::max(safe.shapeHigh, safe.shapeLow + 0.05f);
    p.ambientColor[3] = safe.densityCurve;
    p.lightMarch[0] = std::round(safe.sunSteps);
    p.lightMarch[1] = safe.lightReach;
    p.lightMarch[2] = std::round(safe.msOctaves);
    p.lightMarch[3] = safe.powder;
    p.lightPhase[0] = safe.forwardG;
    p.lightPhase[1] = safe.backwardG;
    p.lightPhase[2] = safe.backwardMix;
    p.lightPhase[3] = safe.silverIntensity;
    p.lightScatter[0] = safe.silverG;
    p.lightScatter[1] = safe.msAttenuation;
    p.lightScatter[2] = safe.msContribution;
    p.lightScatter[3] = safe.msEccentricity;
    p.lightMisc[0] = std::round(safe.phaseModel);
    p.lightMisc[1] = safe.ambientBottom;
    p.lightMisc[2] = safe.hazeDistanceM;
    p.lightMisc[3] = safe.lightDetail;
    p.drainePhase[0] = kCloudDraineHgG;
    p.drainePhase[1] = kCloudDraineG;
    p.drainePhase[2] = kCloudDraineAlpha;
    p.drainePhase[3] = kCloudDraineWeight;
    const std::uint32_t divisor = CloudResolutionDivisor(safe, facts.viewWidth);
    const CloudSampleOffset offset = CloudBayerOffset(facts.frameIndex, divisor);
    p.lowRes[0] = divisor;
    p.lowRes[1] = offset.x;
    p.lowRes[2] = offset.y;
    p.lowRes[3] = static_cast<std::uint32_t>(std::lround(safe.subpixelJitter * 100.0f));
    p.fullSize[0] = facts.viewWidth;
    p.fullSize[1] = facts.viewHeight;
    p.atmosphereMode[0] = facts.atmosphereLit ? 1.0f : 0.0f;
    p.atmosphereMode[1] = facts.skyIntensity;
    p.atmosphereMode[2] = safe.airScale;
    p.atmosphereMode[3] = safe.skyAmbient;
    fx::PackVec3(p.atmosphereCamera, facts.atmosphereCameraKm);
    p.skyShade[0] = safe.skyOcclusion;
    p.skyShade[1] = safe.skyOcclusionReachM;
    p.skyShade[2] = safe.skyFloor;
    p.skyShade[3] = safe.groundBounce;
    p.skyShade2[0] = safe.groundWarmth;
    p.skyShade2[1] = safe.msBlueShift;
    p.skyShade2[2] = safe.farWeatherStartM;
    p.skyShade2[3] = safe.farWeatherMix;
    p.farField[0] = safe.farCoverageLift;
    p.farField[1] = safe.farAirScale;
    p.farField[2] = safe.maxStepDepth;
    p.farField[3] = safe.inScatter;
    p.detailShape[0] = safe.curlAmountM;
    p.detailShape[1] = safe.curlScaleM;
    p.detailShape[2] = safe.wispiness;
    p.detailShape[3] = safe.fineDetail;
    // The tops lean downwind: the noise is read at a place shifted against the wind direction.
    const double windRadians = static_cast<double>(safe.windDirection) * kPi / 180.0;
    p.structure[0] = safe.largeShapeMix;
    p.structure[1] = safe.sizeVariation;
    p.structure[2] = static_cast<float>(-std::cos(windRadians) * static_cast<double>(safe.windShearM));
    p.structure[3] = static_cast<float>(-std::sin(windRadians) * static_cast<double>(safe.windShearM));
    p.structure2[0] = safe.bodyErosion;
    p.structure2[1] = safe.verticalStretch;
    p.structure2[2] = safe.baseVariation;
    p.structure2[3] = safe.farSoftening;
    p.structure3[0] = safe.shapeWorley;
    p.structure3[1] = safe.bodyThresholdMin;
    p.structure3[2] = safe.baseRagged;
    p.structure3[3] = safe.bodyWidth;
    p.lightTune[0] = safe.ambientSaturation;
    p.lightTune[1] = safe.msDiffuse;
    p.lightTune[2] = safe.msGlow;
    p.lightTune2[0] = safe.sunWarmth;
    return p;
}

CloudShadowGrid MakeCloudShadowGrid(
    const VolumetricCloudSettings& safe, const Vec3& eye, const Vec3& sunDirection, std::uint32_t mapSize) noexcept
{
    CloudShadowGrid grid;
    grid.texelM = safe.shadowRangeKm * 1000.0f / static_cast<float>(std::max(mapSize, 1u));
    const double texel = static_cast<double>(grid.texelM);
    // The map is read where a receiver's sun ray crosses the layer: centre it on the crossing of the eye's own ray.
    const double sunUp = std::max(static_cast<double>(sunDirection.y), 0.04);
    const double along = (0.5 * (static_cast<double>(safe.layerBaseM) + static_cast<double>(safe.layerTopM)) - static_cast<double>(eye.y)) / sunUp;
    const double crossX = static_cast<double>(eye.x) + static_cast<double>(sunDirection.x) * along;
    const double crossZ = static_cast<double>(eye.z) + static_cast<double>(sunDirection.z) * along;
    grid.centerX = std::round(crossX / texel) * texel;
    grid.centerZ = std::round(crossZ / texel) * texel;
    return grid;
}

void SetCloudShaftLookup(VolumetricCloudParamsGpu& params, const CloudShadowMap& map) noexcept
{
    params.shadowGrid[0] = 1.0f / std::max(map.extentM, 1.0f);
    params.shadowGrid[1] = map.centerX;
    params.shadowGrid[2] = map.centerZ;
    params.shadowTune[3] = std::min(map.shaftStrength, 1.0f);
    params.shadowShift[2] = std::max(map.shaftSharpness, 1.0f);
}

VolumetricCloudParamsGpu MakeVolumetricCloudShadowParams(const VolumetricCloudSettings& safe,
    const VolumetricCloudFrameFacts& facts, const VolumetricCloudShadowFacts& shadow) noexcept
{
    VolumetricCloudParamsGpu p = MakeVolumetricCloudParams(safe, facts);
    p.shadowGrid[0] = shadow.grid.texelM;
    p.shadowGrid[1] = static_cast<float>(shadow.grid.centerX);
    p.shadowGrid[2] = static_cast<float>(shadow.grid.centerZ);
    p.shadowGrid[3] = 1.0f;
    p.shadowShift[0] = static_cast<float>(shadow.shiftX);
    p.shadowShift[1] = static_cast<float>(shadow.shiftZ);
    p.shadowShift[2] = shadow.historyBlend;
    p.shadowShift[3] = safe.shadowDensity;
    p.shadowTune[0] = safe.shadowSoftness;
    p.shadowTune[1] = std::round(safe.shadowUpdate);
    p.shadowTune[2] = std::round(safe.shadowSteps);
    return p;
}

std::uint32_t CloudResolutionDivisor(const VolumetricCloudSettings& safe, std::uint32_t viewWidth) noexcept
{
    if (safe.resolutionDivisor < 0.5f) {
        if (viewWidth == 0) {
            return 4;
        }
        const long wanted = std::lround(static_cast<double>(viewWidth) / static_cast<double>(kCloudAutoMarchTexels));
        return static_cast<std::uint32_t>(std::clamp(wanted, 1l, static_cast<long>(kCloudMaxResolutionDivisor)));
    }
    const long rounded = std::lround(static_cast<double>(safe.resolutionDivisor));
    return static_cast<std::uint32_t>(std::clamp(rounded, 1l, static_cast<long>(kCloudMaxResolutionDivisor)));
}

CloudSampleOffset CloudBayerOffset(std::uint64_t frameIndex, std::uint32_t divisor) noexcept
{
    // Rank of each cell of the Bayer matrix; the cell holding rank k is visited on frame k.
    constexpr std::uint32_t kBayer2[4] = { 0, 2, 3, 1 };
    // Divisor 3: a 3 x 3 order that keeps consecutive frames apart (no two consecutive ranks are neighbours).
    constexpr std::uint32_t kBayer3[9] = { 0, 7, 3, 6, 4, 1, 2, 8, 5 };
    constexpr std::uint32_t kBayer4[16] = { 0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5 };
    if (divisor < 2 || divisor > kCloudMaxResolutionDivisor) {
        return CloudSampleOffset{};
    }
    const std::uint32_t cells = divisor * divisor;
    const std::uint32_t rank = static_cast<std::uint32_t>(frameIndex % cells);
    if (divisor > 4) {
        // Divisors 5 and 6: cell (rank * stride) mod cells with a stride coprime to the cell count visits every cell once
        // per cycle and keeps consecutive frames far apart.
        const std::uint32_t stride = divisor == 5 ? 16u : 23u;
        const std::uint32_t cell = (rank * stride) % cells;
        return CloudSampleOffset{ cell % divisor, cell / divisor };
    }
    const std::uint32_t* matrix = divisor == 2 ? kBayer2 : (divisor == 3 ? kBayer3 : kBayer4);
    for (std::uint32_t i = 0; i < cells; ++i) {
        if (matrix[i] == rank) {
            return CloudSampleOffset{ i % divisor, i / divisor };
        }
    }
    return CloudSampleOffset{};
}

std::uint32_t CloudOwnSampleIndex(std::uint64_t frameIndex, std::uint32_t divisor) noexcept
{
    const std::uint64_t cycle = (divisor >= 1 && divisor <= kCloudMaxResolutionDivisor) ? std::uint64_t{ divisor } * divisor : 1u;
    return static_cast<std::uint32_t>((frameIndex / cycle) & 255u);
}

float CloudMarchJitter(float blueNoise, std::uint64_t frameIndex, std::uint32_t divisor) noexcept
{
    constexpr double kGoldenRatio = 0.61803398875;
    const double value = static_cast<double>(blueNoise) + kGoldenRatio * CloudOwnSampleIndex(frameIndex, divisor);
    return static_cast<float>(value - std::floor(value));
}

VolumetricCloudResolveParamsGpu MakeVolumetricCloudResolveParams(const VolumetricCloudSettings& safe,
    const VolumetricCloudFrameFacts& facts, const VolumetricCloudResolveFacts& resolve) noexcept
{
    VolumetricCloudResolveParamsGpu p;
    p.invViewProjection = facts.invViewProjection;
    p.prevViewProjection = resolve.prevViewProjection;
    fx::PackVec3(p.eye, facts.eye);
    p.windDelta[0] = resolve.windDelta[0];
    p.windDelta[2] = resolve.windDelta[1];
    const bool accumulate = safe.temporal >= 0.5f;
    p.blend[0] = accumulate ? safe.historyBlend : 1.0f;
    p.blend[1] = accumulate ? safe.freshBlend : 1.0f;
    p.blend[2] = safe.clampExpand;
    p.blend[3] = kCloudHistoryFallbackDistanceM;
    p.motion[0] = accumulate ? safe.motionBlend : 1.0f;
    p.motion[1] = accumulate ? safe.historySharp : 0.0f;
    p.motion[2] = accumulate && safe.adaptiveBlend >= 0.5f ? 1.0f : 0.0f;
    const std::uint32_t divisor = CloudResolutionDivisor(safe, facts.viewWidth);
    const CloudSampleOffset offset = CloudBayerOffset(facts.frameIndex, divisor);
    p.counts[0] = divisor;
    p.counts[1] = offset.x;
    p.counts[2] = offset.y;
    p.counts[3] = (accumulate && resolve.historyValid) ? 1u : 0u;
    p.sizes[0] = facts.viewWidth;
    p.sizes[1] = facts.viewHeight;
    p.sizes[2] = resolve.lowWidth;
    p.sizes[3] = resolve.lowHeight;
    p.sunRay[0] = resolve.sunUv[0];
    p.sunRay[1] = resolve.sunUv[1];
    p.sunRay[2] = safe.sunRays * resolve.sunRayFade;
    p.sunRay[3] = safe.sunStar * resolve.sunRayFade;
    for (int i = 0; i < 3; ++i) {
        p.sunRayColor[i] = resolve.sunColor[i];
    }
    p.sunRayColor[3] = kCloudSunRayGlowRadius;
    return p;
}

float CloudAerialComposite(float scene, float cloudTransmittance, float cloudLight, float airTransmittance,
    float airLight) noexcept
{
    return scene * cloudTransmittance + cloudLight * airTransmittance + (1.0f - cloudTransmittance) * airLight;
}

bool CloudTapUsable(float tapEndKm, float tapDistanceKm, float tapTransmittance, float pixelEndKm) noexcept
{
    constexpr float kRelative = 0.03f;
    constexpr float kSlackKm = 0.02f;
    // The tap must have looked at least as far as this pixel's surface.
    if (tapEndKm < (1.0f - kRelative) * pixelEndKm - kSlackKm) {
        return false;
    }
    // A cloud that sits behind this pixel's surface must not be added to it.
    return tapTransmittance > 0.999f || tapDistanceKm <= (1.0f + kRelative) * pixelEndKm + kSlackKm;
}

} // namespace gte
