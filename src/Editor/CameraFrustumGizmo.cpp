#include "CameraFrustumGizmo.h"

#include "../Math/MathTypes.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace gte {

namespace {

constexpr float kMaxArrowFar = 1.0e6f;
constexpr float kArrowFraction = 0.15f;
constexpr float kPlaneArrowFraction = 0.10f;

bool IsFinite(const Vec3& v) noexcept
{
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}

// One face of the frustum: its name, centre and inward unit normal.
struct Face {
    const char* name;
    Vec3 centre;
    Vec3 normal;
};

// Inward normal of the plane through `apex`, `a` and `b`; `inside` fixes the sign.
Vec3 SideNormal(const Vec3& apex, const Vec3& a, const Vec3& b, const Vec3& inside)
{
    const Vec3 n = Normalize(Cross(a - apex, b - apex));
    return Dot(n, inside - apex) >= 0.0f ? n : n * -1.0f;
}

std::array<Face, 6> MakeFaces(const FrustumCorners& c)
{
    const Vec3 nearCenter = (c.nearC[0] + c.nearC[2]) * 0.5f;
    const Vec3 farCenter = (c.farC[0] + c.farC[2]) * 0.5f;
    const Vec3 inside = (nearCenter + farCenter) * 0.5f;
    const auto side = [&](const char* name, int i, int j) {
        return Face{ name, (c.nearC[i] + c.nearC[j] + c.farC[i] + c.farC[j]) * 0.25f,
            SideNormal(c.apex, c.nearC[i], c.nearC[j], inside) };
    };
    return { side("Left", 0, 3), side("Right", 1, 2), side("Bottom", 0, 1), side("Top", 2, 3),
        Face{ "Near", nearCenter, c.forward }, Face{ "Far", farCenter, c.forward * -1.0f } };
}

// The face whose inward normal is closest to the plane's.
const Face& BestFace(const Plane& plane, const std::array<Face, 6>& faces)
{
    const Face* best = &faces[0];
    float bestDot = Dot(plane.normal, best->normal);
    for (const Face& face : faces) {
        const float d = Dot(plane.normal, face.normal);
        if (d > bestDot) {
            best = &face;
            bestDot = d;
        }
    }
    return *best;
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

const char* NameFrustumPlane(const Plane& plane, const FrustumCorners& basis)
{
    return BestFace(plane, MakeFaces(basis)).name;
}

std::vector<FrustumPlaneArrow> BuildFrustumPlaneArrows(std::span<const Plane> planes, const FrustumCorners& c)
{
    const Vec3 farCenter = (c.farC[0] + c.farC[2]) * 0.5f;
    const float length = std::min(Length(farCenter - c.apex), kMaxArrowFar) * kPlaneArrowFraction;
    const std::array<Face, 6> faces = MakeFaces(c);

    std::vector<FrustumPlaneArrow> arrows;
    arrows.reserve(planes.size());
    for (const Plane& plane : planes) {
        const Face& face = BestFace(plane, faces);
        arrows.push_back({ face.centre, face.centre + plane.normal * length, face.name });
    }
    return arrows;
}

} // namespace gte
