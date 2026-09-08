// Unit tests for task_manager/verlet-integration-10, PHASE1 - proves
// DetectModelColliders() (src/Physics/ModelColliderDetection.h) now copies a
// Static RigidBody's own group/collisionGroupMask VERBATIM into the detected
// ModelColliderDefinition's group/collisionMask fields. Mirrors
// tests/Physics/ModelColliderDetectionTests.cpp's own fixture style
// (MakeBone()/MakeStaticBody() pattern).

#include "Physics/ModelColliderDetection.h"

#include <gtest/gtest.h>

#include <cstdint>

namespace gte {
namespace {

Bone MakeBone(Vec3 position, std::int32_t parentBoneIndex)
{
    Bone bone;
    bone.position = position;
    bone.parentBoneIndex = parentBoneIndex;
    return bone;
}

RigidBody MakeStaticBody(std::int32_t boneIndex, RigidBodyShape shape, Vec3 shapeSize, Vec3 translate)
{
    RigidBody body;
    body.boneIndex = boneIndex;
    body.shape = shape;
    body.shapeSize = shapeSize;
    body.translate = translate;
    body.motionType = RigidBodyMotionType::Static;
    return body;
}

} // namespace

TEST(ModelColliderDetectionGroupMaskTests, StaticRigidBodyGroupAndMaskAreCopiedVerbatimIntoTheDetectedCollider)
{
    SkeletonData skeleton;
    skeleton.bones.push_back(MakeBone(Vec3(0.0f, 0.0f, 0.0f), -1));

    PhysicsData physics;
    RigidBody body = MakeStaticBody(0, RigidBodyShape::Sphere, Vec3(0.5f, 0.0f, 0.0f), Vec3::Zero());
    body.group = 7;
    body.collisionGroupMask = 0b0000000010100000;
    physics.rigidBodies.push_back(body);

    const std::vector<ModelColliderDefinition> colliders = DetectModelColliders(skeleton, &physics);

    ASSERT_EQ(colliders.size(), 1u);
    EXPECT_EQ(colliders[0].group, 7);
    EXPECT_EQ(colliders[0].collisionMask, static_cast<std::uint16_t>(0b0000000010100000));
}

// Verbatim copy, not the Collider-side "default to 0xFFFF" convenience -
// ModelColliderDefinition (the real detection pipeline) always copies
// whatever the PMX file actually said, even a literal collisionGroupMask ==
// 0 (which in real PMX authoring data would mean "collides with nothing" - a
// legitimate, if unusual, authored choice this engine must faithfully
// preserve, never silently override). This test proves PHASE1 did not
// accidentally change ModelColliderDefinition::collisionMask's own semantics
// away from "verbatim copy" into "always 0xFFFF regardless of the source
// RigidBody."
TEST(ModelColliderDetectionGroupMaskTests, DefaultZeroGroupAndDefaultZeroMaskAreStillCopiedVerbatimNotSilentlyReplaced)
{
    SkeletonData skeleton;
    skeleton.bones.push_back(MakeBone(Vec3(0.0f, 0.0f, 0.0f), -1));

    PhysicsData physics;
    // RigidBody's own actual default-constructed group/collisionGroupMask -
    // both 0, never explicitly set by this test.
    RigidBody body = MakeStaticBody(0, RigidBodyShape::Sphere, Vec3(0.5f, 0.0f, 0.0f), Vec3::Zero());
    ASSERT_EQ(body.group, 0);
    ASSERT_EQ(body.collisionGroupMask, 0);
    physics.rigidBodies.push_back(body);

    const std::vector<ModelColliderDefinition> colliders = DetectModelColliders(skeleton, &physics);

    ASSERT_EQ(colliders.size(), 1u);
    EXPECT_EQ(colliders[0].group, 0);
    EXPECT_EQ(colliders[0].collisionMask, 0)
        << "ModelColliderDefinition must copy the source RigidBody's own collisionGroupMask VERBATIM, "
           "never silently default it to 0xFFFF - that convenience lives only on Collider/DynamicJointSettings.";
}

} // namespace gte
