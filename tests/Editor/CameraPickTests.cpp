#include "Editor/CameraPick.h"

#include "Editor/ViewportProjection.h"

#include <gtest/gtest.h>

#include <array>
#include <limits>
#include <vector>

namespace gte {
namespace {

constexpr Vec2 kRectMin{ 0.0f, 0.0f };
constexpr Vec2 kRectMax{ 800.0f, 800.0f };

// Viewer at z = -10 looking down +Z: the candidate at the origin appears at the image centre.
Mat4 ViewerViewProjection(float eyeZ = -10.0f)
{
    const Mat4 view = Mat4::LookAtLH(Vec3(0, 0, eyeZ), Vec3(0, 0, eyeZ + 1.0f), Vec3::Up());
    return Mat4::PerspectiveFovLH_ZO(DegToRad(60.0f), 1.0f, 0.1f, 100.0f, true) * view;
}

FrustumInput Candidate(float x = 0.0f, float farZ = 20.0f)
{
    FrustumInput in;
    in.position = Vec3(x, 0, 0);
    in.fovYDegrees = 40.0f;
    in.nearZ = 1.0f;
    in.farZ = farZ;
    in.aspect = 1.0f;
    return in;
}

TEST(CameraPickTest, ApexClickPicks)
{
    const std::array<CameraPickCandidate, 1> list{ CameraPickCandidate{ 7, Candidate() } };
    const auto hit = PickCamera(list, ViewerViewProjection(), kRectMin, kRectMax, Vec2(413.0f, 400.0f));
    ASSERT_TRUE(hit.has_value());
    EXPECT_EQ(hit->id, 7u);
    EXPECT_TRUE(hit->onApex);
}

TEST(CameraPickTest, FarFromApexAndEdgesPicksNothing)
{
    const std::array<CameraPickCandidate, 1> list{ CameraPickCandidate{ 7, Candidate() } };
    EXPECT_FALSE(PickCamera(list, ViewerViewProjection(), kRectMin, kRectMax, Vec2(415.0f, 400.0f)).has_value());
}

TEST(CameraPickTest, EdgeWithinSixPixelsPicksAndSevenDoesNot)
{
    const FrustumInput in = Candidate();
    const std::array<CameraPickCandidate, 1> list{ CameraPickCandidate{ 3, in } };
    const Mat4 viewProj = ViewerViewProjection();
    const FrustumCorners corners = ComputeFrustumCorners(in);

    Vec2 mid;
    ASSERT_TRUE(ProjectToScreen((corners.farC[0] + corners.farC[1]) * 0.5f, viewProj, kRectMin, kRectMax, mid));
    const Vec2 outward = Normalize(mid - Vec2(400.0f, 400.0f)); // the outermost edge: nothing lies beyond it

    const auto closeHit = PickCamera(list, viewProj, kRectMin, kRectMax, mid + outward * 5.0f);
    ASSERT_TRUE(closeHit.has_value());
    EXPECT_EQ(closeHit->id, 3u);
    EXPECT_FALSE(closeHit->onApex);

    EXPECT_FALSE(PickCamera(list, viewProj, kRectMin, kRectMax, mid + outward * 7.0f).has_value());
}

TEST(CameraPickTest, NearerApexOnScreenWins)
{
    const std::array<CameraPickCandidate, 2> list{ CameraPickCandidate{ 1, Candidate(0.2f) },
        CameraPickCandidate{ 2, Candidate(0.0f) } };
    const auto hit = PickCamera(list, ViewerViewProjection(), kRectMin, kRectMax, Vec2(400.0f, 400.0f));
    ASSERT_TRUE(hit.has_value());
    EXPECT_EQ(hit->id, 2u);
}

TEST(CameraPickTest, CameraBehindViewIsNotPicked)
{
    const std::array<CameraPickCandidate, 1> list{ CameraPickCandidate{ 7, Candidate(0.0f, 5.0f) } };
    EXPECT_FALSE(PickCamera(list, ViewerViewProjection(10.0f), kRectMin, kRectMax, Vec2(400.0f, 400.0f)).has_value());
}

TEST(CameraPickTest, InvalidFrustumIsSkipped)
{
    FrustumInput bad = Candidate();
    bad.fovYDegrees = std::numeric_limits<float>::quiet_NaN();
    const std::array<CameraPickCandidate, 1> list{ CameraPickCandidate{ 7, bad } };
    EXPECT_FALSE(PickCamera(list, ViewerViewProjection(), kRectMin, kRectMax, Vec2(400.0f, 400.0f)).has_value());
}

TEST(CameraPickTest, EmptyListAndZeroRectGiveNothing)
{
    const std::vector<CameraPickCandidate> empty;
    EXPECT_FALSE(PickCamera(empty, ViewerViewProjection(), kRectMin, kRectMax, Vec2(400.0f, 400.0f)).has_value());

    const std::array<CameraPickCandidate, 1> list{ CameraPickCandidate{ 7, Candidate() } };
    EXPECT_FALSE(PickCamera(list, ViewerViewProjection(), Vec2(10.0f, 10.0f), Vec2(10.0f, 10.0f), Vec2(10.0f, 10.0f))
            .has_value());
}

} // namespace
} // namespace gte
