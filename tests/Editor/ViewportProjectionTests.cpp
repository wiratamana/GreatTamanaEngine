#include "Editor/ViewportProjection.h"

#include <gtest/gtest.h>

#include <array>
#include <limits>
#include <vector>

namespace gte {
namespace {

// Camera at the origin looking +Z, 90 degree lens, aspect 1, flipY = true.
Mat4 Lens()
{
    return Mat4::PerspectiveFovLH_ZO(DegToRad(90.0f), 1.0f, 0.1f, 100.0f, true);
}

constexpr Vec2 kMin{ 0.0f, 0.0f };
constexpr Vec2 kMax{ 200.0f, 100.0f };

TEST(ViewportProjectionTest, PointInFrontProjectsToExpectedPixel)
{
    Vec2 out;
    ASSERT_TRUE(ProjectToScreen(Vec3(0, 0, 5), Lens(), kMin, kMax, out));
    EXPECT_NEAR(out.x, 100.0f, 1e-3f);
    EXPECT_NEAR(out.y, 50.0f, 1e-3f);

    // x = +z at fov 90: ndc.x = 1 (right border). Flipped Y: world +y goes to ndc -1 (top row).
    ASSERT_TRUE(ProjectToScreen(Vec3(5, 5, 5), Lens(), kMin, kMax, out));
    EXPECT_NEAR(out.x, 200.0f, 1e-2f);
    EXPECT_NEAR(out.y, 0.0f, 1e-2f);
}

TEST(ViewportProjectionTest, PointBehindOrNonFiniteIsRejected)
{
    Vec2 out(-1.0f, -1.0f);
    EXPECT_FALSE(ProjectToScreen(Vec3(0, 0, -5), Lens(), kMin, kMax, out));
    EXPECT_FALSE(ProjectToScreen(Vec3(std::numeric_limits<float>::quiet_NaN(), 0, 5), Lens(), kMin, kMax, out));
    EXPECT_EQ(out.x, -1.0f);
    EXPECT_EQ(out.y, -1.0f);
}

TEST(ViewportProjectionTest, ClipSegmentFullyBehindReturnsFalse)
{
    Vec4 ca;
    Vec4 cb;
    EXPECT_FALSE(ClipSegmentToView(Lens(), Vec3(0, 0, -5), Vec3(1, 0, -2), ca, cb));
}

TEST(ViewportProjectionTest, ClipSegmentFullyInFrontKeepsEnds)
{
    Vec4 ca;
    Vec4 cb;
    ASSERT_TRUE(ClipSegmentToView(Lens(), Vec3(0, 0, 2), Vec3(1, 0, 4), ca, cb));
    EXPECT_NEAR(ca.w, 2.0f, 1e-4f);
    EXPECT_NEAR(cb.w, 4.0f, 1e-4f);
}

TEST(ViewportProjectionTest, ClipSegmentCrossingKeepsOnlyFrontPart)
{
    Vec4 ca;
    Vec4 cb;
    ASSERT_TRUE(ClipSegmentToView(Lens(), Vec3(0, 0, -2), Vec3(0, 0, 4), ca, cb));
    EXPECT_NEAR(ca.w, kMinClipW, 1e-6f);
    EXPECT_NEAR(cb.w, 4.0f, 1e-4f);
}

TEST(ViewportProjectionTest, PointToSegmentDistance)
{
    EXPECT_NEAR(PointToSegmentDistance(Vec2(5, 3), Vec2(0, 0), Vec2(10, 0)), 3.0f, 1e-5f);
    EXPECT_NEAR(PointToSegmentDistance(Vec2(-3, 4), Vec2(0, 0), Vec2(10, 0)), 5.0f, 1e-5f); // clamps to the end
    EXPECT_NEAR(PointToSegmentDistance(Vec2(3, 4), Vec2(0, 0), Vec2(0, 0)), 5.0f, 1e-5f);   // degenerate segment
}

TEST(ViewportProjectionTest, ProjectSegmentsSkipsBehindAndKeepsFront)
{
    const std::array<FrustumSegment, 2> segments{ FrustumSegment{ Vec3(0, 0, -5), Vec3(1, 0, -2), FrustumEdgeKind::Side },
        FrustumSegment{ Vec3(0, 0, 2), Vec3(0, 0, 4), FrustumEdgeKind::Side } };
    std::vector<OverlaySegment2D> out;
    ProjectSegments(segments, Lens(), kMin, kMax, { 1, 2, 3, 4 }, 2.0f, out);
    ASSERT_EQ(out.size(), 1u);
    EXPECT_NEAR(out[0].a.x, 100.0f, 1e-3f);
    EXPECT_NEAR(out[0].a.y, 50.0f, 1e-3f);
    EXPECT_EQ(out[0].rgba[2], 3);
    EXPECT_EQ(out[0].width, 2.0f);
}

} // namespace
} // namespace gte
