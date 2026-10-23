// Frustum corners recovered from a view-projection matrix.

#include "Editor/CameraFrustumGizmo.h"

#include "Math/MathTypes.h"

#include <gtest/gtest.h>

#include <cmath>
#include <vector>

namespace gte {
namespace {

constexpr float kTolerance = 2.0e-3f;

std::vector<Vec3> AllCorners(const FrustumCorners& c)
{
    std::vector<Vec3> out;
    for (int i = 0; i < 4; ++i) {
        out.push_back(c.nearC[i]);
    }
    for (int i = 0; i < 4; ++i) {
        out.push_back(c.farC[i]);
    }
    return out;
}

bool Contains(const std::vector<Vec3>& points, const Vec3& wanted)
{
    for (const Vec3& p : points) {
        if (Length(p - wanted) <= kTolerance) {
            return true;
        }
    }
    return false;
}

FrustumInput TestCamera()
{
    FrustumInput in;
    in.position = Vec3(1.0f, 2.0f, 3.0f);
    in.rotation = Quat::Identity();
    in.fovYDegrees = 60.0f;
    in.nearZ = 1.0f;
    in.farZ = 20.0f;
    in.aspect = 1.5f;
    return in;
}

Mat4 ViewProjectionOf(const FrustumInput& in, bool flipY)
{
    const Mat4 view = Mat4::LookAtLH(in.position, in.position + Vec3::Forward(), Vec3::Up());
    return Mat4::PerspectiveFovLH_ZO(DegToRad(in.fovYDegrees), in.aspect, in.nearZ, in.farZ, flipY) * view;
}

TEST(FrustumFromViewProjectionTest, MatchesTheCornersOfTheCameraBasis)
{
    const FrustumInput in = TestCamera();
    const std::vector<Vec3> expected = AllCorners(ComputeFrustumCorners(in));
    for (const bool flipY : { false, true }) {
        const std::optional<FrustumCorners> corners = FrustumCornersFromViewProjection(ViewProjectionOf(in, flipY));
        ASSERT_TRUE(corners.has_value());
        const std::vector<Vec3> actual = AllCorners(*corners);
        for (const Vec3& corner : expected) {
            EXPECT_TRUE(Contains(actual, corner)) << "flipY " << flipY;
        }
    }
}

TEST(FrustumFromViewProjectionTest, SingularAndNonFiniteMatricesGiveNothing)
{
    EXPECT_FALSE(FrustumCornersFromViewProjection(Mat4::Zero()).has_value());

    Mat4 bad = ViewProjectionOf(TestCamera(), true);
    bad(0, 0) = std::nanf("");
    EXPECT_FALSE(FrustumCornersFromViewProjection(bad).has_value());
}

TEST(FrustumFromViewProjectionTest, OrthographicBoxHasTheRequestedExtent)
{
    const Mat4 projection = Mat4::OrthographicLH_ZO(-5.0f, 5.0f, -3.0f, 3.0f, 0.5f, 40.0f, true);
    const std::optional<FrustumCorners> corners = FrustumCornersFromViewProjection(projection);
    ASSERT_TRUE(corners.has_value());
    const std::vector<Vec3> actual = AllCorners(*corners);
    for (const float x : { -5.0f, 5.0f }) {
        for (const float y : { -3.0f, 3.0f }) {
            EXPECT_TRUE(Contains(actual, Vec3(x, y, 0.5f)));
            EXPECT_TRUE(Contains(actual, Vec3(x, y, 40.0f)));
        }
    }
    EXPECT_NEAR(corners->forward.z, 1.0f, kTolerance);
}

} // namespace
} // namespace gte
