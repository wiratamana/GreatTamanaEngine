#include "Editor/CameraFrustumGizmo.h"

#include "ECS/Components/Camera.h"
#include "Math/MathTypes.h"
#include "Renderer/Culling/CullingTypes.h"

#include <gtest/gtest.h>

#include <algorithm>
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

    const Vec3 arrowTip = c.apex + c.forward * (10.0f * 0.15f); // 15% of the distance to the far plane centre
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

Plane MakePlane(const Vec3& normal)
{
    Plane p;
    p.normal = Normalize(normal);
    return p;
}

TEST(CameraFrustumGizmoTest, HandBuiltPlanesAreNamedByTheirFace)
{
    const FrustumCorners basis = ComputeFrustumCorners(UnitCamera());
    EXPECT_STREQ(NameFrustumPlane(MakePlane({ 1, 0, 1 }), basis), "Left");
    EXPECT_STREQ(NameFrustumPlane(MakePlane({ -1, 0, 1 }), basis), "Right");
    EXPECT_STREQ(NameFrustumPlane(MakePlane({ 0, 1, 1 }), basis), "Bottom");
    EXPECT_STREQ(NameFrustumPlane(MakePlane({ 0, -1, 1 }), basis), "Top");
    EXPECT_STREQ(NameFrustumPlane(MakePlane({ 0, 0, 1 }), basis), "Near");
    EXPECT_STREQ(NameFrustumPlane(MakePlane({ 0, 0, -1 }), basis), "Far");
}

TEST(CameraFrustumGizmoTest, SidePlanesOfAWideLensAreNotTakenForNearOrFar)
{
    FrustumInput in = UnitCamera();
    in.fovYDegrees = 60.0f;
    in.aspect = 16.0f / 9.0f; // the side normals lean further towards forward than towards right
    const FrustumCorners basis = ComputeFrustumCorners(in);
    const float halfX = std::tan(DegToRad(30.0f)) * in.aspect;
    EXPECT_STREQ(NameFrustumPlane(MakePlane({ 1, 0, halfX }), basis), "Left");
    EXPECT_STREQ(NameFrustumPlane(MakePlane({ -1, 0, halfX }), basis), "Right");
}

TEST(CameraFrustumGizmoTest, RolledCameraPlanesGetDistinctNamesAndArrowsStartOnThem)
{
    FrustumInput in;
    in.position = Vec3(3.0f, -2.0f, 5.0f);
    in.rotation = Quat::FromEulerDegrees(10.0f, 35.0f, 40.0f);
    in.fovYDegrees = 60.0f;
    in.nearZ = 0.5f;
    in.farZ = 50.0f;
    in.aspect = 16.0f / 9.0f;

    const Camera camera{ in.fovYDegrees, in.nearZ, in.farZ, true };
    Transform transform;
    transform.position = in.position;
    transform.rotation = in.rotation;
    const std::array<Plane, 6> planes = ExtractFrustumPlanes(camera.ProjectionMatrix(in.aspect) * Camera::ViewMatrix(transform));
    const FrustumCorners basis = ComputeFrustumCorners(in);
    const std::vector<FrustumPlaneArrow> arrows = BuildFrustumPlaneArrows(planes, basis);
    ASSERT_EQ(arrows.size(), 6u);

    const float expectedLength = Length((basis.farC[0] + basis.farC[2]) * 0.5f - basis.apex) * 0.1f;
    std::array<std::string, 6> names;
    for (std::size_t i = 0; i < 6; ++i) {
        const Plane& plane = planes[i];
        const FrustumPlaneArrow& arrow = arrows[i];
        names[i] = NameFrustumPlane(plane, basis);
        EXPECT_EQ(names[i], arrow.name);
        EXPECT_NEAR(Dot(plane.normal, arrow.from) + plane.distance, 0.0f, 1e-2f) << names[i];
        EXPECT_NEAR(Length(arrow.to - arrow.from), expectedLength, 1e-3f) << names[i];
        EXPECT_TRUE(ApproximatelyEqual(arrow.to - arrow.from, plane.normal * expectedLength, 1e-3f)) << names[i];

        if (names[i] == "Left") { EXPECT_GT(Dot(plane.normal, basis.right), 0.3f); }
        if (names[i] == "Right") { EXPECT_LT(Dot(plane.normal, basis.right), -0.3f); }
        if (names[i] == "Bottom") { EXPECT_GT(Dot(plane.normal, basis.up), 0.3f); }
        if (names[i] == "Top") { EXPECT_LT(Dot(plane.normal, basis.up), -0.3f); }
        if (names[i] == "Near") { EXPECT_GT(Dot(plane.normal, basis.forward), 0.99f); }
        if (names[i] == "Far") { EXPECT_LT(Dot(plane.normal, basis.forward), -0.99f); }
    }
    std::sort(names.begin(), names.end());
    EXPECT_EQ(std::adjacent_find(names.begin(), names.end()), names.end()) << "two planes share a name";
}

} // namespace
} // namespace gte
