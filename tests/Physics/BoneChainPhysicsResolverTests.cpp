// Unit tests for ApplyDynamicChainPhysicsToPose
// (src/Physics/BoneChainPhysicsResolver.h) - the position -> rotation
// bridge that turns a chain of already-simulated Verlet particle positions
// back into BoneLocalOffset rotations the existing FK pipeline already
// understands (verlet-integration-1 campaign,
// PHASE2_BONE_CHAIN_PHYSICS_BRIDGE.md).
//
// IMPORTANT: per this file's own header comment ("IMPORTANT DESIGN NOTE"),
// a bone's own rotation can never move its own world position - only its
// DESCENDANTS'. So landing joint `i` at its simulated target actually
// rewrites joint i's PARENT bone's rotation (rootBoneIndex for the first
// joint, or the previous joint for every one after it) - these tests assert
// against the PARENT's pose entry, never the joint's own, for exactly that
// reason.

#include "Physics/BoneChainPhysicsResolver.h"

#include "Animation/BoneWorldMatrixQuery.h"

#include <gtest/gtest.h>

namespace gte {
namespace {

// A minimal 2-bone rig: root (bind position at the origin) -> joint (bind
// position one unit straight up). rootBoneIndex is the root bone itself
// (the ONLY bone whose rotation can move the joint bone's position);
// jointBoneIndices holds only the joint bone.
SkeletonData BuildTwoBoneSkeleton()
{
    SkeletonData skeleton;

    Bone root;
    root.name = "root";
    root.position = Vec3(0.0f, 0.0f, 0.0f);
    root.parentBoneIndex = -1;
    skeleton.bones.push_back(root); // index 0

    Bone joint;
    joint.name = "joint";
    joint.position = Vec3(0.0f, 1.0f, 0.0f);
    joint.parentBoneIndex = 0;
    skeleton.bones.push_back(joint); // index 1

    return skeleton;
}

DynamicChainDefinition BuildSingleJointDefinition()
{
    DynamicChainDefinition definition;
    definition.rootBoneIndex = 0;
    definition.jointBoneIndices = { 1 };
    definition.jointSettings = { DynamicJointSettings{} };
    definition.restLengths = { 1.0f };
    return definition;
}

} // namespace

// Genuine round-trip correctness test (not just "it compiles"): move the
// joint's simulated target SIDEWAYS by the SAME distance as its own bind
// length, confirm the produced rotation (written into the ROOT bone - the
// joint's PARENT, see this file's own header comment) actually lands the
// joint bone there when fed back through ComputeBoneWorldMatrix().
TEST(BoneChainPhysicsResolverTests, ProducedRotationLandsBoneAtRequestedSimulatedPosition)
{
    SkeletonData skeleton = BuildTwoBoneSkeleton();
    DynamicChainDefinition definition = BuildSingleJointDefinition();

    std::vector<BoneLocalOffset> pose(skeleton.bones.size()); // all-identity (bind pose).
    // Bind offset from root to joint has length 1 along +Y - request the
    // joint instead point along +X, same length, so only a pure rotation
    // (no distance mismatch) is needed to land exactly on the target.
    const std::vector<Vec3> simulatedPositions = { Vec3(1.0f, 0.0f, 0.0f) };

    ApplyDynamicChainPhysicsToPose(skeleton, definition, simulatedPositions, pose);

    EXPECT_FALSE(RepresentSameRotation(pose[0].rotation, Quat::Identity()))
        << "Root bone's rotation (the joint's PARENT) was not touched by the physics pass.";
    EXPECT_TRUE(RepresentSameRotation(pose[1].rotation, Quat::Identity()))
        << "Joint bone's OWN rotation must stay untouched - only its parent's rotation can move it.";

    const Mat4 jointWorld = ComputeBoneWorldMatrix(skeleton, pose, 1);
    const Vec3 landedPosition = jointWorld.TransformPoint(Vec3::Zero());
    EXPECT_TRUE(ApproximatelyEqual(landedPosition, simulatedPositions[0], 1e-4f))
        << "Bone did not land at the requested simulated position after ApplyDynamicChainPhysicsToPose().";

    // Translation must be left completely unchanged (PMX bones never need a
    // translation channel for a purely-rotated FK bend).
    EXPECT_TRUE(ApproximatelyEqual(pose[0].translation, Vec3::Zero()));
}

TEST(BoneChainPhysicsResolverTests, TargetAlreadyAlignedLeavesRotationUntouched)
{
    SkeletonData skeleton = BuildTwoBoneSkeleton();
    DynamicChainDefinition definition = BuildSingleJointDefinition();

    std::vector<BoneLocalOffset> pose(skeleton.bones.size());
    // Simulated target exactly where the bind pose already points (straight
    // up) - no rotation should be necessary.
    const std::vector<Vec3> simulatedPositions = { Vec3(0.0f, 5.0f, 0.0f) };

    ApplyDynamicChainPhysicsToPose(skeleton, definition, simulatedPositions, pose);

    EXPECT_TRUE(RepresentSameRotation(pose[0].rotation, Quat::Identity()))
        << "A target already aligned with the bone's current direction should not rotate its parent.";
}

TEST(BoneChainPhysicsResolverTests, ThreeJointChainAppliesRootToTipInDependencyOrder)
{
    SkeletonData skeleton;
    Bone root;
    root.position = Vec3(0.0f, 0.0f, 0.0f);
    root.parentBoneIndex = -1;
    skeleton.bones.push_back(root); // 0

    Bone jointA;
    jointA.position = Vec3(0.0f, 1.0f, 0.0f);
    jointA.parentBoneIndex = 0;
    skeleton.bones.push_back(jointA); // 1

    Bone jointB;
    jointB.position = Vec3(0.0f, 2.0f, 0.0f);
    jointB.parentBoneIndex = 1;
    skeleton.bones.push_back(jointB); // 2

    DynamicChainDefinition definition;
    definition.rootBoneIndex = 0;
    definition.jointBoneIndices = { 1, 2 };
    definition.jointSettings = { DynamicJointSettings{}, DynamicJointSettings{} };
    definition.restLengths = { 1.0f, 1.0f };

    std::vector<BoneLocalOffset> pose(skeleton.bones.size());
    // Bend the whole chain 90 degrees sideways - jointA lands at (1,0,0) and
    // jointB (one further rest-length past it, same direction) at (2,0,0).
    // Achieving this requires rewriting the ROOT bone's rotation (to swing
    // jointA into place) - jointB then falls out "for free" from the same
    // rotation, since jointA's own rotation was never touched (nothing else
    // needed correcting, exercising the "already aligned, skip" path for
    // jointA's own pose entry).
    const std::vector<Vec3> simulatedPositions = {
        Vec3(1.0f, 0.0f, 0.0f), // jointA's target
        Vec3(2.0f, 0.0f, 0.0f), // jointB's target
    };

    ApplyDynamicChainPhysicsToPose(skeleton, definition, simulatedPositions, pose);

    const Vec3 jointAWorld = ComputeBoneWorldMatrix(skeleton, pose, 1).TransformPoint(Vec3::Zero());
    const Vec3 jointBWorld = ComputeBoneWorldMatrix(skeleton, pose, 2).TransformPoint(Vec3::Zero());

    EXPECT_TRUE(ApproximatelyEqual(jointAWorld, simulatedPositions[0], 1e-4f));
    EXPECT_TRUE(ApproximatelyEqual(jointBWorld, simulatedPositions[1], 1e-4f));

    EXPECT_FALSE(RepresentSameRotation(pose[0].rotation, Quat::Identity()))
        << "Root bone must have been rotated to swing jointA into place.";
}

TEST(BoneChainPhysicsResolverTests, OutOfRangeJointBoneIndexIsSkippedGracefully)
{
    SkeletonData skeleton = BuildTwoBoneSkeleton();
    DynamicChainDefinition definition;
    definition.rootBoneIndex = 0;
    definition.jointBoneIndices = { 99 }; // out of range
    definition.jointSettings = { DynamicJointSettings{} };
    definition.restLengths = { 1.0f };

    std::vector<BoneLocalOffset> pose(skeleton.bones.size());
    const std::vector<Vec3> simulatedPositions = { Vec3(1.0f, 0.0f, 0.0f) };

    ApplyDynamicChainPhysicsToPose(skeleton, definition, simulatedPositions, pose); // Must not crash.

    EXPECT_TRUE(RepresentSameRotation(pose[0].rotation, Quat::Identity()));
    EXPECT_TRUE(RepresentSameRotation(pose[1].rotation, Quat::Identity()));
}

TEST(BoneChainPhysicsResolverTests, OutOfRangeRootBoneIndexIsSkippedGracefully)
{
    SkeletonData skeleton = BuildTwoBoneSkeleton();
    DynamicChainDefinition definition;
    definition.rootBoneIndex = -1; // No real root bone (e.g. a world-anchored chain).
    definition.jointBoneIndices = { 1 };
    definition.jointSettings = { DynamicJointSettings{} };
    definition.restLengths = { 1.0f };

    std::vector<BoneLocalOffset> pose(skeleton.bones.size());
    const std::vector<Vec3> simulatedPositions = { Vec3(1.0f, 0.0f, 0.0f) };

    ApplyDynamicChainPhysicsToPose(skeleton, definition, simulatedPositions, pose); // Must not crash.

    EXPECT_TRUE(RepresentSameRotation(pose[0].rotation, Quat::Identity()));
    EXPECT_TRUE(RepresentSameRotation(pose[1].rotation, Quat::Identity()));
}

} // namespace gte
