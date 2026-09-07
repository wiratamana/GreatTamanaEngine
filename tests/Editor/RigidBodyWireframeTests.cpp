// Unit tests for RigidBodyWireframe (src/Editor/RigidBodyWireframe.h) - the
// pure, per-shape wireframe geometry builder behind the Bone Viewer's
// Rigid Body gizmo (see AGENTS.md, "Testability & Regression Safety", and
// task_manager/verlet-integration-3/PHASE1_RIGID_BODY_WIREFRAME_GEOMETRY_MODULE.md).
// Deliberately pure logic with no ImGui/SDL/Vulkan dependency at all - only
// compiled/linked when GTE_ENABLE_EDITOR AND GTE_ENABLE_PROJECT_PANEL are
// both ON, since RigidBodyWireframe itself is only ever compiled into
// gte_core then (see tests/CMakeLists.txt).

#include "Editor/RigidBodyWireframe.h"

#include "Math/MathTypes.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>

namespace gte {
namespace {

constexpr int kCircleSegments = 24;
constexpr int kHemisphereArcSegments = 16;

float DistanceFromAxis(const Vec3& point, const Vec3& axisOrigin, const Vec3& axisDirection)
{
    // Perpendicular distance from `point` to the infinite line through
    // axisOrigin along axisDirection (assumed already normalized) - used to
    // check a capsule ring's points all lie at exactly `radius` from the
    // capsule's own long axis.
    const Vec3 toPoint = point - axisOrigin;
    const float along = Dot(toPoint, axisDirection);
    const Vec3 closestOnAxis = axisOrigin + axisDirection * along;
    return Length(point - closestOnAxis);
}

TEST(RigidBodyWireframeTest, SphereIsEmptyForNonPositiveRadius)
{
    const auto segments =
        BuildRigidBodyWireframe(RigidBodyShape::Sphere, Vec3(0.0f, 0.0f, 0.0f), Vec3::Zero(), Vec3::Zero());
    EXPECT_TRUE(segments.empty());
}

TEST(RigidBodyWireframeTest, SphereHasThreeCirclesEveryPointAtExactRadiusFromTranslate)
{
    const Vec3 translate(5.0f, 1.0f, -3.0f);
    const auto segments =
        BuildRigidBodyWireframe(RigidBodyShape::Sphere, Vec3(2.0f, 0.0f, 0.0f), translate, Vec3::Zero());

    ASSERT_EQ(segments.size(), static_cast<std::size_t>(kCircleSegments) * 3);
    for (const WireframeSegment& seg : segments) {
        EXPECT_NEAR(Length(seg.a - translate), 2.0f, 0.001f);
        EXPECT_NEAR(Length(seg.b - translate), 2.0f, 0.001f);
    }
}

TEST(RigidBodyWireframeTest, BoxIsEmptyWhenAllHalfExtentsNonPositive)
{
    const auto segments =
        BuildRigidBodyWireframe(RigidBodyShape::Box, Vec3(0.0f, 0.0f, 0.0f), Vec3::Zero(), Vec3::Zero());
    EXPECT_TRUE(segments.empty());
}

TEST(RigidBodyWireframeTest, BoxHasTwelveEdgesWithLengthsMatchingHalfExtentsWhenUnrotated)
{
    const auto segments =
        BuildRigidBodyWireframe(RigidBodyShape::Box, Vec3(1.0f, 2.0f, 3.0f), Vec3::Zero(), Vec3::Zero());

    ASSERT_EQ(segments.size(), 12u);
    int countNearX = 0, countNearY = 0, countNearZ = 0; // Edge lengths 2, 4, 6.
    for (const WireframeSegment& seg : segments) {
        const float len = Length(seg.b - seg.a);
        if (std::fabs(len - 2.0f) < 0.001f) ++countNearX;
        else if (std::fabs(len - 4.0f) < 0.001f) ++countNearY;
        else if (std::fabs(len - 6.0f) < 0.001f) ++countNearZ;
    }
    // 4 edges run along each of the 3 axes.
    EXPECT_EQ(countNearX, 4);
    EXPECT_EQ(countNearY, 4);
    EXPECT_EQ(countNearZ, 4);
}

TEST(RigidBodyWireframeTest, BoxAppliesRotationAndTranslation)
{
    // A 90-degree yaw (around Y) rotates the box's local +X axis onto world
    // -Z, and its local +Z axis onto world +X (Rodrigues' rotation formula
    // for a +90 degree rotation about Y, applied to this box's own
    // right-hand-rule axis-angle convention - see Quat.h) - so a non-cubic
    // box's LARGER half-extent (local Z, 3.0) should end up spanning world
    // X, and its SMALLER half-extent (local X, 1.0) should end up spanning
    // world Z, the reverse of its unrotated axis-aligned layout. Verified
    // by checking every corner's distance from `translate` still equals the
    // same diagonal length (rotation preserves distances), plus that the
    // corners' own world-space X/Z spans swapped magnitudes accordingly.
    const Vec3 translate(10.0f, 0.0f, 0.0f);
    const auto segments = BuildRigidBodyWireframe(
        RigidBodyShape::Box, Vec3(1.0f, 1.0f, 3.0f), translate, Vec3(0.0f, DegToRad(90.0f), 0.0f));

    ASSERT_EQ(segments.size(), 12u);
    const float expectedDiagonal = Length(Vec3(1.0f, 1.0f, 3.0f));
    float maxAbsWorldXOffset = 0.0f;
    float maxAbsWorldZOffset = 0.0f;
    for (const WireframeSegment& seg : segments) {
        EXPECT_NEAR(Length(seg.a - translate), expectedDiagonal, 0.01f);
        maxAbsWorldXOffset = std::max(maxAbsWorldXOffset, std::fabs(seg.a.x - translate.x));
        maxAbsWorldZOffset = std::max(maxAbsWorldZOffset, std::fabs(seg.a.z - translate.z));
    }
    EXPECT_NEAR(maxAbsWorldXOffset, 3.0f, 0.01f); // Local Z half-extent (3.0) now spans world X.
    EXPECT_NEAR(maxAbsWorldZOffset, 1.0f, 0.01f); // Local X half-extent (1.0) now spans world Z.
}

TEST(RigidBodyWireframeTest, CapsuleIsEmptyForNonPositiveRadius)
{
    const auto segments =
        BuildRigidBodyWireframe(RigidBodyShape::Capsule, Vec3(0.0f, 2.0f, 0.0f), Vec3::Zero(), Vec3::Zero());
    EXPECT_TRUE(segments.empty());
}

TEST(RigidBodyWireframeTest, CapsuleHasExpectedSegmentCount)
{
    const auto segments =
        BuildRigidBodyWireframe(RigidBodyShape::Capsule, Vec3(1.0f, 2.0f, 0.0f), Vec3::Zero(), Vec3::Zero());

    const std::size_t expected = static_cast<std::size_t>(kCircleSegments) * 2 // two rings
        + 4 // four verticals
        + static_cast<std::size_t>(kHemisphereArcSegments) * 4; // four hemisphere arcs
    EXPECT_EQ(segments.size(), expected);
}

TEST(RigidBodyWireframeTest, CapsuleRingPointsAreAtCorrectRadiusFromLocalAxisWhenUnrotated)
{
    const Vec3 translate(0.0f, 0.0f, 0.0f);
    const auto segments =
        BuildRigidBodyWireframe(RigidBodyShape::Capsule, Vec3(1.5f, 4.0f, 0.0f), translate, Vec3::Zero());

    ASSERT_FALSE(segments.empty());
    // Every ring/vertical/arc point must be within radius 1.5 (rings/
    // verticals exactly at 1.5 from the local Y axis; hemisphere arc points
    // trace the same radius around their own cap center) of SOME point on
    // the local Y axis - a loose but meaningful bound checking nothing
    // drifted arbitrarily far from the capsule's own axis.
    for (const WireframeSegment& seg : segments) {
        EXPECT_LE(DistanceFromAxis(seg.a, translate, Vec3::Up()), 1.5f + 0.01f);
        EXPECT_LE(DistanceFromAxis(seg.b, translate, Vec3::Up()), 1.5f + 0.01f);
    }
}

} // namespace
} // namespace gte
