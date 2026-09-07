#include "RigidBodyWireframe.h"

#include "../Math/MathTypes.h" // kEpsilon, DegToRad, RadToDeg
#include "../Math/Quat.h"

#include <algorithm>
#include <cmath>

namespace gte {

namespace {

// Matches ImGui-overlay-quality roundness at the small on-screen sizes this
// window typically draws these at - not meant to be a configurable knob.
constexpr int kCircleSegments = 24;
constexpr int kHemisphereArcSegments = 16;

// PMX's own rigid-body rotation order - see this file's header comment and
// PHASE0_MASTER_STRATEGY.md's Culprit #3 for why this exact call is correct
// and must not be "simplified" to a different Euler order.
Quat RotationFromPmxEuler(const Vec3& rotateRadians) noexcept
{
    return Quat::FromEulerDegrees(
        RadToDeg(rotateRadians.x), RadToDeg(rotateRadians.y), RadToDeg(rotateRadians.z));
}

Vec3 ToWorld(const Vec3& local, const Quat& rotation, const Vec3& translate) noexcept
{
    return rotation.RotateVector(local) + translate;
}

// Appends one closed loop of `segmentCount` points (localPointAt(i) for i
// in [0, segmentCount)) as `segmentCount` connected line segments (last
// point back to the first) - shared by every circle this file draws
// (sphere's 3 great circles, capsule's 2 rings).
template <typename LocalPointFn>
void AppendClosedLoop(std::vector<WireframeSegment>& out, int segmentCount, const Quat& rotation,
    const Vec3& translate, LocalPointFn localPointAt)
{
    Vec3 previous = ToWorld(localPointAt(0), rotation, translate);
    const Vec3 first = previous;
    for (int i = 1; i <= segmentCount; ++i) {
        const Vec3 current = i == segmentCount ? first : ToWorld(localPointAt(i), rotation, translate);
        out.push_back(WireframeSegment{ previous, current });
        previous = current;
    }
}

// Appends one OPEN arc of `segmentCount` segments (segmentCount + 1 sampled
// points, localPointAt(i) for i in [0, segmentCount]) - shared by every
// hemisphere arc this file draws (capsule's 4 end-cap arcs).
template <typename LocalPointFn>
void AppendOpenArc(std::vector<WireframeSegment>& out, int segmentCount, const Quat& rotation, const Vec3& translate,
    LocalPointFn localPointAt)
{
    Vec3 previous = ToWorld(localPointAt(0), rotation, translate);
    for (int i = 1; i <= segmentCount; ++i) {
        const Vec3 current = ToWorld(localPointAt(i), rotation, translate);
        out.push_back(WireframeSegment{ previous, current });
        previous = current;
    }
}

std::vector<WireframeSegment> BuildSphereWireframe(const Vec3& shapeSize, const Vec3& translate, const Quat& rotation)
{
    const float radius = shapeSize.x;
    if (radius <= kEpsilon) {
        return {};
    }

    std::vector<WireframeSegment> segments;
    segments.reserve(static_cast<std::size_t>(kCircleSegments) * 3);

    // Three great circles, one per cardinal plane - together they read as a
    // "wire sphere" from any viewing angle, and (since a sphere is
    // rotationally symmetric) rotation only changes WHICH three great
    // circles are drawn, never the overall silhouette - still applied for
    // consistency/correctness with the other two shapes.
    const auto xyCircle = [radius](int i) {
        const float t = (kTwoPi * static_cast<float>(i)) / static_cast<float>(kCircleSegments);
        return Vec3(std::cos(t) * radius, std::sin(t) * radius, 0.0f);
    };
    const auto yzCircle = [radius](int i) {
        const float t = (kTwoPi * static_cast<float>(i)) / static_cast<float>(kCircleSegments);
        return Vec3(0.0f, std::cos(t) * radius, std::sin(t) * radius);
    };
    const auto xzCircle = [radius](int i) {
        const float t = (kTwoPi * static_cast<float>(i)) / static_cast<float>(kCircleSegments);
        return Vec3(std::cos(t) * radius, 0.0f, std::sin(t) * radius);
    };

    AppendClosedLoop(segments, kCircleSegments, rotation, translate, xyCircle);
    AppendClosedLoop(segments, kCircleSegments, rotation, translate, yzCircle);
    AppendClosedLoop(segments, kCircleSegments, rotation, translate, xzCircle);
    return segments;
}

std::vector<WireframeSegment> BuildBoxWireframe(const Vec3& shapeSize, const Vec3& translate, const Quat& rotation)
{
    const float hx = std::max(0.0f, shapeSize.x);
    const float hy = std::max(0.0f, shapeSize.y);
    const float hz = std::max(0.0f, shapeSize.z);
    if (hx <= kEpsilon && hy <= kEpsilon && hz <= kEpsilon) {
        return {}; // Fully degenerate on every axis - nothing meaningful to draw.
    }

    // 8 corners, indexed by 3 bits (bit0 = +/-X, bit1 = +/-Y, bit2 = +/-Z).
    Vec3 localCorners[8];
    for (int i = 0; i < 8; ++i) {
        localCorners[i] = Vec3((i & 1) ? hx : -hx, (i & 2) ? hy : -hy, (i & 4) ? hz : -hz);
    }
    Vec3 worldCorners[8];
    for (int i = 0; i < 8; ++i) {
        worldCorners[i] = ToWorld(localCorners[i], rotation, translate);
    }

    // 12 edges: every pair of corners whose indices differ in EXACTLY one
    // bit (i.e. differ along exactly one axis) - the standard box edge
    // adjacency, derived directly from the 3-bit corner indexing above
    // rather than a hand-written literal edge list, so it can never drift
    // out of sync with localCorners' own indexing convention.
    std::vector<WireframeSegment> segments;
    segments.reserve(12);
    for (int i = 0; i < 8; ++i) {
        for (int bit = 0; bit < 3; ++bit) {
            const int j = i ^ (1 << bit);
            if (j > i) { // Each edge counted once (i < j).
                segments.push_back(WireframeSegment{ worldCorners[i], worldCorners[j] });
            }
        }
    }
    return segments;
}

std::vector<WireframeSegment> BuildCapsuleWireframe(const Vec3& shapeSize, const Vec3& translate, const Quat& rotation)
{
    const float radius = shapeSize.x;
    if (radius <= kEpsilon) {
        return {};
    }
    const float halfHeight = std::max(0.0f, shapeSize.y) * 0.5f; // Capsule axis = local +Y (see PhysicsData.h).

    std::vector<WireframeSegment> segments;
    segments.reserve(static_cast<std::size_t>(kCircleSegments) * 2 + 4
        + static_cast<std::size_t>(kHemisphereArcSegments) * 4);

    // Two rings (the cylindrical body's top/bottom silhouette), in the
    // local XZ plane, at y = +/-halfHeight.
    const auto ringAt = [radius](float y) {
        return [radius, y](int i) {
            const float t = (kTwoPi * static_cast<float>(i)) / static_cast<float>(kCircleSegments);
            return Vec3(std::cos(t) * radius, y, std::sin(t) * radius);
        };
    };
    AppendClosedLoop(segments, kCircleSegments, rotation, translate, ringAt(halfHeight));
    AppendClosedLoop(segments, kCircleSegments, rotation, translate, ringAt(-halfHeight));

    // Four straight lines connecting the two rings (the cylinder's
    // "sides"), at 0/90/180/270 degrees around the ring.
    for (int i = 0; i < 4; ++i) {
        const float t = (kHalfPi * static_cast<float>(i));
        const Vec3 top = ToWorld(Vec3(std::cos(t) * radius, halfHeight, std::sin(t) * radius), rotation, translate);
        const Vec3 bottom =
            ToWorld(Vec3(std::cos(t) * radius, -halfHeight, std::sin(t) * radius), rotation, translate);
        segments.push_back(WireframeSegment{ top, bottom });
    }

    // Four hemisphere arcs - 2 per end cap (one in the local XY plane, one
    // in the local ZY plane), each a half-circle from the ring's edge up/
    // down to the pole - the standard "wire capsule" cap treatment.
    const auto topXyArc = [radius, halfHeight](int i) {
        const float t = (kPi * static_cast<float>(i)) / static_cast<float>(kHemisphereArcSegments);
        return Vec3(std::cos(t) * radius, halfHeight + std::sin(t) * radius, 0.0f);
    };
    const auto topZyArc = [radius, halfHeight](int i) {
        const float t = (kPi * static_cast<float>(i)) / static_cast<float>(kHemisphereArcSegments);
        return Vec3(0.0f, halfHeight + std::sin(t) * radius, std::cos(t) * radius);
    };
    const auto bottomXyArc = [radius, halfHeight](int i) {
        const float t = (kPi * static_cast<float>(i)) / static_cast<float>(kHemisphereArcSegments);
        return Vec3(std::cos(t) * radius, -halfHeight - std::sin(t) * radius, 0.0f);
    };
    const auto bottomZyArc = [radius, halfHeight](int i) {
        const float t = (kPi * static_cast<float>(i)) / static_cast<float>(kHemisphereArcSegments);
        return Vec3(0.0f, -halfHeight - std::sin(t) * radius, std::cos(t) * radius);
    };
    AppendOpenArc(segments, kHemisphereArcSegments, rotation, translate, topXyArc);
    AppendOpenArc(segments, kHemisphereArcSegments, rotation, translate, topZyArc);
    AppendOpenArc(segments, kHemisphereArcSegments, rotation, translate, bottomXyArc);
    AppendOpenArc(segments, kHemisphereArcSegments, rotation, translate, bottomZyArc);

    return segments;
}

} // namespace

std::vector<WireframeSegment> BuildRigidBodyWireframe(
    RigidBodyShape shape, const Vec3& shapeSize, const Vec3& translate, const Vec3& rotateRadians)
{
    const Quat rotation = RotationFromPmxEuler(rotateRadians);
    switch (shape) {
    case RigidBodyShape::Sphere:
        return BuildSphereWireframe(shapeSize, translate, rotation);
    case RigidBodyShape::Box:
        return BuildBoxWireframe(shapeSize, translate, rotation);
    case RigidBodyShape::Capsule:
        return BuildCapsuleWireframe(shapeSize, translate, rotation);
    }
    return {};
}

} // namespace gte
