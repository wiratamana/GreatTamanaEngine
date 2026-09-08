// Unit tests for task_manager/verlet-integration-8 - the anchor-bone
// rigidity fix (src/Physics/BoneChainPhysicsResolver.cpp, Phase 1). This is
// the full end-to-end, PhysicsSystem::Update()-level regression test for the
// exact bug reported in task_manager/verlet-integration-7/
// INVESTIGATION_FULL_BODY_DISTORTION_ON_TRANSFORM_DRAG.md: a chain anchor
// that is ALSO a real, non-participating FK body bone (a stand-in for MMD's
// own 下半身/legs), with several accessory joints hubbed directly off that
// same anchor (a stand-in for the reported model's ~23-25 "(root child)"
// hair/skirt strands) - the "leg" bone's own local pose must never move, no
// matter how the hub accessories simulate or how the entity's own Transform
// is dragged. All Tier 1 - no GPU/Renderer/ImGui dependency beyond a plain
// Registry, mirroring PhysicsSystemFreezeAndCulpritFTests.cpp's/
// PhysicsSystemWorldSpaceRootMotionTests.cpp's own conventions exactly.

#include "Game/Physics/PhysicsSystem.h"

#include "Animation/BoneWorldMatrixQuery.h"
#include "ECS/Components/DynamicChainRig.h"
#include "ECS/Components/ResolvedAnimationPose.h"
#include "ECS/Components/Transform.h"
#include "ECS/Registry.h"
#include "Game/Animation/SkeletalRigCache.h"
#include "Math/Mat4.h"
#include "Math/Quat.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <string>
#include <vector>

namespace gte {
namespace {

// Synthetic rig: root(0, no rigid body) -> anchor(1, STATIC, e.g.
// "lower_body") -> leg(2, NO rigid body at all - an ordinary, non-
// participating FK body bone, exactly like a real character's thigh bone
// descending from 下半身) ; anchor(1) is ALSO the direct parent of FIVE
// independent Dynamic accessory bones (3..7, e.g. hair/skirt strand roots),
// each with its own Joint back to the anchor's rigid body - reproducing the
// reported model's own "(root child)" hub shape at a manageable scale.
SkinnedMeshData BuildHubAnchorWithLegFixture()
{
    SkinnedMeshData data;
    Bone root;
    root.name = "root";
    root.position = Vec3(0.0f, 0.0f, 0.0f);
    root.parentBoneIndex = -1;
    data.skeleton.bones.push_back(root); // 0

    Bone anchor;
    anchor.name = "lower_body";
    anchor.position = Vec3(0.0f, 1.0f, 0.0f);
    anchor.parentBoneIndex = 0;
    data.skeleton.bones.push_back(anchor); // 1

    Bone leg;
    leg.name = "leg";
    leg.position = Vec3(0.0f, -0.5f, 0.0f); // hangs below the anchor.
    leg.parentBoneIndex = 1;
    data.skeleton.bones.push_back(leg); // 2

    PhysicsData physics;
    RigidBody anchorBody;
    anchorBody.boneIndex = 1;
    anchorBody.motionType = RigidBodyMotionType::Static;
    physics.rigidBodies.push_back(anchorBody); // 0

    for (int k = 0; k < 5; ++k) {
        Bone accessory;
        accessory.name = "accessory_" + std::to_string(k);
        accessory.position = Vec3(0.0f, 1.0f, 0.0f); // bind-identical to the anchor, like a real hair root.
        accessory.parentBoneIndex = 1;
        data.skeleton.bones.push_back(accessory); // 3..7

        RigidBody accessoryBody;
        accessoryBody.boneIndex = 3 + k;
        accessoryBody.motionType = RigidBodyMotionType::Dynamic;
        physics.rigidBodies.push_back(accessoryBody);

        Joint anchorToAccessory;
        anchorToAccessory.rigidBodyAIndex = 0;
        anchorToAccessory.rigidBodyBIndex = static_cast<std::int32_t>(physics.rigidBodies.size() - 1);
        physics.joints.push_back(anchorToAccessory);
    }

    data.physics = std::move(physics);
    return data;
}

} // namespace

TEST(PhysicsSystemAnchorRigidityRegressionTests, LegBoneNeverMovesWhileHubAccessoriesSimulateContinuously)
{
    PhysicsSystem physicsSystem;
    Registry registry;
    physicsSystem.GetGlobalPhysicsSettings().gravity = Vec3(0.0f, -9.8f, 0.0f);

    const SkinnedMeshData data = BuildHubAnchorWithLegFixture();
    physicsSystem.RegisterDynamicChains("HubAnchorLegModel.gta", data);

    const Entity entity = registry.CreateEntity();
    physicsSystem.AttachDynamicChainRigIfNeeded(registry, entity, "HubAnchorLegModel.gta");
    ASSERT_TRUE(registry.HasComponent<DynamicChainRig>(entity));

    ResolvedAnimationPose& pose = registry.AddComponent<ResolvedAnimationPose>(entity);
    pose.pose.resize(data.skeleton.bones.size());

    const Vec3 legWorldBindPose = ComputeBoneWorldMatrix(data.skeleton, pose.pose, 2).TransformPoint(Vec3::Zero());

    for (int i = 0; i < 300; ++i) {
        physicsSystem.Update(registry, 1.0 / 60.0);
        const ResolvedAnimationPose* current = registry.TryGetComponent<ResolvedAnimationPose>(entity);
        ASSERT_NE(current, nullptr);
        const Vec3 legWorldNow = ComputeBoneWorldMatrix(data.skeleton, current->pose, 2).TransformPoint(Vec3::Zero());
        EXPECT_TRUE(ApproximatelyEqual(legWorldNow, legWorldBindPose, 1e-4f))
            << "Frame " << i << ": the leg bone moved even though it is not a member of any "
               "DynamicChainDefinition - the reported 'whole body looks ragdoll-simulated' bug has regressed.";
    }
}

TEST(PhysicsSystemAnchorRigidityRegressionTests, LegBoneNeverMovesAcrossATransformDrag)
{
    // Reproduces the user's own literal reported reproduction steps:
    // dragging the entity's Transform.position must never visibly move the
    // rigid body (here: the "leg" bone) - only the hub accessories may lag.
    PhysicsSystem physicsSystem;
    Registry registry;
    physicsSystem.GetGlobalPhysicsSettings().gravity = Vec3(0.0f, -9.8f, 0.0f);

    const SkinnedMeshData data = BuildHubAnchorWithLegFixture();
    physicsSystem.RegisterDynamicChains("HubAnchorLegDragModel.gta", data);

    const Entity entity = registry.CreateEntity();
    physicsSystem.AttachDynamicChainRigIfNeeded(registry, entity, "HubAnchorLegDragModel.gta");
    Transform& transform = registry.AddComponent<Transform>(entity);

    ResolvedAnimationPose& pose = registry.AddComponent<ResolvedAnimationPose>(entity);
    pose.pose.resize(data.skeleton.bones.size());

    for (int i = 0; i < 30; ++i) {
        physicsSystem.Update(registry, 1.0 / 60.0);
    }

    // Drag Position.x, exactly like the reported Inspector screenshot.
    transform.position.x = -1.270f;
    physicsSystem.Update(registry, 1.0 / 60.0);

    const ResolvedAnimationPose* after = registry.TryGetComponent<ResolvedAnimationPose>(entity);
    ASSERT_NE(after, nullptr);

    // The leg's LOCAL (bone-space) pose must be untouched by the drag - only
    // its WORLD position should move, and only by rigidly following the
    // entity's own Transform (exactly like every other rigid FK bone would).
    EXPECT_TRUE(RepresentSameRotation(after->pose[1].rotation, Quat::Identity()))
        << "Anchor bone rotation must stay untouched even across a live Transform drag.";
    EXPECT_TRUE(ApproximatelyEqual(after->pose[1].translation, Vec3::Zero()));
    EXPECT_TRUE(RepresentSameRotation(after->pose[2].rotation, Quat::Identity()))
        << "Leg bone's own local pose must be completely unaffected by the drag.";
    EXPECT_TRUE(ApproximatelyEqual(after->pose[2].translation, Vec3::Zero()));
}

} // namespace gte
