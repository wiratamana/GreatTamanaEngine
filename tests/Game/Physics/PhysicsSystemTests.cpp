// Unit tests for PhysicsSystem (task_manager/verlet-integration-1/
// PHASE3_PIPELINE_INTEGRATION_AND_FIXED_TIMESTEP.md and
// PHASE4_PARAMETER_AUTHORING_AND_DATA_DRIVEN_CONFIG.md) -
// src/Game/Physics/PhysicsSystem.h/.cpp. Phase 3's own tests (still present
// below, unchanged) confirmed PhysicsSystem::Update() was a genuine, safe
// NO-OP while DynamicChainRigCache was a deliberate, provable stub; Phase 4
// adds a REAL, non-stub, end-to-end test proving a registered dynamic bone
// chain actually visibly moves under gravity - no ECS/GPU/Renderer
// dependency beyond a plain Registry, Tier 1.

#include "Game/Physics/PhysicsSystem.h"

#include "ECS/Components/DynamicChainRig.h"
#include "ECS/Components/ResolvedAnimationPose.h"
#include "ECS/Registry.h"
#include "Game/Animation/SkeletalRigCache.h"

#include <gtest/gtest.h>

namespace gte {
namespace {

TEST(PhysicsSystemTests, UpdateNeverModifiesResolvedAnimationPoseSinceNoChainsAreEverRegisteredYet)
{
    PhysicsSystem physicsSystem;
    Registry registry;

    const Entity entity = registry.CreateEntity();
    DynamicChainRig& rig = registry.AddComponent<DynamicChainRig>(entity);
    rig.meshGtaPath = "SomeModel.gta";
    rig.enabled = true;

    ResolvedAnimationPose& pose = registry.AddComponent<ResolvedAnimationPose>(entity);
    pose.pose.resize(2);
    pose.pose[0].translation = Vec3{ 1.0f, 2.0f, 3.0f };
    pose.pose[1].rotation = Quat::FromAxisAngle(Vec3::Up(), 0.5f);
    const std::vector<BoneLocalOffset> before = pose.pose;

    physicsSystem.Update(registry, 0.016);

    const ResolvedAnimationPose* afterPose = registry.TryGetComponent<ResolvedAnimationPose>(entity);
    ASSERT_NE(afterPose, nullptr);
    ASSERT_EQ(afterPose->pose.size(), before.size());
    for (std::size_t i = 0; i < before.size(); ++i) {
        EXPECT_TRUE(ApproximatelyEqual(afterPose->pose[i].translation, before[i].translation));
        EXPECT_TRUE(ApproximatelyEqual(afterPose->pose[i].rotation, before[i].rotation));
    }
}

TEST(PhysicsSystemTests, UpdateSafelyNoOpsOnAnEntityWithDynamicChainRigButNoResolvedAnimationPoseYet)
{
    PhysicsSystem physicsSystem;
    Registry registry;

    const Entity entity = registry.CreateEntity();
    DynamicChainRig& rig = registry.AddComponent<DynamicChainRig>(entity);
    rig.meshGtaPath = "SomeModel.gta";
    rig.enabled = true;

    EXPECT_NO_FATAL_FAILURE(physicsSystem.Update(registry, 0.016));
    EXPECT_FALSE(registry.HasComponent<ResolvedAnimationPose>(entity));
}

TEST(PhysicsSystemTests, UpdateSafelyNoOpsOnAnEntityWithNeitherComponentAtAll)
{
    PhysicsSystem physicsSystem;
    Registry registry;

    const Entity entity = registry.CreateEntity();
    (void)entity; // Deliberately carries neither DynamicChainRig nor ResolvedAnimationPose.

    EXPECT_NO_FATAL_FAILURE(physicsSystem.Update(registry, 0.016));
}

TEST(PhysicsSystemTests, UpdateSafelyNoOpsOnAnEmptyRegistry)
{
    PhysicsSystem physicsSystem;
    Registry registry;

    EXPECT_NO_FATAL_FAILURE(physicsSystem.Update(registry, 0.016));
}

TEST(PhysicsSystemTests, DisabledDynamicChainRigIsSkippedEntirely)
{
    PhysicsSystem physicsSystem;
    Registry registry;

    const Entity entity = registry.CreateEntity();
    DynamicChainRig& rig = registry.AddComponent<DynamicChainRig>(entity);
    rig.meshGtaPath = "SomeModel.gta";
    rig.enabled = false;

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

TEST(PhysicsSystemTests, RegisteredDynamicChainVisiblyDivergesFromPureFkPoseUnderGravity)
{
    PhysicsSystem physicsSystem;
    Registry registry;

    // Synthetic 4-bone rig: root(0, not flagged) -> chainRoot(1, not
    // flagged, the chain's anchor) -> joint1(2, flagged) -> joint2(3,
    // flagged) - a 2-joint deformAfterPhysics run, well above
    // DynamicChainDetectionDefaults::minimumChainLength (2). Deliberately
    // extends along +X (perpendicular to gravity, which points along -Y) -
    // a chain colinear WITH gravity would only ever compress/stretch along
    // its own bind axis (fully undone by the structural distance
    // constraint, producing zero net rotation - see
    // BoneChainPhysicsResolverTests.cpp's own "already aligned" case), never
    // genuinely swing sideways the way this test needs to observe.
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
    joint1.deformAfterPhysics = true;
    data.skeleton.bones.push_back(joint1); // 2

    Bone joint2;
    joint2.position = Vec3(3.0f, 0.0f, 0.0f);
    joint2.parentBoneIndex = 2;
    joint2.deformAfterPhysics = true;
    data.skeleton.bones.push_back(joint2); // 3

    const std::string path = "SyntheticDynamicChainModel.gta";
    physicsSystem.RegisterDynamicChains(path, data);

    const Entity entity = registry.CreateEntity();
    physicsSystem.AttachDynamicChainRigIfNeeded(registry, entity, path);

    const DynamicChainRig* rig = registry.TryGetComponent<DynamicChainRig>(entity);
    ASSERT_NE(rig, nullptr) << "A real 2-joint chain should have been detected/attached this phase.";
    EXPECT_EQ(rig->meshGtaPath, path);
    ASSERT_EQ(rig->chainStates.size(), 1u);

    // Drive a pure-FK ResolvedAnimationPose exactly as
    // AnimationSystem::EvaluatePoses() would (bind pose - all identity) -
    // this test needs NO AnimationSystem involvement at all, further proof
    // the two systems are genuinely decoupled (see PHASE3's own v3 Revision
    // Notice).
    ResolvedAnimationPose& pose = registry.AddComponent<ResolvedAnimationPose>(entity);
    pose.pose.resize(data.skeleton.bones.size());
    const std::vector<BoneLocalOffset> pureFkPose = pose.pose;

    physicsSystem.GetGlobalPhysicsSettings().gravity = Vec3(0.0f, -9.8f, 0.0f);

    for (int i = 0; i < 30; ++i) {
        physicsSystem.Update(registry, 1.0 / 60.0);
    }

    const ResolvedAnimationPose* afterPose = registry.TryGetComponent<ResolvedAnimationPose>(entity);
    ASSERT_NE(afterPose, nullptr);
    ASSERT_EQ(afterPose->pose.size(), pureFkPose.size());

    bool anyDiffers = false;
    for (std::size_t i = 0; i < pureFkPose.size(); ++i) {
        if (!RepresentSameRotation(afterPose->pose[i].rotation, pureFkPose[i].rotation)
            || !ApproximatelyEqual(afterPose->pose[i].translation, pureFkPose[i].translation)) {
            anyDiffers = true;
            break;
        }
    }
    EXPECT_TRUE(anyDiffers)
        << "PhysicsSystem::Update() never changed the pose despite gravity and a registered dynamic chain - "
           "a physics-driven bone must visibly move where a static FK bone would not.";
}

} // namespace
} // namespace gte
