#include "PhysicsTestChainFixture.h"

#include <gtest/gtest.h>

namespace gte {
namespace {

using physicstest::BuildChainModel;
using physicstest::ChainScene;
using physicstest::kAnchorBone;

// Chain whose first joint (bone 2 at x = 2) starts at the centre of a Static sphere on the root bone.
SkinnedMeshData BuildOverlappingModel()
{
    SkinnedMeshData data = BuildChainModel(2, 1.0f);
    PhysicsData& physics = *data.physics;
    for (RigidBody& body : physics.rigidBodies) {
        body.collisionGroupMask = 0xFFFF;
    }

    RigidBody collider;
    collider.boneIndex = 0;
    collider.motionType = RigidBodyMotionType::Static;
    collider.shape = RigidBodyShape::Sphere;
    collider.shapeSize = Vec3(0.5f, 0.0f, 0.0f);
    collider.translate = Vec3(2.0f, 0.0f, 0.0f);
    collider.collisionGroupMask = 0xFFFF;
    physics.rigidBodies.push_back(collider);
    return data;
}

float FirstJointDisplacementAfterOneFrame(bool fitEnabled)
{
    ChainScene scene(BuildOverlappingModel());
    GlobalPhysicsSettings& settings = scene.physics.GetGlobalPhysicsSettings();
    settings.gravity = Vec3::Zero();
    settings.bendLimitsEnabled = false;
    settings.interpolateOutput = false;
    settings.collisionEnabled = true;
    settings.useCollisionFit = fitEnabled;

    scene.SetAnimatedAnchorX(0.0f);
    scene.physics.Update(scene.registry, 1.0 / 60.0);
    return Length(scene.BoneWorld(kAnchorBone + 1) - scene.AnimatedWorld(kAnchorBone + 1, 0.0f));
}

TEST(PhysicsSystemCollisionFitTests, BindPoseOverlapIsFittedAtRegistration)
{
    ChainScene scene(BuildOverlappingModel());
    const DynamicChainRigCache::ModelEntry* model = scene.physics.GetDynamicChainRigCache().TryGet(scene.path);
    ASSERT_NE(model, nullptr);
    ASSERT_FALSE(model->colliders.empty());
    ASSERT_EQ(model->chains.size(), 1u);
    EXPECT_FALSE(model->chains[0].jointSettings[0].ignoredColliderIndices.empty());
}

TEST(PhysicsSystemCollisionFitTests, FittedJointIsNotPushedOnTheFirstFrame)
{
    EXPECT_LT(FirstJointDisplacementAfterOneFrame(true), 1e-4f);
}

TEST(PhysicsSystemCollisionFitTests, UnfittedJointIsPushedOutOfTheCollider)
{
    EXPECT_GT(FirstJointDisplacementAfterOneFrame(false), 0.05f);
}

} // namespace
} // namespace gte
