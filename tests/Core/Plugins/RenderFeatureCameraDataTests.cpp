// Tier-1 tests for ResolveRenderFeatureCameraData()/RenderFeatureCameraDataWarningTracker
// (src/Core/Plugins/RenderFeatureCameraData.h/.cpp) - pure logic, fed with a
// hand-built RenderPassViewData, no live Core/Renderer/Vulkan device needed.

#include "Core/Plugins/RenderFeatureCameraData.h"
#include "Application/RenderPassViewData.h" // RenderPassViewData's real definition - only forward-declared above.

#include <gtest/gtest.h>

#include <cmath>

namespace gte {
namespace {

RenderPassViewData MakeViewData(const Mat4& viewProjection, const Vec3& eyeWorldPosition)
{
    RenderPassViewData data;
    data.viewProjection = viewProjection;
    data.eyeWorldPosition = eyeWorldPosition;
    return data;
}

TEST(RenderFeatureCameraDataTest, WellConditionedViewProjectionProducesAValidEyeRelativeCamera)
{
    const Vec3 eye(1.0f, 2.0f, -5.0f);
    const Mat4 viewProjection = Mat4::PerspectiveFovLH_ZO(1.0f, 16.0f / 9.0f, 0.1f, 100.0f)
        * Mat4::LookAtLH(eye, Vec3(0.0f, 0.0f, 0.0f), Vec3(0.0f, 1.0f, 0.0f));
    RenderPassViewData viewData = MakeViewData(viewProjection, eye);

    const RenderFeatureCameraData result = ResolveRenderFeatureCameraData(rg::RenderViewId::Named("Primary"), &viewData);

    EXPECT_TRUE(result.relative.valid);
    EXPECT_EQ(result.eyeWorldPosition.x, eye.x);
    EXPECT_EQ(result.eyeWorldPosition.y, eye.y);
    EXPECT_EQ(result.eyeWorldPosition.z, eye.z);
    EXPECT_TRUE(ApproximatelyEqual(result.relative.inverse * result.relative.viewProjection, Mat4::Identity(), 1.0e-4f));
}

TEST(RenderFeatureCameraDataTest, LargeOrthographicViewProjectionProducesValidInverse)
{
    // Small determinant (~1e-12), but a regular matrix.
    const float halfHeight = 1155.0f;
    const float farZ = 200000.0f;
    const Mat4 viewProjection = Mat4::OrthographicLH_ZO(-halfHeight * 1.6f, halfHeight * 1.6f, -halfHeight, halfHeight, -farZ, farZ, true)
        * Mat4::LookAtLH(Vec3(0.0f, 2000.0f, 0.0f), Vec3(0.0f, 0.0f, 0.0f), Vec3(0.0f, 0.0f, 1.0f));
    RenderPassViewData viewData = MakeViewData(viewProjection, Vec3(0.0f, 2000.0f, 0.0f));

    const RenderFeatureCameraData result = ResolveRenderFeatureCameraData(rg::RenderViewId::Named("Primary"), &viewData);

    EXPECT_TRUE(result.relative.valid);
}

TEST(RenderFeatureCameraDataTest, SingularViewProjectionProducesAnInvalidIdentityCamera)
{
    // A default-constructed Mat4 is all-zero (see Mat4.h's own constructor
    // doc comment) - its determinant is 0, so the inverse must be rejected.
    RenderPassViewData viewData = MakeViewData(Mat4{}, Vec3(0.0f, 0.0f, 0.0f));

    const RenderFeatureCameraData result = ResolveRenderFeatureCameraData(rg::RenderViewId::Named("Primary"), &viewData);

    EXPECT_FALSE(result.relative.valid);
    // The fallback is Identity(), never a default-constructed (all-zero) Mat4{}.
    EXPECT_TRUE(ApproximatelyEqual(result.relative.inverse, Mat4::Identity()));
    EXPECT_FALSE(ApproximatelyEqual(result.relative.inverse, Mat4{}));
}

TEST(RenderFeatureCameraDataTest, SecondDifferentViewGoingSingularAfterwardStillReportsItsOwnInvalidResult)
{
    RenderPassViewData firstViewData = MakeViewData(Mat4{}, Vec3(0.0f, 0.0f, 0.0f));
    const RenderFeatureCameraData firstResult =
        ResolveRenderFeatureCameraData(rg::RenderViewId::Named("Primary"), &firstViewData);
    ASSERT_FALSE(firstResult.relative.valid);

    // A SECOND, DIFFERENT view going singular afterward must produce its own,
    // fresh invalid result - never silently "fixed" by the first view's
    // already-triggered warning.
    RenderPassViewData secondViewData = MakeViewData(Mat4{}, Vec3(0.0f, 0.0f, 0.0f));
    const RenderFeatureCameraData secondResult =
        ResolveRenderFeatureCameraData(rg::RenderViewId::Named("Secondary"), &secondViewData);

    EXPECT_FALSE(secondResult.relative.valid);
    EXPECT_TRUE(ApproximatelyEqual(secondResult.relative.inverse, Mat4::Identity()));
}

TEST(RenderFeatureCameraDataTest, NullViewDataProducesAQuietInvalidDefaultResult)
{
    const RenderFeatureCameraData result = ResolveRenderFeatureCameraData(rg::RenderViewId::Named("Primary"), nullptr);

    EXPECT_FALSE(result.relative.valid);
}

TEST(RenderFeatureCameraDataTest, EyeRelativeCameraIsBuiltFromTheMatrixWhenViewDataHasNoParts)
{
    const Vec3 eye(2000.0f, 2.0f, -5000.0f);
    const Mat4 viewProjection = Mat4::PerspectiveFovLH_ZO(1.0f, 16.0f / 9.0f, 0.1f, 1000.0f)
        * Mat4::LookAtLH(eye, eye + Vec3(0.0f, 0.0f, 1.0f), Vec3(0.0f, 1.0f, 0.0f));
    RenderPassViewData viewData = MakeViewData(viewProjection, eye);

    const RenderFeatureCameraData result = ResolveRenderFeatureCameraData(rg::RenderViewId::Named("Primary"), &viewData);

    EXPECT_TRUE(result.relative.valid);
    // A point on the optical axis, 10 m ahead, sits at the clip centre in the eye-relative camera.
    const Vec4 clip = result.relative.viewProjection * Vec4(0.0f, 0.0f, 10.0f, 1.0f);
    EXPECT_NEAR(clip.x / clip.w, 0.0f, 1e-3f);
    EXPECT_NEAR(clip.y / clip.w, 0.0f, 1e-3f);
}

TEST(RenderFeatureCameraDataTest, EyeRelativeCameraPrefersTheMatrixWithoutTheEyeRounding)
{
    const Vec3 eye(54000.0f, 40.0f, -120000.0f);
    const Mat4 projection = Mat4::PerspectiveFovLH_ZO(1.0f, 16.0f / 9.0f, 0.1f, 1000.0f);
    const Mat4 viewProjection = projection * Mat4::LookAtLH(eye, eye + Vec3(0.0f, 0.0f, 1.0f), Vec3(0.0f, 1.0f, 0.0f));
    RenderPassViewData viewData = MakeViewData(viewProjection, eye);
    viewData.eyeRelativeViewProjection = MakeEyeRelativeViewProjection(viewProjection, projection);

    const RenderFeatureCameraData result = ResolveRenderFeatureCameraData(rg::RenderViewId::Named("Primary"), &viewData);

    ASSERT_TRUE(result.relative.valid);
    EXPECT_TRUE(ApproximatelyEqual(result.relative.viewProjection, *viewData.eyeRelativeViewProjection));
}

// Far from the origin the float inverse of the absolute matrix is rejected for most poses (the positive control);
// the eye-relative camera stays valid and exact for every one of them.
TEST(RenderFeatureCameraDataTest, FarCameraStaysValidWhereTheAbsoluteFloatInverseFails)
{
    const Vec3 eye(54000.0f, 40.0f, -120000.0f);
    const Mat4 projection = Mat4::PerspectiveFovLH_ZO(1.0f, 16.0f / 9.0f, 0.1f, 1000.0f, true);
    int absoluteRejected = 0;
    for (int i = 0; i < 40; ++i) {
        const float yaw = 0.157f * static_cast<float>(i);
        const Vec3 forward(std::sin(yaw), 0.1f, std::cos(yaw));
        const Mat4 viewProjection = projection * Mat4::LookToLH(eye, forward, Vec3::Up());
        Mat4 absoluteInverse;
        absoluteRejected += viewProjection.TryInverse(absoluteInverse) ? 0 : 1;

        RenderPassViewData viewData = MakeViewData(viewProjection, eye);
        viewData.eyeRelativeViewProjection = MakeEyeRelativeViewProjection(viewProjection, projection);
        const RenderFeatureCameraData result = ResolveRenderFeatureCameraData(rg::RenderViewId::Named("Primary"), &viewData);
        ASSERT_TRUE(result.relative.valid);
        // The optical axis, 10 m ahead, lands at the clip centre.
        const Vec3 ahead = Normalize(forward) * 10.0f;
        const Vec4 clip = result.relative.viewProjection * Vec4(ahead.x, ahead.y, ahead.z, 1.0f);
        EXPECT_NEAR(clip.x / clip.w, 0.0f, 1.0e-4f);
        EXPECT_NEAR(clip.y / clip.w, 0.0f, 1.0e-4f);
    }
    EXPECT_GT(absoluteRejected, 10);
}

// RenderFeatureCameraDataWarningTracker is the pure, Reset()-able "already
// warned" bookkeeping ResolveRenderFeatureCameraData()'s own log-once-per-view
// behavior is built on (mirrors ImGuiIdConflictTracker.h's precedent - see
// tests/Editor/ImGuiIdConflictTrackerTests.cpp for the identical pattern).
// These tests construct their own fresh instance, completely independent of
// the production file-local singleton.

TEST(RenderFeatureCameraDataWarningTrackerTest, FirstCallForAFreshViewReturnsTrue)
{
    RenderFeatureCameraDataWarningTracker tracker;

    EXPECT_TRUE(tracker.ShouldWarnOnce(rg::RenderViewId::Named("Primary")));
}

TEST(RenderFeatureCameraDataWarningTrackerTest, SecondCallForTheSameViewReturnsFalse)
{
    RenderFeatureCameraDataWarningTracker tracker;

    EXPECT_TRUE(tracker.ShouldWarnOnce(rg::RenderViewId::Named("Primary")));
    EXPECT_FALSE(tracker.ShouldWarnOnce(rg::RenderViewId::Named("Primary")));
}

TEST(RenderFeatureCameraDataWarningTrackerTest, ADifferentViewGoingSingularAfterwardsStillGetsItsOwnFreshWarning)
{
    RenderFeatureCameraDataWarningTracker tracker;

    EXPECT_TRUE(tracker.ShouldWarnOnce(rg::RenderViewId::Named("Primary")));
    EXPECT_FALSE(tracker.ShouldWarnOnce(rg::RenderViewId::Named("Primary")));

    // A second, different RenderViewId must get its own fresh "first warning"
    // - never silently swallowed by the first view's already-fired warning.
    EXPECT_TRUE(tracker.ShouldWarnOnce(rg::RenderViewId::Named("Secondary")));
    EXPECT_FALSE(tracker.ShouldWarnOnce(rg::RenderViewId::Named("Secondary")));
}

TEST(RenderFeatureCameraDataWarningTrackerTest, ResetClearsSeenViewsSoAPreviouslyWarnedViewIsFreshAgain)
{
    RenderFeatureCameraDataWarningTracker tracker;

    EXPECT_TRUE(tracker.ShouldWarnOnce(rg::RenderViewId::Named("Primary")));
    EXPECT_FALSE(tracker.ShouldWarnOnce(rg::RenderViewId::Named("Primary")));

    tracker.Reset();

    EXPECT_TRUE(tracker.ShouldWarnOnce(rg::RenderViewId::Named("Primary")));
}

} // namespace
} // namespace gte
