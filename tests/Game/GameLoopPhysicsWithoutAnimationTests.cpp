// End-to-end proof for task_manager/verlet-integration-7/
// PHASE1_BASELINE_RESOLVED_POSE_FOR_NONANIMATED_PHYSICS_RIGS.md (Step 3.4) -
// wires AnimationSystem::EvaluatePoses() -> PhysicsSystem::Update() back to
// back, by hand, in the EXACT SAME fixed order Game::Update() itself uses
// (see Game.cpp), for an entity carrying ONLY a DynamicChainRig - no
// SkeletalAnimator component ever exists on it, and Game::PlayAnimationOnEntity()/
// AnimationSystem::Play() is never called at all. This is the literal,
// automated proof that "T-pose actually simulates physics" now holds, wired
// exactly the way the real engine calls it - no GPU/SDL/ImGui dependency
// beyond a plain Registry, Tier 1.

#include "Game/Animation/AnimationSystem.h"
#include "Game/Physics/PhysicsSystem.h"

#include "ECS/Components/DynamicChainRig.h"
#include "ECS/Components/ResolvedAnimationPose.h"
#include "ECS/Components/SkeletalAnimator.h"
#include "ECS/Registry.h"
#include "Game/Animation/SkeletalRigCache.h"
#include "Game/Instantiation/MeshInstantiationSystem.h"
#include "Game/RenderSystem.h"

#include <gtest/gtest.h>

namespace gte {
namespace {

TEST(GameLoopPhysicsWithoutAnimationTests,
    ADynamicChainRigOnlyEntityVisiblySagsUnderGravityAcrossManyFramesWithNoAnimationEverPlayed)
{
    RenderSystem renderSystem;
    MeshInstantiationSystem meshInstantiationSystem(renderSystem);
    AnimationSystem animationSystem(renderSystem, meshInstantiationSystem);
    PhysicsSystem physicsSystem;
    Registry registry;

    // Synthetic 4-bone rig - same shape as
    // PhysicsSystemTests.cpp's own RegisteredDynamicChainVisiblyDivergesFromPureFkPoseUnderGravity:
    // root(0, no rigid body) -> chainRoot(1, a STATIC anchor rigid body) ->
    // joint1(2, Dynamic) -> joint2(3, Dynamic), extending along +X
    // (perpendicular to gravity, which points along -Y) so the chain can
    // genuinely swing sideways instead of only compressing/stretching along
    // its own bind axis.
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

    const std::string path = "SyntheticNonAnimatedDynamicChainModel.gta";

    // Both registration calls happen at the SAME hand-off site in the real
    // engine (Game::CreateMeshEntityFromGtaFile()) - mirrored here exactly.
    animationSystem.RegisterSkinnedMesh(path, data);
    physicsSystem.RegisterDynamicChains(path, data);

    const Entity entity = registry.CreateEntity();
    physicsSystem.AttachDynamicChainRigIfNeeded(registry, entity, path);

    const DynamicChainRig* rig = registry.TryGetComponent<DynamicChainRig>(entity);
    ASSERT_NE(rig, nullptr) << "A real 2-joint chain should have been detected/attached.";
    ASSERT_EQ(rig->chainStates.size(), 1u);

    // Deliberately NEVER call animationSystem.Play(...) for this entity - no
    // SkeletalAnimator component ever exists on it, by construction. This is
    // the whole point of this test: physics must still run.
    ASSERT_FALSE(registry.HasComponent<SkeletalAnimator>(entity));

    physicsSystem.GetGlobalPhysicsSettings().gravity = Vec3(0.0f, -9.8f, 0.0f);

    // Exactly Game::Update()'s own two-call sequence (its third call,
    // SkinAndUpload(), is Phase 2's concern and irrelevant to this
    // pose-level assertion).
    for (int i = 0; i < 60; ++i) {
        animationSystem.EvaluatePoses(registry, 1.0 / 60.0);
        physicsSystem.Update(registry, 1.0 / 60.0);
    }

    ASSERT_TRUE(registry.HasComponent<ResolvedAnimationPose>(entity));
    const ResolvedAnimationPose* resolvedPose = registry.TryGetComponent<ResolvedAnimationPose>(entity);
    ASSERT_NE(resolvedPose, nullptr);
    ASSERT_EQ(resolvedPose->pose.size(), data.skeleton.bones.size());

    // Ground truth: a fresh, pure bind pose (all-default BoneLocalOffset) -
    // exactly what EvaluatePoses()'s own baseline pass alone would have
    // produced with no physics running at all.
    const std::vector<BoneLocalOffset> pureBindPose(data.skeleton.bones.size());

    bool anyDiffers = false;
    for (std::size_t i = 0; i < pureBindPose.size(); ++i) {
        if (!RepresentSameRotation(resolvedPose->pose[i].rotation, pureBindPose[i].rotation)
            || !ApproximatelyEqual(resolvedPose->pose[i].translation, pureBindPose[i].translation)) {
            anyDiffers = true;
            break;
        }
    }
    EXPECT_TRUE(anyDiffers) << "PhysicsSystem::Update() never changed the pose despite gravity and a registered "
                               "dynamic chain on a NEVER-animated entity - T-pose physics simulation is not working.";
}

} // namespace
} // namespace gte
