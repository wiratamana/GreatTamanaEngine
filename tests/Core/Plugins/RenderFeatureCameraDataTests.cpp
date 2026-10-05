// Tier-1 tests for ResolveRenderFeatureCameraData()/RenderFeatureCameraDataWarningTracker
// (src/Core/Plugins/RenderFeatureCameraData.h/.cpp) - pure logic, fed with a
// hand-built RenderPassViewData, no live Core/Renderer/Vulkan device needed.

#include "Core/Plugins/RenderFeatureCameraData.h"

#include <gtest/gtest.h>

namespace gte {
namespace {

RenderPassViewData MakeViewData(const Mat4& viewProjection, const Vec3& eyeWorldPosition)
{
    RenderPassViewData data;
    data.viewProjection = viewProjection;
    data.eyeWorldPosition = eyeWorldPosition;
    return data;
}

TEST(RenderFeatureCameraDataTest, WellConditionedViewProjectionProducesValidCheckedInverse)
{
    const Vec3 eye(1.0f, 2.0f, -5.0f);
    const Mat4 viewProjection = Mat4::PerspectiveFovLH_ZO(1.0f, 16.0f / 9.0f, 0.1f, 100.0f)
        * Mat4::LookAtLH(eye, Vec3(0.0f, 0.0f, 0.0f), Vec3(0.0f, 1.0f, 0.0f));
    RenderPassViewData viewData = MakeViewData(viewProjection, eye);

    const RenderFeatureCameraData result = ResolveRenderFeatureCameraData(rg::RenderViewId::Named("Game"), &viewData);

    EXPECT_TRUE(result.invViewProjectionValid);
    EXPECT_EQ(result.eyeWorldPosition.x, eye.x);
    EXPECT_EQ(result.eyeWorldPosition.y, eye.y);
    EXPECT_EQ(result.eyeWorldPosition.z, eye.z);

    Mat4 expectedInverse;
    ASSERT_TRUE(viewProjection.TryInverse(expectedInverse));
    EXPECT_TRUE(ApproximatelyEqual(result.invViewProjection, expectedInverse));
}

TEST(RenderFeatureCameraDataTest, SingularViewProjectionProducesInvalidIdentityFallback)
{
    // A default-constructed Mat4 is all-zero (see Mat4.h's own constructor
    // doc comment) - its determinant is 0, so TryInverse() must reject it.
    RenderPassViewData viewData = MakeViewData(Mat4{}, Vec3(0.0f, 0.0f, 0.0f));

    const RenderFeatureCameraData result = ResolveRenderFeatureCameraData(rg::RenderViewId::Named("Game"), &viewData);

    EXPECT_FALSE(result.invViewProjectionValid);
    EXPECT_TRUE(ApproximatelyEqual(result.invViewProjection, Mat4::Identity()));
    // Confirm the fallback is genuinely Identity(), never a default-constructed
    // (all-zero) Mat4{} - the two must never be confused.
    EXPECT_FALSE(ApproximatelyEqual(result.invViewProjection, Mat4{}));
}

TEST(RenderFeatureCameraDataTest, SecondDifferentViewGoingSingularAfterwardStillReportsItsOwnInvalidResult)
{
    RenderPassViewData firstViewData = MakeViewData(Mat4{}, Vec3(0.0f, 0.0f, 0.0f));
    const RenderFeatureCameraData firstResult =
        ResolveRenderFeatureCameraData(rg::RenderViewId::Named("Game"), &firstViewData);
    ASSERT_FALSE(firstResult.invViewProjectionValid);

    // A SECOND, DIFFERENT view going singular afterward must produce its own,
    // fresh invalid result - never silently "fixed" by the first view's
    // already-triggered warning.
    RenderPassViewData secondViewData = MakeViewData(Mat4{}, Vec3(0.0f, 0.0f, 0.0f));
    const RenderFeatureCameraData secondResult =
        ResolveRenderFeatureCameraData(rg::RenderViewId::Named("Scene"), &secondViewData);

    EXPECT_FALSE(secondResult.invViewProjectionValid);
    EXPECT_TRUE(ApproximatelyEqual(secondResult.invViewProjection, Mat4::Identity()));
}

TEST(RenderFeatureCameraDataTest, NullViewDataProducesAQuietInvalidDefaultResult)
{
    const RenderFeatureCameraData result = ResolveRenderFeatureCameraData(rg::RenderViewId::Named("Game"), nullptr);

    EXPECT_FALSE(result.invViewProjectionValid);
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

    EXPECT_TRUE(tracker.ShouldWarnOnce(rg::RenderViewId::Named("Game")));
}

TEST(RenderFeatureCameraDataWarningTrackerTest, SecondCallForTheSameViewReturnsFalse)
{
    RenderFeatureCameraDataWarningTracker tracker;

    EXPECT_TRUE(tracker.ShouldWarnOnce(rg::RenderViewId::Named("Game")));
    EXPECT_FALSE(tracker.ShouldWarnOnce(rg::RenderViewId::Named("Game")));
}

TEST(RenderFeatureCameraDataWarningTrackerTest, ADifferentViewGoingSingularAfterwardsStillGetsItsOwnFreshWarning)
{
    RenderFeatureCameraDataWarningTracker tracker;

    EXPECT_TRUE(tracker.ShouldWarnOnce(rg::RenderViewId::Named("Game")));
    EXPECT_FALSE(tracker.ShouldWarnOnce(rg::RenderViewId::Named("Game")));

    // A second, different RenderViewId must get its own fresh "first warning"
    // - never silently swallowed by the first view's already-fired warning.
    EXPECT_TRUE(tracker.ShouldWarnOnce(rg::RenderViewId::Named("Scene")));
    EXPECT_FALSE(tracker.ShouldWarnOnce(rg::RenderViewId::Named("Scene")));
}

TEST(RenderFeatureCameraDataWarningTrackerTest, ResetClearsSeenViewsSoAPreviouslyWarnedViewIsFreshAgain)
{
    RenderFeatureCameraDataWarningTracker tracker;

    EXPECT_TRUE(tracker.ShouldWarnOnce(rg::RenderViewId::Named("Game")));
    EXPECT_FALSE(tracker.ShouldWarnOnce(rg::RenderViewId::Named("Game")));

    tracker.Reset();

    EXPECT_TRUE(tracker.ShouldWarnOnce(rg::RenderViewId::Named("Game")));
}

} // namespace
} // namespace gte
