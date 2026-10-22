// Settings validation, freeze-camera state and debug-key parsing of the GpuDrivenScene feature.

#include "Features/GpuDrivenScene/GpuDrivenSceneTypes.h"

#include <gtest/gtest.h>

namespace gte {
namespace {

fx::ClusterCamera CameraAt(float x)
{
    fx::ClusterCamera camera;
    camera.eyeOrDirection = Vec3{ x, 0.0f, 0.0f };
    return camera;
}

TEST(GpuDrivenSceneTypesTest, SanitizeResetsOutOfRangeDebugView)
{
    GpuDrivenSceneSettings s;
    s.debugView = static_cast<GpuDrivenSceneDebugView>(200);
    EXPECT_EQ(SanitizeGpuDrivenSceneSettings(s).debugView, GpuDrivenSceneDebugView::Off);

    s.debugView = GpuDrivenSceneDebugView::ClusterId;
    EXPECT_EQ(SanitizeGpuDrivenSceneSettings(s).debugView, GpuDrivenSceneDebugView::ClusterId);
}

TEST(GpuDrivenSceneTypesTest, DefaultsKeepConeOffAndFrustumOn)
{
    const GpuDrivenSceneSettings s;
    EXPECT_TRUE(s.enabled);
    EXPECT_TRUE(s.frustum);
    EXPECT_FALSE(s.cone);
    EXPECT_FALSE(s.freezeCulling);
    EXPECT_FALSE(s.collectStats);

    GpuDrivenSceneSettings edited;
    EXPECT_EQ(ApplyGpuDrivenSceneSetting(edited, "collectStats", "true"), GpuDrivenSceneSetResult::Applied);
    EXPECT_TRUE(edited.collectStats);
}

TEST(GpuDrivenSceneTypesTest, FreezeStoresFirstCameraAndKeepsIt)
{
    FrozenCullCamera state;
    const Extent2D extent{ 640, 360 };

    const fx::ClusterCamera first = CameraAt(1.0f);
    const fx::ClusterCamera& used1 = ResolveCullCamera(state, true, true, first, extent);
    EXPECT_TRUE(state.frozen);
    EXPECT_FLOAT_EQ(used1.eyeOrDirection.x, 1.0f);

    const fx::ClusterCamera moved = CameraAt(9.0f);
    const fx::ClusterCamera& used2 = ResolveCullCamera(state, true, true, moved, extent);
    EXPECT_FLOAT_EQ(used2.eyeOrDirection.x, 1.0f);
}

TEST(GpuDrivenSceneTypesTest, ResizeDisableAndUnfreezeClearTheFrozenCamera)
{
    const Extent2D extent{ 640, 360 };
    {
        FrozenCullCamera state;
        ResolveCullCamera(state, true, true, CameraAt(1.0f), extent);
        const fx::ClusterCamera live = CameraAt(5.0f);
        const fx::ClusterCamera& used = ResolveCullCamera(state, true, true, live, Extent2D{ 800, 600 });
        EXPECT_FLOAT_EQ(used.eyeOrDirection.x, 5.0f); // A resize re-freezes on the live camera.
        EXPECT_EQ(state.extent.width, 800u);
    }
    {
        FrozenCullCamera state;
        ResolveCullCamera(state, true, true, CameraAt(1.0f), extent);
        const fx::ClusterCamera live = CameraAt(5.0f);
        EXPECT_FLOAT_EQ(ResolveCullCamera(state, false, true, live, extent).eyeOrDirection.x, 5.0f);
        EXPECT_FALSE(state.frozen);
    }
    {
        FrozenCullCamera state;
        ResolveCullCamera(state, true, true, CameraAt(1.0f), extent);
        const fx::ClusterCamera live = CameraAt(5.0f);
        EXPECT_FLOAT_EQ(ResolveCullCamera(state, true, false, live, extent).eyeOrDirection.x, 5.0f);
        EXPECT_FALSE(state.frozen);
    }
}

TEST(GpuDrivenSceneTypesTest, InvalidLiveCameraIsNeverFrozen)
{
    FrozenCullCamera state;
    fx::ClusterCamera bad = CameraAt(1.0f);
    bad.valid = false;
    ResolveCullCamera(state, true, true, bad, Extent2D{ 64, 64 });
    EXPECT_FALSE(state.frozen);
}

TEST(GpuDrivenSceneTypesTest, SettingKeysApplyOrReject)
{
    GpuDrivenSceneSettings s;
    EXPECT_EQ(ApplyGpuDrivenSceneSetting(s, "cone", "true"), GpuDrivenSceneSetResult::Applied);
    EXPECT_TRUE(s.cone);
    EXPECT_EQ(ApplyGpuDrivenSceneSetting(s, "frustum", "0"), GpuDrivenSceneSetResult::Applied);
    EXPECT_FALSE(s.frustum);
    EXPECT_EQ(ApplyGpuDrivenSceneSetting(s, "debugView", "ClusterId"), GpuDrivenSceneSetResult::Applied);
    EXPECT_EQ(s.debugView, GpuDrivenSceneDebugView::ClusterId);

    const GpuDrivenSceneSettings before = s;
    EXPECT_EQ(ApplyGpuDrivenSceneSetting(s, "cone", "maybe"), GpuDrivenSceneSetResult::BadValue);
    EXPECT_EQ(ApplyGpuDrivenSceneSetting(s, "debugView", "Nope"), GpuDrivenSceneSetResult::BadValue);
    EXPECT_EQ(ApplyGpuDrivenSceneSetting(s, "occlusion", "maybe"), GpuDrivenSceneSetResult::BadValue);
    EXPECT_FALSE(s.occlusion);
    EXPECT_EQ(ApplyGpuDrivenSceneSetting(s, "occlusion", "true"), GpuDrivenSceneSetResult::Applied);
    EXPECT_TRUE(s.occlusion);
    s.occlusion = false;
    EXPECT_EQ(ApplyGpuDrivenSceneSetting(s, "bogus", "true"), GpuDrivenSceneSetResult::UnknownKey);
    EXPECT_EQ(s.cone, before.cone);
    EXPECT_EQ(s.debugView, before.debugView);
}

TEST(GpuDrivenSceneTypesTest, OutcomeTextNamesInertReasonAndRefusal)
{
    fx::ClusterStats stats;
    EXPECT_TRUE(DescribeClusterOutcome(0, stats).empty());
    EXPECT_FALSE(DescribeClusterOutcome(3, stats).empty());

    stats.declared = true;
    EXPECT_TRUE(DescribeClusterOutcome(3, stats).empty());

    stats.inertReason = fx::ClusterInertReason::CaptureArmed;
    EXPECT_EQ(DescribeClusterOutcome(3, stats), "capture armed");
}

} // namespace
} // namespace gte
