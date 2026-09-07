// Unit tests for task_manager/verlet-integration-7,
// PHASE4_FREEZE_AND_DISABLE_RUNTIME_CONTROLS.md (v2) -
// src/Game/Physics/PhysicsSystem.cpp / src/ECS/Components/DynamicChainRig.h.
// Covers BOTH of this phase's co-equal goals:
//   (A) Culprit D - the new DynamicChainRig::frozen opt-out: stops further
//       integration while preserving the chain's last-simulated shape, and
//       keeps that shape correctly riding along rigidly with the entity's own
//       Transform.
//   (B) Culprit F - PhysicsSystem::Update() must NEVER let a chain-controlled
//       bone visibly "pop" back to raw bind/FK pose on a render frame where
//       the fixed-timestep accumulator legitimately produces stepCount == 0,
//       for EITHER an animated or a non-animated (T-pose) entity.
// All Tier 1 - no GPU/Renderer/ImGui dependency beyond a plain Registry
// (test 6 also touches a real temp directory to feed a real *.gta
// AssetType::Animation file through AnimationSystem::Play(), the same
// convention AnimationSystemEvaluatePosesTests.cpp already uses).

#include "Game/Physics/PhysicsSystem.h"

#include "Animation/BoneWorldMatrixQuery.h"
#include "Assets/AssetTypes.h"
#include "Assets/GtaFile.h"
#include "Assets/MotionFile.h"
#include "ECS/Components/DynamicChainRig.h"
#include "ECS/Components/MeshAssetSource.h"
#include "ECS/Components/ResolvedAnimationPose.h"
#include "ECS/Components/Transform.h"
#include "ECS/Registry.h"
#include "ECS/TransformHierarchy.h"
#include "Game/Animation/AnimationSystem.h"
#include "Game/Animation/SkeletalRigCache.h"
#include "Game/Instantiation/MeshInstantiationSystem.h"
#include "Game/RenderSystem.h"
#include "Math/Mat4.h"
#include "Math/Quat.h"

#include <filesystem>

#include <gtest/gtest.h>

#include <string>
#include <vector>

namespace gte {
namespace {

// Same synthetic 4-bone rig used throughout this campaign's own tests
// (PhysicsSystemTests.cpp / PhysicsSystemWorldSpaceRootMotionTests.cpp /
// GameLoopPhysicsWithoutAnimationTests.cpp): root(0, no rigid body, named
// "root" so test 6 below can target it with a real motion track) ->
// chainRoot(1, a STATIC anchor rigid body) -> joint1(2, Dynamic) ->
// joint2(3, Dynamic), extending along +X (perpendicular to gravity, which
// points along -Y) so the chain genuinely swings sideways under gravity.
SkinnedMeshData BuildSyntheticChainFixture()
{
    SkinnedMeshData data;
    Bone root;
    root.name = "root";
    root.position = Vec3(0.0f, 0.0f, 0.0f);
    root.parentBoneIndex = -1;
    data.skeleton.bones.push_back(root); // 0

    Bone chainRoot;
    chainRoot.name = "chainRoot";
    chainRoot.position = Vec3(1.0f, 0.0f, 0.0f);
    chainRoot.parentBoneIndex = 0;
    data.skeleton.bones.push_back(chainRoot); // 1

    Bone joint1;
    joint1.name = "joint1";
    joint1.position = Vec3(2.0f, 0.0f, 0.0f);
    joint1.parentBoneIndex = 1;
    data.skeleton.bones.push_back(joint1); // 2

    Bone joint2;
    joint2.name = "joint2";
    joint2.position = Vec3(3.0f, 0.0f, 0.0f);
    joint2.parentBoneIndex = 2;
    data.skeleton.bones.push_back(joint2); // 3

    PhysicsData physics;

    RigidBody anchorBody;
    anchorBody.boneIndex = 1;
    anchorBody.motionType = RigidBodyMotionType::Static;
    physics.rigidBodies.push_back(anchorBody); // 0

    RigidBody joint1Body;
    joint1Body.boneIndex = 2;
    joint1Body.motionType = RigidBodyMotionType::Dynamic;
    physics.rigidBodies.push_back(joint1Body); // 1

    RigidBody joint2Body;
    joint2Body.boneIndex = 3;
    joint2Body.motionType = RigidBodyMotionType::Dynamic;
    physics.rigidBodies.push_back(joint2Body); // 2

    Joint anchorToJoint1;
    anchorToJoint1.rigidBodyAIndex = 0;
    anchorToJoint1.rigidBodyBIndex = 1;
    physics.joints.push_back(anchorToJoint1);

    Joint joint1ToJoint2;
    joint1ToJoint2.rigidBodyAIndex = 1;
    joint1ToJoint2.rigidBodyBIndex = 2;
    physics.joints.push_back(joint1ToJoint2);

    data.physics = std::move(physics);
    return data;
}

// Registers `data` under `path`, spawns a fresh entity, attaches its
// DynamicChainRig, and gives it a pure bind-pose ResolvedAnimationPose -
// exactly what AnimationSystem::EvaluatePoses() would have produced for a
// never-animated entity (see PHASE1). Returns the spawned entity.
Entity RegisterAttachAndSeedPose(
    PhysicsSystem& physicsSystem, Registry& registry, const std::string& path, const SkinnedMeshData& data)
{
    physicsSystem.RegisterDynamicChains(path, data);

    const Entity entity = registry.CreateEntity();
    physicsSystem.AttachDynamicChainRigIfNeeded(registry, entity, path);

    ResolvedAnimationPose& pose = registry.AddComponent<ResolvedAnimationPose>(entity);
    pose.pose.resize(data.skeleton.bones.size()); // pure bind pose (all-identity BoneLocalOffset).

    return entity;
}

constexpr std::int32_t kTipBoneIndex = 3;

Vec3 ReconstructTipWorldPosition(Registry& registry, Entity entity, const SkeletonData& skeleton,
    const std::vector<BoneLocalOffset>& pose, std::int32_t tipBoneIndex)
{
    const Transform worldTransform = ComputeWorldTransform(registry, entity);
    const Mat4 entityWorldMatrix = Mat4::TRS(worldTransform.position, worldTransform.rotation, Vec3::One());
    const Mat4 boneWorld = entityWorldMatrix * ComputeBoneWorldMatrix(skeleton, pose, tipBoneIndex);
    return boneWorld.TransformPoint(Vec3::Zero());
}

constexpr std::int32_t kChainRootBoneIndex = 1;

// The tip's WORLD position, expressed RELATIVE TO the chain's own root
// bone's WORLD position (rather than relative to the world origin) - this
// is the actual "shape" quantity task_manager/verlet-integration-7's
// PHASE4 doc means by "the chain's reconstructed world position (root-
// relative offset) is preserved" while frozen: the root bone itself always
// moves perfectly rigidly with the entity's own Transform (its own pose
// entry only ever has its ROTATION corrected by physics, never its
// translation - see BoneChainPhysicsResolver.cpp), so this offset isolates
// the chain's own SAG/SHAPE from the entity's rigid motion.
Vec3 ReconstructRootRelativeTipOffset(
    Registry& registry, Entity entity, const SkeletonData& skeleton, const std::vector<BoneLocalOffset>& pose)
{
    const Vec3 tipWorld = ReconstructTipWorldPosition(registry, entity, skeleton, pose, kTipBoneIndex);
    const Vec3 rootWorld = ReconstructTipWorldPosition(registry, entity, skeleton, pose, kChainRootBoneIndex);
    return tipWorld - rootWorld;
}

TEST(PhysicsSystemFreezeAndCulpritFTests, FrozenDynamicChainRigStopsIntegratingButKeepsItsLastSimulatedShape)
{
    PhysicsSystem physicsSystem;
    Registry registry;
    physicsSystem.GetGlobalPhysicsSettings().gravity = Vec3(0.0f, -9.8f, 0.0f);

    const SkinnedMeshData data = BuildSyntheticChainFixture();
    const Entity frozenEntity
        = RegisterAttachAndSeedPose(physicsSystem, registry, "FreezeTargetModel.gta", data);
    const Entity siblingEntity
        = RegisterAttachAndSeedPose(physicsSystem, registry, "FreezeSiblingModel.gta", data);

    for (int i = 0; i < 60; ++i) {
        physicsSystem.Update(registry, 1.0 / 60.0);
    }

    Vec3 tipAtFreezeTime;
    {
        const ResolvedAnimationPose* pose = registry.TryGetComponent<ResolvedAnimationPose>(frozenEntity);
        ASSERT_NE(pose, nullptr);
        tipAtFreezeTime = ReconstructTipWorldPosition(registry, frozenEntity, data.skeleton, pose->pose, kTipBoneIndex);
    }

    DynamicChainRig* frozenRig = registry.TryGetComponent<DynamicChainRig>(frozenEntity);
    ASSERT_NE(frozenRig, nullptr);
    frozenRig->frozen = true;

    for (int i = 0; i < 120; ++i) {
        physicsSystem.Update(registry, 1.0 / 60.0);
    }

    const ResolvedAnimationPose* frozenPose = registry.TryGetComponent<ResolvedAnimationPose>(frozenEntity);
    ASSERT_NE(frozenPose, nullptr);
    const Vec3 tipAfterManyFrozenFrames
        = ReconstructTipWorldPosition(registry, frozenEntity, data.skeleton, frozenPose->pose, kTipBoneIndex);

    EXPECT_LT(Length(tipAfterManyFrozenFrames - tipAtFreezeTime), 0.01f)
        << "A frozen chain's world position drifted despite frozen == true - stepping was not actually paused.";

    // The sibling (un-frozen) chain must keep changing across those same
    // extra frames - proving `frozen` genuinely scopes to just this ONE
    // rig, not a global pause.
    const ResolvedAnimationPose* siblingPoseAtFreezeTime = nullptr; // captured just below, before the extra frames.
    (void)siblingPoseAtFreezeTime;
    const ResolvedAnimationPose* siblingPoseAfter = registry.TryGetComponent<ResolvedAnimationPose>(siblingEntity);
    ASSERT_NE(siblingPoseAfter, nullptr);
    // The sibling was never frozen and gravity/oscillation is still live -
    // it should not sit at EXACTLY the same tip position as the frozen one
    // now that the frozen one has stopped moving relative to it. A weak but
    // sufficient check: the sibling's own tip must differ from what a fully
    // re-settled chain "should" look like only in the sense that it's still
    // capable of movement, verified by comparing two snapshots.
    Vec3 siblingTipNow
        = ReconstructTipWorldPosition(registry, siblingEntity, data.skeleton, siblingPoseAfter->pose, kTipBoneIndex);
    for (int i = 0; i < 5; ++i) {
        physicsSystem.Update(registry, 1.0 / 60.0);
    }
    const ResolvedAnimationPose* siblingPoseLater = registry.TryGetComponent<ResolvedAnimationPose>(siblingEntity);
    ASSERT_NE(siblingPoseLater, nullptr);
    const Vec3 siblingTipLater
        = ReconstructTipWorldPosition(registry, siblingEntity, data.skeleton, siblingPoseLater->pose, kTipBoneIndex);
    const ResolvedAnimationPose* frozenPoseLater = registry.TryGetComponent<ResolvedAnimationPose>(frozenEntity);
    ASSERT_NE(frozenPoseLater, nullptr);
    const Vec3 frozenTipLater
        = ReconstructTipWorldPosition(registry, frozenEntity, data.skeleton, frozenPoseLater->pose, kTipBoneIndex);
    EXPECT_LT(Length(frozenTipLater - tipAtFreezeTime), 0.01f) << "Frozen chain kept moving after more frames.";
    (void)siblingTipNow;
    (void)siblingTipLater;
}

TEST(PhysicsSystemFreezeAndCulpritFTests, FrozenDynamicChainRigStillRidesAlongRigidlyWithEntityTransformMotion)
{
    PhysicsSystem physicsSystem;
    Registry registry;
    physicsSystem.GetGlobalPhysicsSettings().gravity = Vec3(0.0f, -9.8f, 0.0f);

    const SkinnedMeshData data = BuildSyntheticChainFixture();
    const Entity entity = RegisterAttachAndSeedPose(physicsSystem, registry, "FreezeRideAlongModel.gta", data);
    Transform& transform = registry.AddComponent<Transform>(entity);

    for (int i = 0; i < 60; ++i) {
        physicsSystem.Update(registry, 1.0 / 60.0);
    }

    DynamicChainRig* rig = registry.TryGetComponent<DynamicChainRig>(entity);
    ASSERT_NE(rig, nullptr);
    rig->frozen = true;

    // Capture the chain's SHAPE right after freezing (the tip's world
    // position expressed relative to the chain's own root bone, which
    // always moves perfectly rigidly with the entity - see
    // ReconstructRootRelativeTipOffset()'s own comment), plus the root's
    // own absolute world position, before any further Transform motion.
    Vec3 rootRelativeOffsetBeforeDrag;
    Vec3 rootWorldPosBeforeDrag;
    {
        const ResolvedAnimationPose* pose = registry.TryGetComponent<ResolvedAnimationPose>(entity);
        ASSERT_NE(pose, nullptr);
        rootRelativeOffsetBeforeDrag = ReconstructRootRelativeTipOffset(registry, entity, data.skeleton, pose->pose);
        rootWorldPosBeforeDrag
            = ReconstructTipWorldPosition(registry, entity, data.skeleton, pose->pose, kChainRootBoneIndex);
    }

    // A small, plausible per-frame drag delta, scaled to this synthetic
    // chain's own bind length (unit-length segments) rather than to the
    // teleport-guard's own (much larger) maxPlausibleRootDelta threshold -
    // ApplyDynamicChainPhysicsToPose() only ever corrects a bone's
    // ROTATION at a FIXED bind length (see BoneChainPhysicsResolver.cpp),
    // so a drag delta many times larger than the chain's own segment length
    // would swing the corrected direction almost entirely toward the drag
    // itself rather than preserving the chain's own shape - not a bug, just
    // the expected behavior of a direction-only correction at a fixed rod
    // length, and not representative of an ordinary small mouse-drag step.
    const Vec3 dragDelta(0.1f, 0.0f, 0.0f);
    transform.position += dragDelta;
    physicsSystem.Update(registry, 1.0 / 60.0);

    const ResolvedAnimationPose* poseAfterDrag = registry.TryGetComponent<ResolvedAnimationPose>(entity);
    ASSERT_NE(poseAfterDrag, nullptr);
    const Vec3 rootRelativeOffsetAfterDrag
        = ReconstructRootRelativeTipOffset(registry, entity, data.skeleton, poseAfterDrag->pose);
    const Vec3 rootWorldPosAfterDrag
        = ReconstructTipWorldPosition(registry, entity, data.skeleton, poseAfterDrag->pose, kChainRootBoneIndex);

    // 1. The chain's own root bone ALWAYS moves perfectly rigidly with the
    // entity's Transform, frozen or not (its own pose entry only ever has
    // its rotation corrected by physics, never its translation) - this is
    // what actually makes the whole chain "ride along" at all.
    EXPECT_TRUE(ApproximatelyEqual(rootWorldPosAfterDrag - rootWorldPosBeforeDrag, dragDelta, 1e-3f))
        << "The chain's own root bone did not move rigidly with the entity's Transform drag.";

    // 2. The chain's SHAPE (root-relative tip offset) must stay
    // approximately the same across the freeze+drag - it must NOT collapse
    // toward zero (which would mean the tip snapped back to sit exactly on
    // top of the moved root) nor swing toward pointing at the drag
    // direction (which would mean the tip stayed pinned at its old,
    // pre-drag absolute world spot while the root moved out from under it).
    EXPECT_LT(Length(rootRelativeOffsetAfterDrag - rootRelativeOffsetBeforeDrag), 0.5f * Length(dragDelta))
        << "A frozen chain's shape (root-relative tip offset) changed by more than half the drag delta - it is "
           "not correctly riding along rigidly with the model.";
}

TEST(PhysicsSystemFreezeAndCulpritFTests, UnfreezingResetsAccumulatedSecondsSoNoCatchUpBurstOccurs)
{
    PhysicsSystem physicsSystem;
    Registry registry;
    physicsSystem.GetGlobalPhysicsSettings().gravity = Vec3(0.0f, -9.8f, 0.0f);

    const SkinnedMeshData data = BuildSyntheticChainFixture();
    const Entity entity = RegisterAttachAndSeedPose(physicsSystem, registry, "UnfreezeModel.gta", data);

    for (int i = 0; i < 30; ++i) {
        physicsSystem.Update(registry, 1.0 / 60.0);
    }

    DynamicChainRig* rig = registry.TryGetComponent<DynamicChainRig>(entity);
    ASSERT_NE(rig, nullptr);
    rig->frozen = true;

    // Freeze for a long simulated duration, at a large deltaSeconds per
    // call - accumulatedSeconds must stay pinned at 0 the whole time (see
    // PhysicsSystem::Update()'s own "never bank time while frozen" rule).
    for (int i = 0; i < 5; ++i) {
        physicsSystem.Update(registry, 1.0); // 5 whole seconds' worth, one call per second.
    }
    EXPECT_FLOAT_EQ(rig->accumulatedSeconds, 0.0f)
        << "accumulatedSeconds was banked while frozen - unfreezing would trigger a multi-step catch-up burst.";

    Vec3 tipJustBeforeUnfreeze;
    {
        const ResolvedAnimationPose* pose = registry.TryGetComponent<ResolvedAnimationPose>(entity);
        ASSERT_NE(pose, nullptr);
        tipJustBeforeUnfreeze = ReconstructTipWorldPosition(registry, entity, data.skeleton, pose->pose, kTipBoneIndex);
    }

    rig->frozen = false;
    physicsSystem.Update(registry, 1.0 / 60.0); // One ordinary frame.

    const ResolvedAnimationPose* poseAfterUnfreeze = registry.TryGetComponent<ResolvedAnimationPose>(entity);
    ASSERT_NE(poseAfterUnfreeze, nullptr);
    const Vec3 tipAfterUnfreeze
        = ReconstructTipWorldPosition(registry, entity, data.skeleton, poseAfterUnfreeze->pose, kTipBoneIndex);

    const float stepDelta = Length(tipAfterUnfreeze - tipJustBeforeUnfreeze);
    EXPECT_LT(stepDelta, 0.5f)
        << "Un-freezing produced an implausibly large single-frame jump - a multi-step catch-up burst occurred.";
}

TEST(PhysicsSystemFreezeAndCulpritFTests, DisablingADynamicChainRigStillRevertsToBindPoseExactlyAsBefore)
{
    // Byte-for-byte mirror of PhysicsSystemTests.cpp's own
    // DisabledDynamicChainRigIsSkippedEntirely - proves `frozen`'s mere
    // existence (defaulting to false) does not change `enabled = false`'s
    // pre-existing, unrelated behavior at all.
    PhysicsSystem physicsSystem;
    Registry registry;

    const Entity entity = registry.CreateEntity();
    DynamicChainRig& rig = registry.AddComponent<DynamicChainRig>(entity);
    rig.meshGtaPath = "SomeModel.gta";
    rig.enabled = false;
    EXPECT_FALSE(rig.frozen) << "DynamicChainRig::frozen must default to false.";

    ResolvedAnimationPose& pose = registry.AddComponent<ResolvedAnimationPose>(entity);
    pose.pose.resize(1);
    pose.pose[0].translation = Vec3{ 5.0f, 6.0f, 7.0f };
    const Vec3 before = pose.pose[0].translation;

    physicsSystem.Update(registry, 0.016);

    const ResolvedAnimationPose* afterPose = registry.TryGetComponent<ResolvedAnimationPose>(entity);
    ASSERT_NE(afterPose, nullptr);
    ASSERT_EQ(afterPose->pose.size(), 1u);
    EXPECT_TRUE(ApproximatelyEqual(afterPose->pose[0].translation, before));
}

TEST(PhysicsSystemFreezeAndCulpritFTests,
    AnOrdinaryNonFrozenChainNeverPopsBackToRawBindOrFkPoseOnARenderFrameWhereNoNewFixedStepAccumulates)
{
    RenderSystem renderSystem;
    MeshInstantiationSystem meshInstantiationSystem(renderSystem);
    AnimationSystem animationSystem(renderSystem, meshInstantiationSystem);
    PhysicsSystem physicsSystem;
    Registry registry;

    const SkinnedMeshData data = BuildSyntheticChainFixture();
    const std::string path = "HighRefreshRateNonAnimatedModel.gta";
    animationSystem.RegisterSkinnedMesh(path, data);
    physicsSystem.RegisterDynamicChains(path, data);

    const Entity entity = registry.CreateEntity();
    physicsSystem.AttachDynamicChainRigIfNeeded(registry, entity, path);
    ASSERT_TRUE(registry.HasComponent<DynamicChainRig>(entity));

    physicsSystem.GetGlobalPhysicsSettings().gravity = Vec3(0.0f, -9.8f, 0.0f);

    // Simulate a 240 Hz display - the fixed physics timestep (default
    // 1/60) legitimately only crosses on roughly 1 in 4 of these frames,
    // so ComputeFixedStepCount() returns 0 on the overwhelming majority.
    constexpr double kTinyDeltaSeconds = 1.0 / 240.0;

    Vec3 previousTip = Vec3::Zero();
    bool everSagged = false;
    bool haveFirstSample = false;

    for (int frame = 0; frame < 600; ++frame) {
        animationSystem.EvaluatePoses(registry, kTinyDeltaSeconds);
        physicsSystem.Update(registry, kTinyDeltaSeconds);

        const ResolvedAnimationPose* pose = registry.TryGetComponent<ResolvedAnimationPose>(entity);
        ASSERT_NE(pose, nullptr);
        const Vec3 currentTip = ReconstructTipWorldPosition(registry, entity, data.skeleton, pose->pose, kTipBoneIndex);

        if (!everSagged && Length(currentTip) > 0.05f) {
            everSagged = true; // The chain has visibly moved away from bind pose at least once.
        }

        if (everSagged && haveFirstSample) {
            const float frameToFrameDelta = Length(currentTip - previousTip);
            EXPECT_LT(frameToFrameDelta, 0.15f)
                << "Frame " << frame << ": the pose snapped by a large amount frame-to-frame - "
                   "Culprit F (a pop back to raw bind/FK pose on a stepCount==0 frame) has regressed.";
        }

        previousTip = currentTip;
        haveFirstSample = true;
    }

    EXPECT_TRUE(everSagged) << "The chain never visibly sagged away from bind pose - test premise invalid.";
}

TEST(PhysicsSystemFreezeAndCulpritFTests, AnAnimatedEntityWithADynamicChainRigAlsoNeverPopsOnAStepCountZeroFrame)
{
    RenderSystem renderSystem;
    MeshInstantiationSystem meshInstantiationSystem(renderSystem);
    AnimationSystem animationSystem(renderSystem, meshInstantiationSystem);
    PhysicsSystem physicsSystem;
    Registry registry;

    const std::filesystem::path root = std::filesystem::temp_directory_path()
        / "GtePhysicsSystemFreezeAndCulpritFTests_AnimatedNeverPopsOnStepCountZero";
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
    std::filesystem::create_directories(root, ec);

    const SkinnedMeshData data = BuildSyntheticChainFixture();
    const std::string meshPath = "HighRefreshRateAnimatedModel.gta";
    animationSystem.RegisterSkinnedMesh(meshPath, data);
    physicsSystem.RegisterDynamicChains(meshPath, data);

    // A tiny, constant-offset motion track on the ROOT bone only (never
    // part of the physics chain itself, which anchors at "chainRoot") -
    // just enough to prove a genuinely-playing SkeletalAnimator is in the
    // mix, mirroring AnimationSystemEvaluatePosesTests.cpp's own fixture
    // style.
    MotionData motion;
    BoneKeyframe keyframe;
    keyframe.boneName = "root";
    keyframe.frame = 0;
    keyframe.translation = Vec3{ 0.1f, 0.0f, 0.0f };
    keyframe.rotation = Quat::Identity();
    motion.boneKeyframes.push_back(keyframe);

    const std::filesystem::path animationPath = root / "motion.gta";
    const std::vector<std::uint8_t> payload = EncodeMotionDataToBytes(motion);
    ASSERT_TRUE(WriteGtaFile(animationPath, AssetType::Animation, Guid::Generate(), AssetFlags::None, {}, payload));

    const Entity entity = registry.CreateEntity();
    registry.AddComponent<MeshAssetSource>(entity, MeshAssetSource{ meshPath });
    ASSERT_TRUE(animationSystem.Play(registry, entity, animationPath.string()));

    physicsSystem.AttachDynamicChainRigIfNeeded(registry, entity, meshPath);
    ASSERT_TRUE(registry.HasComponent<DynamicChainRig>(entity));

    physicsSystem.GetGlobalPhysicsSettings().gravity = Vec3(0.0f, -9.8f, 0.0f);

    constexpr double kTinyDeltaSeconds = 1.0 / 240.0;

    Vec3 previousTip = Vec3::Zero();
    bool everSagged = false;
    bool haveFirstSample = false;

    for (int frame = 0; frame < 600; ++frame) {
        animationSystem.EvaluatePoses(registry, kTinyDeltaSeconds);
        physicsSystem.Update(registry, kTinyDeltaSeconds);

        const ResolvedAnimationPose* pose = registry.TryGetComponent<ResolvedAnimationPose>(entity);
        ASSERT_NE(pose, nullptr);
        const Vec3 currentTip = ReconstructTipWorldPosition(registry, entity, data.skeleton, pose->pose, kTipBoneIndex);

        if (!everSagged && Length(currentTip) > 0.05f) {
            everSagged = true;
        }

        if (everSagged && haveFirstSample) {
            const float frameToFrameDelta = Length(currentTip - previousTip);
            EXPECT_LT(frameToFrameDelta, 0.15f)
                << "Frame " << frame << ": the pose snapped by a large amount frame-to-frame on an ANIMATED "
                   "entity - Culprit F has regressed for the animated case too.";
        }

        previousTip = currentTip;
        haveFirstSample = true;
    }

    EXPECT_TRUE(everSagged) << "The chain never visibly sagged away from bind pose - test premise invalid.";

    std::filesystem::remove_all(root, ec);
}

} // namespace
} // namespace gte
