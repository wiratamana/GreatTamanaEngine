// Tier-1 tests for the view jitter helpers, View::DrawViewProjection and the jitter verb. No Core, no GPU.

#include "Core/FeatureKit/FeatureKit.h"
#include "Core/FeatureKit/ViewJitter.h"
#include "ECS/Components/Camera.h"
#include "ECS/Registry.h"

#include <gtest/gtest.h>

#include <cmath>
#include <cstring>
#include <limits>

namespace gte::fx {
namespace {

Mat4 RealCameraMatrix()
{
    Transform transform;
    transform.position = Vec3{ 0.0f, 0.0f, -5.0f };
    return Camera{}.ProjectionMatrix(16.0f / 9.0f) * Camera::ViewMatrix(transform);
}

bool BitEqual(const Mat4& a, const Mat4& b)
{
    return std::memcmp(&a, &b, sizeof(Mat4)) == 0;
}

TEST(ViewJitterTest, ZeroJitterReturnsTheMatrixBitForBit)
{
    const Mat4 vp = RealCameraMatrix();
    EXPECT_TRUE(BitEqual(ApplyClipJitter(vp, Vec2{ 0.0f, 0.0f }), vp));
    EXPECT_TRUE(BitEqual(ApplyClipJitter(vp, Vec2{ -0.0f, 0.0f }), vp));
    EXPECT_TRUE(BitEqual(ApplyClipJitter(vp, Vec2{ 0.0f, -0.0f }), vp));
}

TEST(ViewJitterTest, JitterShiftsXyAfterTheDivideAndNothingElse)
{
    const Mat4 vp = RealCameraMatrix();
    const Vec2 jitter{ 0.002f, -0.0015f };
    const Mat4 jittered = ApplyClipJitter(vp, jitter);

    for (const Vec3 point : { Vec3{ 0.0f, 0.0f, 0.0f }, Vec3{ 1.0f, 2.0f, 3.0f }, Vec3{ -2.0f, 0.5f, 10.0f },
             Vec3{ 3.0f, -1.5f, 40.0f } }) {
        const Vec4 a = vp * Vec4(point, 1.0f);
        const Vec4 b = jittered * Vec4(point, 1.0f);
        ASSERT_GT(a.w, 0.0f);
        EXPECT_NEAR(b.x / b.w, a.x / a.w + jitter.x, 1e-5f);
        EXPECT_NEAR(b.y / b.w, a.y / a.w + jitter.y, 1e-5f);
        EXPECT_NEAR(b.z / b.w, a.z / a.w, 1e-5f);
        EXPECT_FLOAT_EQ(b.w, a.w);
    }
}

TEST(ViewJitterTest, ValidityChecksFinitenessAndRange)
{
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float inf = std::numeric_limits<float>::infinity();
    EXPECT_FALSE(IsValidViewJitter(Vec2{ nan, 0.0f }));
    EXPECT_FALSE(IsValidViewJitter(Vec2{ 0.0f, nan }));
    EXPECT_FALSE(IsValidViewJitter(Vec2{ inf, 0.0f }));
    EXPECT_FALSE(IsValidViewJitter(Vec2{ 0.0f, -inf }));
    EXPECT_FALSE(IsValidViewJitter(Vec2{ 0.51f, 0.0f }));
    EXPECT_FALSE(IsValidViewJitter(Vec2{ 0.0f, -0.51f }));
    EXPECT_TRUE(IsValidViewJitter(Vec2{ 0.0f, 0.0f }));
    EXPECT_TRUE(IsValidViewJitter(Vec2{ -0.5f, 0.5f }));
}

TEST(ViewJitterTest, ViewWithoutJitterDrawsWithItsOwnMatrix)
{
    View view;
    view.viewProjection = RealCameraMatrix();
    EXPECT_EQ(view.jitterNdc, Vec2::Zero());
    EXPECT_TRUE(BitEqual(view.DrawViewProjection(), view.viewProjection));

    view.jitterNdc = Vec2{ 0.001f, 0.002f };
    EXPECT_FALSE(BitEqual(view.DrawViewProjection(), view.viewProjection));
}

TEST(ViewJitterTest, ClusterDrawCameraUsesTheJitterAndClusterCameraDoesNot)
{
    View view;
    view.viewProjection = RealCameraMatrix();
    view.eye = Vec3{ 0.0f, 0.0f, -5.0f };
    view.jitterNdc = Vec2{ 0.001f, -0.002f };

    EXPECT_TRUE(BitEqual(ClusterCameraOf(view).viewProjection, view.viewProjection));
    EXPECT_TRUE(BitEqual(ClusterDrawCameraOf(view).viewProjection, view.DrawViewProjection()));
    EXPECT_FALSE(BitEqual(ClusterDrawCameraOf(view).viewProjection, view.viewProjection));
    EXPECT_TRUE(ClusterDrawCameraOf(view).valid);

    view.jitterNdc = Vec2::Zero();
    EXPECT_TRUE(BitEqual(ClusterDrawCameraOf(view).viewProjection, ClusterCameraOf(view).viewProjection));
}

TEST(ViewJitterTest, SetViewJitterWithoutAnEngineContextIsRefused)
{
    static Registry registry;
    View view;
    view.scope = rg::ViewScope::Primary;
    PreOpaqueFrame frame(view, registry, 1);
    EXPECT_FALSE(frame.SetViewJitter(Vec2{ 0.001f, 0.001f }));
}

} // namespace
} // namespace gte::fx
