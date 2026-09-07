// Unit tests for PhysicsSystem's world-space, root-motion-aware chain
// simulation (task_manager/verlet-integration-7,
// PHASE3_WORLD_SPACE_ROOT_MOTION_AWARE_CHAIN_SIMULATION.md) - the fix for
// Culprit C: PhysicsSystem::Update() now composes the owning ECS entity's
// REAL, fully-resolved world position/rotation (via
// ECS/TransformHierarchy.h's ComputeWorldTransform(), scale deliberately
// excluded per PHASE0_MASTER_STRATEGY.md's Revision Notes (v2), Finding #1)
// with the bone-local pose before feeding the Verlet solver, instead of
// simulating in blind bone-local "model space" as if the entity's own
// Transform never existed. This is the literal, automated version of the
// user's own acceptance test: "i want hair or skirt get physically move by
// simulation if i drag model position left and right without to actually
// run animation on it."
//
// Mirrors PhysicsSystemTests.cpp's own synthetic 4-bone-rig fixture
// construction style exactly (a Static anchor + two Dynamic-body,
// perpendicular-to-gravity chain), reused across every test below via a
// shared helper - all Tier 1, no ECS/Renderer/GPU dependency beyond a plain
// Registry.

#include "Game/Physics/PhysicsSystem.h"

#include "Animation/BoneWorldMatrixQuery.h"
#include "ECS/Components/DynamicChainRig.h"
#include "ECS/Components/ResolvedAnimationPose.h"
#include "ECS/Components/Transform.h"
#include "ECS/Registry.h"
#include "ECS/TransformHierarchy.h"
#include "Game/Animation/SkeletalRigCache.h"
#include "Math/Mat4.h"
#include "Math/Quat.h"

#include <gtest/gtest.h>

#include <cmath>
#include <string>
#include <vector>

namespace gte {
namespace {

// Builds the exact same synthetic 2-joint chain fixture
// PhysicsSystemTests.cpp's own RegisteredDynamicChainVisiblyDivergesFromPureFkPoseUnderGravity
// test uses: root(0, no rigid body) -> chainRoot(1, a STATIC rigid body -
// this chain's anchor) -> joint1(2, a Dynamic rigid body) -> joint2(3, a
// Dynamic rigid body), extending along +X (perpendicular to gravity, which
// points along -Y) so it genuinely swings sideways under gravity instead of
// only stretching/compressing along its own bind axis.
SkinnedMeshData BuildSyntheticChainFixture()
{
    SkinnedMeshData data;
    Bone root;
    root.position = Vec3(0.0f, 0.0f, 0.0f);
    root.parentBoneIndex = -1;
    data.skeleton.bones.push_back(root); // 0

    Bone chainRoot;
    chainRoot.position = Vec3(1.0f, 0.0f, 0.0f);
    chainRoot.parentBoneIndex = 0;
    data.skeleton.bones.push_back(chainRoot); // 1

    Bone joint1;
    joint1.position = Vec3(2.0f, 0.0f, 0.0f);
    joint1.parentBoneIndex = 1;
    data.skeleton.bones.push_back(joint1); // 2

    Bone joint2;
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

// Reconstructs the tip bone's (bone index 3) WORLD position exactly the way
// PhysicsSystem::Update() itself now does: the entity's own resolved
// (scale-free) world transform composed with the bone-local pose.
Vec3 ReconstructTipWorldPosition(Registry& registry, Entity entity, const SkeletonData& skeleton,
    const std::vector<BoneLocalOffset>& pose, std::int32_t tipBoneIndex)
{
    const Transform worldTransform = ComputeWorldTransform(registry, entity);
    const Mat4 entityWorldMatrix = Mat4::TRS(worldTransform.position, worldTransform.rotation, Vec3::One());
    const Mat4 boneWorld = entityWorldMatrix * ComputeBoneWorldMatrix(skeleton, pose, tipBoneIndex);
    return boneWorld.TransformPoint(Vec3::Zero());
}

constexpr std::int32_t kTipBoneIndex = 3;

TEST(PhysicsSystemWorldSpaceRootMotionTests, EntityTransformTranslationProducesInertialLagInSimulatedChain)
{
    PhysicsSystem physicsSystem;
    Registry registry;
    physicsSystem.GetGlobalPhysicsSettings().gravity = Vec3(0.0f, -9.8f, 0.0f);

    const SkinnedMeshData data = BuildSyntheticChainFixture();
    const std::string path = "WorldSpaceDragModel.gta";
    const Entity entity = RegisterAttachAndSeedPose(physicsSystem, registry, path, data);

    Transform& transform = registry.AddComponent<Transform>(entity); // left at identity.

    for (int i = 0; i < 60; ++i) {
        physicsSystem.Update(registry, 1.0 / 60.0);
    }

    const ResolvedAnimationPose* poseBeforeDrag = registry.TryGetComponent<ResolvedAnimationPose>(entity);
    ASSERT_NE(poseBeforeDrag, nullptr);
    const Vec3 tipBeforeDrag
        = ReconstructTipWorldPosition(registry, entity, data.skeleton, poseBeforeDrag->pose, kTipBoneIndex);

    // A single, plausible drag delta, well under the default
    // maxPlausibleRootDelta (10.0f) - simulates one frame of a mouse-drag-
    // driven Transform update.
    transform.position += Vec3(2.0f, 0.0f, 0.0f);
    physicsSystem.Update(registry, 1.0 / 60.0);

    const ResolvedAnimationPose* poseAfterDrag = registry.TryGetComponent<ResolvedAnimationPose>(entity);
    ASSERT_NE(poseAfterDrag, nullptr);
    const Vec3 tipAfterDrag
        = ReconstructTipWorldPosition(registry, entity, data.skeleton, poseAfterDrag->pose, kTipBoneIndex);

    const Vec3 actualDelta = tipAfterDrag - tipBeforeDrag;
    const float actualDeltaLength = Length(actualDelta);

    EXPECT_LT(actualDeltaLength, 2.0f)
        << "The tip's world position moved by the FULL rigid drag delta (or more) - PhysicsSystem is still "
           "simulating in blind model space with zero knowledge the entity's own Transform moved.";
    EXPECT_GT(actualDeltaLength, 0.0f)
        << "The tip's world position did not move AT ALL in response to the drag - PhysicsSystem never "
           "composed the entity's Transform into the simulation at all.";
}

TEST(PhysicsSystemWorldSpaceRootMotionTests,
    ASequenceOfSmallContinuousTransformDeltasNeverTripsTheTeleportGuardWhileALargeSingleFrameJumpStillDoes)
{
    PhysicsSystem physicsSystem;
    physicsSystem.GetGlobalPhysicsSettings().gravity = Vec3(0.0f, -9.8f, 0.0f);

    // (a) A sequence of small, continuous per-frame deltas (a plausible
    // 60fps mouse-drag) must never trip the re-seed guard - i.e. the tip
    // must never jump discontinuously by anywhere close to the FULL
    // accumulated delta in a single step.
    {
        Registry registry;
        const SkinnedMeshData data = BuildSyntheticChainFixture();
        const std::string path = "SmoothDragModel.gta";
        const Entity entity = RegisterAttachAndSeedPose(physicsSystem, registry, path, data);
        Transform& transform = registry.AddComponent<Transform>(entity);

        for (int i = 0; i < 30; ++i) {
            physicsSystem.Update(registry, 1.0 / 60.0);
        }

        Vec3 previousTip;
        {
            const ResolvedAnimationPose* pose = registry.TryGetComponent<ResolvedAnimationPose>(entity);
            ASSERT_NE(pose, nullptr);
            previousTip = ReconstructTipWorldPosition(registry, entity, data.skeleton, pose->pose, kTipBoneIndex);
        }

        for (int i = 0; i < 10; ++i) {
            transform.position += Vec3(0.05f, 0.0f, 0.0f); // A plausible per-frame drag speed.
            physicsSystem.Update(registry, 1.0 / 60.0);

            const ResolvedAnimationPose* pose = registry.TryGetComponent<ResolvedAnimationPose>(entity);
            ASSERT_NE(pose, nullptr);
            const Vec3 currentTip = ReconstructTipWorldPosition(registry, entity, data.skeleton, pose->pose, kTipBoneIndex);

            const float stepDelta = Length(currentTip - previousTip);
            EXPECT_LT(stepDelta, 1.0f)
                << "A single small drag step produced a large, discontinuous tip jump - the teleport-guard "
                   "re-seed fired for an ordinary continuous drag, which must never happen.";
            previousTip = currentTip;
        }
    }

    // (b) A FRESH scenario: one single-frame jump far beyond
    // maxPlausibleRootDelta must still trip the guard and cleanly re-seed -
    // no NaN/Inf, and the tip lands consistently near the new bind/FK
    // target instead of flying off to infinity.
    {
        Registry registry;
        const SkinnedMeshData data = BuildSyntheticChainFixture();
        const std::string path = "TeleportModel.gta";
        const Entity entity = RegisterAttachAndSeedPose(physicsSystem, registry, path, data);
        Transform& transform = registry.AddComponent<Transform>(entity);

        for (int i = 0; i < 30; ++i) {
            physicsSystem.Update(registry, 1.0 / 60.0);
        }

        transform.position += Vec3(500.0f, 0.0f, 0.0f); // Far beyond the default maxPlausibleRootDelta (10.0f).
        physicsSystem.Update(registry, 1.0 / 60.0);

        const ResolvedAnimationPose* pose = registry.TryGetComponent<ResolvedAnimationPose>(entity);
        ASSERT_NE(pose, nullptr);
        const Vec3 tipAfterTeleport
            = ReconstructTipWorldPosition(registry, entity, data.skeleton, pose->pose, kTipBoneIndex);

        EXPECT_TRUE(std::isfinite(tipAfterTeleport.x) && std::isfinite(tipAfterTeleport.y)
            && std::isfinite(tipAfterTeleport.z))
            << "A large single-frame teleport produced a non-finite tip position - the re-seed safety net failed.";

        // Re-seeded means the tip should land close to the animated/bind
        // target's own world position (bone index 3's bind-pose offset from
        // the NEW entity position), not somewhere wildly divergent.
        const Vec3 expectedNearBindTarget
            = ReconstructTipWorldPosition(registry, entity, data.skeleton, std::vector<BoneLocalOffset>(data.skeleton.bones.size()), kTipBoneIndex);
        EXPECT_LT(Length(tipAfterTeleport - expectedNearBindTarget), 1.0f)
            << "After a teleport, the chain did not re-seed close to the bind pose at the new position.";
    }
}

TEST(PhysicsSystemWorldSpaceRootMotionTests, IdentityOrMissingTransformProducesByteIdenticalResultsToPreWorldSpaceBehavior)
{
    PhysicsSystem physicsSystemNoTransform;
    Registry registryNoTransform;
    physicsSystemNoTransform.GetGlobalPhysicsSettings().gravity = Vec3(0.0f, -9.8f, 0.0f);
    const SkinnedMeshData dataNoTransform = BuildSyntheticChainFixture();
    const Entity entityNoTransform
        = RegisterAttachAndSeedPose(physicsSystemNoTransform, registryNoTransform, "NoTransformModel.gta", dataNoTransform);
    // Deliberately NEVER adds a Transform component to entityNoTransform.

    PhysicsSystem physicsSystemIdentityTransform;
    Registry registryIdentityTransform;
    physicsSystemIdentityTransform.GetGlobalPhysicsSettings().gravity = Vec3(0.0f, -9.8f, 0.0f);
    const SkinnedMeshData dataIdentityTransform = BuildSyntheticChainFixture();
    const Entity entityIdentityTransform = RegisterAttachAndSeedPose(
        physicsSystemIdentityTransform, registryIdentityTransform, "IdentityTransformModel.gta", dataIdentityTransform);
    registryIdentityTransform.AddComponent<Transform>(entityIdentityTransform); // explicit, untouched default.

    for (int i = 0; i < 45; ++i) {
        physicsSystemNoTransform.Update(registryNoTransform, 1.0 / 60.0);
        physicsSystemIdentityTransform.Update(registryIdentityTransform, 1.0 / 60.0);
    }

    const ResolvedAnimationPose* poseNoTransform = registryNoTransform.TryGetComponent<ResolvedAnimationPose>(entityNoTransform);
    const ResolvedAnimationPose* poseIdentityTransform
        = registryIdentityTransform.TryGetComponent<ResolvedAnimationPose>(entityIdentityTransform);
    ASSERT_NE(poseNoTransform, nullptr);
    ASSERT_NE(poseIdentityTransform, nullptr);
    ASSERT_EQ(poseNoTransform->pose.size(), poseIdentityTransform->pose.size());

    for (std::size_t i = 0; i < poseNoTransform->pose.size(); ++i) {
        EXPECT_TRUE(ApproximatelyEqual(poseNoTransform->pose[i].translation, poseIdentityTransform->pose[i].translation));
        EXPECT_TRUE(RepresentSameRotation(poseNoTransform->pose[i].rotation, poseIdentityTransform->pose[i].rotation));
    }
}

TEST(PhysicsSystemWorldSpaceRootMotionTests, ATransformParentedUnderAMovingAncestorStillProducesCorrectlyComposedWorldSpaceSimulation)
{
    PhysicsSystem physicsSystem;
    Registry registry;
    physicsSystem.GetGlobalPhysicsSettings().gravity = Vec3(0.0f, -9.8f, 0.0f);

    const SkinnedMeshData data = BuildSyntheticChainFixture();
    const std::string path = "ParentedDragModel.gta";
    const Entity entity = RegisterAttachAndSeedPose(physicsSystem, registry, path, data);
    Transform& transform = registry.AddComponent<Transform>(entity); // local, relative to `vehicle` below.

    const Entity vehicle = registry.CreateEntity();
    registry.AddComponent<Transform>(vehicle); // left at identity initially.

    ASSERT_TRUE(SetParent(registry, entity, vehicle, /*worldPositionStays=*/false));
    (void)transform;

    for (int i = 0; i < 30; ++i) {
        physicsSystem.Update(registry, 1.0 / 60.0);
    }

    Vec3 tipBeforeVehicleMove;
    {
        const ResolvedAnimationPose* pose = registry.TryGetComponent<ResolvedAnimationPose>(entity);
        ASSERT_NE(pose, nullptr);
        tipBeforeVehicleMove = ReconstructTipWorldPosition(registry, entity, data.skeleton, pose->pose, kTipBoneIndex);
    }

    // Move the VEHICLE (the physics entity's own ANCESTOR), not the physics
    // entity's own local Transform, across several frames.
    Transform& vehicleTransform = registry.GetComponent<Transform>(vehicle);
    for (int i = 0; i < 5; ++i) {
        vehicleTransform.position += Vec3(0.3f, 0.0f, 0.0f);
        physicsSystem.Update(registry, 1.0 / 60.0);
    }

    const ResolvedAnimationPose* poseAfter = registry.TryGetComponent<ResolvedAnimationPose>(entity);
    ASSERT_NE(poseAfter, nullptr);
    const Vec3 tipAfterVehicleMove = ReconstructTipWorldPosition(registry, entity, data.skeleton, poseAfter->pose, kTipBoneIndex);

    const float totalRigidDelta = 0.3f * 5.0f; // The vehicle's own total rigid displacement.
    const float actualDelta = Length(tipAfterVehicleMove - tipBeforeVehicleMove);

    EXPECT_LT(actualDelta, totalRigidDelta)
        << "The tip lagged less than the vehicle's own rigid motion should have produced - "
           "ComputeWorldTransform()'s full recursive parent-chain walk isn't feeding this phase's math.";
    EXPECT_GT(actualDelta, 0.0f) << "The tip never moved at all in response to its ANCESTOR's own motion.";
}

TEST(PhysicsSystemWorldSpaceRootMotionTests, EntityTransformScaleNeverAffectsTheSimulatedPoseOrTheTeleportGuardThreshold)
{
    // Run the SAME synthetic-chain-under-gravity-and-drag scenario on THREE
    // separate entities/registries, differing ONLY in Transform::scale -
    // must produce IDENTICAL simulated poses.
    const std::vector<Vec3> scales = { Vec3::One(), Vec3(2.0f, 2.0f, 2.0f), Vec3(0.25f, 0.25f, 0.25f) };

    std::vector<std::vector<BoneLocalOffset>> resultingPoses;
    for (const Vec3& scale : scales) {
        PhysicsSystem physicsSystem;
        Registry registry;
        physicsSystem.GetGlobalPhysicsSettings().gravity = Vec3(0.0f, -9.8f, 0.0f);

        const SkinnedMeshData data = BuildSyntheticChainFixture();
        const Entity entity = RegisterAttachAndSeedPose(physicsSystem, registry, "ScaleInvarianceModel.gta", data);
        Transform& transform = registry.AddComponent<Transform>(entity);
        transform.scale = scale;

        for (int i = 0; i < 30; ++i) {
            physicsSystem.Update(registry, 1.0 / 60.0);
        }

        // The same drag gesture, regardless of scale.
        transform.position += Vec3(2.0f, 0.0f, 0.0f);
        physicsSystem.Update(registry, 1.0 / 60.0);

        const ResolvedAnimationPose* pose = registry.TryGetComponent<ResolvedAnimationPose>(entity);
        ASSERT_NE(pose, nullptr);
        resultingPoses.push_back(pose->pose);
    }

    ASSERT_EQ(resultingPoses.size(), scales.size());
    for (std::size_t i = 1; i < resultingPoses.size(); ++i) {
        ASSERT_EQ(resultingPoses[i].size(), resultingPoses[0].size());
        for (std::size_t boneIndex = 0; boneIndex < resultingPoses[0].size(); ++boneIndex) {
            EXPECT_TRUE(ApproximatelyEqual(
                resultingPoses[i][boneIndex].translation, resultingPoses[0][boneIndex].translation, 1e-3f))
                << "Bone " << boneIndex << " translation differed between scale " << i << " and the unscaled baseline - "
                   "the simulation must be completely scale-invariant.";
            EXPECT_TRUE(RepresentSameRotation(resultingPoses[i][boneIndex].rotation, resultingPoses[0][boneIndex].rotation, 1e-3f))
                << "Bone " << boneIndex << " rotation differed between scale " << i << " and the unscaled baseline.";
        }
    }
}

} // namespace
} // namespace gte
