#include "GtaoMath.h"

#include <algorithm>
#include <cmath>

namespace gte {

GtaoSettings SanitizeGtaoSettings(const GtaoSettings& settings) noexcept
{
    constexpr GtaoSettings d{};
    GtaoSettings s = settings;

    s.radius = fx::SanitizeFinite(settings.radius, 0.05f, 10.0f, d.radius);
    s.falloffFraction = fx::SanitizeFinite(settings.falloffFraction, 0.05f, 1.0f, d.falloffFraction);
    s.sliceCount = std::clamp<std::uint32_t>(settings.sliceCount, 1u, 8u);
    s.stepCount = std::clamp<std::uint32_t>(settings.stepCount, 1u, 12u);
    s.sampleDistributionPower = fx::SanitizeFinite(settings.sampleDistributionPower, 1.0f, 4.0f, d.sampleDistributionPower);
    s.thinOccluderCompensation = fx::SanitizeFinite(settings.thinOccluderCompensation, 0.0f, 0.7f, d.thinOccluderCompensation);
    s.finalPower = fx::SanitizeFinite(settings.finalPower, 0.5f, 4.0f, d.finalPower);
    s.minVisibility = fx::SanitizeFinite(settings.minVisibility, 0.0f, 0.5f, d.minVisibility);
    s.strength = fx::SanitizeFinite(settings.strength, 0.0f, 1.0f, d.strength);
    s.maxDistance = fx::SanitizeFinite(settings.maxDistance, 1.0f, 100000.0f, d.maxDistance);
    s.distanceFadeFraction = fx::SanitizeFinite(settings.distanceFadeFraction, 0.0f, 0.9f, d.distanceFadeFraction);
    s.resolutionDivisor = settings.resolutionDivisor <= 1u ? 1u : 2u;
    s.historyWeight = fx::SanitizeFinite(settings.historyWeight, 0.0f, 0.98f, d.historyWeight);
    s.depthSigma = fx::SanitizeFinite(settings.depthSigma, 0.001f, 0.5f, d.depthSigma);
    s.debugView = static_cast<std::uint8_t>(settings.debugView) <= static_cast<std::uint8_t>(GtaoDebugView::AoOnly)
        ? settings.debugView
        : GtaoDebugView::Off;
    return s;
}

GtaoFalloff MakeGtaoFalloff(float radius, float falloffFraction) noexcept
{
    const float range = std::max(falloffFraction * radius, 1e-6f);
    const float from = radius - range;
    return GtaoFalloff{ -1.0f / range, from / range + 1.0f };
}

float GtaoScreenRadiusPixels(float radius, float viewDepth, float pixelScale, float maxPixels) noexcept
{
    return std::min(radius / std::max(viewDepth * pixelScale, 1e-6f), maxPixels);
}

GtaoSliceFrame MakeGtaoSliceFrame(const Vec3& viewVec, const Vec3& normal, const Vec3& sliceDir) noexcept
{
    GtaoSliceFrame s;
    const Vec3 ortho = sliceDir - viewVec * Dot(sliceDir, viewVec);
    s.orthoDir = Normalize(ortho);
    const Vec3 axis = Normalize(Cross(s.orthoDir, viewVec));
    const Vec3 projN = normal - axis * Dot(normal, axis);
    s.projLen = std::max(Length(projN), 1e-5f);
    const float dotOrtho = Dot(s.orthoDir, projN);
    const float signNorm = dotOrtho > 0.0f ? 1.0f : (dotOrtho < 0.0f ? -1.0f : 0.0f);
    s.cosNorm = std::clamp(Dot(projN, viewVec) / s.projLen, 0.0f, 1.0f);
    s.n = signNorm * std::acos(s.cosNorm);
    return s;
}

float GtaoArcIntegral(float n, float cosNorm, float h) noexcept
{
    return (cosNorm + 2.0f * h * std::sin(n) - std::cos(2.0f * h - n)) * 0.25f;
}

float GtaoSliceVisibility(const GtaoSliceFrame& slice, float horizonCos0, float horizonCos1) noexcept
{
    float h0 = -std::acos(std::clamp(horizonCos1, -1.0f, 1.0f));
    float h1 = std::acos(std::clamp(horizonCos0, -1.0f, 1.0f));
    h0 = slice.n + std::clamp(h0 - slice.n, -kHalfPi, kHalfPi);
    h1 = slice.n + std::clamp(h1 - slice.n, -kHalfPi, kHalfPi);
    return slice.projLen * (GtaoArcIntegral(slice.n, slice.cosNorm, h0) + GtaoArcIntegral(slice.n, slice.cosNorm, h1));
}

GtaoMainParamsGpu MakeGtaoMainParams(const fx::CameraBasis& basis, const GtaoSettings& safe,
    const Mat4& invViewProjection, std::uint32_t noiseFrame) noexcept
{
    GtaoMainParamsGpu p;
    p.invViewProjection = invViewProjection;
    const GtaoFalloff falloff = MakeGtaoFalloff(safe.radius, safe.falloffFraction);

    fx::PackVec3(p.eyeAndPixelScale, basis.eye, basis.pixelScale);
    fx::PackVec3(p.rightAndRadius, basis.right, safe.radius);
    fx::PackVec3(p.downAndFalloffMul, basis.down, falloff.mul);
    fx::PackVec3(p.forwardAndFalloffAdd, basis.forward, falloff.add);

    p.tuning[0] = safe.finalPower;
    p.tuning[1] = safe.thinOccluderCompensation;
    p.tuning[2] = safe.sampleDistributionPower;
    p.tuning[3] = safe.minVisibility;

    // smoothstep() needs distinct edges, so a zero fade width still spans a sliver.
    const float fadeEnd = safe.maxDistance;
    p.distanceFade[0] = std::min(fadeEnd * (1.0f - safe.distanceFadeFraction), fadeEnd * 0.999f);
    p.distanceFade[1] = fadeEnd;
    p.distanceFade[2] = kGtaoMaxScreenRadiusPixels;

    p.counts[0] = safe.sliceCount;
    p.counts[1] = safe.stepCount;
    p.counts[2] = noiseFrame;
    p.counts[3] = safe.resolutionDivisor;
    return p;
}

GtaoCompositePush MakeGtaoCompositePush(const fx::CameraBasis& basis, const GtaoSettings& safe,
    const Mat4& invViewProjection, std::uint32_t divisor) noexcept
{
    GtaoCompositePush p;
    p.invViewProjection = invViewProjection;
    fx::PackVec3(p.eye, basis.eye);
    fx::PackVec3(p.forward, basis.forward);
    p.strengthReservedSigma[0] = safe.strength;
    p.strengthReservedSigma[1] = 0.0f;
    p.strengthReservedSigma[2] = safe.depthSigma;
    p.divisorDebug[0] = divisor;
    p.divisorDebug[1] = static_cast<std::uint32_t>(safe.debugView);
    return p;
}

} // namespace gte
