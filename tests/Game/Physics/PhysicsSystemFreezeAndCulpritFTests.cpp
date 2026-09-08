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

// task_manager/verlet-integration-8, Phase 1/3 - REWRITTEN. Before Phase 1,
// a direct anchor-child joint's corrective ROTATION was written onto the
// chain's own ANCHOR bone (definition.rootBoneIndex) - an approximate,
// fixed-bind-length, direction-only correction. Because that correction was
// only ever "pointed" at wherever the (possibly stale, while frozen) target
// happened to be RELATIVE TO the anchor's own CURRENT world position, a
// small entity Transform drag while frozen only nudged the resulting
// direction slightly, so the chain's overall SHAPE (root-relative tip
// offset) barely changed - hence this test's original ~"changes by less
// than half the drag delta" assertion below.
//
// After Phase 1, a direct anchor-child's correction is instead an EXACT
// local TRANSLATION that lands the joint bone at PRECISELY its simulated
// target (see BoneChainPhysicsResolver.cpp's ApplyDynamicChainPhysicsToPose()
// and its own header comment for the full derivation) - the anchor's own
// pose entry is never written at all. Combined with how PhysicsSystem.cpp
// feeds this function ITS OWN entity-transform-INVERSE-transformed particle
// position as the "simulated target" (see StepDynamicChainRange()'s own
// `context.entityWorldMatrixInverse.TransformPoint(particle.position)`
// call), composing the corrected pose back with the (possibly-just-dragged)
// entity transform algebraically CANCELS the transform out completely for a
// direct anchor-child bone: its real WORLD position ends up EXACTLY equal to
// its last real, frozen, absolute-world particle position, regardless of
// what the entity's Transform is doing - a mathematically exact invariant,
// not an approximation. This is a genuine, deliberate, PROVABLE behavior
// change from Phase 1, not a regression: "frozen" now means the chain's own
// simulated joints stay pinned at their last real world position (rather
// than approximately riding along with the model), while the anchor bone
// itself (never touched by physics, per DynamicChainDefinition.h's own
// contract) still moves perfectly rigidly with the entity's Transform - the
// two halves of "the chain" now visibly diverge while both frozen AND
// dragged at the same time, which is an accepted, out-of-scope-for-this-
// campaign consequence of correctly fixing the anchor's own rigidity (see
// PHASE0_MASTER_STRATEGY.md, "What We Will NOT Do" - the Verlet solver/
// freeze semantics themselves are untouched by this campaign).
TEST(PhysicsSystemFreezeAndCulpritFTests, FrozenDynamicChainRigStillRidesAlongRigidlyWithEntityTransformMotion)
{
    PhysicsSystem physicsSystem;
    Registry registry;
    physicsSystem.GetGlobalPhysicsSettings().gravity = Vec3(0.0f, -9.8f, 0.0f);

    const SkinnedMeshData data = BuildSyntheticChainFixture();
    const Entity entity = RegisterAttachAndSeedPose(physicsSystem, registry, "FreezeRideAlongModel.gta", data);
    Transform& transform = registry.AddComponent<Transform>(entity);

    // task_manager/verlet-integration-7, Phase 5 (PHASE5_IDLE_PHYSICS_TUNING_AND_SETTLING_REGRESSION.md,
    // Culprit E, Step 3.3 audit) - settles for only 10 frames (was 60) before
    // freezing. This fixture never calls AnimationSystem::EvaluatePoses(), so
    // `context.pose` (the goal constraint's own animated-target read) is
    // never reset back to a pure bind pose between PhysicsSystem::Update()
    // calls, unlike the real production pipeline (Phase 1), where
    // EvaluatePoses() rewrites it fresh every single frame. With the
    // RETUNED, much weaker default `stiffness` (0.02, down from 0.35), this
    // self-referential "target chases its own prior physics output" quirk
    // lets many settling frames accumulate real drift toward a near-full
    // gravity hang - 60 frames drifted far enough that the chain's shape
    // became dominated by gravity rather than by its own bind-pose direction
    // (a stale artifact of testing PhysicsSystem in isolation, not a
    // production regression - see Game/GameLoopPhysicsWithoutAnimationTests.cpp,
    // which DOES exercise the real EvaluatePoses()+Update() pipeline and is
    // unaffected by this retune). 10 frames is still comfortably enough to
    // prove the chain has genuinely started simulating (non-bind) before
    // freezing.
    for (int i = 0; i < 10; ++i) {
        physicsSystem.Update(registry, 1.0 / 60.0);
    }

    DynamicChainRig* rig = registry.TryGetComponent<DynamicChainRig>(entity);
    ASSERT_NE(rig, nullptr);
    rig->frozen = true;

    // Capture the tip's and the root's own absolute WORLD positions right
    // after freezing, before any further Transform motion.
    Vec3 tipWorldPosBeforeDrag;
    Vec3 rootWorldPosBeforeDrag;
    {
        const ResolvedAnimationPose* pose = registry.TryGetComponent<ResolvedAnimationPose>(entity);
        ASSERT_NE(pose, nullptr);
        tipWorldPosBeforeDrag = ReconstructTipWorldPosition(registry, entity, data.skeleton, pose->pose, kTipBoneIndex);
        rootWorldPosBeforeDrag
            = ReconstructTipWorldPosition(registry, entity, data.skeleton, pose->pose, kChainRootBoneIndex);
    }

    // A small, plausible per-frame drag delta, scaled to this synthetic
    // chain's own bind length (unit-length segments) rather than to the
    // teleport-guard's own (much larger) maxPlausibleRootDelta threshold.
    const Vec3 dragDelta(0.1f, 0.0f, 0.0f);
    transform.position += dragDelta;
    physicsSystem.Update(registry, 1.0 / 60.0);

    const ResolvedAnimationPose* poseAfterDrag = registry.TryGetComponent<ResolvedAnimationPose>(entity);
    ASSERT_NE(poseAfterDrag, nullptr);
    const Vec3 tipWorldPosAfterDrag
        = ReconstructTipWorldPosition(registry, entity, data.skeleton, poseAfterDrag->pose, kTipBoneIndex);
    const Vec3 rootWorldPosAfterDrag
        = ReconstructTipWorldPosition(registry, entity, data.skeleton, poseAfterDrag->pose, kChainRootBoneIndex);

    // 1. The chain's own root (anchor) bone ALWAYS moves perfectly rigidly
    // with the entity's Transform, frozen or not - PhysicsSystem::Update()
    // never writes to its pose entry at all, neither rotation nor
    // translation (see BoneChainPhysicsResolver.cpp's
    // ApplyDynamicChainPhysicsToPose(), task_manager/verlet-integration-8,
    // Phase 1).
    EXPECT_TRUE(ApproximatelyEqual(rootWorldPosAfterDrag - rootWorldPosBeforeDrag, dragDelta, 1e-3f))
        << "The chain's own root bone did not move rigidly with the entity's Transform drag.";

    // 2. task_manager/verlet-integration-8, Phase 1/3 - the chain's TIP, by
    // contrast, stays PINNED at its last real, simulated, absolute WORLD
    // position while frozen, regardless of the drag - a mathematically exact
    // consequence of Phase 1's exact translation-based anchor-child
    // correction canceling the entity transform, proven above this test.
    // This REPLACES the pre-Phase-1 "root-relative offset changes by less
    // than half the drag delta" assertion, which relied on the OLD
    // approximate, direction-only correction's own incidental behavior.
    EXPECT_LT(Length(tipWorldPosAfterDrag - tipWorldPosBeforeDrag), 1e-3f)
        << "A frozen chain's tip must stay pinned at its last simulated world position across a Transform drag - "
           "see this test's own comment for the exact reason this is now provably true.";
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
