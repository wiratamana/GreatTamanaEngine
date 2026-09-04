// Unit tests for PhysicsSystem (Phase 3,
// task_manager/verlet-integration-1/PHASE3_PIPELINE_INTEGRATION_AND_FIXED_TIMESTEP.md)
// - src/Game/Physics/PhysicsSystem.h/.cpp. This phase's own
// DynamicChainRigCache is a deliberate, provable stub (real chain detection
// is Phase 4's job - see DynamicChainRigCache.h's own file comment), so
// every test here confirms PhysicsSystem::Update() is a genuine, safe NO-OP
// today - no ECS/GPU/Renderer dependency beyond a plain Registry, Tier 1.

#include "Game/Physics/PhysicsSystem.h"

#include "ECS/Components/DynamicChainRig.h"
#include "ECS/Components/ResolvedAnimationPose.h"
#include "ECS/Registry.h"

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

} // namespace
} // namespace gte
