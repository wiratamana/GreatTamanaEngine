// Unit tests for DetectModelColliders() (src/Physics/ModelColliderDetection.h) -
// task_manager/verlet-integration-9, PHASE3: replaces the old fake
// single-sphere heuristic with a real reader of every
// RigidBodyMotionType::Static PMX rigid body (any of Sphere/Box/Capsule),
// precomputing each one's bind-pose-relative offset from its own tracked
// bone. Pure function - hand-built SkeletonData/PhysicsData fixtures, no
// ECS/GPU/file I/O involved, mirroring
// tests/Physics/DynamicChainDetectionTests.cpp's own fixture style.

#include "Physics/ModelColliderDetection.h"

#include "Animation/BoneWorldMatrixQuery.h"
#include "Math/Mat4.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <vector>

namespace gte {
namespace {

Bone MakeBone(Vec3 position, std::int32_t parentBoneIndex)
{
    Bone bone;
    bone.position = position;
    bone.parentBoneIndex = parentBoneIndex;
    return bone;
}

RigidBody MakeStaticBody(std::int32_t boneIndex, RigidBodyShape shape, Vec3 shapeSize, Vec3 translate, Vec3 rotateRadians = Vec3::Zero())
{
    RigidBody body;
    body.boneIndex = boneIndex;
    body.shape = shape;
    body.shapeSize = shapeSize;
    body.translate = translate;
    body.rotateRadians = rotateRadians;
    body.motionType = RigidBodyMotionType::Static;
    return body;
}

} // namespace

TEST(ModelColliderDetectionTests, StaticSphereBodyAtBoneOriginProducesAColliderWithZeroLocalOffset)
{
    SkeletonData skeleton;
    skeleton.bones.push_back(MakeBone(Vec3(1.0f, 2.0f, 3.0f), -1)); // 0

    PhysicsData physics;
    physics.rigidBodies.push_back(
        MakeStaticBody(0, RigidBodyShape::Sphere, Vec3(0.5f, 0.0f, 0.0f), Vec3(1.0f, 2.0f, 3.0f)));

    const std::vector<ModelColliderDefinition> colliders = DetectModelColliders(skeleton, &physics);

    ASSERT_EQ(colliders.size(), 1u);
    EXPECT_EQ(colliders[0].boneIndex, 0);
    EXPECT_EQ(colliders[0].shape, RigidBodyShape::Sphere);
    EXPECT_TRUE(ApproximatelyEqual(colliders[0].localOffsetPosition, Vec3::Zero()));
    EXPECT_TRUE(RepresentSameRotation(colliders[0].localOffsetRotation, Quat::Identity()));
}

TEST(ModelColliderDetectionTests, StaticBoxBodyOffsetFromItsBoneProducesTheCorrectLocalOffset)
{
    SkeletonData skeleton;
    skeleton.bones.push_back(MakeBone(Vec3(0.0f, 1.0f, 0.0f), -1)); // 0

    const Vec3 bodyTranslate(0.5f, 1.5f, -0.25f);
    const Vec3 bodyRotateRadians(0.2f, 0.4f, -0.1f);
    PhysicsData physics;
    physics.rigidBodies.push_back(
        MakeStaticBody(0, RigidBodyShape::Box, Vec3(0.3f, 0.4f, 0.5f), bodyTranslate, bodyRotateRadians));

    const std::vector<ModelColliderDefinition> colliders = DetectModelColliders(skeleton, &physics);

    ASSERT_EQ(colliders.size(), 1u);

    // Genuine round-trip proof: composing the bone's bind-pose world matrix
    // with the produced local offset must reproduce exactly the rigid
    // body's own authored bind-pose transform.
    const std::vector<BoneLocalOffset> bindPose;
    const Mat4 bindBoneWorld = ComputeBoneWorldMatrix(skeleton, bindPose, 0);
    const Quat expectedRotation
        = Quat::FromEulerDegrees(RadToDeg(bodyRotateRadians.x), RadToDeg(bodyRotateRadians.y), RadToDeg(bodyRotateRadians.z));
    const Mat4 reconstructed
        = bindBoneWorld * Mat4::TRS(colliders[0].localOffsetPosition, colliders[0].localOffsetRotation, Vec3::One());
    const Mat4 expected = Mat4::TRS(bodyTranslate, expectedRotation, Vec3::One());

    EXPECT_TRUE(ApproximatelyEqual(reconstructed.TransformPoint(Vec3::Zero()), expected.TransformPoint(Vec3::Zero()), 1e-4f));
    EXPECT_TRUE(RepresentSameRotation(Quat::FromMat4(reconstructed), Quat::FromMat4(expected), 1e-4f));
}

TEST(ModelColliderDetectionTests, DynamicAndDynamicAndBoneMergeBodiesAreNeverIncluded)
{
    SkeletonData skeleton;
    skeleton.bones.push_back(MakeBone(Vec3(0.0f, 0.0f, 0.0f), -1));

    PhysicsData physics;
    physics.rigidBodies.push_back(MakeStaticBody(0, RigidBodyShape::Sphere, Vec3(0.5f, 0.0f, 0.0f), Vec3::Zero()));
    RigidBody dynamicBody = MakeStaticBody(0, RigidBodyShape::Sphere, Vec3(0.5f, 0.0f, 0.0f), Vec3::Zero());
    dynamicBody.motionType = RigidBodyMotionType::Dynamic;
    physics.rigidBodies.push_back(dynamicBody);
    RigidBody mergeBody = MakeStaticBody(0, RigidBodyShape::Sphere, Vec3(0.5f, 0.0f, 0.0f), Vec3::Zero());
    mergeBody.motionType = RigidBodyMotionType::DynamicAndBoneMerge;
    physics.rigidBodies.push_back(mergeBody);

    const std::vector<ModelColliderDefinition> colliders = DetectModelColliders(skeleton, &physics);

    ASSERT_EQ(colliders.size(), 1u);
}

TEST(ModelColliderDetectionTests, UnattachedOrOutOfRangeBoneIndexIsExcluded)
{
    SkeletonData skeleton;
    skeleton.bones.push_back(MakeBone(Vec3(0.0f, 0.0f, 0.0f), -1));

    PhysicsData physics;
    physics.rigidBodies.push_back(MakeStaticBody(-1, RigidBodyShape::Sphere, Vec3(0.5f, 0.0f, 0.0f), Vec3::Zero()));
    physics.rigidBodies.push_back(MakeStaticBody(999, RigidBodyShape::Sphere, Vec3(0.5f, 0.0f, 0.0f), Vec3::Zero()));

    const std::vector<ModelColliderDefinition> colliders = DetectModelColliders(skeleton, &physics);

    EXPECT_TRUE(colliders.empty());
}

TEST(ModelColliderDetectionTests, DegenerateShapesOfEachKindAreExcluded)
{
    SkeletonData skeleton;
    skeleton.bones.push_back(MakeBone(Vec3(0.0f, 0.0f, 0.0f), -1));

    PhysicsData physics;
    physics.rigidBodies.push_back(MakeStaticBody(0, RigidBodyShape::Sphere, Vec3(0.0f, 0.0f, 0.0f), Vec3::Zero()));
    physics.rigidBodies.push_back(MakeStaticBody(0, RigidBodyShape::Box, Vec3(0.0f, 0.0f, 0.0f), Vec3::Zero()));
    physics.rigidBodies.push_back(MakeStaticBody(0, RigidBodyShape::Capsule, Vec3(0.0f, 1.0f, 0.0f), Vec3::Zero()));
    // A capsule with a non-positive height but a positive radius IS still valid.
    physics.rigidBodies.push_back(MakeStaticBody(0, RigidBodyShape::Capsule, Vec3(0.5f, -1.0f, 0.0f), Vec3::Zero()));

    const std::vector<ModelColliderDefinition> colliders = DetectModelColliders(skeleton, &physics);

    ASSERT_EQ(colliders.size(), 1u);
    EXPECT_EQ(colliders[0].shape, RigidBodyShape::Capsule);
    EXPECT_NEAR(colliders[0].shapeSize.x, 0.5f, 1e-4f);
}

TEST(ModelColliderDetectionTests, NullPhysicsDataDetectsNothing)
{
    SkeletonData skeleton;
    skeleton.bones.push_back(MakeBone(Vec3(0.0f, 0.0f, 0.0f), -1));

    const std::vector<ModelColliderDefinition> colliders = DetectModelColliders(skeleton, nullptr);

    EXPECT_TRUE(colliders.empty());
}

TEST(ModelColliderDetectionTests, ResultIsSortedByAscendingBoneIndex)
{
    SkeletonData skeleton;
    skeleton.bones.push_back(MakeBone(Vec3(0.0f, 0.0f, 0.0f), -1)); // 0
    skeleton.bones.push_back(MakeBone(Vec3(1.0f, 0.0f, 0.0f), -1)); // 1
    skeleton.bones.push_back(MakeBone(Vec3(2.0f, 0.0f, 0.0f), -1)); // 2

    PhysicsData physics;
    physics.rigidBodies.push_back(MakeStaticBody(2, RigidBodyShape::Sphere, Vec3(0.5f, 0.0f, 0.0f), Vec3(2.0f, 0.0f, 0.0f)));
    physics.rigidBodies.push_back(MakeStaticBody(0, RigidBodyShape::Sphere, Vec3(0.5f, 0.0f, 0.0f), Vec3(0.0f, 0.0f, 0.0f)));
    physics.rigidBodies.push_back(MakeStaticBody(1, RigidBodyShape::Sphere, Vec3(0.5f, 0.0f, 0.0f), Vec3(1.0f, 0.0f, 0.0f)));

    const std::vector<ModelColliderDefinition> colliders = DetectModelColliders(skeleton, &physics);

    ASSERT_EQ(colliders.size(), 3u);
    EXPECT_TRUE(std::is_sorted(colliders.begin(), colliders.end(),
        [](const ModelColliderDefinition& a, const ModelColliderDefinition& b) { return a.boneIndex < b.boneIndex; }));
    EXPECT_EQ(colliders[0].boneIndex, 0);
    EXPECT_EQ(colliders[1].boneIndex, 1);
    EXPECT_EQ(colliders[2].boneIndex, 2);
}

} // namespace gte
