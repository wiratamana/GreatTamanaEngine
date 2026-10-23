#include "CameraFrustumGizmo.h"

#include "../Math/MathTypes.h"

#include <algorithm>
#include <cmath>

namespace gte {

namespace {

constexpr float kMaxArrowFar = 1.0e6f;
constexpr float kArrowFraction = 0.15f;

bool IsFinite(const Vec3& v) noexcept
{
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}

} // namespace

bool FrustumInputIsValid(const FrustumInput& in) noexcept
{
    const Quat& q = in.rotation;
    if (!IsFinite(in.position) || !std::isfinite(q.x) || !std::isfinite(q.y) || !std::isfinite(q.z)
        || !std::isfinite(q.w) || !std::isfinite(in.fovYDegrees) || !std::isfinite(in.nearZ)
        || !std::isfinite(in.farZ) || !std::isfinite(in.aspect)) {
        return false;
    }
    if (q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w < 1.0e-6f) {
        return false;
    }
    return in.fovYDegrees >= kFrustumMinFovDegrees && in.fovYDegrees <= kFrustumMaxFovDegrees && in.nearZ > 0.0f
        && in.farZ > in.nearZ && in.farZ <= kFrustumMaxFar && in.aspect > 0.0f;
}

FrustumCorners ComputeFrustumCorners(const FrustumInput& in)
{
    FrustumCorners out;
    const Quat rotation = Normalize(in.rotation);
    out.apex = in.position;
    out.forward = rotation.RotateVector(Vec3::Forward());
    out.up = rotation.RotateVector(Vec3::Up());
    out.right = Normalize(Cross(out.up, out.forward));

    const float tanHalf = std::tan(DegToRad(in.fovYDegrees) * 0.5f);
    const float distances[2] = { in.nearZ, in.farZ };
    Vec3* rings[2] = { out.nearC, out.farC };
    for (int r = 0; r < 2; ++r) {
        const Vec3 center = in.position + out.forward * distances[r];
        const float hy = distances[r] * tanHalf;
        const float hx = hy * in.aspect;
        const Vec3 dx = out.right * hx;
        const Vec3 dy = out.up * hy;
        rings[r][0] = center - dx - dy;
        rings[r][1] = center + dx - dy;
        rings[r][2] = center + dx + dy;
        rings[r][3] = center - dx + dy;
    }
    return out;
}

std::vector<FrustumSegment> BuildFrustumSegments(const FrustumCorners& c)
{
    std::vector<FrustumSegment> segments;
    segments.reserve(17);
    for (int i = 0; i < 4; ++i) {
        const int next = (i + 1) & 3;
        segments.push_back({ c.nearC[i], c.nearC[next], FrustumEdgeKind::Near });
    }
    for (int i = 0; i < 4; ++i) {
        const int next = (i + 1) & 3;
        segments.push_back({ c.farC[i], c.farC[next], FrustumEdgeKind::Far });
    }
    for (int i = 0; i < 4; ++i) {
        segments.push_back({ c.nearC[i], c.farC[i], FrustumEdgeKind::Side });
    }
    for (int i = 0; i < 4; ++i) {
        segments.push_back({ c.apex, c.nearC[i], FrustumEdgeKind::Apex });
    }

    const Vec3 farCenter = (c.farC[0] + c.farC[2]) * 0.5f;
    const float farDistance = std::min(Length(farCenter - c.apex), kMaxArrowFar);
    segments.push_back({ c.apex, c.apex + c.forward * (farDistance * kArrowFraction), FrustumEdgeKind::Arrow });
    return segments;
}

const char* NameFrustumPlane(const Plane& plane, const FrustumCorners& basis) noexcept
{
    const float dRight = Dot(plane.normal, basis.right);
    const float dUp = Dot(plane.normal, basis.up);
    const float dForward = Dot(plane.normal, basis.forward);
    const float aRight = std::fabs(dRight);
    const float aUp = std::fabs(dUp);
    const float aForward = std::fabs(dForward);

    if (aRight >= aUp && aRight >= aForward) {
        return dRight >= 0.0f ? "Left" : "Right";
    }
    if (aUp >= aForward) {
        return dUp >= 0.0f ? "Bottom" : "Top";
    }
    return dForward >= 0.0f ? "Near" : "Far";
}

} // namespace gte
