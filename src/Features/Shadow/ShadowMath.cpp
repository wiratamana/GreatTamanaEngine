#include "ShadowMath.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdlib>

namespace gte {

namespace {

constexpr float kRadiusGranularity = 16.0f; // Sphere radius is rounded up to 1/16 world unit.
constexpr float kMinCascadeRadius = 1.0f / kRadiusGranularity;

bool IsFinite(const Vec3& v) noexcept
{
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}

float SanitizeFloat(float value, float lo, float hi, float fallback) noexcept
{
    return std::isfinite(value) ? std::clamp(value, lo, hi) : fallback;
}

// Nearest candidate by absolute difference; candidates ascend, so a tie goes to the larger one.
template <std::size_t N>
std::uint32_t NearestOf(const std::array<std::uint32_t, N>& candidates, std::uint32_t value) noexcept
{
    std::uint32_t best = candidates[0];
    std::int64_t bestDiff = std::int64_t{ 1 } << 40;
    for (const std::uint32_t candidate : candidates) {
        const std::int64_t diff = std::llabs(static_cast<std::int64_t>(candidate) - static_cast<std::int64_t>(value));
        if (diff <= bestDiff) {
            best = candidate;
            bestDiff = diff;
        }
    }
    return best;
}

constexpr std::array<std::uint32_t, 4> kResolutions{ 512, 1024, 2048, 4096 };
constexpr std::array<std::uint32_t, 4> kFilterSamples{ 1, 4, 8, 16 };

Vec3 SafeSunDirection(const Vec3& directionTowardSun) noexcept
{
    if (!IsFinite(directionTowardSun) || LengthSquared(directionTowardSun) < 1e-12f) {
        return Vec3::Up();
    }
    return Normalize(directionTowardSun);
}

std::int64_t ToGridIndex(double value) noexcept
{
    constexpr double kLimit = 9.0e18;
    return static_cast<std::int64_t>(std::clamp(value, -kLimit, kLimit));
}

} // namespace

Vec3 ReconstructWorldPositionFromDepth(
    const Mat4& invViewProjection, float ndcX, float ndcY, float depthZeroToOne) noexcept
{
    const Vec4 clip(ndcX, ndcY, depthZeroToOne, 1.0f);
    const Vec4 world = invViewProjection * clip;
    return world.w != 0.0f ? world.XYZ() / world.w : Vec3::Zero();
}

ShadowSettings SanitizeShadowSettings(const ShadowSettings& settings) noexcept
{
    constexpr ShadowSettings d{};
    ShadowSettings s = settings;

    s.cascadeCount = std::clamp<std::uint32_t>(settings.cascadeCount, 1u, kMaxShadowCascades);
    s.maxMapResolution = NearestOf(kResolutions, settings.maxMapResolution);
    s.maxDistance = SanitizeFloat(settings.maxDistance, 1.0f, 100000.0f, d.maxDistance);
    s.splitLambda = SanitizeFloat(settings.splitLambda, 0.0f, 1.0f, d.splitLambda);
    s.cascadeBlendFraction = SanitizeFloat(settings.cascadeBlendFraction, 0.0f, 0.5f, d.cascadeBlendFraction);
    s.distanceFadeFraction = SanitizeFloat(settings.distanceFadeFraction, 0.0f, 0.5f, d.distanceFadeFraction);
    s.casterRange = SanitizeFloat(settings.casterRange, 1.0f, kMaxCasterRange, d.casterRange);
    s.casterCullPaddingFraction =
        SanitizeFloat(settings.casterCullPaddingFraction, 0.0f, 2.0f, d.casterCullPaddingFraction);
    s.constBiasTexels = SanitizeFloat(settings.constBiasTexels, 0.0f, 16.0f, d.constBiasTexels);
    s.slopeBiasTexels = SanitizeFloat(settings.slopeBiasTexels, 0.0f, 16.0f, d.slopeBiasTexels);
    s.normalOffsetTexels = SanitizeFloat(settings.normalOffsetTexels, 0.0f, 16.0f, d.normalOffsetTexels);
    s.filterRadiusTexels = SanitizeFloat(settings.filterRadiusTexels, 0.0f, 8.0f, d.filterRadiusTexels);
    s.filterSamples = NearestOf(kFilterSamples, settings.filterSamples);
    s.strength = SanitizeFloat(settings.strength, 0.0f, 1.0f, d.strength);
    s.debugView = static_cast<std::uint8_t>(settings.debugView) <= static_cast<std::uint8_t>(ShadowDebugView::Mask)
        ? settings.debugView
        : ShadowDebugView::Off;
    s.sdsmPaddingFraction = SanitizeFloat(settings.sdsmPaddingFraction, 0.0f, 1.0f, d.sdsmPaddingFraction);
    s.sdsmShrinkSeconds = SanitizeFloat(settings.sdsmShrinkSeconds, 0.0f, 10.0f, d.sdsmShrinkSeconds);
    s.sdsmQuantizeSteps = std::min<std::uint32_t>(settings.sdsmQuantizeSteps, 16u);
    return s;
}

ShadowViewFrustum ExtractViewFrustum(const Vec3& eye, const Mat4& invViewProjection) noexcept
{
    constexpr float kNdc[4][2] = { { -1.0f, -1.0f }, { 1.0f, -1.0f }, { 1.0f, 1.0f }, { -1.0f, 1.0f } };

    // Near corners only: the far plane unprojects with poor precision at large far/near ratios.
    std::array<Vec3, 4> corners;
    Vec3 nearCenter = Vec3::Zero();
    for (std::size_t i = 0; i < 4; ++i) {
        corners[i] = ReconstructWorldPositionFromDepth(invViewProjection, kNdc[i][0], kNdc[i][1], 0.0f);
        nearCenter += corners[i];
    }
    nearCenter *= 0.25f;
    const Vec3 farCenter = ReconstructWorldPositionFromDepth(invViewProjection, 0.0f, 0.0f, 1.0f);

    // The plane normal's sign depends on the matrix, so orient it away from the eye.
    Vec3 forward = Normalize(Cross(corners[1] - corners[0], corners[2] - corners[0]));
    if (Dot(forward, nearCenter - eye) < 0.0f) {
        forward = -forward;
    }

    ShadowViewFrustum frustum;
    frustum.eye = eye;
    frustum.forward = forward;
    frustum.viewNear = Dot(nearCenter - eye, forward);
    frustum.viewFar = Dot(farCenter - eye, forward);
    const bool finite = IsFinite(eye) && IsFinite(forward) && IsFinite(nearCenter) && IsFinite(farCenter)
        && std::isfinite(frustum.viewNear) && std::isfinite(frustum.viewFar);
    if (!finite || !(frustum.viewNear > 0.0f) || !(frustum.viewFar > frustum.viewNear)) {
        return frustum;
    }
    for (std::size_t i = 0; i < 4; ++i) {
        frustum.rays[i] = (corners[i] - eye) / frustum.viewNear;
    }
    frustum.valid = true;
    return frustum;
}

ShadowCascadeSet BuildShadowCascades(const ShadowViewFrustum& frustum, const Vec3& directionTowardSun,
    const ShadowSettings& safe, std::uint32_t resolution, std::optional<ShadowDepthRange> sdsmRange) noexcept
{
    ShadowCascadeSet set;
    if (!frustum.valid || resolution == 0 || safe.cascadeCount == 0) {
        return set;
    }
    const std::uint32_t count = std::min(safe.cascadeCount, kMaxShadowCascades);

    const float shadowFar = std::min(frustum.viewFar, safe.maxDistance);
    if (!(shadowFar > frustum.viewNear * 1.02f)) {
        return set;
    }

    float rangeNear = frustum.viewNear;
    float rangeFar = shadowFar;
    if (sdsmRange && std::isfinite(sdsmRange->nearDepth) && std::isfinite(sdsmRange->farDepth)) {
        rangeNear = std::clamp(sdsmRange->nearDepth, frustum.viewNear, shadowFar * 0.99f);
        rangeFar = std::clamp(sdsmRange->farDepth, rangeNear * 1.01f, shadowFar);
        set.sdsmActive = true;
    }
    set.fittedRange = { rangeNear, rangeFar };

    // Practical split scheme over [rangeNear, rangeFar]; the last split is exactly rangeFar.
    std::array<float, kMaxShadowCascades> split{};
    for (std::uint32_t i = 1; i <= count; ++i) {
        const double t = static_cast<double>(i) / count;
        const double uniform = rangeNear + (static_cast<double>(rangeFar) - rangeNear) * t;
        const double logarithmic = rangeNear * std::pow(static_cast<double>(rangeFar) / rangeNear, t);
        split[i - 1] = static_cast<float>(uniform + (logarithmic - uniform) * safe.splitLambda);
    }
    split[count - 1] = rangeFar;

    // The last cascade does not blend; the distance fade ends it.
    std::array<float, kMaxShadowCascades> blendWidth{};
    for (std::uint32_t c = 0; c + 1 < count; ++c) {
        const float prevSplit = c == 0 ? rangeNear : split[c - 1];
        blendWidth[c] = safe.cascadeBlendFraction * (split[c] - prevSplit);
    }

    // Light rotation: the light looks along -directionTowardSun.
    const Vec3 sunDirection = SafeSunDirection(directionTowardSun);
    const Vec3 lightForward = Normalize(-sunDirection);
    const Vec3 upHint = std::abs(Dot(lightForward, Vec3::Up())) > 0.999f ? Vec3::Forward() : Vec3::Up();
    const Mat4 lightRotation = Mat4::LookAtLH(Vec3::Zero(), lightForward, upHint);

    for (std::uint32_t c = 0; c < count; ++c) {
        ShadowCascade& cascade = set.cascades[c];
        cascade.sliceStart = c == 0 ? rangeNear : split[c - 1] - blendWidth[c - 1];
        cascade.sliceEnd = split[c];

        // Camera-relative slice corners keep the sphere independent of eye position and rotation.
        std::array<Vec3, 8> rel;
        Vec3 centerRel = Vec3::Zero();
        for (std::size_t i = 0; i < 4; ++i) {
            rel[i] = frustum.rays[i] * cascade.sliceStart;
            rel[i + 4] = frustum.rays[i] * cascade.sliceEnd;
        }
        for (const Vec3& p : rel) {
            centerRel += p;
        }
        centerRel *= 0.125f;
        float radius = 0.0f;
        for (const Vec3& p : rel) {
            radius = std::max(radius, Length(p - centerRel));
        }
        radius = std::max(std::ceil(radius * kRadiusGranularity) / kRadiusGranularity, kMinCascadeRadius);

        // Light-space center in double: the texel snap fails in float far from the origin.
        const double cx = static_cast<double>(frustum.eye.x) + centerRel.x;
        const double cy = static_cast<double>(frustum.eye.y) + centerRel.y;
        const double cz = static_cast<double>(frustum.eye.z) + centerRel.z;
        double lx = lightRotation(0, 0) * cx + lightRotation(0, 1) * cy + lightRotation(0, 2) * cz;
        double ly = lightRotation(1, 0) * cx + lightRotation(1, 1) * cy + lightRotation(1, 2) * cz;
        const double lz = lightRotation(2, 0) * cx + lightRotation(2, 1) * cy + lightRotation(2, 2) * cz;

        const double texel = 2.0 * static_cast<double>(radius) / resolution;
        if (safe.stabilizeCascades) {
            cascade.gridX = ToGridIndex(std::floor(lx / texel + 0.5));
            cascade.gridY = ToGridIndex(std::floor(ly / texel + 0.5));
            lx = static_cast<double>(cascade.gridX) * texel;
            ly = static_cast<double>(cascade.gridY) * texel;
        }

        // The near plane sits casterRange beyond the sphere toward the sun; xy is centered on the snapped center.
        const double zMin = lz - static_cast<double>(radius) - safe.casterRange;
        const double zMax = lz + radius;
        const Mat4 view = Mat4::Translation(
                              Vec3(static_cast<float>(-lx), static_cast<float>(-ly), static_cast<float>(-zMin)))
            * lightRotation;
        const Mat4 proj = Mat4::OrthographicLH_ZO(
            -radius, radius, -radius, radius, 0.0f, static_cast<float>(zMax - zMin), /*flipY=*/true);

        cascade.viewProjection = proj * view;
        cascade.radius = radius;
        cascade.texelWorldSize = static_cast<float>(texel);
        cascade.depthRange = static_cast<float>(zMax - zMin);
        cascade.lightForward = Vec3(lightRotation(2, 0), lightRotation(2, 1), lightRotation(2, 2));
        cascade.lightNearZ = zMin;
    }

    set.valid = true;
    set.count = count;
    set.cameraPosition = frustum.eye;
    set.cameraForward = frustum.forward;
    set.directionTowardSun = sunDirection;
    set.shadowDistance = shadowFar;
    set.fadeStart = shadowFar * (1.0f - safe.distanceFadeFraction);
    return set;
}

namespace {

// Double vector: the swept hull of the camera slice needs more headroom than the float cascade math has.
struct Vec3d {
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
};

Vec3d operator+(const Vec3d& a, const Vec3d& b) noexcept { return { a.x + b.x, a.y + b.y, a.z + b.z }; }
Vec3d operator-(const Vec3d& a, const Vec3d& b) noexcept { return { a.x - b.x, a.y - b.y, a.z - b.z }; }
Vec3d operator*(const Vec3d& a, double s) noexcept { return { a.x * s, a.y * s, a.z * s }; }
double Dot(const Vec3d& a, const Vec3d& b) noexcept { return a.x * b.x + a.y * b.y + a.z * b.z; }
double Length(const Vec3d& a) noexcept { return std::sqrt(Dot(a, a)); }

Vec3d Cross(const Vec3d& a, const Vec3d& b) noexcept
{
    return { a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x };
}

Vec3d ToDouble(const Vec3& v) noexcept { return { v.x, v.y, v.z }; }

bool IsFinite(const Vec3d& v) noexcept { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); }

// Texels a lookup can land away from the receiver's own texel, besides the normal offset and the filter radius.
constexpr double kNearestTexelReach = 1.0; // The nearest-texel read lands up to one texel away.
constexpr double kRasterSlackTexels = 1.0; // Sub-texel rasterization slack.
constexpr double kJitterPixels = 2.0;      // The view is unjittered; the image is drawn jittered.

// Float rounding of the frustum extraction (see FrustumNoise) is padded by this factor; a view whose noise reaches the
// refusal limit gets no planes.
constexpr double kFloatRoundoff = 1.2e-7;
constexpr double kNoisePadFactor = 24.0;
constexpr double kNoiseRefusal = 0.01;

// Safety margin for evaluating a plane in float: relative to the plane offset and the largest coordinate.
constexpr double kPlaneFloatMargin = 4.0e-7;

constexpr std::size_t kHullPointCount = 16;
constexpr std::size_t kHullFaceCapacity = 64;

// Relative error of a near-plane corner ray: the extraction runs in float on eye-sized numbers,
// the near rectangle is small.
double FrustumNoise(const ShadowViewFrustum& frustum) noexcept
{
    const double tanX = 0.5 * Length(ToDouble(frustum.rays[1]) - ToDouble(frustum.rays[0]));
    const double tanY = 0.5 * Length(ToDouble(frustum.rays[3]) - ToDouble(frustum.rays[0]));
    const double nearRect = 2.0 * std::min(tanX, tanY) * frustum.viewNear;
    const double maxEye = std::max({ std::abs(static_cast<double>(frustum.eye.x)),
        std::abs(static_cast<double>(frustum.eye.y)), std::abs(static_cast<double>(frustum.eye.z)), 1.0 });
    return nearRect > 0.0 ? kFloatRoundoff * maxEye / nearRect : 1e30;
}

struct DepthSlab {
    double lo = 0.0;
    double hi = 0.0;
};

// View-depth range whose lookups can read the map of cascade `c`. The first cascade also serves depth below its slice,
// the last one up to the fade end, and a lookup outside map c - 1 falls back to map c.
DepthSlab ReceiverSlab(const ShadowViewFrustum& frustum, const ShadowCascadeSet& set, std::uint32_t c,
    double lookupReach) noexcept
{
    const std::uint32_t last = set.count - 1;
    const auto startOf = [&](std::uint32_t k) {
        return k == 0 ? static_cast<double>(frustum.viewNear) : static_cast<double>(set.cascades[k].sliceStart);
    };
    const auto endOf = [&](std::uint32_t k) {
        return k == last ? static_cast<double>(set.shadowDistance) : static_cast<double>(set.cascades[k].sliceEnd);
    };
    DepthSlab slab{ c > 0 ? startOf(c - 1) : startOf(c), endOf(c) };

    // A lookup moved toward the sun by up to `lookupReach` picks its cascade by the moved depth.
    const double sunAlongForward = Dot(ToDouble(set.directionTowardSun), ToDouble(frustum.forward));
    slab.lo -= std::max(0.0, lookupReach * sunAlongForward);
    slab.hi += std::max(0.0, -lookupReach * sunAlongForward);
    slab.lo = std::max(slab.lo, static_cast<double>(frustum.viewNear));
    slab.hi = std::min(slab.hi, static_cast<double>(frustum.viewFar));
    return slab;
}

// World padding around every receiver point of cascade `c`: bias, filter, camera jitter and frustum float noise.
double CasterPad(const ShadowViewFrustum& frustum, const ShadowCascade& cascade, const ShadowSettings& safe,
    double hiDepth, double viewWidth, double viewHeight) noexcept
{
    const double texel = cascade.texelWorldSize;
    double pad = (safe.normalOffsetTexels + safe.filterRadiusTexels + kNearestTexelReach + kRasterSlackTexels) * texel;
    const double tanX = 0.5 * Length(ToDouble(frustum.rays[1]) - ToDouble(frustum.rays[0]));
    const double tanY = 0.5 * Length(ToDouble(frustum.rays[3]) - ToDouble(frustum.rays[0]));
    pad += kJitterPixels * hiDepth * std::max(2.0 * tanX / viewWidth, 2.0 * tanY / viewHeight);
    pad += kNoisePadFactor * FrustumNoise(frustum) * hiDepth;
    return pad;
}

// Outer half-space of a convex hull: every hull point satisfies dot(n, p) <= d.
struct HullFace {
    Vec3d n;
    double d = 0.0;
};

// Faces of the convex hull of the points, by brute force over all point triples. Degenerate triples and duplicate faces
// are dropped; faces past the capacity are dropped too (a missing face only loosens the bound).
std::size_t HullFacesOf(const std::array<Vec3d, kHullPointCount>& pts, std::array<HullFace, kHullFaceCapacity>& faces) noexcept
{
    double scale = 0.0;
    for (const Vec3d& p : pts) {
        scale = std::max({ scale, std::abs(p.x), std::abs(p.y), std::abs(p.z) });
    }
    const double tolerance = 1e-9 * (1.0 + scale);
    std::size_t count = 0;
    for (std::size_t a = 0; a < pts.size(); ++a) {
        for (std::size_t b = a + 1; b < pts.size(); ++b) {
            for (std::size_t c = b + 1; c < pts.size(); ++c) {
                const Vec3d e1 = pts[b] - pts[a];
                const Vec3d e2 = pts[c] - pts[a];
                Vec3d n = Cross(e1, e2);
                const double length = Length(n);
                if (length < 1e-4 * Length(e1) * Length(e2) || length < 1e-12) {
                    continue;
                }
                n = n * (1.0 / length);
                const double offset = Dot(n, pts[a]);
                bool above = false;
                bool below = false;
                for (const Vec3d& p : pts) {
                    const double v = Dot(n, p) - offset;
                    above = above || v > tolerance;
                    below = below || v < -tolerance;
                }
                if (above && below) {
                    continue; // The triple does not span a face.
                }
                if (above) {
                    n = n * -1.0;
                }
                // The support value of all points is exact even when the normal is noisy.
                double support = -1e300;
                for (const Vec3d& p : pts) {
                    support = std::max(support, Dot(n, p));
                }
                bool duplicate = false;
                for (std::size_t i = 0; i < count && !duplicate; ++i) {
                    duplicate = Dot(faces[i].n, n) > 0.99999 && std::abs(faces[i].d - support) < 1e-4 * (1.0 + std::abs(support));
                }
                if (!duplicate && count < faces.size()) {
                    faces[count++] = HullFace{ n, support };
                }
            }
        }
    }
    return count;
}

} // namespace

bool IsPerspectiveViewProjection(const Mat4& viewProjection) noexcept
{
    const float w = std::abs(viewProjection(3, 0)) + std::abs(viewProjection(3, 1)) + std::abs(viewProjection(3, 2));
    return w > 1e-6f;
}

ShadowCasterPlanes BuildShadowCasterPlanes(const ShadowViewFrustum& frustum, const ShadowCascadeSet& set,
    std::uint32_t cascade, const ShadowSettings& safe, float viewWidthPixels, float viewHeightPixels,
    float lookupReachWorld) noexcept
{
    ShadowCasterPlanes result;
    if (!frustum.valid || !set.valid || cascade >= set.count || !(viewWidthPixels > 0.0f) || !(viewHeightPixels > 0.0f)) {
        return result;
    }
    if (!(FrustumNoise(frustum) < kNoiseRefusal)) {
        return result; // The extracted rays are too noisy to bound anything.
    }
    const ShadowCascade& cascadeData = set.cascades[cascade];
    const Vec3d lightForward = ToDouble(cascadeData.lightForward);
    if (!(std::abs(Length(lightForward) - 1.0) < 1e-3)) {
        return result;
    }
    const Vec3d towardSun = lightForward * -1.0;
    const DepthSlab slab = ReceiverSlab(frustum, set, cascade, std::max(0.0f, lookupReachWorld));
    if (!(slab.hi > slab.lo)) {
        return result;
    }
    const double pad = CasterPad(frustum, cascadeData, safe, slab.hi, viewWidthPixels, viewHeightPixels);

    // The slab corners, and the same corners swept toward the sun up to the map's near plane.
    const Vec3d eye = ToDouble(frustum.eye);
    std::array<Vec3d, kHullPointCount> points;
    for (std::size_t i = 0; i < 4; ++i) {
        points[i] = eye + ToDouble(frustum.rays[i]) * slab.lo;
        points[i + 4] = eye + ToDouble(frustum.rays[i]) * slab.hi;
    }
    double maxCoordinate = 0.0;
    for (std::size_t i = 0; i < 8; ++i) {
        const double sweep = std::max(0.0, Dot(points[i], lightForward) - cascadeData.lightNearZ + pad);
        points[i + 8] = points[i] + towardSun * sweep;
    }
    for (const Vec3d& p : points) {
        if (!IsFinite(p) || !std::isfinite(pad)) {
            return result;
        }
        maxCoordinate = std::max({ maxCoordinate, std::abs(p.x), std::abs(p.y), std::abs(p.z) });
    }

    std::array<HullFace, kHullFaceCapacity> faces;
    const std::size_t faceCount = HullFacesOf(points, faces);
    for (std::size_t i = 0; i < faceCount && result.planes.count < fx::kMaxExtraCullPlanes; ++i) {
        const double offset = faces[i].d + pad;
        Plane plane;
        plane.normal = Vec3(static_cast<float>(-faces[i].n.x), static_cast<float>(-faces[i].n.y),
            static_cast<float>(-faces[i].n.z));
        plane.distance = static_cast<float>(offset + kPlaneFloatMargin * (std::abs(offset) + maxCoordinate));
        if (!std::isfinite(plane.normal.x) || !std::isfinite(plane.normal.y) || !std::isfinite(plane.normal.z)
            || !std::isfinite(plane.distance)) {
            return ShadowCasterPlanes{};
        }
        result.planes.planes[result.planes.count++] = plane;
    }
    result.valid = result.planes.count > 0;
    return result;
}

ShadowMaskParamsGpu MakeShadowMaskParams(
    const ShadowMapData& map, const ShadowSettings& safe, const Mat4& invViewProjection) noexcept
{
    const ShadowCascadeSet& set = map.cascades;
    ShadowMaskParamsGpu p;
    p.invViewProjection = invViewProjection;

    const std::uint32_t count = std::min(set.count, kMaxShadowCascades);
    for (std::uint32_t c = 0; c < count; ++c) {
        const ShadowCascade& cascade = set.cascades[c];
        p.cascadeViewProjection[c] = cascade.viewProjection;
        p.cascadeSplitFar[c] = cascade.sliceEnd;
        p.cascadeBlendWidth[c] =
            c + 1 < count ? std::max(0.0f, cascade.sliceEnd - set.cascades[c + 1].sliceStart) : 0.0f;
        p.cascadeTexelWorld[c] = cascade.texelWorldSize;
        p.cascadeDepthRange[c] = cascade.depthRange;
    }
    for (std::uint32_t c = count; c < kMaxShadowCascades && count > 0; ++c) {
        p.cascadeSplitFar[c] = set.cascades[count - 1].sliceEnd;
    }

    p.cameraPositionAndFadeStart[0] = set.cameraPosition.x;
    p.cameraPositionAndFadeStart[1] = set.cameraPosition.y;
    p.cameraPositionAndFadeStart[2] = set.cameraPosition.z;
    p.cameraPositionAndFadeStart[3] = set.fadeStart;
    p.cameraForwardAndFadeEnd[0] = set.cameraForward.x;
    p.cameraForwardAndFadeEnd[1] = set.cameraForward.y;
    p.cameraForwardAndFadeEnd[2] = set.cameraForward.z;
    p.cameraForwardAndFadeEnd[3] = set.shadowDistance;
    p.sunDirection[0] = set.directionTowardSun.x;
    p.sunDirection[1] = set.directionTowardSun.y;
    p.sunDirection[2] = set.directionTowardSun.z;

    // A filter tap at radius r sees r texels of depth change on a slope, so the slope term covers it.
    p.biasParams[0] = safe.constBiasTexels;
    p.biasParams[1] = safe.normalOffsetTexels;
    p.biasParams[2] = std::max(safe.slopeBiasTexels, safe.filterRadiusTexels);

    p.filterRadiusTexels = safe.filterRadiusTexels;
    p.texelUvSize = map.resolution > 0 ? 1.0f / static_cast<float>(map.resolution) : 0.0f;
    p.filterSamples = safe.filterSamples;
    p.cascadeCount = count;
    return p;
}

SunShadowReceiver MakeSunShadowReceiver(const ShadowMapData& map, const ShadowSettings& safe) noexcept
{
    static_assert(kSunShadowReceiverMaxCascades == kMaxShadowCascades);
    const ShadowCascadeSet& set = map.cascades;
    SunShadowReceiver r;

    const std::uint32_t count = std::min(set.count, kSunShadowReceiverMaxCascades);
    r.cascadeCount = count;
    r.resolution = map.resolution;
    for (std::uint32_t c = 0; c < count; ++c) {
        const ShadowCascade& cascade = set.cascades[c];
        r.maps[c] = map.maps[c];
        r.viewProjection[c] = cascade.viewProjection;
        r.splitFar[c] = cascade.sliceEnd;
        r.blendWidth[c] =
            c + 1 < count ? std::max(0.0f, cascade.sliceEnd - set.cascades[c + 1].sliceStart) : 0.0f;
        r.texelWorld[c] = cascade.texelWorldSize;
        r.depthRange[c] = cascade.depthRange;
    }
    for (std::uint32_t c = count; c < kSunShadowReceiverMaxCascades && count > 0; ++c) {
        r.splitFar[c] = set.cascades[count - 1].sliceEnd;
    }

    r.cameraPosition = set.cameraPosition;
    r.cameraForward = set.cameraForward;
    r.directionTowardSun = set.directionTowardSun;
    r.shadowDistance = set.shadowDistance;

    r.fadeStart = set.fadeStart;
    r.materialStrength = safe.applyInMaterials ? safe.strength : 0.0f;
    r.filterRadiusTexels = safe.filterRadiusTexels;
    r.constBiasTexels = safe.constBiasTexels;
    r.normalOffsetTexels = safe.normalOffsetTexels;
    r.slopeBiasTexels = std::max(safe.slopeBiasTexels, safe.filterRadiusTexels);
    r.filterSamples = safe.filterSamples;
    r.depthFitted = set.sdsmActive;
    return r;
}

float SunElevationDegrees(const Vec3& directionTowardSun) noexcept
{
    const Vec3 d = SafeSunDirection(directionTowardSun);
    constexpr float kRadToDeg = 57.29577951308232f;
    return std::asin(std::clamp(d.y, -1.0f, 1.0f)) * kRadToDeg;
}

ShadowDepthDecoded DecodeDepth(const ShadowDepthRangeGpu& gpu) noexcept
{
    if (gpu.minDepthBits == 0xFFFFFFFFu) {
        return { ShadowDepthDecode::NoSurface, {} };
    }
    const float nearDepth = std::bit_cast<float>(gpu.minDepthBits);
    const float farDepth = std::bit_cast<float>(~gpu.invMaxDepthBits);
    if (!std::isfinite(nearDepth) || !std::isfinite(farDepth) || !(nearDepth > 0.0f) || farDepth < nearDepth) {
        return { ShadowDepthDecode::Invalid, {} };
    }
    return { ShadowDepthDecode::Range, ShadowDepthRange{ nearDepth, farDepth } };
}

float ExponentialBlendFactor(float deltaSeconds, float timeConstantSeconds) noexcept
{
    if (timeConstantSeconds <= 0.0f) {
        return 1.0f;
    }
    if (!std::isfinite(deltaSeconds) || !std::isfinite(timeConstantSeconds) || !(deltaSeconds > 0.0f)) {
        return 0.0f;
    }
    return -std::expm1(-deltaSeconds / timeConstantSeconds);
}

ShadowSdsmState UpdateSdsmState(const ShadowSdsmState& previous, std::optional<ShadowDepthSample> sample,
    const ShadowViewFrustum& frustum, const ShadowSettings& safe, std::uint64_t frameIndex,
    float deltaSeconds) noexcept
{
    // A restarted frame counter or a stale state starts over from the next sample.
    ShadowSdsmState prev = previous;
    if (prev.valid
        && (frameIndex < prev.lastSampleFrame || frameIndex - prev.lastSampleFrame > kSdsmMaxHoldFrames)) {
        prev = ShadowSdsmState{};
    }
    if (!sample || !frustum.valid) {
        return prev;
    }

    const float shadowFar = std::min(frustum.viewFar, safe.maxDistance);
    const float pad = safe.sdsmPaddingFraction;
    const float targetNear = std::max(frustum.viewNear, sample->range.nearDepth * (1.0f - pad));
    const float targetFar = std::min(shadowFar, sample->range.farDepth * (1.0f + pad));
    if (!(targetNear < targetFar)) {
        prev.valid = false; // Visible geometry lies outside the shadow distance.
        return prev;
    }

    ShadowSdsmState next;
    const float r = ExponentialBlendFactor(deltaSeconds, safe.sdsmShrinkSeconds);
    if (!prev.valid) {
        next.smoothNear = targetNear;
        next.smoothFar = targetFar;
    } else {
        next.smoothNear = targetNear < prev.smoothNear ? targetNear : prev.smoothNear + (targetNear - prev.smoothNear) * r;
        next.smoothFar = targetFar > prev.smoothFar ? targetFar : prev.smoothFar + (targetFar - prev.smoothFar) * r;
    }
    next.smoothNear = std::max(next.smoothNear, frustum.viewNear);
    next.smoothFar = std::min(next.smoothFar, shadowFar);

    // Near rounds down and far rounds up on a log grid, so the splits stay put between grid steps.
    next.used = { next.smoothNear, next.smoothFar };
    if (safe.sdsmQuantizeSteps > 0) {
        const float steps = static_cast<float>(safe.sdsmQuantizeSteps);
        next.used.nearDepth = std::exp2(std::floor(std::log2(next.smoothNear) * steps) / steps);
        next.used.farDepth = std::exp2(std::ceil(std::log2(next.smoothFar) * steps) / steps);
        next.used.nearDepth = std::max(next.used.nearDepth, frustum.viewNear);
        next.used.farDepth = std::min(next.used.farDepth, shadowFar);
    }

    next.raw = sample->range;
    next.lastSampleFrame = sample->frame;
    next.valid = true;
    return next;
}

} // namespace gte
