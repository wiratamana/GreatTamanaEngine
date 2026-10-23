#include "Editor/CameraFrustumGizmo.h"

#include "ECS/Components/Camera.h"
#include "Renderer/Culling/CullingTypes.h"

#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <limits>
#include <string>

namespace gte {
namespace {

FrustumInput UnitCamera()
{
    FrustumInput in;
    in.fovYDegrees = 90.0f;
    in.nearZ = 1.0f;
    in.farZ = 10.0f;
    in.aspect = 1.0f;
    return in;
}

TEST(CameraFrustumGizmoTest, CornersOfNinetyDegreeCameraAtOrigin)
{
    const FrustumCorners c = ComputeFrustumCorners(UnitCamera());
    EXPECT_TRUE(ApproximatelyEqual(c.nearC[0], Vec3(-1, -1, 1), 1e-4f));
    EXPECT_TRUE(ApproximatelyEqual(c.nearC[1], Vec3(1, -1, 1), 1e-4f));
    EXPECT_TRUE(ApproximatelyEqual(c.nearC[2], Vec3(1, 1, 1), 1e-4f));
    EXPECT_TRUE(ApproximatelyEqual(c.nearC[3], Vec3(-1, 1, 1), 1e-4f));
    EXPECT_TRUE(ApproximatelyEqual(c.farC[0], Vec3(-10, -10, 10), 1e-4f));
    EXPECT_TRUE(ApproximatelyEqual(c.farC[2], Vec3(10, 10, 10), 1e-4f));
}

TEST(CameraFrustumGizmoTest, CornersLieOnExtractedPlanes)
{
    const FrustumInput in = UnitCamera();
    const Camera camera{ in.fovYDegrees, in.nearZ, in.farZ, true };
    Transform transform;
    const Mat4 viewProj = camera.ProjectionMatrix(in.aspect) * Camera::ViewMatrix(transform);
    const std::array<Plane, 6> planes = ExtractFrustumPlanes(viewProj);
    const FrustumCorners c = ComputeFrustumCorners(in);

    for (const Vec3* ring : { c.nearC, c.farC }) {
        for (int i = 0; i < 4; ++i) {
            int onPlane = 0;
            for (const Plane& plane : planes) {
                const float d = Dot(plane.normal, ring[i]) + plane.distance;
                EXPECT_GE(d, -1e-2f);
                if (std::fabs(d) < 1e-2f) {
                    ++onPlane;
                }
            }
            EXPECT_EQ(onPlane, 3);
        }
    }
}

TEST(CameraFrustumGizmoTest, SegmentListHasSeventeenSegmentsAndKnownEndpoints)
{
    const FrustumCorners c = ComputeFrustumCorners(UnitCamera());
    const std::vector<FrustumSegment> segments = BuildFrustumSegments(c);
    ASSERT_EQ(segments.size(), 17u);

    const Vec3 arrowTip = c.apex + c.forward * (std::sqrt(300.0f) * 0.15f);
    const auto known = [&](const Vec3& p) {
        if (ApproximatelyEqual(p, c.apex, 1e-4f) || ApproximatelyEqual(p, arrowTip, 1e-3f)) {
            return true;
        }
        for (int i = 0; i < 4; ++i) {
            if (ApproximatelyEqual(p, c.nearC[i], 1e-4f) || ApproximatelyEqual(p, c.farC[i], 1e-4f)) {
                return true;
            }
        }
        return false;
    };
    for (const FrustumSegment& s : segments) {
        EXPECT_TRUE(known(s.a));
        EXPECT_TRUE(known(s.b));
    }
    EXPECT_EQ(segments.back().kind, FrustumEdgeKind::Arrow);
}

TEST(CameraFrustumGizmoTest, ValidityRejectsBadInput)
{
    EXPECT_TRUE(FrustumInputIsValid(UnitCamera()));

    FrustumInput in = UnitCamera();
    in.fovYDegrees = std::numeric_limits<float>::quiet_NaN();
    EXPECT_FALSE(FrustumInputIsValid(in));

    in = UnitCamera();
    in.fovYDegrees = 0.0f;
    EXPECT_FALSE(FrustumInputIsValid(in));

    in = UnitCamera();
    in.farZ = in.nearZ;
    EXPECT_FALSE(FrustumInputIsValid(in));

    in = UnitCamera();
    in.position.x = std::numeric_limits<float>::infinity();
    EXPECT_FALSE(FrustumInputIsValid(in));
}

TEST(CameraFrustumGizmoTest, PlaneNamesFollowInwardNormals)
{
    const FrustumCorners basis = ComputeFrustumCorners(UnitCamera());
    const auto name = [&](const Vec3& n) { return std::string(NameFrustumPlane(Plane{ n, 0.0f }, basis)); };
    EXPECT_EQ(name(Vec3(1, 0, 0)), "Left");
    EXPECT_EQ(name(Vec3(-1, 0, 0)), "Right");
    EXPECT_EQ(name(Vec3(0, 1, 0)), "Bottom");
    EXPECT_EQ(name(Vec3(0, -1, 0)), "Top");
    EXPECT_EQ(name(Vec3(0, 0, 1)), "Near");
    EXPECT_EQ(name(Vec3(0, 0, -1)), "Far");
}

TEST(CameraFrustumGizmoTest, PlaneNamesOfRolledCamera)
{
    FrustumInput in = UnitCamera();
    in.rotation = Quat::FromAxisAngle(Vec3::Forward(), DegToRad(90.0f));
    const FrustumCorners basis = ComputeFrustumCorners(in);
    // A roll swaps which world axis is "right": the plane whose normal equals basis.right is still Left.
    EXPECT_STREQ(NameFrustumPlane(Plane{ basis.right, 0.0f }, basis), "Left");
    EXPECT_STREQ(NameFrustumPlane(Plane{ -basis.up, 0.0f }, basis), "Top");
}

} // namespace
} // namespace gte
