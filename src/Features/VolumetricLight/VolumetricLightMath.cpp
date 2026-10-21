#include "VolumetricLightMath.h"

#include "../../Game/Lighting/SunLightModulation.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>

namespace gte {

namespace {

// A frame has a camera cut when the eye jumps or the view turns past these limits.
constexpr float kCutMinDistance = 2.0f;
constexpr float kCutDistanceFraction = 0.25f; // Of the previous max distance.
constexpr float kCutMinForwardDot = 0.8660254f; // cos(30 deg).

// History fades out as the sun turns by this angle.
constexpr float kSunMoveResetDegrees = 1.0f;

// Sun elevation range of the horizon fade.
constexpr float kFadeStartDegrees = -2.0f;
constexpr float kFadeEndDegrees = 5.0f;

bool IsFinite(const Vec3& v) noexcept
{
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}

float SanitizeFloat(float value, float lo, float hi, float fallback) noexcept
{
    return std::isfinite(value) ? std::clamp(value, lo, hi) : fallback;
}

// Clamps to 0..1; NaN gives 0.
float Saturate(float value) noexcept
{
    return value > 0.0f ? (value < 1.0f ? value : 1.0f) : 0.0f;
}

float SmoothStep(float edge0, float edge1, float x) noexcept
{
    const float t = Saturate((x - edge0) / (edge1 - edge0));
    return t * t * (3.0f - 2.0f * t);
}

// Nearest candidate; candidates ascend, so a tie goes to the larger one.
template <std::size_t N>
std::uint32_t NearestOf(const std::array<std::uint32_t, N>& candidates, std::uint32_t value) noexcept
{
    std::uint32_t best = candidates[0];
    std::uint32_t bestDiff = ~0u;
    for (const std::uint32_t candidate : candidates) {
        const std::uint32_t diff = candidate > value ? candidate - value : value - candidate;
        if (diff <= bestDiff) {
            best = candidate;
            bestDiff = diff;
        }
    }
    return best;
}

constexpr std::array<std::uint32_t, 4> kShadowSampleCounts{ 1, 2, 4, 8 };

Vec3 SafeDirection(const Vec3& direction) noexcept
{
    if (!IsFinite(direction) || LengthSquared(direction) < 1e-12f) {
        return Vec3::Up();
    }
    return Normalize(direction);
}

void Store(float (&dst)[4], const Vec3& v, float w = 0.0f) noexcept
{
    dst[0] = v.x;
    dst[1] = v.y;
    dst[2] = v.z;
    dst[3] = w;
}

std::uint64_t HashFloat(std::uint64_t hash, float value) noexcept
{
    hash ^= std::bit_cast<std::uint32_t>(value);
    return hash * 1099511628211ull;
}

float RadicalInverse(std::uint32_t index, std::uint32_t base) noexcept
{
    float factor = 1.0f;
    float result = 0.0f;
    while (index > 0) {
        factor /= static_cast<float>(base);
        result += factor * static_cast<float>(index % base);
        index /= base;
    }
    return result;
}

double WrapOffset(double value) noexcept
{
    if (!std::isfinite(value)) {
        return 0.0;
    }
    double wrapped = std::fmod(value, kVolumetricLightNoiseWrap);
    if (wrapped < 0.0) {
        wrapped += kVolumetricLightNoiseWrap;
    }
    return wrapped >= kVolumetricLightNoiseWrap ? 0.0 : wrapped;
}

} // namespace

VolumetricLightSettings SanitizeVolumetricLightSettings(const VolumetricLightSettings& settings) noexcept
{
    constexpr VolumetricLightSettings d{};
    VolumetricLightSettings s = settings;

    s.density = SanitizeFloat(settings.density, 0.0f, 1.0f, d.density);
    s.albedo = SanitizeFloat(settings.albedo, 0.0f, 1.0f, d.albedo);
    s.anisotropyG = SanitizeFloat(settings.anisotropyG, -0.95f, 0.95f, d.anisotropyG);
    s.heightDensity = SanitizeFloat(settings.heightDensity, 0.0f, 1.0f, d.heightDensity);
    s.heightBase = SanitizeFloat(settings.heightBase, -100000.0f, 100000.0f, d.heightBase);
    s.heightFalloff = SanitizeFloat(settings.heightFalloff, 0.0f, 2.0f, d.heightFalloff);
    s.noiseAmount = SanitizeFloat(settings.noiseAmount, 0.0f, 1.0f, d.noiseAmount);
    s.noiseScale = SanitizeFloat(settings.noiseScale, 0.001f, 4.0f, d.noiseScale);
    s.windX = SanitizeFloat(settings.windX, -20.0f, 20.0f, d.windX);
    s.windZ = SanitizeFloat(settings.windZ, -20.0f, 20.0f, d.windZ);
    s.maxDistance = SanitizeFloat(settings.maxDistance, kVolumetricLightMinDistance, 2000.0f, d.maxDistance);
    s.startDistance = SanitizeFloat(settings.startDistance, 0.0f, s.maxDistance, d.startDistance);
    s.depthExponent = SanitizeFloat(settings.depthExponent, 1.0f, 4.0f, d.depthExponent);
    s.sunIntensity = SanitizeFloat(settings.sunIntensity, 0.0f, 16.0f, d.sunIntensity);
    s.ambientR = SanitizeFloat(settings.ambientR, 0.0f, 4.0f, d.ambientR);
    s.ambientG = SanitizeFloat(settings.ambientG, 0.0f, 4.0f, d.ambientG);
    s.ambientB = SanitizeFloat(settings.ambientB, 0.0f, 4.0f, d.ambientB);
    s.ambientIntensity = SanitizeFloat(settings.ambientIntensity, 0.0f, 16.0f, d.ambientIntensity);
    s.ambientShadowedFraction = SanitizeFloat(settings.ambientShadowedFraction, 0.0f, 1.0f, d.ambientShadowedFraction);
    s.shadowSamples = NearestOf(kShadowSampleCounts, settings.shadowSamples);
    s.shadowFilterTexels = SanitizeFloat(settings.shadowFilterTexels, 0.0f, 8.0f, d.shadowFilterTexels);
    s.shadowBiasTexels = SanitizeFloat(settings.shadowBiasTexels, 0.0f, 16.0f, d.shadowBiasTexels);
    s.historyBlend = SanitizeFloat(settings.historyBlend, 0.0f, 0.98f, d.historyBlend);
    s.strength = SanitizeFloat(settings.strength, 0.0f, 1.0f, d.strength);
    s.skyDistanceFraction = SanitizeFloat(settings.skyDistanceFraction, 0.0f, 1.0f, d.skyDistanceFraction);
    s.debugView = static_cast<std::uint8_t>(settings.debugView) <= static_cast<std::uint8_t>(VolumetricLightDebugView::Transmittance)
        ? settings.debugView
        : VolumetricLightDebugView::Off;
    return s;
}

std::uint64_t VolumetricLightLightingKey(const VolumetricLightSettings& safe, bool atmosphereLight) noexcept
{
    std::uint64_t hash = 14695981039346656037ull;
    for (const float value : { safe.density, safe.albedo, safe.anisotropyG, safe.heightDensity, safe.heightBase,
             safe.heightFalloff, safe.noiseAmount, safe.noiseScale, safe.startDistance, safe.sunIntensity,
             safe.ambientR, safe.ambientG, safe.ambientB, safe.ambientIntensity, safe.ambientShadowedFraction,
             safe.shadowFilterTexels, safe.shadowBiasTexels, static_cast<float>(safe.shadowSamples) }) {
        hash = HashFloat(hash, value);
    }
    hash = HashFloat(hash, atmosphereLight ? 1.0f : 0.0f);
    return hash;
}

float VolumetricLightSliceToDistance(float slice, float sliceCount, float maxDistance, float exponent) noexcept
{
    if (!(sliceCount > 0.0f) || !(maxDistance > 0.0f) || !std::isfinite(maxDistance) || !std::isfinite(exponent)) {
        return 0.0f;
    }
    return maxDistance * std::pow(Saturate(slice / sliceCount), std::max(exponent, 1e-6f));
}

float VolumetricLightDistanceToSlice(float distance, float sliceCount, float maxDistance, float exponent) noexcept
{
    if (!(sliceCount > 0.0f) || !(maxDistance > 0.0f) || !std::isfinite(maxDistance) || !std::isfinite(exponent)) {
        return 0.0f;
    }
    return std::pow(Saturate(distance / maxDistance), 1.0f / std::max(exponent, 1e-6f)) * sliceCount;
}

float VolumetricLightEffectiveMaxDistance(
    const VolumetricLightSettings& safe, bool hasReceiver, float lastSplitFar) noexcept
{
    if (!hasReceiver || !(lastSplitFar > 0.0f) || !std::isfinite(lastSplitFar)) {
        return safe.maxDistance;
    }
    return std::max(kVolumetricLightMinDistance, std::min(safe.maxDistance, lastSplitFar));
}

Vec3 VolumetricLightRayDirection(float u, float v, const Mat4& invViewProjection) noexcept
{
    const float ndcX = u * 2.0f - 1.0f;
    const float ndcY = v * 2.0f - 1.0f;
    const Vec4 nearPoint = invViewProjection * Vec4(ndcX, ndcY, 0.0f, 1.0f);
    const Vec4 farPoint = invViewProjection * Vec4(ndcX, ndcY, 1.0f, 1.0f);
    if (nearPoint.w == 0.0f || farPoint.w == 0.0f) {
        return Vec3::Zero();
    }
    return Normalize(farPoint.XYZ() / farPoint.w - nearPoint.XYZ() / nearPoint.w);
}

Vec3 DeriveVolumetricLightForward(const Mat4& invViewProjection) noexcept
{
    return VolumetricLightRayDirection(0.5f, 0.5f, invViewProjection);
}

Vec3 VolumetricLightCellToWorld(const VolumetricLightCell& cell, const Vec3& jitter, const Vec3& eye,
    const Mat4& invViewProjection, float maxDistance, float exponent) noexcept
{
    const float u = (static_cast<float>(cell.x) + jitter.x) / static_cast<float>(kVolumetricLightGridWidth);
    const float v = (static_cast<float>(cell.y) + jitter.y) / static_cast<float>(kVolumetricLightGridHeight);
    const float distance = VolumetricLightSliceToDistance(
        static_cast<float>(cell.z) + jitter.z, static_cast<float>(kVolumetricLightGridDepth), maxDistance, exponent);
    return eye + VolumetricLightRayDirection(u, v, invViewProjection) * distance;
}

VolumetricLightCoord VolumetricLightWorldToCoord(const Vec3& world, const Vec3& eye, const Mat4& viewProjection,
    float maxDistance, float exponent) noexcept
{
    VolumetricLightCoord coord;
    const Vec4 clip = viewProjection * Vec4(world.x, world.y, world.z, 1.0f);
    if (!(clip.w > 0.0f)) {
        return coord;
    }
    coord.u = clip.x / clip.w * 0.5f + 0.5f;
    coord.v = clip.y / clip.w * 0.5f + 0.5f;
    const float distance = Length(world - eye);
    coord.slice = VolumetricLightDistanceToSlice(
        distance, static_cast<float>(kVolumetricLightGridDepth), maxDistance, exponent);
    coord.valid = coord.u >= 0.0f && coord.u <= 1.0f && coord.v >= 0.0f && coord.v <= 1.0f && distance <= maxDistance;
    return coord;
}

Extent2D VolumetricLightAtlasExtent() noexcept
{
    return Extent2D{ kVolumetricLightAtlasWidth, kVolumetricLightAtlasHeight };
}

VolumetricLightAtlasTexel VolumetricLightAtlasTexelOf(const VolumetricLightCell& cell) noexcept
{
    return VolumetricLightAtlasTexel{
        (cell.z % kVolumetricLightAtlasTilesX) * kVolumetricLightGridWidth + cell.x,
        (cell.z / kVolumetricLightAtlasTilesX) * kVolumetricLightGridHeight + cell.y,
    };
}

float VolumetricLightPhase(float cosTheta, float g) noexcept
{
    const float c = std::isfinite(cosTheta) ? std::clamp(cosTheta, -1.0f, 1.0f) : 0.0f;
    const float k = std::isfinite(g) ? std::clamp(g, -0.99f, 0.99f) : 0.0f;
    const float denom = 1.0f + k * k - 2.0f * k * c;
    return (1.0f - k * k) / (4.0f * kPi * std::pow(std::max(denom, 1e-4f), 1.5f));
}

float VolumetricLightExtinction(const VolumetricLightSettings& safe, float worldY, float distance) noexcept
{
    const float y = std::isfinite(worldY) ? worldY : 0.0f;
    float sigma = safe.density + safe.heightDensity * std::exp(-safe.heightFalloff * std::max(y - safe.heightBase, 0.0f));
    if (safe.startDistance > 0.0f) {
        sigma *= SmoothStep(0.0f, safe.startDistance, distance);
    }
    return std::isfinite(sigma) ? std::max(sigma, 0.0f) : 0.0f;
}

VolumetricLightSliceIntegral VolumetricLightIntegrateSlice(
    const Vec3& scattering, float extinction, float thickness) noexcept
{
    const Vec3 s = IsFinite(scattering) ? Vec3{ std::max(scattering.x, 0.0f), std::max(scattering.y, 0.0f),
                                              std::max(scattering.z, 0.0f) }
                                        : Vec3::Zero();
    const float sigma = std::isfinite(extinction) ? std::max(extinction, 0.0f) : 0.0f;
    const float dz = std::isfinite(thickness) ? std::max(thickness, 0.0f) : 0.0f;

    VolumetricLightSliceIntegral out;
    out.transmittance = std::exp(-sigma * dz);
    out.inScatter = sigma > 1e-6f ? (s - s * out.transmittance) / sigma : s * dz;
    return out;
}

float VolumetricLightSunFade(const Vec3& directionTowardSun) noexcept
{
    const Vec3 d = SafeDirection(directionTowardSun);
    const float elevation = RadToDeg(std::asin(std::clamp(d.y, -1.0f, 1.0f)));
    return SmoothStep(kFadeStartDegrees, kFadeEndDegrees, elevation);
}

VolumetricLightCascadePick VolumetricLightPickCascade(float planarDepth, const SunShadowReceiver& receiver) noexcept
{
    VolumetricLightCascadePick pick;
    const std::uint32_t count = std::min(receiver.cascadeCount, kSunShadowReceiverMaxCascades);
    if (count == 0) {
        return pick;
    }
    pick.index = count - 1;
    for (std::uint32_t i = 0; i < count; ++i) {
        if (planarDepth < receiver.splitFar[i]) {
            pick.index = i;
            break;
        }
    }
    const float width = receiver.blendWidth[pick.index];
    if (pick.index + 1 < count && width > 0.0f) {
        const float start = receiver.splitFar[pick.index] - width;
        if (planarDepth > start) {
            pick.blendToNext = Saturate((planarDepth - start) / width);
        }
    }
    return pick;
}

bool VolumetricLightProjectToCascade(
    const Mat4& viewProjection, const Vec3& world, float& u, float& v, float& z) noexcept
{
    const Vec4 clip = viewProjection * Vec4(world.x, world.y, world.z, 1.0f);
    if (!(clip.w > 1e-6f)) {
        return false;
    }
    u = clip.x / clip.w * 0.5f + 0.5f;
    v = clip.y / clip.w * 0.5f + 0.5f;
    z = clip.z / clip.w;
    return u >= 0.0f && u <= 1.0f && v >= 0.0f && v <= 1.0f && z >= 0.0f && z <= 1.0f;
}

std::array<double, 3> VolumetricLightAdvanceWind(
    const std::array<double, 3>& offset, float windX, float windZ, float deltaSeconds) noexcept
{
    if (!std::isfinite(deltaSeconds) || !(deltaSeconds > 0.0f)) {
        return offset;
    }
    const double dt = deltaSeconds;
    const double wx = std::isfinite(windX) ? windX : 0.0;
    const double wz = std::isfinite(windZ) ? windZ : 0.0;
    return { WrapOffset(offset[0] + wx * dt), 0.0, WrapOffset(offset[2] + wz * dt) };
}

Vec3 VolumetricLightHaltonJitter(std::uint32_t frame) noexcept
{
    const std::uint32_t index = frame % kVolumetricLightHaltonCycle + 1;
    return Vec3{ RadicalInverse(index, 2), RadicalInverse(index, 3), RadicalInverse(index, 5) };
}

bool VolumetricLightHistoryUsable(const VolumetricLightHistory& previous, const Vec3& eye, const Vec3& forward,
    std::uint64_t frame, std::uint64_t lightingKey) noexcept
{
    if (!previous.valid || previous.frame + 1 != frame || previous.stableFrames < 1
        || previous.lightingKey != lightingKey) {
        return false;
    }
    const float cutDistance = std::max(kCutMinDistance, kCutDistanceFraction * previous.maxDistance);
    const bool jumped = !(Length(eye - previous.eye) <= cutDistance);
    const bool turned = !(Dot(forward, previous.forward) >= kCutMinForwardDot);
    return !jumped && !turned;
}

float VolumetricLightHistoryWeight(const VolumetricLightHistory& previous, const Vec3& sunDirection, float blend) noexcept
{
    if (!std::isfinite(blend)) {
        return 0.0f;
    }
    const float cosAngle = Dot(SafeDirection(previous.sunDirection), SafeDirection(sunDirection));
    const float degrees = RadToDeg(std::acos(std::clamp(cosAngle, -1.0f, 1.0f)));
    return std::clamp(blend, 0.0f, 1.0f) * Saturate(1.0f - degrees / kSunMoveResetDegrees);
}

VolumetricLightHistory MakeVolumetricLightHistory(const VolumetricLightFrameFacts& facts, float depthExponent,
    std::uint64_t lightingKey, const VolumetricLightHistory& previous) noexcept
{
    const bool continuous =
        previous.valid && previous.frame + 1 == facts.frameIndex && previous.lightingKey == lightingKey;

    VolumetricLightHistory h;
    h.valid = true;
    h.frame = facts.frameIndex;
    h.stableFrames = continuous ? previous.stableFrames + 1 : 0;
    h.viewProjection = facts.viewProjection;
    h.eye = facts.eye;
    h.forward = facts.forward;
    h.sunDirection = SafeDirection(facts.sunDirection);
    h.maxDistance = facts.maxDistance;
    h.depthExponent = depthExponent;
    h.lightingKey = lightingKey;
    return h;
}

VolumetricLightInjectParamsGpu MakeVolumetricLightInjectParams(const VolumetricLightSettings& safe,
    const SunShadowReceiver* receiver, const VolumetricLightFrameFacts& facts) noexcept
{
    VolumetricLightInjectParamsGpu p;
    p.invViewProjection = facts.invViewProjection;

    if (receiver != nullptr) {
        const std::uint32_t count = std::min(receiver->cascadeCount, kSunShadowReceiverMaxCascades);
        for (std::uint32_t c = 0; c < count; ++c) {
            p.cascadeViewProjection[c] = receiver->viewProjection[c];
            p.cascadeSplitFar[c] = receiver->splitFar[c];
            p.cascadeBlendWidth[c] = receiver->blendWidth[c];
            p.cascadeTexelWorld[c] = receiver->texelWorld[c];
            p.cascadeDepthRange[c] = receiver->depthRange[c];
        }
        for (std::uint32_t c = count; c < kSunShadowReceiverMaxCascades && count > 0; ++c) {
            p.cascadeSplitFar[c] = receiver->splitFar[count - 1];
        }
        p.shadowParams[2] = receiver->resolution > 0 ? 1.0f / static_cast<float>(receiver->resolution) : 0.0f;
        p.counts[0] = count;
    }

    const Vec3 sunDirection = SafeDirection(facts.sunDirection);
    const SunLightModulation light =
        SanitizeSunLightModulation(SunLightModulation{ facts.atmosphereLight, facts.sunTint, facts.ambientScale });
    const float directScale = kVolumetricLightSunDirectScale * safe.sunIntensity;
    Vec3 sunRadiance;
    float ambientScale;
    if (light.active) {
        sunRadiance = facts.sunIlluminance * light.sunTint * directScale;
        ambientScale = safe.ambientIntensity * light.ambientScale;
    } else {
        const float fade = VolumetricLightSunFade(sunDirection);
        sunRadiance = facts.sunIlluminance * (directScale * fade);
        ambientScale = safe.ambientIntensity
            * (kVolumetricLightNightAmbientFraction + (1.0f - kVolumetricLightNightAmbientFraction) * fade);
    }

    Store(p.eyeAndMaxDistance, facts.eye, facts.maxDistance);
    Store(p.forwardAndExponent, facts.forward, safe.depthExponent);
    Store(p.sunDirectionAndG, sunDirection, safe.anisotropyG);
    Store(p.sunRadiance, sunRadiance);
    Store(p.ambientRadiance, Vec3{ safe.ambientR, safe.ambientG, safe.ambientB } * ambientScale);

    p.densityParams[0] = safe.density;
    p.densityParams[1] = safe.albedo;
    p.densityParams[2] = safe.startDistance;
    p.densityParams[3] = safe.noiseAmount;
    p.heightParams[0] = safe.heightDensity;
    p.heightParams[1] = safe.heightBase;
    p.heightParams[2] = safe.heightFalloff;
    p.heightParams[3] = safe.noiseScale;
    Store(p.noiseOffset, Vec3{ static_cast<float>(facts.windOffset[0]), static_cast<float>(facts.windOffset[1]),
                              static_cast<float>(facts.windOffset[2]) });
    Store(p.jitter, facts.jitter);
    p.shadowParams[0] = safe.shadowBiasTexels;
    p.shadowParams[1] = safe.shadowFilterTexels;
    p.shadowParams[3] = safe.ambientShadowedFraction;
    p.counts[1] = safe.shadowSamples;
    p.counts[2] = static_cast<std::uint32_t>(facts.frameIndex);
    return p;
}

VolumetricLightTemporalParamsGpu MakeVolumetricLightTemporalParams(const VolumetricLightSettings& safe,
    const VolumetricLightFrameFacts& facts, const VolumetricLightHistory& previous, float weight, bool usable) noexcept
{
    const bool valid = usable && previous.valid && previous.maxDistance > 0.0f;

    VolumetricLightTemporalParamsGpu p;
    p.invViewProjection = facts.invViewProjection;
    p.prevViewProjection = previous.viewProjection;
    Store(p.eyeAndMaxDistance, facts.eye, facts.maxDistance);
    Store(p.prevEyeAndMaxDistance, previous.eye, previous.maxDistance);
    p.exponentsAndBlend[0] = safe.depthExponent;
    p.exponentsAndBlend[1] = previous.depthExponent;
    p.exponentsAndBlend[2] = valid ? weight : 0.0f;
    p.exponentsAndBlend[3] = valid ? 1.0f : 0.0f;
    p.atlas[0] = kVolumetricLightAtlasTilesX;
    p.atlas[1] = kVolumetricLightAtlasTilesY;
    return p;
}

VolumetricLightCompositePush MakeVolumetricLightCompositePush(const VolumetricLightSettings& safe,
    const Mat4& invViewProjection, const Vec3& eye, const VolumetricLightResult& result,
    std::uint64_t frameIndex) noexcept
{
    VolumetricLightCompositePush p;
    p.invViewProjection = invViewProjection;
    Store(p.eyeAndMaxDistance, eye, result.maxDistance);
    p.strengthExponentReserved[0] = safe.strength;
    p.strengthExponentReserved[1] = result.depthExponent;
    p.strengthExponentReserved[2] = 0.0f;
    p.strengthExponentReserved[3] = 0.0f;
    p.frameAndDebug[0] = static_cast<std::uint32_t>(frameIndex);
    p.frameAndDebug[1] = static_cast<std::uint32_t>(safe.debugView);
    p.frameAndDebug[2] = std::bit_cast<std::uint32_t>(safe.skyDistanceFraction);
    return p;
}

} // namespace gte
