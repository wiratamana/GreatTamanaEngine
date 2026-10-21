// CPU checks of the shared screen-space helpers: camera basis, extent scaling, history rules, resolve block.

#include "Core/FeatureKit/ScreenSpaceView.h"

#include <gtest/gtest.h>

#include <cmath>
#include <cstddef>
#include <limits>

namespace gte::fx {
namespace {

struct TestCamera {
    Mat4 viewProjection;
    Mat4 inverse;
    Vec3 eye;
};

TestCamera MakeCamera(bool flipY, bool orthographic = false)
{
    TestCamera c;
    c.eye = Vec3{ 3.0f, 1.7f, -4.0f };
    const Mat4 view = Mat4::LookAtLH(c.eye, Vec3{ 1.0f, 1.0f, 2.0f }, Vec3::Up());
    const Mat4 proj = orthographic ? Mat4::OrthographicLH_ZO(-8.0f, 8.0f, -4.5f, 4.5f, 0.1f, 100.0f, flipY)
                                   : Mat4::PerspectiveFovLH_ZO(1.0f, 16.0f / 9.0f, 0.1f, 200.0f, flipY);
    c.viewProjection = proj * view;
    EXPECT_TRUE(c.viewProjection.TryInverse(c.inverse));
    return c;
}

TEST(ScreenSpaceViewTest, BasisMatchesPerspectiveForBothYConventions)
{
    for (const bool flipY : { false, true }) {
        const TestCamera cam = MakeCamera(flipY);
        const CameraBasis basis = DeriveCameraBasis(cam.eye, cam.inverse, Extent2D{ 1280, 720 });
        ASSERT_TRUE(basis.valid) << "flipY " << flipY;

        EXPECT_NEAR(Dot(basis.right, basis.forward), 0.0f, 1e-4f);
        EXPECT_NEAR(Dot(basis.down, basis.forward), 0.0f, 1e-4f);
        const Vec3 toTarget = Normalize(Vec3{ 1.0f, 1.0f, 2.0f } - cam.eye);
        EXPECT_NEAR(Dot(basis.forward, toTarget), 1.0f, 1e-4f);

        const float expected = 2.0f * std::tan(0.5f) * (16.0f / 9.0f) / 1280.0f;
        EXPECT_NEAR(basis.pixelScale, expected, expected * 1e-3f) << "flipY " << flipY;
    }
}

TEST(ScreenSpaceViewTest, BasisRejectsOrthographicCamera)
{
    const TestCamera cam = MakeCamera(false, true);
    EXPECT_FALSE(DeriveCameraBasis(cam.eye, cam.inverse, Extent2D{ 1280, 720 }).valid);
}

TEST(ScreenSpaceViewTest, BasisRejectsEmptyExtent)
{
    const TestCamera cam = MakeCamera(false);
    EXPECT_FALSE(DeriveCameraBasis(cam.eye, cam.inverse, Extent2D{ 0, 720 }).valid);
}

// The world point of a pixel equals eye + viewZ * (forward + right * ox * scale + down * oy * scale).
TEST(ScreenSpaceViewTest, UnprojectAgreesWithBasisAtCornersAndCentre)
{
    const Extent2D extent{ 1280, 720 };
    for (const bool flipY : { false, true }) {
        const TestCamera cam = MakeCamera(flipY);
        const CameraBasis b = DeriveCameraBasis(cam.eye, cam.inverse, extent);
        ASSERT_TRUE(b.valid);

        const float xs[] = { 0.0f, 640.0f, 1279.0f };
        const float ys[] = { 0.0f, 360.0f, 719.0f };
        for (const float px : xs) {
            for (const float py : ys) {
                const float ndcX = (px + 0.5f) / static_cast<float>(extent.width) * 2.0f - 1.0f;
                const float ndcY = (py + 0.5f) / static_cast<float>(extent.height) * 2.0f - 1.0f;
                const Vec3 world = UnprojectDepth(cam.inverse, ndcX, ndcY, 0.5f);
                const float viewZ = Dot(world - cam.eye, b.forward);
                const float ox = px + 0.5f - 0.5f * static_cast<float>(extent.width);
                const float oy = py + 0.5f - 0.5f * static_cast<float>(extent.height);
                const Vec3 expected =
                    cam.eye + (b.forward + b.right * (ox * b.pixelScale) + b.down * (oy * b.pixelScale)) * viewZ;
                EXPECT_LT(Length(world - expected), 1e-3f * viewZ) << "flipY " << flipY << " px " << px << " py " << py;
            }
        }
    }
}

TEST(ScreenSpaceViewTest, ScaledExtentRoundsUpAndNeverReachesZero)
{
    const Extent2D odd = ScaledExtent(Extent2D{ 1919, 1079 }, 2);
    EXPECT_EQ(odd.width, 960u);
    EXPECT_EQ(odd.height, 540u);
    const Extent2D tiny = ScaledExtent(Extent2D{ 1, 1 }, 2);
    EXPECT_EQ(tiny.width, 1u);
    EXPECT_EQ(tiny.height, 1u);
    EXPECT_EQ(ScaledExtent(Extent2D{ 0, 0 }, 2).width, 1u);
    EXPECT_EQ(ScaledExtent(Extent2D{ 7, 7 }, 0).width, 7u);
}

TEST(ScreenSpaceViewTest, SanitizeFiniteClampsAndReplacesNonFinite)
{
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float inf = std::numeric_limits<float>::infinity();
    EXPECT_FLOAT_EQ(SanitizeFinite(nan, 0.0f, 1.0f, 0.25f), 0.25f);
    EXPECT_FLOAT_EQ(SanitizeFinite(inf, 0.0f, 1.0f, 0.25f), 0.25f);
    EXPECT_FLOAT_EQ(SanitizeFinite(-inf, 0.0f, 1.0f, 0.25f), 0.25f);
    EXPECT_FLOAT_EQ(SanitizeFinite(-3.0f, 0.0f, 1.0f, 0.25f), 0.0f);
    EXPECT_FLOAT_EQ(SanitizeFinite(3.0f, 0.0f, 1.0f, 0.25f), 1.0f);
    EXPECT_FLOAT_EQ(SanitizeFinite(0.5f, 0.0f, 1.0f, 0.25f), 0.5f);
}

TEST(ScreenSpaceViewTest, PackVec3WritesXyzAndW)
{
    float dst[4] = { 9.0f, 9.0f, 9.0f, 9.0f };
    PackVec3(dst, Vec3{ 1.0f, 2.0f, 3.0f }, 4.0f);
    EXPECT_FLOAT_EQ(dst[0], 1.0f);
    EXPECT_FLOAT_EQ(dst[1], 2.0f);
    EXPECT_FLOAT_EQ(dst[2], 3.0f);
    EXPECT_FLOAT_EQ(dst[3], 4.0f);
    PackVec3(dst, Vec3{ 5.0f, 6.0f, 7.0f });
    EXPECT_FLOAT_EQ(dst[3], 0.0f);
}

TEST(ScreenSpaceViewTest, HistoryRulesRejectEveryUnsafeCase)
{
    CameraBasis basis;
    basis.valid = true;
    basis.forward = Vec3::Forward();
    const Extent2D extent{ 640, 360 };
    TemporalHistory good = MakeHistory(basis, Mat4::Identity(), 10, extent, 2, TemporalHistory{});
    good.stableFrames = 2;

    EXPECT_TRUE(HistoryUsable(good, basis, 11, extent, 2));
    EXPECT_FALSE(HistoryUsable(TemporalHistory{}, basis, 11, extent, 2));
    EXPECT_FALSE(HistoryUsable(good, basis, 12, extent, 2)); // Frame gap.
    EXPECT_FALSE(HistoryUsable(good, basis, 11, Extent2D{ 641, 360 }, 2));
    EXPECT_FALSE(HistoryUsable(good, basis, 11, extent, 1));

    TemporalHistory young = good;
    young.stableFrames = 1;
    EXPECT_FALSE(HistoryUsable(young, basis, 11, extent, 2));

    CameraBasis jumped = basis;
    jumped.eye = Vec3{ 50.0f, 0.0f, 0.0f };
    EXPECT_FALSE(HistoryUsable(good, jumped, 11, extent, 2));

    CameraBasis turned = basis;
    turned.forward = Vec3::Back();
    EXPECT_FALSE(HistoryUsable(good, turned, 11, extent, 2));
}

TEST(ScreenSpaceViewTest, MakeHistoryCountsOnlyUninterruptedStableFrames)
{
    CameraBasis basis;
    basis.valid = true;
    const Extent2D extent{ 640, 360 };

    const TemporalHistory first = MakeHistory(basis, Mat4::Identity(), 5, extent, 2, TemporalHistory{});
    EXPECT_EQ(first.stableFrames, 0u);
    const TemporalHistory second = MakeHistory(basis, Mat4::Identity(), 6, extent, 2, first);
    EXPECT_EQ(second.stableFrames, 1u);
    const TemporalHistory afterGap = MakeHistory(basis, Mat4::Identity(), 9, extent, 2, second);
    EXPECT_EQ(afterGap.stableFrames, 0u);
    const TemporalHistory resized = MakeHistory(basis, Mat4::Identity(), 10, Extent2D{ 320, 180 }, 2, afterGap);
    EXPECT_EQ(resized.stableFrames, 0u);
}

TEST(ScreenSpaceViewTest, ResolveBlockLayoutMatchesStd430)
{
    EXPECT_EQ(offsetof(TemporalResolveParamsGpu, prevViewProjection), 64u);
    EXPECT_EQ(offsetof(TemporalResolveParamsGpu, eye), 128u);
    EXPECT_EQ(offsetof(TemporalResolveParamsGpu, forward), 144u);
    EXPECT_EQ(offsetof(TemporalResolveParamsGpu, prevEye), 160u);
    EXPECT_EQ(offsetof(TemporalResolveParamsGpu, prevForward), 176u);
    EXPECT_EQ(offsetof(TemporalResolveParamsGpu, filter), 192u);
    EXPECT_EQ(offsetof(TemporalResolveParamsGpu, counts), 208u);
    EXPECT_EQ(sizeof(TemporalResolveParamsGpu), 224u);
}

TEST(ScreenSpaceViewTest, ResolveParamsCarryHistoryAndExtent)
{
    CameraBasis basis;
    basis.valid = true;
    basis.eye = Vec3{ 1.0f, 2.0f, 3.0f };
    TemporalHistory previous;
    previous.viewProjection = Mat4::Translation(Vec3{ 1.0f, 0.0f, 0.0f });
    previous.eye = Vec3{ 9.0f, 8.0f, 7.0f };
    previous.forward = Vec3{ 0.0f, 1.0f, 0.0f };
    const TemporalResolveSettings settings{ 0.05f, 0.5f, false, 2 };

    const TemporalResolveParamsGpu p =
        MakeTemporalResolveParams(basis, previous, settings, Extent2D{ 1280, 720 }, Mat4::Identity(), true);

    EXPECT_TRUE(ApproximatelyEqual(p.prevViewProjection, previous.viewProjection));
    EXPECT_FLOAT_EQ(p.eye[2], 3.0f);
    EXPECT_FLOAT_EQ(p.prevEye[0], 9.0f);
    EXPECT_FLOAT_EQ(p.prevForward[1], 1.0f);
    EXPECT_FLOAT_EQ(p.filter[0], 0.05f);
    EXPECT_FLOAT_EQ(p.filter[1], 0.5f);
    EXPECT_FLOAT_EQ(p.filter[2], 0.05f);
    EXPECT_FLOAT_EQ(p.filter[3], 0.0f);
    EXPECT_EQ(p.counts[0], 2u);
    EXPECT_EQ(p.counts[1], 1u);
    EXPECT_EQ(p.counts[2], 1280u);
    EXPECT_EQ(p.counts[3], 720u);
}

} // namespace
} // namespace gte::fx
