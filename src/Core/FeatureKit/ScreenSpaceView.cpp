#include "ScreenSpaceView.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace gte::fx {

namespace {

// A frame has a camera cut when the eye jumps or the view turns past these limits.
constexpr float kCutDistance = 20.0f;
constexpr float kCutMinForwardDot = 0.5f;

constexpr float kMinSpan = 1e-4f;         // Smallest usable image-plane span, world units.
constexpr float kMaxSkew = 1e-3f;         // |dot(right, down)| limit.
constexpr float kPerspectiveTol = 0.005f; // Relative change of the frustum width over depth.
constexpr float kSquarePixelTol = 0.01f;

bool IsFinite(const Vec3& v) noexcept
{
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}

bool SameExtent(Extent2D a, Extent2D b) noexcept
{
    return a.width == b.width && a.height == b.height;
}

} // namespace

Vec3 UnprojectDepth(const Mat4& invViewProjection, float ndcX, float ndcY, float depthZeroToOne) noexcept
{
    const Vec4 world = invViewProjection * Vec4(ndcX, ndcY, depthZeroToOne, 1.0f);
    return world.w != 0.0f ? world.XYZ() / world.w : Vec3::Zero();
}

Extent2D ScaledExtent(Extent2D extent, std::uint32_t divisor) noexcept
{
    const std::uint32_t d = std::max(divisor, 1u);
    const auto scaled = [d](std::uint32_t size) { return std::max(1u, (size + d - 1u) / d); };
    return Extent2D{ scaled(extent.width), scaled(extent.height) };
}

float SanitizeFinite(float value, float lo, float hi, float fallback) noexcept
{
    return std::isfinite(value) ? std::clamp(value, lo, hi) : fallback;
}

void PackVec3(float (&dst)[4], const Vec3& v, float w) noexcept
{
    dst[0] = v.x;
    dst[1] = v.y;
    dst[2] = v.z;
    dst[3] = w;
}

CameraBasis DeriveCameraBasis(const Vec3& eye, const Mat4& invViewProjection, Extent2D extent) noexcept
{
    CameraBasis basis;
    if (extent.width == 0 || extent.height == 0 || !IsFinite(eye)) {
        return basis;
    }

    // Points of one NDC depth lie in one view-depth plane, so pairs across the image give the camera axes.
    const auto at = [&](float x, float y, float depth) { return UnprojectDepth(invViewProjection, x, y, depth); };
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

bool HistoryUsable(const TemporalHistory& previous, const CameraBasis& basis, std::uint64_t frame, Extent2D extent,
    std::uint32_t divisor) noexcept
{
    if (!previous.valid || previous.frame + 1 != frame || !SameExtent(previous.extent, extent)
        || previous.divisor != divisor || previous.stableFrames < 2) {
        return false;
    }
    const bool jumped = Length(basis.eye - previous.eye) > kCutDistance;
    const bool turned = Dot(basis.forward, previous.forward) < kCutMinForwardDot;
    return !jumped && !turned;
}

TemporalHistory MakeHistory(const CameraBasis& basis, const Mat4& viewProjection, std::uint64_t frame, Extent2D extent,
    std::uint32_t divisor, const TemporalHistory& previous) noexcept
{
    const bool continuous = previous.valid && previous.frame + 1 == frame && SameExtent(previous.extent, extent)
        && previous.divisor == divisor;

    TemporalHistory h;
    h.valid = true;
    h.frame = frame;
    h.extent = extent;
    h.divisor = divisor;
    h.stableFrames = continuous ? previous.stableFrames + 1 : 0;
    h.viewProjection = viewProjection;
    h.eye = basis.eye;
    h.forward = basis.forward;
    return h;
}

TemporalResolveParamsGpu MakeTemporalResolveParams(const CameraBasis& basis, const TemporalHistory& previous,
    const TemporalResolveSettings& settings, Extent2D viewExtent, const Mat4& invViewProjection,
    bool historyValid) noexcept
{
    TemporalResolveParamsGpu p;
    p.invViewProjection = invViewProjection;
    p.prevViewProjection = previous.viewProjection;
    PackVec3(p.eye, basis.eye);
    PackVec3(p.forward, basis.forward);
    PackVec3(p.prevEye, previous.eye);
    PackVec3(p.prevForward, previous.forward);

    // The history depth tolerance reuses the bilateral sigma; the filters then reject the same depth gaps.
    p.filter[0] = settings.depthSigma;
    p.filter[1] = settings.historyWeight;
    p.filter[2] = settings.depthSigma;
    p.filter[3] = settings.spatial ? 1.0f : 0.0f;

    p.counts[0] = settings.divisor;
    p.counts[1] = historyValid ? 1u : 0u;
    p.counts[2] = viewExtent.width;
    p.counts[3] = viewExtent.height;
    return p;
}

} // namespace gte::fx
