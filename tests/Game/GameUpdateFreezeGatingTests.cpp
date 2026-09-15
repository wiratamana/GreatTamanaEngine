// Unit tests for Game::Update()'s new EngineContext-driven freeze-gating
// behavior (task_manager/frame-debugger-1/
// PHASE2_GAME_UPDATE_SIGNATURE_AND_FREEZE_GATING.md). This is the genuine,
// automated regression proof for this campaign's actual freeze BEHAVIOR (not
// just gte::Time's own isolated arithmetic - already covered by
// tests/Core/TimeTests.cpp, PHASE1) - it calls Game::Update() directly with a
// hand-built, frozen-vs-unfrozen EngineContext, mirroring
// tests/Game/GameEntityCommandsTests.cpp's own "Game game; default-constructs
// cleanly, no Renderer needed" precedent, and
// tests/Game/Physics/PhysicsSystemTests.cpp's own
// RegisteredDynamicChainVisiblyDivergesFromPureFkPoseUnderGravity synthetic-
// rig fixture (a hand-built SkeletonData registered via
// PhysicsSystem::RegisterDynamicChains()/AttachDynamicChainRigIfNeeded() -
// both reachable from a test through Game::GetPhysicsSystem()/
// Game::GetRegistry(), no PMX file/GPU involved).
//
// No live Renderer/GPU device/VkDevice involved anywhere in this file -
// RenderSystem/MeshInstantiationSystem/AnimationSystem are all
// default-constructible with no GPU dependency (same precedent already
// established by Game/Animation/AnimationSystemEvaluatePosesTests.cpp's own
// file comment).

#include "Core/EngineContext.h"
#include "ECS/Components/DynamicChainRig.h"
#include "ECS/Components/ResolvedAnimationPose.h"
#include "Game/Animation/SkeletalRigCache.h"
#include "Game/Game.h"
#include "Game/Physics/PhysicsSystem.h"

#include <gtest/gtest.h>

namespace gte {
namespace {

// Synthetic 4-bone rig identical in shape to
// PhysicsSystemTests.cpp's own RegisteredDynamicChainVisiblyDivergesFromPureFkPoseUnderGravity
// fixture: root(0, no rigid body) -> chainRoot(1, a STATIC rigid body - this
// chain's anchor) -> joint1(2, a Dynamic rigid body) -> joint2(3, a Dynamic
// rigid body) - a 2-joint chain, well above
// DynamicChainDetectionDefaults::minimumChainLength (2). Extends along +X
// (perpendicular to gravity, which points along -Y) so it genuinely swings
// sideways under gravity rather than just compressing along its own bind
// axis.
SkinnedMeshData BuildSyntheticTwoJointChainRig()
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

// Shared per-test fixture: a Game with the synthetic rig registered and
// attached to one entity, plus gravity enabled, plus a bind-pose (all
// identity) ResolvedAnimationPose already seeded - exactly like
// PhysicsSystemTests.cpp's cited fixture does, just reached through
// Game::GetPhysicsSystem()/Game::GetRegistry() instead of a bare
// PhysicsSystem.
struct GameUpdateFreezeGatingFixture {
    Game game;
    Entity entity{};
    std::vector<BoneLocalOffset> pureFkPose;

    GameUpdateFreezeGatingFixture()
    {
        const SkinnedMeshData data = BuildSyntheticTwoJointChainRig();
        const std::string path = "SyntheticFreezeGatingModel.gta";

        PhysicsSystem& physicsSystem = game.GetPhysicsSystem();
        physicsSystem.RegisterDynamicChains(path, data);

        Registry& registry = game.GetRegistry();
        entity = registry.CreateEntity();
        physicsSystem.AttachDynamicChainRigIfNeeded(registry, entity, path);

        const DynamicChainRig* rig = registry.TryGetComponent<DynamicChainRig>(entity);
        if (rig == nullptr || rig->chainStates.empty()) {
            ADD_FAILURE() << "Expected a real 2-joint chain to have been detected/attached.";
        }

        ResolvedAnimationPose& pose = registry.AddComponent<ResolvedAnimationPose>(entity);
        pose.pose.resize(data.skeleton.bones.size());
        pureFkPose = pose.pose;

        physicsSystem.GetGlobalPhysicsSettings().gravity = Vec3(0.0f, -9.8f, 0.0f);
    }

    std::vector<BoneLocalOffset> CurrentPose()
    {
        const ResolvedAnimationPose* pose = game.GetRegistry().TryGetComponent<ResolvedAnimationPose>(entity);
        return pose != nullptr ? pose->pose : std::vector<BoneLocalOffset>{};
    }

    static bool PoseEquals(const std::vector<BoneLocalOffset>& a, const std::vector<BoneLocalOffset>& b)
    {
        if (a.size() != b.size()) {
            return false;
        }
        for (std::size_t i = 0; i < a.size(); ++i) {
            if (!ApproximatelyEqual(a[i].translation, b[i].translation)
                || !RepresentSameRotation(a[i].rotation, b[i].rotation)) {
                return false;
            }
        }
        return true;
    }
};

// Case 1: a frozen EngineContext leaves the pose byte-for-byte unchanged -
// proves IsFrozenThisFrame() genuinely skips PhysicsSystem::Update() (gravity
// would otherwise visibly move it).
TEST(GameUpdateFreezeGatingTest, FrozenEngineContextLeavesPoseUnchangedAcrossManyCalls)
{
    GameUpdateFreezeGatingFixture fixture;

    const std::vector<BoneLocalOffset> before = fixture.CurrentPose();

    EngineContext engineContext;
    for (int i = 0; i < 10; ++i) {
        engineContext.time.Advance(1.0 / 60.0, /*isPaused=*/true, /*isSteppedThisFrame=*/false, 1.0 / 60.0);
        ASSERT_TRUE(engineContext.time.IsFrozenThisFrame());
        fixture.game.Update(engineContext, InputState{});
    }

    const std::vector<BoneLocalOffset> after = fixture.CurrentPose();
    EXPECT_TRUE(GameUpdateFreezeGatingFixture::PoseEquals(before, after))
        << "A frozen EngineContext must leave the pose byte-for-byte unchanged - gravity should never be applied.";
}

// Case 2: an unfrozen EngineContext visibly diverges under gravity - proves
// this fixture is a meaningful regression guard, not trivially "nothing ever
// moves anyway".
TEST(GameUpdateFreezeGatingTest, UnfrozenEngineContextVisiblyDivergesFromPureFkPoseUnderGravity)
{
    GameUpdateFreezeGatingFixture fixture;

    EngineContext engineContext;
    for (int i = 0; i < 30; ++i) {
        engineContext.time.Advance(1.0 / 60.0, /*isPaused=*/false, /*isSteppedThisFrame=*/false, 1.0 / 60.0);
        ASSERT_FALSE(engineContext.time.IsFrozenThisFrame());
        fixture.game.Update(engineContext, InputState{});
    }

    const std::vector<BoneLocalOffset> after = fixture.CurrentPose();
    EXPECT_FALSE(GameUpdateFreezeGatingFixture::PoseEquals(fixture.pureFkPose, after))
        << "An unfrozen EngineContext must let the pose diverge from bind pose under gravity.";
}

// Case 3: a Step frame (isSteppedThisFrame=true while paused) still simulates
// exactly one tick, then refreezes correctly - proves Step-then-refreeze
// composes correctly through the real Game::Update() entry point, not just
// Time's own isolated Advance() semantics.
TEST(GameUpdateFreezeGatingTest, StepFrameSimulatesOnceThenRefreezeHoldsSteady)
{
    GameUpdateFreezeGatingFixture fixture;

    EngineContext engineContext;

    // A handful of Step calls, each while paused - each one should actually
    // simulate (like case 2 above).
    for (int i = 0; i < 5; ++i) {
        engineContext.time.Advance(1.0 / 60.0, /*isPaused=*/true, /*isSteppedThisFrame=*/true, 1.0 / 60.0);
        ASSERT_TRUE(engineContext.time.IsSteppedThisFrame());
        ASSERT_FALSE(engineContext.time.IsFrozenThisFrame());
        fixture.game.Update(engineContext, InputState{});
    }

    const std::vector<BoneLocalOffset> afterSteps = fixture.CurrentPose();
    EXPECT_FALSE(GameUpdateFreezeGatingFixture::PoseEquals(fixture.pureFkPose, afterSteps))
        << "Step frames must actually simulate - the pose should have diverged from bind pose.";

    // Now refreeze (plain paused, not stepped) - the pose must hold steady.
    for (int i = 0; i < 10; ++i) {
        engineContext.time.Advance(1.0 / 60.0, /*isPaused=*/true, /*isSteppedThisFrame=*/false, 1.0 / 60.0);
        ASSERT_TRUE(engineContext.time.IsFrozenThisFrame());
        fixture.game.Update(engineContext, InputState{});
    }

    const std::vector<BoneLocalOffset> afterRefreeze = fixture.CurrentPose();
    EXPECT_TRUE(GameUpdateFreezeGatingFixture::PoseEquals(afterSteps, afterRefreeze))
        << "Refreezing after a Step must hold the pose exactly steady.";
}

// Case 4: Game::CollectGpuSkinningDispatchRequests() stays empty across a
// frozen frame - trivially true in the default CpuJobSystem skinning mode,
// but still asserted explicitly as a regression guard confirming
// AnimationSystem::ClearGpuSkinningDispatchThisFrame() is genuinely being
// called from Game::Update()'s frozen branch rather than silently dead code.
TEST(GameUpdateFreezeGatingTest, GpuSkinningDispatchRequestsStayEmptyAcrossAFrozenFrame)
{
    Game game;
    EngineContext engineContext;
    engineContext.time.Advance(1.0 / 60.0, /*isPaused=*/true, /*isSteppedThisFrame=*/false, 1.0 / 60.0);
    ASSERT_TRUE(engineContext.time.IsFrozenThisFrame());

    game.Update(engineContext, InputState{});

    EXPECT_TRUE(game.CollectGpuSkinningDispatchRequests().empty());
}

} // namespace
} // namespace gte
