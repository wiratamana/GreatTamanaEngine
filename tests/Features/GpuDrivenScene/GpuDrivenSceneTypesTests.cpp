// Settings, per-view cull plan, frozen camera, count history and debug keys of the GpuDrivenScene feature.

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

TEST(GpuDrivenSceneTypesTest, SanitizeResetsOutOfRangeEnums)
{
    GpuDrivenSceneSettings s;
    s.debugView = static_cast<GpuDrivenSceneDebugView>(200);
    s.sceneCull = static_cast<fx::ViewCullChoice>(200);
    s.sceneSource = static_cast<fx::ViewCullSource>(200);
    s.ghost = static_cast<fx::GhostStyle>(200);
    const GpuDrivenSceneSettings clean = SanitizeGpuDrivenSceneSettings(s);
    EXPECT_EQ(clean.debugView, GpuDrivenSceneDebugView::Off);
    EXPECT_EQ(clean.sceneCull, fx::ViewCullChoice::FollowSettings);
    EXPECT_EQ(clean.sceneSource, fx::ViewCullSource::SceneCamera);
    EXPECT_EQ(clean.ghost, fx::GhostStyle::NotDrawn);

    s = GpuDrivenSceneSettings{};
    s.debugView = GpuDrivenSceneDebugView::ClusterId;
    s.sceneCull = fx::ViewCullChoice::Frustum;
    EXPECT_EQ(SanitizeGpuDrivenSceneSettings(s).debugView, GpuDrivenSceneDebugView::ClusterId);
    EXPECT_EQ(SanitizeGpuDrivenSceneSettings(s).sceneCull, fx::ViewCullChoice::Frustum);
}

TEST(GpuDrivenSceneTypesTest, DefaultsKeepConeOffAndFrustumOn)
{
    const GpuDrivenSceneSettings s;
    EXPECT_TRUE(s.enabled);
    EXPECT_TRUE(s.frustum);
    EXPECT_FALSE(s.cone);
    EXPECT_FALSE(s.freezeCulling);
    EXPECT_FALSE(s.collectStats);
    EXPECT_EQ(s.sceneCull, fx::ViewCullChoice::FollowSettings);
    EXPECT_EQ(s.sceneSource, fx::ViewCullSource::SceneCamera);
    EXPECT_EQ(s.freezeSerial, 0u);

    GpuDrivenSceneSettings edited;
    EXPECT_EQ(ApplyGpuDrivenSceneSetting(edited, "collectStats", "true"), GpuDrivenSceneSetResult::Applied);
    EXPECT_TRUE(edited.collectStats);
}

TEST(GpuDrivenSceneTypesTest, FreezeStoresFirstCameraAndKeepsIt)
{
    FrozenCullCamera state;
    const fx::ClusterCamera first = CameraAt(1.0f);
    const fx::ClusterCamera& used1 = ResolveCullCamera(state, true, 0, first, 10);
    EXPECT_TRUE(state.frozen);
    EXPECT_EQ(state.frame, 10u);
    EXPECT_FLOAT_EQ(used1.eyeOrDirection.x, 1.0f);

    const fx::ClusterCamera moved = CameraAt(9.0f);
    const fx::ClusterCamera& used2 = ResolveCullCamera(state, true, 0, moved, 11);
    EXPECT_FLOAT_EQ(used2.eyeOrDirection.x, 1.0f);
    EXPECT_EQ(state.frame, 10u);
}

TEST(GpuDrivenSceneTypesTest, NewSerialRecapturesAndNotRequestedUnfreezes)
{
    FrozenCullCamera state;
    ResolveCullCamera(state, true, 1, CameraAt(1.0f), 1);

    const fx::ClusterCamera live = CameraAt(5.0f);
    EXPECT_FLOAT_EQ(ResolveCullCamera(state, true, 2, live, 2).eyeOrDirection.x, 5.0f);
    EXPECT_EQ(state.frame, 2u);

    EXPECT_FLOAT_EQ(ResolveCullCamera(state, false, 2, CameraAt(7.0f), 3).eyeOrDirection.x, 7.0f);
    EXPECT_FALSE(state.frozen);
}

TEST(GpuDrivenSceneTypesTest, InvalidLiveCameraIsNeverFrozen)
{
    FrozenCullCamera state;
    fx::ClusterCamera bad = CameraAt(1.0f);
    bad.valid = false;
    ResolveCullCamera(state, true, 0, bad, 1);
    EXPECT_FALSE(state.frozen);

    ResolveCullCamera(state, true, 0, CameraAt(2.0f), 2);
    ASSERT_TRUE(state.frozen);
    ResolveCullCamera(state, true, 1, bad, 3); // A new serial with an invalid camera keeps the old capture.
    EXPECT_FLOAT_EQ(state.camera.eyeOrDirection.x, 2.0f);
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
    EXPECT_EQ(ApplyGpuDrivenSceneSetting(s, "sceneCull", "FrustumHiz"), GpuDrivenSceneSetResult::Applied);
    EXPECT_EQ(s.sceneCull, fx::ViewCullChoice::FrustumHiz);
    EXPECT_EQ(ApplyGpuDrivenSceneSetting(s, "sceneSource", "Frozen"), GpuDrivenSceneSetResult::Applied);
    EXPECT_EQ(s.sceneSource, fx::ViewCullSource::Frozen);
    EXPECT_EQ(ApplyGpuDrivenSceneSetting(s, "sceneCone", "true"), GpuDrivenSceneSetResult::Applied);
    EXPECT_TRUE(s.sceneCone);
    EXPECT_EQ(ApplyGpuDrivenSceneSetting(s, "ghost", "NotDrawn"), GpuDrivenSceneSetResult::Applied);

    const GpuDrivenSceneSettings before = s;
    EXPECT_EQ(ApplyGpuDrivenSceneSetting(s, "cone", "maybe"), GpuDrivenSceneSetResult::BadValue);
    EXPECT_EQ(ApplyGpuDrivenSceneSetting(s, "debugView", "Nope"), GpuDrivenSceneSetResult::BadValue);
    EXPECT_EQ(ApplyGpuDrivenSceneSetting(s, "sceneCull", "frustum"), GpuDrivenSceneSetResult::BadValue);
    EXPECT_EQ(ApplyGpuDrivenSceneSetting(s, "sceneSource", ""), GpuDrivenSceneSetResult::BadValue);
    EXPECT_EQ(ApplyGpuDrivenSceneSetting(s, "ghost", "Red"), GpuDrivenSceneSetResult::BadValue);
    EXPECT_EQ(ApplyGpuDrivenSceneSetting(s, "bogus", "true"), GpuDrivenSceneSetResult::UnknownKey);
    EXPECT_EQ(ApplyGpuDrivenSceneSetting(s, "freezeSerial", "3"), GpuDrivenSceneSetResult::UnknownKey);
    EXPECT_EQ(s.cone, before.cone);
    EXPECT_EQ(s.debugView, before.debugView);
    EXPECT_EQ(s.sceneCull, before.sceneCull);
    EXPECT_EQ(s.sceneSource, before.sceneSource);
}

TEST(GpuDrivenSceneTypesTest, EveryKeyHasAnErrorText)
{
    for (const GpuDrivenSceneSettingKey& key : kGpuDrivenSceneSettingKeys) {
        EXPECT_FALSE(key.name.empty());
        EXPECT_FALSE(key.allowed.empty()) << key.name;
        GpuDrivenSceneSettings s;
        EXPECT_EQ(ApplyGpuDrivenSceneSetting(s, key.name, "###"), GpuDrivenSceneSetResult::BadValue) << key.name;
    }
}

TEST(GpuDrivenSceneTypesTest, GameViewIgnoresSceneOnlyFields)
{
    GpuDrivenSceneSettings s;
    s.frustum = true;
    s.occlusion = false;
    s.freezeCulling = false;
    const ViewCullTests before = ResolveViewCullTests(s, false);

    s.sceneCull = fx::ViewCullChoice::FrustumHiz;
    s.sceneCone = true;
    s.sceneSource = fx::ViewCullSource::Frozen;
    s.freezeSerial = 9;
    const ViewCullTests after = ResolveViewCullTests(s, false);
    EXPECT_EQ(before.frustum, after.frustum);
    EXPECT_EQ(before.cone, after.cone);
    EXPECT_EQ(before.occlusion, after.occlusion);
    EXPECT_FALSE(ViewFreezeRequested(s, false));
    EXPECT_FALSE(ViewWantsHiz(s, false));
}

TEST(GpuDrivenSceneTypesTest, SceneChoicesMapToTests)
{
    GpuDrivenSceneSettings s;
    s.frustum = false;
    s.occlusion = true; // The settings say Custom; FollowSettings must show exactly that.

    EXPECT_EQ(CullModeOf(ResolveViewCullTests(s, true)), fx::ViewCullMode::Custom);
    for (const fx::ViewCullChoice choice :
        { fx::ViewCullChoice::Off, fx::ViewCullChoice::Frustum, fx::ViewCullChoice::FrustumHiz }) {
        ApplyViewCullChoice(s, choice, true);
        const ViewCullTests t = ResolveViewCullTests(s, true);
        const fx::ViewCullMode mode = CullModeOf(t);
        EXPECT_EQ(static_cast<int>(mode), static_cast<int>(choice) - 1);
        EXPECT_EQ(t.cone, choice != fx::ViewCullChoice::Off); // Off is the no-culling baseline.
    }
    ApplyViewCullChoice(s, fx::ViewCullChoice::FollowSettings, false);
    EXPECT_EQ(CullModeOf(ResolveViewCullTests(s, true)), fx::ViewCullMode::Custom);
}

TEST(GpuDrivenSceneTypesTest, FrozenViewNeverWantsHiz)
{
    GpuDrivenSceneSettings s;
    s.sceneCull = fx::ViewCullChoice::FrustumHiz;
    EXPECT_TRUE(ViewWantsHiz(s, true));
    EXPECT_FALSE(ViewWantsHiz(s, false)); // The Game view still follows its own settings.

    s.sceneSource = fx::ViewCullSource::Frozen;
    EXPECT_TRUE(ViewFreezeRequested(s, true));
    EXPECT_FALSE(ViewWantsHiz(s, true));

    s.sceneSource = fx::ViewCullSource::SceneCamera;
    s.enabled = false;
    EXPECT_FALSE(ViewWantsHiz(s, true));
}

TEST(GpuDrivenSceneTypesTest, OcclusionStateNeverAppliedForAnotherCamera)
{
    using fx::OcclusionState;
    EXPECT_EQ(OcclusionStateOf(false, true, false), OcclusionState::NotRequested);
    EXPECT_EQ(OcclusionStateOf(true, false, true), OcclusionState::CameraNotLive);
    EXPECT_EQ(OcclusionStateOf(true, true, false), OcclusionState::Unusable);
    EXPECT_EQ(OcclusionStateOf(true, true, true), OcclusionState::Applied);
}

TEST(GpuDrivenSceneTypesTest, RequestAppliesOnceAndFreezeBumpsTheSerial)
{
    GpuDrivenSceneSettings s;
    fx::ViewCullRequest request;
    request.pending = true;
    request.choice = fx::ViewCullChoice::Frustum;
    request.cone = true;
    request.source = fx::ViewCullSource::SceneCamera;

    s = ApplyViewCullRequest(s, request);
    EXPECT_EQ(s.sceneCull, fx::ViewCullChoice::Frustum);
    EXPECT_TRUE(s.sceneCone);
    EXPECT_EQ(s.freezeSerial, 0u);

    request.freezeHere = true;
    s = ApplyViewCullRequest(s, request);
    EXPECT_EQ(s.sceneSource, fx::ViewCullSource::Frozen);
    EXPECT_EQ(s.freezeSerial, 1u);
    s = ApplyViewCullRequest(s, request);
    EXPECT_EQ(s.freezeSerial, 2u); // A second press captures again.

    request.resetAll = true;
    s = ApplyViewCullRequest(s, request);
    EXPECT_EQ(s.sceneCull, fx::ViewCullChoice::FollowSettings);
    EXPECT_EQ(s.sceneSource, fx::ViewCullSource::SceneCamera);
    EXPECT_FALSE(s.sceneCone);
}

TEST(GpuDrivenSceneTypesTest, RequestNeverTouchesGameViewSettings)
{
    GpuDrivenSceneSettings s;
    s.frustum = false;
    s.occlusion = true;
    s.freezeCulling = true;
    fx::ViewCullRequest request;
    request.pending = true;
    request.choice = fx::ViewCullChoice::Off;
    request.freezeHere = true;

    const GpuDrivenSceneSettings next = ApplyViewCullRequest(s, request);
    EXPECT_EQ(next.frustum, s.frustum);
    EXPECT_EQ(next.occlusion, s.occlusion);
    EXPECT_EQ(next.freezeCulling, s.freezeCulling);
    EXPECT_EQ(next.enabled, s.enabled);
}

TEST(GpuDrivenSceneTypesTest, TestedHistoryPairsAFrameWithItsOwnValue)
{
    TestedHistory history;
    EXPECT_FALSE(history.At(0).has_value());
    for (std::uint64_t frame = 10; frame < 14; ++frame) {
        history.Record(frame, frame * 100);
    }
    for (std::uint64_t frame = 10; frame < 14; ++frame) {
        ASSERT_TRUE(history.At(frame).has_value());
        EXPECT_EQ(*history.At(frame), frame * 100);
    }
    history.Record(14, 1400);
    EXPECT_FALSE(history.At(10).has_value()); // The oldest entry was overwritten.
    EXPECT_FALSE(history.At(18).has_value()); // Same slot, other frame.
    EXPECT_EQ(*history.At(14), 1400u);
}

TEST(GpuDrivenSceneTypesTest, StatsHoldRunsThreeFramesAfterTheLastRequest)
{
    StatsHold hold;
    EXPECT_FALSE(hold.Step(false));
    EXPECT_TRUE(hold.Step(true));
    EXPECT_TRUE(hold.Step(false));
    EXPECT_TRUE(hold.Step(false));
    EXPECT_TRUE(hold.Step(false));
    EXPECT_FALSE(hold.Step(false));
    EXPECT_TRUE(hold.Step(true)); // Asking again restarts the hold.
}

TEST(GpuDrivenSceneTypesTest, ReportIsAPureFunctionOfSettingsAndSnapshot)
{
    GpuDrivenSceneSettings s;
    s.sceneCull = fx::ViewCullChoice::FrustumHiz;
    s.sceneCone = true;

    GpuDrivenSceneViewSnapshot snap;
    snap.frameIndex = 42;
    snap.rendered = true;
    snap.claimedEntities = 5;
    snap.testedClusters = 100;
    snap.testedAtVisibleFrame = 90;
    snap.visibleClusters = 30;
    snap.visibleValid = true;
    snap.occlusion = fx::OcclusionState::Applied;
    snap.frozen = true;
    snap.frozenFrame = 40;

    const fx::ViewCullReport r = MakeViewCullReport(s, snap);
    EXPECT_TRUE(r.valid);
    EXPECT_TRUE(r.rendered);
    EXPECT_EQ(r.mode, fx::ViewCullMode::FrustumHiz);
    EXPECT_TRUE(r.cone);
    EXPECT_EQ(r.frame, 42u);
    EXPECT_EQ(r.tested, 100u);
    EXPECT_EQ(r.testedAtVisibleFrame, 90u);
    EXPECT_EQ(r.visible, 30u);
    EXPECT_EQ(r.claimedInstances, 5u);
    EXPECT_EQ(r.occlusion, fx::OcclusionState::Applied);
    EXPECT_EQ(r.frozenFrame, 40u);

    snap.hasPixels = false;
    EXPECT_FALSE(MakeViewCullReport(s, snap).rendered);
}

TEST(GpuDrivenSceneTypesTest, OutcomeTextNamesInertReasonAndRefusal)
{
    fx::ClusterStats stats;
    EXPECT_TRUE(DescribeClusterOutcome(0, stats).empty());
    EXPECT_FALSE(DescribeClusterOutcome(3, stats).empty());
    EXPECT_TRUE(ClusterOutcomeRefused(3, stats));

    stats.declared = true;
    EXPECT_TRUE(DescribeClusterOutcome(3, stats).empty());
    EXPECT_FALSE(ClusterOutcomeRefused(3, stats));

    stats.inertReason = fx::ClusterInertReason::CaptureArmed;
    EXPECT_EQ(DescribeClusterOutcome(3, stats), "capture armed");
}

} // namespace
} // namespace gte
