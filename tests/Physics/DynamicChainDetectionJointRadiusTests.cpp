// Unit tests for DetectDynamicChains()'s PHASE2 addition
// (task_manager/verlet-integration-10,
// PHASE2_JOINT_OWN_RIGID_BODY_SHAPE_AS_COLLISION_RADIUS.md): each joint's own
// matched Dynamic/DynamicAndBoneMerge RigidBody shape/shapeSize is now also
// derived into DynamicJointSettings::collisionRadius (Step G), mirroring
// this file's own sibling DynamicChainDetectionTests.cpp's fixture style
// (a local, richer MakeRigidBody()-equivalent helper, since the existing
// helper over there only has mass/linearDamping parameters). Pure function -
// hand-built SkeletonData/PhysicsData fixtures, no ECS/GPU/file I/O involved.

#include "Physics/DynamicChainDetection.h"

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

RigidBody MakeRigidBodyWithShape(
    std::int32_t boneIndex, RigidBodyMotionType motionType, RigidBodyShape shape, Vec3 shapeSize)
{
    RigidBody body;
    body.boneIndex = boneIndex;
    body.motionType = motionType;
    body.shape = shape;
    body.shapeSize = shapeSize;
    return body;
}

Joint MakeJoint(std::int32_t rigidBodyAIndex, std::int32_t rigidBodyBIndex)
{
    Joint joint;
    joint.rigidBodyAIndex = rigidBodyAIndex;
    joint.rigidBodyBIndex = rigidBodyBIndex;
    return joint;
}

// Shared fixture: bone 0 = static anchor, bone 1 = a single dynamic joint
// whose own RigidBody is given whatever shape/shapeSize the caller wants.
DynamicChainDetectionResult DetectWithSingleJointShape(RigidBodyShape shape, Vec3 shapeSize)
{
    SkeletonData skeleton;
    skeleton.bones.push_back(MakeBone(Vec3(0.0f, 0.0f, 0.0f), -1)); // 0 - anchor
    skeleton.bones.push_back(MakeBone(Vec3(0.0f, 1.0f, 0.0f), 0)); // 1

    PhysicsData physics;
    physics.rigidBodies.push_back(MakeRigidBodyWithShape(0, RigidBodyMotionType::Static, RigidBodyShape::Sphere, Vec3::Zero()));
    physics.rigidBodies.push_back(MakeRigidBodyWithShape(1, RigidBodyMotionType::Dynamic, shape, shapeSize));
    physics.joints.push_back(MakeJoint(0, 1));

    // minimumChainLength defaults to 2 (DynamicChainDetection.h) - this
    // fixture is deliberately a single-joint chain, so it must be lowered to
    // 1 here or the whole chain would be silently discarded by Step G.
    DynamicChainDetectionDefaults defaults{};
    defaults.minimumChainLength = 1;
    return DetectDynamicChains(skeleton, &physics, defaults);
}

} // namespace

TEST(DynamicChainDetectionJointRadiusTests, SphereShapedDynamicBodyProducesCollisionRadiusEqualToItsOwnAuthoredRadius)
{
    const DynamicChainDetectionResult detection = DetectWithSingleJointShape(RigidBodyShape::Sphere, Vec3(0.4f, 0.0f, 0.0f));

    ASSERT_EQ(detection.chains.size(), 1u);
    ASSERT_EQ(detection.chains[0].jointSettings.size(), 1u);
    EXPECT_NEAR(detection.chains[0].jointSettings[0].collisionRadius, 0.4f, 1e-5f);
}

TEST(DynamicChainDetectionJointRadiusTests, CapsuleShapedDynamicBodyProducesCollisionRadiusEqualToItsOwnRadiusIgnoringHeight)
{
    // x = radius (0.3), y = FULL height (5.0, deliberately large) - the
    // height must NOT influence the derived radius at all (see
    // DeriveJointCollisionRadius()'s own doc comment for why).
    const DynamicChainDetectionResult detection = DetectWithSingleJointShape(RigidBodyShape::Capsule, Vec3(0.3f, 5.0f, 0.0f));

    ASSERT_EQ(detection.chains.size(), 1u);
    ASSERT_EQ(detection.chains[0].jointSettings.size(), 1u);
    EXPECT_NEAR(detection.chains[0].jointSettings[0].collisionRadius, 0.3f, 1e-5f);
}

TEST(DynamicChainDetectionJointRadiusTests, BoxShapedDynamicBodyProducesCollisionRadiusEqualToItsSmallestHalfExtent)
{
    const DynamicChainDetectionResult detection = DetectWithSingleJointShape(RigidBodyShape::Box, Vec3(0.5f, 0.2f, 0.8f));

    ASSERT_EQ(detection.chains.size(), 1u);
    ASSERT_EQ(detection.chains[0].jointSettings.size(), 1u);
    EXPECT_NEAR(detection.chains[0].jointSettings[0].collisionRadius, 0.2f, 1e-5f); // smallest of 0.5/0.2/0.8.
}

TEST(DynamicChainDetectionJointRadiusTests, DegenerateZeroSizedShapeClampsCollisionRadiusToZeroNeverNegative)
{
    // A degenerate/zero-authored shape (e.g. a malformed .pmx) must never
    // produce a non-positive-but-nonzero or negative collisionRadius - see
    // DeriveJointCollisionRadius()'s own "clamped to exactly 0.0f" doc
    // comment. Box's smallest half-extent here is a negative value, which
    // must still clamp to exactly 0.0f, not propagate as negative.
    const DynamicChainDetectionResult detection = DetectWithSingleJointShape(RigidBodyShape::Box, Vec3(0.5f, -0.2f, 0.8f));

    ASSERT_EQ(detection.chains.size(), 1u);
    ASSERT_EQ(detection.chains[0].jointSettings.size(), 1u);
    EXPECT_NEAR(detection.chains[0].jointSettings[0].collisionRadius, 0.0f, 1e-5f);
}

TEST(DynamicChainDetectionJointRadiusTests, EachJointInAMultiJointChainDerivesItsOwnIndependentRadius)
{
    // 0 = static anchor, 1/2/3 = a straight 3-joint FK chain, each with a
    // DIFFERENT shape/shapeSize on its own RigidBody - proves Step G's own
    // per-joint loop derives each joint's radius independently rather than
    // e.g. accidentally reusing the first match for every joint.
    SkeletonData skeleton;
    skeleton.bones.push_back(MakeBone(Vec3(0.0f, 0.0f, 0.0f), -1)); // 0 - anchor
    skeleton.bones.push_back(MakeBone(Vec3(0.0f, 1.0f, 0.0f), 0)); // 1
    skeleton.bones.push_back(MakeBone(Vec3(0.0f, 2.0f, 0.0f), 1)); // 2
    skeleton.bones.push_back(MakeBone(Vec3(0.0f, 3.0f, 0.0f), 2)); // 3

    PhysicsData physics;
    physics.rigidBodies.push_back(MakeRigidBodyWithShape(0, RigidBodyMotionType::Static, RigidBodyShape::Sphere, Vec3::Zero()));
    physics.rigidBodies.push_back(
        MakeRigidBodyWithShape(1, RigidBodyMotionType::Dynamic, RigidBodyShape::Sphere, Vec3(0.1f, 0.0f, 0.0f)));
    physics.rigidBodies.push_back(
        MakeRigidBodyWithShape(2, RigidBodyMotionType::Dynamic, RigidBodyShape::Capsule, Vec3(0.2f, 3.0f, 0.0f)));
    physics.rigidBodies.push_back(
        MakeRigidBodyWithShape(3, RigidBodyMotionType::Dynamic, RigidBodyShape::Box, Vec3(0.9f, 0.3f, 0.6f)));
    physics.joints.push_back(MakeJoint(0, 1));
    physics.joints.push_back(MakeJoint(1, 2));
    physics.joints.push_back(MakeJoint(2, 3));

    const DynamicChainDetectionDefaults defaults{};
    const DynamicChainDetectionResult detection = DetectDynamicChains(skeleton, &physics, defaults);

    ASSERT_EQ(detection.chains.size(), 1u);
    ASSERT_EQ(detection.chains[0].jointSettings.size(), 3u);
    EXPECT_NEAR(detection.chains[0].jointSettings[0].collisionRadius, 0.1f, 1e-5f);
    EXPECT_NEAR(detection.chains[0].jointSettings[1].collisionRadius, 0.2f, 1e-5f);
    EXPECT_NEAR(detection.chains[0].jointSettings[2].collisionRadius, 0.3f, 1e-5f); // smallest of 0.9/0.3/0.6.
}

} // namespace gte
