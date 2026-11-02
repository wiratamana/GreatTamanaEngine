#include "Application/PhysicsDebugCommandDispatch.h"

#include "ECS/Components/DynamicChainRig.h"
#include "ECS/Components/Name.h"
#include "ECS/Components/ResolvedAnimationPose.h"
#include "ECS/Components/SkeletalAnimator.h"
#include "Game/Animation/SkeletalRigCache.h"
#include "Game/Game.h"
#include "Game/Physics/PhysicsDebugSettings.h"

#include <gtest/gtest.h>

namespace gte {
namespace {

constexpr const char* kModelPath = "DispatchTestModel.gta";
constexpr const char* kEntityName = "Dancer";

// Anchor bone 1 (static body) with a two-joint chain along +X.
SkinnedMeshData MakeChainModel()
{
    SkinnedMeshData data;
    for (int i = 0; i < 4; ++i) {
        Bone bone;
        bone.name = i == 1 ? "Anchor" : "Joint_" + std::to_string(i);
        bone.position = Vec3(static_cast<float>(i), 0.0f, 0.0f);
        bone.parentBoneIndex = i - 1;
        data.skeleton.bones.push_back(bone);
    }

    PhysicsData physics;
    const std::int32_t boneOfBody[3] = { 1, 2, 3 };
    for (int i = 0; i < 3; ++i) {
        RigidBody body;
        body.boneIndex = boneOfBody[i];
        body.motionType = i == 0 ? RigidBodyMotionType::Static : RigidBodyMotionType::Dynamic;
        physics.rigidBodies.push_back(body);
    }
    for (int i = 0; i < 2; ++i) {
        Joint joint;
        joint.rigidBodyAIndex = i;
        joint.rigidBodyBIndex = i + 1;
        physics.joints.push_back(joint);
    }
    data.physics = std::move(physics);
    return data;
}

struct Scene {
    Game game;
    Entity entity = kInvalidEntity;

    Scene()
    {
        game.GetPhysicsSystem().RegisterDynamicChains(kModelPath, MakeChainModel());
        Registry& registry = game.GetRegistry();
        entity = registry.CreateEntity();
        registry.AddComponent<Name>(entity).value = kEntityName;
        game.GetPhysicsSystem().AttachDynamicChainRigIfNeeded(registry, entity, kModelPath);
        ResetPose();

        SkeletalAnimator& animator = registry.AddComponent<SkeletalAnimator>(entity);
        animator.meshGtaPath = kModelPath;
        animator.frame = 12.0f;
        animator.speed = 0.5f;
        animator.playing = false;
        animator.loop = false;
    }

    void ResetPose()
    {
        Registry& registry = game.GetRegistry();
        ResolvedAnimationPose* pose = registry.TryGetComponent<ResolvedAnimationPose>(entity);
        if (pose == nullptr) {
            pose = &registry.AddComponent<ResolvedAnimationPose>(entity);
        }
        pose->pose.assign(4, BoneLocalOffset{});
    }

    PhysicsDebugCommandResult Run(PhysicsDebugCommandKind kind, const PhysicsDebugCommandRequest& request)
    {
        PhysicsDebugCommandRequest copy = request;
        copy.kind = kind;
        return ExecutePhysicsDebugCommand(game, copy);
    }

    PhysicsDebugCommandRequest ForEntity() const
    {
        PhysicsDebugCommandRequest request;
        request.entityName = kEntityName;
        return request;
    }

    PhysicsDebugCommandRequest RunRequest(std::uint32_t frames = 30) const
    {
        PhysicsDebugCommandRequest request = ForEntity();
        request.run.entityName = kEntityName;
        request.run.frames = frames;
        request.run.renderHz = 60.0f;
        request.run.startFrame = 3.0f;
        return request;
    }
};

void ExpectSameStats(const MetricStats& a, const MetricStats& b)
{
    EXPECT_EQ(a.count, b.count);
    EXPECT_DOUBLE_EQ(a.mean, b.mean);
    EXPECT_DOUBLE_EQ(a.p50, b.p50);
    EXPECT_DOUBLE_EQ(a.p95, b.p95);
    EXPECT_DOUBLE_EQ(a.p99, b.p99);
    EXPECT_DOUBLE_EQ(a.max, b.max);
}

void ExpectSameGroup(const GroupMetricsReport& a, const GroupMetricsReport& b)
{
    EXPECT_EQ(a.key, b.key);
    EXPECT_EQ(a.jointCount, b.jointCount);
    ExpectSameStats(a.distFromAnimated, b.distFromAnimated);
    ExpectSameStats(a.segDirErrDeg, b.segDirErrDeg);
    ExpectSameStats(a.bendDeltaDeg, b.bendDeltaDeg);
    ExpectSameStats(a.limitViolationDeg, b.limitViolationDeg);
    ExpectSameStats(a.absStretch, b.absStretch);
    ExpectSameStats(a.writebackErr, b.writebackErr);
    ExpectSameStats(a.accelLocal, b.accelLocal);
}

TEST(PhysicsDebugCommandDispatchTests, UnknownEntityIs404)
{
    Scene scene;
    PhysicsDebugCommandRequest request;
    request.entityName = "Nobody";
    const PhysicsDebugCommandResult result = scene.Run(PhysicsDebugCommandKind::State, request);
    EXPECT_FALSE(result.success);
    EXPECT_EQ(result.httpStatus, 404);
    EXPECT_NE(result.errorMessage.find("unknown entity 'Nobody'"), std::string::npos);
}

TEST(PhysicsDebugCommandDispatchTests, EntityWithoutPhysicsChainsIs409)
{
    Scene scene;
    const Entity plain = scene.game.GetRegistry().CreateEntity();
    scene.game.GetRegistry().AddComponent<Name>(plain).value = "Plain";
    PhysicsDebugCommandRequest request;
    request.entityName = "Plain";
    const PhysicsDebugCommandResult result = scene.Run(PhysicsDebugCommandKind::Joints, request);
    EXPECT_EQ(result.httpStatus, 409);
    EXPECT_NE(result.errorMessage.find("has no physics chains"), std::string::npos);
}

TEST(PhysicsDebugCommandDispatchTests, ListAndStateDescribeTheRig)
{
    Scene scene;
    const PhysicsDebugCommandResult list = scene.Run(PhysicsDebugCommandKind::List, {});
    ASSERT_TRUE(list.success);
    ASSERT_EQ(list.list.entries.size(), 1u);
    EXPECT_EQ(list.list.entries[0].name, kEntityName);
    EXPECT_EQ(list.list.entries[0].chainCount, 1u);
    EXPECT_EQ(list.list.entries[0].jointCount, 2u);
    EXPECT_TRUE(list.list.entries[0].animator.present);
    EXPECT_FLOAT_EQ(list.list.entries[0].animator.frame, 12.0f);

    const PhysicsDebugCommandResult state = scene.Run(PhysicsDebugCommandKind::State, scene.ForEntity());
    ASSERT_TRUE(state.success);
    ASSERT_EQ(state.state.chains.size(), 1u);
    EXPECT_EQ(state.state.chains[0].joints, 2u);
    EXPECT_EQ(state.state.chains[0].rootChildren, 1u);
    EXPECT_EQ(state.state.chains[0].anchorBoneName, "Anchor");
    EXPECT_FALSE(state.state.settings.empty());
}

TEST(PhysicsDebugCommandDispatchTests, SetRigKeysNeedAnEntity)
{
    Scene scene;
    PhysicsDebugCommandRequest request;
    request.pairs = { { "rig_enabled", "false" } };
    const PhysicsDebugCommandResult result = scene.Run(PhysicsDebugCommandKind::Set, request);
    EXPECT_EQ(result.httpStatus, 400);
    EXPECT_EQ(result.errorKey, "rig_enabled");
}

TEST(PhysicsDebugCommandDispatchTests, SetAppliesGlobalAndRigKeys)
{
    Scene scene;
    PhysicsDebugCommandRequest request = scene.ForEntity();
    request.pairs = { { "gravity_y", "-5" }, { "rig_frozen", "true" } };
    const PhysicsDebugCommandResult result = scene.Run(PhysicsDebugCommandKind::Set, request);
    ASSERT_TRUE(result.success);
    EXPECT_FLOAT_EQ(scene.game.GetPhysicsSystem().GetGlobalPhysicsSettings().gravity.y, -5.0f);
    EXPECT_TRUE(scene.game.GetRegistry().TryGetComponent<DynamicChainRig>(scene.entity)->frozen);
    EXPECT_FALSE(result.settings.empty());
}

TEST(PhysicsDebugCommandDispatchTests, SetWithOneBadPairChangesNothing)
{
    Scene scene;
    const PhysicsSettingPairs before = DescribeSettings(scene.game.GetPhysicsSystem().GetGlobalPhysicsSettings());
    PhysicsDebugCommandRequest request = scene.ForEntity();
    request.pairs = { { "damping_multiplier", "2" }, { "gravity_y", "12abc" }, { "rig_frozen", "true" } };
    const PhysicsDebugCommandResult result = scene.Run(PhysicsDebugCommandKind::Set, request);
    EXPECT_EQ(result.httpStatus, 400);
    EXPECT_EQ(result.errorKey, "gravity_y");
    EXPECT_EQ(DescribeSettings(scene.game.GetPhysicsSystem().GetGlobalPhysicsSettings()), before);
    EXPECT_FALSE(scene.game.GetRegistry().TryGetComponent<DynamicChainRig>(scene.entity)->frozen);
}

TEST(PhysicsDebugCommandDispatchTests, PresetBadNameIs400AndLegacyApplies)
{
    Scene scene;
    PhysicsDebugCommandRequest request;
    request.presetName = "nope";
    EXPECT_EQ(scene.Run(PhysicsDebugCommandKind::Preset, request).httpStatus, 400);

    request.presetName = "legacy";
    const PhysicsDebugCommandResult legacy = scene.Run(PhysicsDebugCommandKind::Preset, request);
    ASSERT_TRUE(legacy.success);
    EXPECT_FLOAT_EQ(scene.game.GetPhysicsSystem().GetGlobalPhysicsSettings().gravity.y, -9.8f);
}

TEST(PhysicsDebugCommandDispatchTests, CaptureReportsTheWindowAndRejectsOutOfRange)
{
    Scene scene;
    PhysicsDebugCommandRequest request;
    request.captureEnabled = true;
    request.windowFrames = 5;
    const PhysicsDebugCommandResult on = scene.Run(PhysicsDebugCommandKind::Capture, request);
    ASSERT_TRUE(on.success);
    EXPECT_TRUE(on.capture.enabled);
    EXPECT_EQ(on.capture.windowFrames, 5u);

    request.windowFrames = 601;
    EXPECT_EQ(scene.Run(PhysicsDebugCommandKind::Capture, request).httpStatus, 400);

    request.captureEnabled = false;
    request.windowFrames = 5;
    EXPECT_FALSE(scene.Run(PhysicsDebugCommandKind::Capture, request).capture.enabled);
}

TEST(PhysicsDebugCommandDispatchTests, MetricsNeedCapture)
{
    Scene scene;
    const PhysicsDebugCommandResult result = scene.Run(PhysicsDebugCommandKind::Metrics, scene.ForEntity());
    EXPECT_EQ(result.httpStatus, 409);
}

TEST(PhysicsDebugCommandDispatchTests, LiveCaptureFeedsMetrics)
{
    Scene scene;
    PhysicsDebugCommandRequest capture;
    capture.captureEnabled = true;
    capture.windowFrames = 20;
    ASSERT_TRUE(scene.Run(PhysicsDebugCommandKind::Capture, capture).success);
    for (int i = 0; i < 8; ++i) {
        scene.game.StepSimulationHeadless(1.0 / 60.0);
    }
    const PhysicsDebugCommandResult metrics = scene.Run(PhysicsDebugCommandKind::Metrics, scene.ForEntity());
    ASSERT_TRUE(metrics.success) << metrics.errorMessage;
    EXPECT_EQ(metrics.metrics.frames, 8u);
    EXPECT_NEAR(metrics.metrics.renderHz, 60.0f, 0.5f);
}

TEST(PhysicsDebugCommandDispatchTests, RunValidatesItsInput)
{
    Scene scene;
    PhysicsDebugCommandRequest request = scene.RunRequest(0);
    PhysicsDebugCommandResult result = scene.Run(PhysicsDebugCommandKind::Run, request);
    EXPECT_EQ(result.httpStatus, 400);
    EXPECT_EQ(result.errorKey, "frames");

    request = scene.RunRequest(10);
    request.run.warmupFrames = 10;
    result = scene.Run(PhysicsDebugCommandKind::Run, request);
    EXPECT_EQ(result.errorKey, "warmup_frames");

    request = scene.RunRequest(10);
    request.run.renderHz = 5.0f;
    EXPECT_EQ(scene.Run(PhysicsDebugCommandKind::Run, request).errorKey, "render_hz");
}

TEST(PhysicsDebugCommandDispatchTests, RunWithAnInvalidTemporaryPairChangesNothing)
{
    Scene scene;
    const PhysicsSettingPairs before = DescribeSettings(scene.game.GetPhysicsSystem().GetGlobalPhysicsSettings());
    PhysicsDebugCommandRequest request = scene.RunRequest();
    request.run.settingsPairs = { { "gravity_y", "-50" }, { "no_such_key", "1" } };
    const PhysicsDebugCommandResult result = scene.Run(PhysicsDebugCommandKind::Run, request);
    EXPECT_EQ(result.httpStatus, 400);
    EXPECT_EQ(result.errorKey, "no_such_key");
    EXPECT_EQ(DescribeSettings(scene.game.GetPhysicsSystem().GetGlobalPhysicsSettings()), before);
    EXPECT_FLOAT_EQ(scene.game.GetRegistry().TryGetComponent<SkeletalAnimator>(scene.entity)->frame, 12.0f);
}

TEST(PhysicsDebugCommandDispatchTests, RunRestoresSettingsAnimatorAndRigState)
{
    Scene scene;
    Registry& registry = scene.game.GetRegistry();
    scene.game.StepSimulationHeadless(1.0 / 60.0);

    const DynamicChainRig rigBefore = *registry.TryGetComponent<DynamicChainRig>(scene.entity);
    const SkeletalAnimator animatorBefore = *registry.TryGetComponent<SkeletalAnimator>(scene.entity);
    const PhysicsSettingPairs settingsBefore = DescribeSettings(scene.game.GetPhysicsSystem().GetGlobalPhysicsSettings());
    ASSERT_TRUE(rigBefore.chainStates[0].initialized);

    PhysicsDebugCommandRequest request = scene.RunRequest();
    request.run.settingsPairs = { { "damping_multiplier", "2" } };
    const PhysicsDebugCommandResult result = scene.Run(PhysicsDebugCommandKind::Run, request);
    ASSERT_TRUE(result.success) << result.errorMessage;
    EXPECT_EQ(result.run.metrics.frames, 30u);

    const DynamicChainRig& rigAfter = *registry.TryGetComponent<DynamicChainRig>(scene.entity);
    const SkeletalAnimator& animatorAfter = *registry.TryGetComponent<SkeletalAnimator>(scene.entity);
    EXPECT_EQ(DescribeSettings(scene.game.GetPhysicsSystem().GetGlobalPhysicsSettings()), settingsBefore);
    EXPECT_FLOAT_EQ(animatorAfter.frame, animatorBefore.frame);
    EXPECT_FLOAT_EQ(animatorAfter.speed, animatorBefore.speed);
    EXPECT_EQ(animatorAfter.playing, animatorBefore.playing);
    EXPECT_EQ(animatorAfter.loop, animatorBefore.loop);
    EXPECT_EQ(rigAfter.enabled, rigBefore.enabled);
    EXPECT_EQ(rigAfter.frozen, rigBefore.frozen);
    EXPECT_FLOAT_EQ(rigAfter.accumulatedSeconds, rigBefore.accumulatedSeconds);
    ASSERT_EQ(rigAfter.chainStates[0].particles.size(), rigBefore.chainStates[0].particles.size());
    for (std::size_t i = 0; i < rigAfter.chainStates[0].particles.size(); ++i) {
        const Vec3 after = rigAfter.chainStates[0].particles[i].position;
        const Vec3 before = rigBefore.chainStates[0].particles[i].position;
        EXPECT_FLOAT_EQ(after.x, before.x);
        EXPECT_FLOAT_EQ(after.y, before.y);
        EXPECT_FLOAT_EQ(after.z, before.z);
    }
    EXPECT_FALSE(scene.game.GetPhysicsSystem().GetDebugRecorder().Enabled());
}

TEST(PhysicsDebugCommandDispatchTests, TwoIdenticalRunsGiveIdenticalMetrics)
{
    Scene scene;
    PhysicsDebugCommandRequest request = scene.RunRequest(40);
    request.run.traceBones = { "Joint_3" };
    request.run.snapshotFrames = { 10 };

    scene.ResetPose();
    const PhysicsDebugCommandResult first = scene.Run(PhysicsDebugCommandKind::Run, request);
    scene.ResetPose();
    const PhysicsDebugCommandResult second = scene.Run(PhysicsDebugCommandKind::Run, request);
    ASSERT_TRUE(first.success) << first.errorMessage;
    ASSERT_TRUE(second.success) << second.errorMessage;

    ASSERT_EQ(first.run.metrics.groups.size(), second.run.metrics.groups.size());
    for (std::size_t i = 0; i < first.run.metrics.groups.size(); ++i) {
        ExpectSameGroup(first.run.metrics.groups[i], second.run.metrics.groups[i]);
    }
    ExpectSameGroup(first.run.metrics.all, second.run.metrics.all);
    EXPECT_EQ(first.run.metrics.stepsHistogram, second.run.metrics.stepsHistogram);
    EXPECT_EQ(first.run.settingsUsed, second.run.settingsUsed);

    const auto& histogram = first.run.metrics.stepsHistogram;
    EXPECT_EQ(histogram[0] + histogram[1] + histogram[2] + histogram[3], 40u);
    ASSERT_EQ(first.run.trace.size(), 1u);
    EXPECT_EQ(first.run.trace[0].display.size(), 40u);
    ASSERT_EQ(first.run.snapshots.size(), 1u);
    EXPECT_EQ(first.run.snapshots[0].frame, 10u);
    EXPECT_EQ(first.run.snapshots[0].joints.size(), 2u);
}

TEST(PhysicsDebugCommandDispatchTests, RunRejectsAnUnknownTraceBone)
{
    Scene scene;
    PhysicsDebugCommandRequest request = scene.RunRequest();
    request.run.traceBones = { "NoSuchBone" };
    const PhysicsDebugCommandResult result = scene.Run(PhysicsDebugCommandKind::Run, request);
    EXPECT_EQ(result.httpStatus, 400);
    EXPECT_EQ(result.errorKey, "trace.bones");
}

TEST(PhysicsDebugCommandDispatchTests, JointsReportRowsWithLiveData)
{
    Scene scene;
    PhysicsDebugCommandRequest request = scene.ForEntity();
    const PhysicsDebugCommandResult all = scene.Run(PhysicsDebugCommandKind::Joints, request);
    ASSERT_TRUE(all.success) << all.errorMessage;
    EXPECT_EQ(all.joints.total, 2u);
    ASSERT_EQ(all.joints.rows.size(), 2u);
    EXPECT_EQ(all.joints.rows[0].bone, "Joint_2");
    EXPECT_EQ(all.joints.rows[0].parentBone, "Anchor");
    EXPECT_EQ(all.joints.rows[1].parentBone, "Joint_2");
    EXPECT_TRUE(all.joints.rows[0].live.valid);

    request.groupFilter = "Joint";
    EXPECT_EQ(scene.Run(PhysicsDebugCommandKind::Joints, request).joints.total, 2u);
    request.groupFilter = "Missing";
    EXPECT_EQ(scene.Run(PhysicsDebugCommandKind::Joints, request).joints.total, 0u);

    request.groupFilter.clear();
    request.first = 1;
    request.count = 1;
    const PhysicsDebugCommandResult page = scene.Run(PhysicsDebugCommandKind::Joints, request);
    ASSERT_EQ(page.joints.rows.size(), 1u);
    EXPECT_EQ(page.joints.rows[0].bone, "Joint_3");
    EXPECT_EQ(page.joints.total, 2u);
}

TEST(PhysicsDebugCommandDispatchTests, CollidersListTheStaticBodies)
{
    Scene scene;
    const PhysicsDebugCommandResult result = scene.Run(PhysicsDebugCommandKind::Colliders, scene.ForEntity());
    ASSERT_TRUE(result.success) << result.errorMessage;
    for (const PhysicsColliderRow& row : result.colliders.rows) {
        EXPECT_FALSE(row.shape.empty());
        EXPECT_EQ(row.boneName, "Anchor");
    }
}

TEST(PhysicsDebugCommandDispatchTests, ImageBuildsASnapshotAndRejectsBadViews)
{
    Scene scene;
    PhysicsDebugCommandRequest request = scene.ForEntity();
    request.image.entityName = kEntityName;
    request.image.view = "negz";
    request.image.showLimits = true;
    const PhysicsDebugCommandResult ok = scene.Run(PhysicsDebugCommandKind::Image, request);
    ASSERT_TRUE(ok.success) << ok.errorMessage;
    EXPECT_TRUE(ok.snapshot.valid);
    EXPECT_FALSE(ok.snapshot.segments.empty());
    EXPECT_FALSE(ok.snapshot.points.empty());

    request.image.view = "sideways";
    const PhysicsDebugCommandResult bad = scene.Run(PhysicsDebugCommandKind::Image, request);
    EXPECT_EQ(bad.httpStatus, 400);
    EXPECT_EQ(bad.errorKey, "view");
}

TEST(PhysicsDebugCommandDispatchTests, ImageWithoutAPoseIs409)
{
    Scene scene;
    scene.game.GetRegistry().RemoveComponent<ResolvedAnimationPose>(scene.entity);
    PhysicsDebugCommandRequest request = scene.ForEntity();
    request.image.entityName = kEntityName;
    EXPECT_EQ(scene.Run(PhysicsDebugCommandKind::Image, request).httpStatus, 409);
}

TEST(PhysicsDebugCommandDispatchTests, SeekValidatesAndMovesTheAnimator)
{
    Scene scene;
    PhysicsDebugCommandRequest request = scene.ForEntity();
    request.frame = -1.0f;
    EXPECT_EQ(scene.Run(PhysicsDebugCommandKind::Seek, request).httpStatus, 400);

    request.frame = 40.0f;
    const PhysicsDebugCommandResult result = scene.Run(PhysicsDebugCommandKind::Seek, request);
    ASSERT_TRUE(result.success);
    EXPECT_FLOAT_EQ(result.animator.frame, 40.0f);
}

TEST(PhysicsDebugCommandDispatchTests, PlayRefusesAnEntityWithoutSkinData)
{
    Scene scene;
    PhysicsDebugCommandRequest request = scene.ForEntity();
    request.animationGtaPath = "missing.gta";
    const PhysicsDebugCommandResult result = scene.Run(PhysicsDebugCommandKind::Play, request);
    EXPECT_EQ(result.httpStatus, 400);
    EXPECT_NE(result.errorMessage.find("play refused"), std::string::npos);
}

TEST(PhysicsDebugCommandDispatchTests, ResetDropsTheSimulationState)
{
    Scene scene;
    scene.game.StepSimulationHeadless(1.0 / 60.0);
    ASSERT_TRUE(scene.game.GetRegistry().TryGetComponent<DynamicChainRig>(scene.entity)->chainStates[0].initialized);
    ASSERT_TRUE(scene.Run(PhysicsDebugCommandKind::Reset, scene.ForEntity()).success);
    EXPECT_FALSE(scene.game.GetRegistry().TryGetComponent<DynamicChainRig>(scene.entity)->chainStates[0].initialized);
}

} // namespace
} // namespace gte
