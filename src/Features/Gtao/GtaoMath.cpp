#include "GtaoMath.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace gte {

namespace {

// A frame has a camera cut when the eye jumps or the view turns past these limits.
constexpr float kCutDistance = 20.0f;
constexpr float kCutMinForwardDot = 0.5f;

constexpr float kMinSpan = 1e-4f;      // Smallest usable image-plane span, world units.
constexpr float kMaxSkew = 1e-3f;      // |dot(right, down)| limit.
constexpr float kPerspectiveTol = 0.02f; // Relative change of the frustum width over depth.
constexpr float kSquarePixelTol = 0.02f;

bool IsFinite(const Vec3& v) noexcept
{
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}

float SanitizeFloat(float value, float lo, float hi, float fallback) noexcept
{
    return std::isfinite(value) ? std::clamp(value, lo, hi) : fallback;
}

bool SameExtent(Extent2D a, Extent2D b) noexcept
{
    return a.width == b.width && a.height == b.height;
}

void Store(float (&dst)[4], const Vec3& v, float w = 0.0f) noexcept
{
    dst[0] = v.x;
    dst[1] = v.y;
    dst[2] = v.z;
    dst[3] = w;
}

} // namespace

Vec3 UnprojectGtaoDepth(const Mat4& invViewProjection, float ndcX, float ndcY, float depthZeroToOne) noexcept
{
    const Vec4 world = invViewProjection * Vec4(ndcX, ndcY, depthZeroToOne, 1.0f);
    return world.w != 0.0f ? world.XYZ() / world.w : Vec3::Zero();
}

GtaoSettings SanitizeGtaoSettings(const GtaoSettings& settings) noexcept
{
    constexpr GtaoSettings d{};
    GtaoSettings s = settings;

    s.radius = SanitizeFloat(settings.radius, 0.05f, 10.0f, d.radius);
    s.falloffFraction = SanitizeFloat(settings.falloffFraction, 0.05f, 1.0f, d.falloffFraction);
    s.sliceCount = std::clamp<std::uint32_t>(settings.sliceCount, 1u, 8u);
    s.stepCount = std::clamp<std::uint32_t>(settings.stepCount, 1u, 12u);
    s.sampleDistributionPower = SanitizeFloat(settings.sampleDistributionPower, 1.0f, 4.0f, d.sampleDistributionPower);
    s.thinOccluderCompensation = SanitizeFloat(settings.thinOccluderCompensation, 0.0f, 0.7f, d.thinOccluderCompensation);
    s.finalPower = SanitizeFloat(settings.finalPower, 0.5f, 4.0f, d.finalPower);
    s.minVisibility = SanitizeFloat(settings.minVisibility, 0.0f, 0.5f, d.minVisibility);
    s.strength = SanitizeFloat(settings.strength, 0.0f, 1.0f, d.strength);
    s.maxDistance = SanitizeFloat(settings.maxDistance, 1.0f, 100000.0f, d.maxDistance);
    s.distanceFadeFraction = SanitizeFloat(settings.distanceFadeFraction, 0.0f, 0.9f, d.distanceFadeFraction);
    s.resolutionDivisor = settings.resolutionDivisor <= 1u ? 1u : 2u;
    s.historyWeight = SanitizeFloat(settings.historyWeight, 0.0f, 0.98f, d.historyWeight);
    s.depthSigma = SanitizeFloat(settings.depthSigma, 0.001f, 0.5f, d.depthSigma);
    s.debugView = static_cast<std::uint8_t>(settings.debugView) <= static_cast<std::uint8_t>(GtaoDebugView::AoOnly)
        ? settings.debugView
        : GtaoDebugView::Off;
    return s;
}

Extent2D AoExtentFor(Extent2D viewExtent, std::uint32_t divisor) noexcept
{
    const std::uint32_t d = std::max(divisor, 1u);
    const auto scaled = [d](std::uint32_t size) { return std::max(1u, (size + d - 1u) / d); };
    return Extent2D{ scaled(viewExtent.width), scaled(viewExtent.height) };
}

GtaoCameraBasis DeriveGtaoCameraBasis(const Vec3& eye, const Mat4& invViewProjection, Extent2D extent) noexcept
{
    GtaoCameraBasis basis;
    if (extent.width == 0 || extent.height == 0 || !IsFinite(eye)) {
        return basis;
    }

    // Points of one NDC depth lie in one view-depth plane, so pairs across the image give the camera axes.
    const auto at = [&](float x, float y, float depth) { return UnprojectGtaoDepth(invViewProjection, x, y, depth); };
    constexpr float kMid = 0.5f;
    const Vec3 spanH = at(1.0f, 0.0f, kMid) - at(-1.0f, 0.0f, kMid);
    const Vec3 spanV = at(0.0f, 1.0f, kMid) - at(0.0f, -1.0f, kMid);
    const float lenH = Length(spanH);
    const float lenV = Length(spanV);
    if (!(lenH > kMinSpan) || !(lenV > kMinSpan)) {
        return basis;
    }
    const Vec3 right = spanH / lenH;
    const Vec3 down = spanV / lenV;
    if (!(std::abs(Dot(right, down)) < kMaxSkew)) {
        return basis;
    }

    // The sign of the cross product depends on the matrix, so orient the axis toward the scene.
    Vec3 forward = Normalize(Cross(right, down));
    if (Dot(forward, at(0.0f, 0.0f, kMid) - eye) < 0.0f) {
        forward = -forward;
    }
    if (!IsFinite(forward) || LengthSquared(forward) < 0.5f) {
        return basis;
    }

    // Image-plane size over view depth at one NDC depth. Constant over depth only for a perspective matrix.
    const auto planeRatio = [&](float depth, bool horizontal) {
        const Vec3 a = horizontal ? at(-1.0f, 0.0f, depth) : at(0.0f, -1.0f, depth);
        const Vec3 b = horizontal ? at(1.0f, 0.0f, depth) : at(0.0f, 1.0f, depth);
        const float z = Dot(b - eye, forward);
        return z > 1e-4f ? Length(b - a) / z : std::numeric_limits<float>::quiet_NaN();
    };
    const float tanH = planeRatio(kMid, true);
    const float tanHNear = planeRatio(0.25f, true);
    const float tanHFar = planeRatio(0.75f, true);
    const float tanV = planeRatio(kMid, false);
    if (!std::isfinite(tanH) || !std::isfinite(tanHNear) || !std::isfinite(tanHFar) || !std::isfinite(tanV)) {
        return basis;
    }
    if (std::abs(tanHNear - tanHFar) > kPerspectiveTol * std::max(tanHNear, tanHFar)) {
        return basis;
    }
    const float aspect = static_cast<float>(extent.height) / static_cast<float>(extent.width);
    if (std::abs(tanV - tanH * aspect) > kSquarePixelTol * tanV) {
        return basis;
    }

    basis.valid = true;
    basis.eye = eye;
    basis.right = right;
    basis.down = down;
    basis.forward = forward;
    basis.pixelScale = tanH / static_cast<float>(extent.width);
    return basis;
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

GtaoMainParamsGpu MakeGtaoMainParams(
    const GtaoCameraBasis& basis, const GtaoSettings& safe, const Mat4& invViewProjection, std::uint32_t noiseFrame) noexcept
{
    GtaoMainParamsGpu p;
    p.invViewProjection = invViewProjection;
    const GtaoFalloff falloff = MakeGtaoFalloff(safe.radius, safe.falloffFraction);

    Store(p.eyeAndPixelScale, basis.eye, basis.pixelScale);
    Store(p.rightAndRadius, basis.right, safe.radius);
    Store(p.downAndFalloffMul, basis.down, falloff.mul);
    Store(p.forwardAndFalloffAdd, basis.forward, falloff.add);

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

GtaoResolveParamsGpu MakeGtaoResolveParams(const GtaoCameraBasis& basis, const GtaoHistory& previous,
    const GtaoSettings& safe, Extent2D viewExtent, const Mat4& invViewProjection, bool historyValid) noexcept
{
    GtaoResolveParamsGpu p;
    p.invViewProjection = invViewProjection;
    p.prevViewProjection = previous.viewProjection;
    Store(p.eye, basis.eye);
    Store(p.forward, basis.forward);
    Store(p.prevEye, previous.eye);
    Store(p.prevForward, previous.forward);

    // The history depth tolerance reuses the bilateral sigma; the filters then reject the same depth gaps.
    p.filter[0] = safe.depthSigma;
    p.filter[1] = safe.historyWeight;
    p.filter[2] = safe.depthSigma;
    p.filter[3] = safe.spatialDenoise ? 1.0f : 0.0f;

    p.counts[0] = safe.resolutionDivisor;
    p.counts[1] = historyValid ? 1u : 0u;
    p.counts[2] = viewExtent.width;
    p.counts[3] = viewExtent.height;
    return p;
}

GtaoCompositePush MakeGtaoCompositePush(const GtaoCameraBasis& basis, const GtaoSettings& safe,
    const Mat4& invViewProjection, std::uint32_t divisor) noexcept
{
    GtaoCompositePush p;
    p.invViewProjection = invViewProjection;
    Store(p.eye, basis.eye);
    Store(p.forward, basis.forward);
    p.strengthLinearSigma[0] = safe.strength;
    p.strengthLinearSigma[1] = safe.applyInLinearSpace ? 1.0f : 0.0f;
    p.strengthLinearSigma[2] = safe.depthSigma;
    p.divisorDebug[0] = divisor;
    p.divisorDebug[1] = static_cast<std::uint32_t>(safe.debugView);
    return p;
}

bool HistoryUsable(const GtaoHistory& previous, const GtaoCameraBasis& basis, std::uint64_t frame, Extent2D aoExtent,
    std::uint32_t divisor) noexcept
{
    if (!previous.valid || previous.frame + 1 != frame || !SameExtent(previous.extent, aoExtent)
        || previous.divisor != divisor || previous.stableFrames < 2) {
        return false;
    }
    const bool jumped = Length(basis.eye - previous.eye) > kCutDistance;
    const bool turned = Dot(basis.forward, previous.forward) < kCutMinForwardDot;
    return !jumped && !turned;
}

GtaoHistory MakeHistory(const GtaoCameraBasis& basis, const Mat4& viewProjection, std::uint64_t frame,
    Extent2D aoExtent, std::uint32_t divisor, const GtaoHistory& previous) noexcept
{
    const bool continuous = previous.valid && previous.frame + 1 == frame && SameExtent(previous.extent, aoExtent)
        && previous.divisor == divisor;

    GtaoHistory h;
    h.valid = true;
    h.frame = frame;
    h.extent = aoExtent;
    h.divisor = divisor;
    h.stableFrames = continuous ? previous.stableFrames + 1 : 0;
    h.viewProjection = viewProjection;
    h.eye = basis.eye;
    h.forward = basis.forward;
    return h;
}

} // namespace gte
