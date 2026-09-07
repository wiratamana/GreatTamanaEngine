// Unit tests for DetectDynamicChains() (src/Physics/DynamicChainDetection.h) -
// task_manager/verlet-integration-6, Phase 3: a full rewrite of the previous
// Bone::deformAfterPhysics-driven algorithm, now walking the real RigidBody/
// Joint graph (RigidBodyJointGraph.h) plus the skeleton's own real bone
// ancestry. Every test in this file replaces the old (deleted) suite, which
// asserted the deformAfterPhysics-driven behavior that no longer exists.
// Pure function - hand-built SkeletonData/PhysicsData fixtures, no ECS/GPU/
// file I/O involved.

#include "Physics/DynamicChainDetection.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <iterator>
#include <set>
#include <utility>
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

RigidBody MakeRigidBody(std::int32_t boneIndex, RigidBodyMotionType motionType, float mass = 1.0f, float linearDamping = 0.0f)
{
    RigidBody body;
    body.boneIndex = boneIndex;
    body.motionType = motionType;
    body.mass = mass;
    body.linearDamping = linearDamping;
    return body;
}

Joint MakeJoint(std::int32_t rigidBodyAIndex, std::int32_t rigidBodyBIndex)
{
    Joint joint;
    joint.rigidBodyAIndex = rigidBodyAIndex;
    joint.rigidBodyBIndex = rigidBodyBIndex;
    return joint;
}

void ExpectChainsEqual(const DynamicChainDefinition& a, const DynamicChainDefinition& b)
{
    EXPECT_EQ(a.rootBoneIndex, b.rootBoneIndex);
    EXPECT_EQ(a.jointBoneIndices, b.jointBoneIndices);
    EXPECT_EQ(a.parentJointIndex, b.parentJointIndex);
    ASSERT_EQ(a.extraConstraints.size(), b.extraConstraints.size());
    for (std::size_t i = 0; i < a.extraConstraints.size(); ++i) {
        EXPECT_EQ(a.extraConstraints[i].jointIndexA, b.extraConstraints[i].jointIndexA);
        EXPECT_EQ(a.extraConstraints[i].jointIndexB, b.extraConstraints[i].jointIndexB);
        EXPECT_NEAR(a.extraConstraints[i].restLength, b.extraConstraints[i].restLength, 1e-4f);
    }
}

} // namespace

TEST(DynamicChainDetectionTests, SimpleLinearRigWithOneStaticAnchorAndThreeDynamicBodiesDetectsOneLinearChain)
{
    // 0 = root/static-anchor-bone, 1/2/3 = a straight FK chain.
    SkeletonData skeleton;
    skeleton.bones.push_back(MakeBone(Vec3(0.0f, 0.0f, 0.0f), -1)); // 0 - anchor
    skeleton.bones.push_back(MakeBone(Vec3(0.0f, 1.0f, 0.0f), 0)); // 1
    skeleton.bones.push_back(MakeBone(Vec3(0.0f, 2.5f, 0.0f), 1)); // 2 (1.5 units from 1)
    skeleton.bones.push_back(MakeBone(Vec3(0.0f, 4.5f, 0.0f), 2)); // 3 (2.0 units from 2)

    PhysicsData physics;
    physics.rigidBodies.push_back(MakeRigidBody(0, RigidBodyMotionType::Static));
    physics.rigidBodies.push_back(MakeRigidBody(1, RigidBodyMotionType::Dynamic));
    physics.rigidBodies.push_back(MakeRigidBody(2, RigidBodyMotionType::Dynamic));
    physics.rigidBodies.push_back(MakeRigidBody(3, RigidBodyMotionType::Dynamic));
    physics.joints.push_back(MakeJoint(0, 1));
    physics.joints.push_back(MakeJoint(1, 2));
    physics.joints.push_back(MakeJoint(2, 3));

    const DynamicChainDetectionDefaults defaults{};
    const DynamicChainDetectionResult detection = DetectDynamicChains(skeleton, &physics, defaults);

    ASSERT_EQ(detection.chains.size(), 1u);
    const DynamicChainDefinition& chain = detection.chains[0];
    EXPECT_EQ(chain.rootBoneIndex, 0);
    ASSERT_EQ(chain.jointBoneIndices.size(), 3u);
    EXPECT_EQ(chain.jointBoneIndices[0], 1);
    EXPECT_EQ(chain.jointBoneIndices[1], 2);
    EXPECT_EQ(chain.jointBoneIndices[2], 3);
    ASSERT_EQ(chain.parentJointIndex.size(), 3u);
    EXPECT_EQ(chain.parentJointIndex[0], -1);
    EXPECT_EQ(chain.parentJointIndex[1], 0);
    EXPECT_EQ(chain.parentJointIndex[2], 1);
    EXPECT_TRUE(chain.extraConstraints.empty());

    ASSERT_EQ(chain.restLengths.size(), 3u);
    EXPECT_NEAR(chain.restLengths[0], 1.0f, 1e-4f);
    EXPECT_NEAR(chain.restLengths[1], 1.5f, 1e-4f);
    EXPECT_NEAR(chain.restLengths[2], 2.0f, 1e-4f);

    EXPECT_TRUE(detection.diagnostics.orphanedDynamicBoneIndices.empty());
    EXPECT_TRUE(detection.diagnostics.crossChainJointsDropped.empty());
    EXPECT_TRUE(detection.diagnostics.duplicateBoneRigidBodyAssignmentsDropped.empty());
}

TEST(DynamicChainDetectionTests, SpiderWebSkirtWithFourBranchesAndCrossBraceProducesOneSingleTreeChain)
{
    // 0 = static hip anchor; 1/2/3/4 = dynamic strand roots (direct skeleton
    // children of 0); 5/6/7/8 = dynamic tips (each a skeleton child of its
    // own strand root: 5<-1, 6<-2, 7<-3, 8<-4).
    SkeletonData skeleton;
    skeleton.bones.push_back(MakeBone(Vec3(0.0f, 0.0f, 0.0f), -1)); // 0
    skeleton.bones.push_back(MakeBone(Vec3(1.0f, 0.0f, 0.0f), 0)); // 1
    skeleton.bones.push_back(MakeBone(Vec3(-1.0f, 0.0f, 0.0f), 0)); // 2
    skeleton.bones.push_back(MakeBone(Vec3(0.0f, 0.0f, 1.0f), 0)); // 3
    skeleton.bones.push_back(MakeBone(Vec3(0.0f, 0.0f, -1.0f), 0)); // 4
    skeleton.bones.push_back(MakeBone(Vec3(1.0f, -1.0f, 0.0f), 1)); // 5
    skeleton.bones.push_back(MakeBone(Vec3(-1.0f, -1.0f, 0.0f), 2)); // 6
    skeleton.bones.push_back(MakeBone(Vec3(0.0f, -1.0f, 1.0f), 3)); // 7
    skeleton.bones.push_back(MakeBone(Vec3(0.0f, -1.0f, -1.0f), 4)); // 8

    PhysicsData physics;
    for (std::int32_t i = 0; i < 9; ++i) {
        physics.rigidBodies.push_back(
            MakeRigidBody(i, i == 0 ? RigidBodyMotionType::Static : RigidBodyMotionType::Dynamic));
    }
    // Real hierarchy edges.
    physics.joints.push_back(MakeJoint(0, 1));
    physics.joints.push_back(MakeJoint(0, 2));
    physics.joints.push_back(MakeJoint(0, 3));
    physics.joints.push_back(MakeJoint(0, 4));
    physics.joints.push_back(MakeJoint(1, 5));
    physics.joints.push_back(MakeJoint(2, 6));
    physics.joints.push_back(MakeJoint(3, 7));
    physics.joints.push_back(MakeJoint(4, 8));
    // Extra "ring brace" joint directly between siblings 1 and 2.
    physics.joints.push_back(MakeJoint(1, 2));

    const DynamicChainDetectionDefaults defaults{};
    const DynamicChainDetectionResult detection = DetectDynamicChains(skeleton, &physics, defaults);

    ASSERT_EQ(detection.chains.size(), 1u); // Never four - one single tree chain.
    const DynamicChainDefinition& chain = detection.chains[0];
    EXPECT_EQ(chain.rootBoneIndex, 0);
    ASSERT_EQ(chain.jointBoneIndices.size(), 8u);

    const std::set<std::int32_t> jointBoneSet(chain.jointBoneIndices.begin(), chain.jointBoneIndices.end());
    EXPECT_EQ(jointBoneSet, (std::set<std::int32_t>{ 1, 2, 3, 4, 5, 6, 7, 8 }));

    // Locate each bone's own position within jointBoneIndices, then confirm
    // the tree shape: 1/2/3/4 are direct root children, 5/6/7/8 are each the
    // corresponding strand root's own child.
    auto positionOf = [&](std::int32_t boneIndex) -> std::int32_t {
        for (std::size_t i = 0; i < chain.jointBoneIndices.size(); ++i) {
            if (chain.jointBoneIndices[i] == boneIndex) {
                return static_cast<std::int32_t>(i);
            }
        }
        return -1;
    };
    const std::int32_t pos1 = positionOf(1), pos2 = positionOf(2), pos3 = positionOf(3), pos4 = positionOf(4);
    const std::int32_t pos5 = positionOf(5), pos6 = positionOf(6), pos7 = positionOf(7), pos8 = positionOf(8);
    EXPECT_EQ(chain.parentJointIndex[static_cast<std::size_t>(pos1)], -1);
    EXPECT_EQ(chain.parentJointIndex[static_cast<std::size_t>(pos2)], -1);
    EXPECT_EQ(chain.parentJointIndex[static_cast<std::size_t>(pos3)], -1);
    EXPECT_EQ(chain.parentJointIndex[static_cast<std::size_t>(pos4)], -1);
    EXPECT_EQ(chain.parentJointIndex[static_cast<std::size_t>(pos5)], pos1);
    EXPECT_EQ(chain.parentJointIndex[static_cast<std::size_t>(pos6)], pos2);
    EXPECT_EQ(chain.parentJointIndex[static_cast<std::size_t>(pos7)], pos3);
    EXPECT_EQ(chain.parentJointIndex[static_cast<std::size_t>(pos8)], pos4);

    ASSERT_EQ(chain.extraConstraints.size(), 1u);
    const std::set<std::int32_t> bracePositions{ chain.extraConstraints[0].jointIndexA, chain.extraConstraints[0].jointIndexB };
    EXPECT_EQ(bracePositions, (std::set<std::int32_t>{ pos1, pos2 }));
    EXPECT_NEAR(chain.extraConstraints[0].restLength, 2.0f, 1e-4f); // Distance between bones 1 and 2.

    EXPECT_TRUE(detection.diagnostics.orphanedDynamicBoneIndices.empty());
    EXPECT_TRUE(detection.diagnostics.crossChainJointsDropped.empty());
    EXPECT_TRUE(detection.diagnostics.duplicateBoneRigidBodyAssignmentsDropped.empty());
}

TEST(DynamicChainDetectionTests, NonHierarchyAlignedJointDoesNotBecomeATreeEdgeButAppearsAsExtraConstraint)
{
    // 0 = static anchor; 1/2 = dynamic SIBLING bones (both direct children of 0).
    SkeletonData skeleton;
    skeleton.bones.push_back(MakeBone(Vec3(0.0f, 0.0f, 0.0f), -1)); // 0
    skeleton.bones.push_back(MakeBone(Vec3(1.0f, 0.0f, 0.0f), 0)); // 1
    skeleton.bones.push_back(MakeBone(Vec3(-1.0f, 0.0f, 0.0f), 0)); // 2

    PhysicsData physics;
    physics.rigidBodies.push_back(MakeRigidBody(0, RigidBodyMotionType::Static));
    physics.rigidBodies.push_back(MakeRigidBody(1, RigidBodyMotionType::Dynamic));
    physics.rigidBodies.push_back(MakeRigidBody(2, RigidBodyMotionType::Dynamic));
    physics.joints.push_back(MakeJoint(0, 1));
    physics.joints.push_back(MakeJoint(0, 2));
    physics.joints.push_back(MakeJoint(1, 2)); // Sibling-to-sibling "ring brace" joint.

    const DynamicChainDetectionDefaults defaults{};
    const DynamicChainDetectionResult detection = DetectDynamicChains(skeleton, &physics, defaults);

    ASSERT_EQ(detection.chains.size(), 1u);
    const DynamicChainDefinition& chain = detection.chains[0];
    EXPECT_EQ(chain.rootBoneIndex, 0);
    ASSERT_EQ(chain.jointBoneIndices.size(), 2u);
    EXPECT_EQ(chain.parentJointIndex[0], -1);
    EXPECT_EQ(chain.parentJointIndex[1], -1);

    ASSERT_EQ(chain.extraConstraints.size(), 1u);
    const std::set<std::int32_t> bracePositions{ chain.extraConstraints[0].jointIndexA, chain.extraConstraints[0].jointIndexB };
    EXPECT_EQ(bracePositions, (std::set<std::int32_t>{ 0, 1 }));
}

TEST(DynamicChainDetectionTests, MultipleIndependentAnchorsProduceSeparateDisjointChains)
{
    SkeletonData skeleton;
    skeleton.bones.push_back(MakeBone(Vec3(0.0f, 0.0f, 0.0f), -1)); // 0 - anchor A
    skeleton.bones.push_back(MakeBone(Vec3(0.0f, 1.0f, 0.0f), 0)); // 1
    skeleton.bones.push_back(MakeBone(Vec3(0.0f, 2.0f, 0.0f), 1)); // 2
    skeleton.bones.push_back(MakeBone(Vec3(5.0f, 0.0f, 0.0f), -1)); // 3 - anchor B
    skeleton.bones.push_back(MakeBone(Vec3(5.0f, 1.0f, 0.0f), 3)); // 4
    skeleton.bones.push_back(MakeBone(Vec3(5.0f, 2.0f, 0.0f), 4)); // 5

    PhysicsData physics;
    physics.rigidBodies.push_back(MakeRigidBody(0, RigidBodyMotionType::Static));
    physics.rigidBodies.push_back(MakeRigidBody(1, RigidBodyMotionType::Dynamic));
    physics.rigidBodies.push_back(MakeRigidBody(2, RigidBodyMotionType::Dynamic));
    physics.rigidBodies.push_back(MakeRigidBody(3, RigidBodyMotionType::Static));
    physics.rigidBodies.push_back(MakeRigidBody(4, RigidBodyMotionType::Dynamic));
    physics.rigidBodies.push_back(MakeRigidBody(5, RigidBodyMotionType::Dynamic));
    physics.joints.push_back(MakeJoint(0, 1));
    physics.joints.push_back(MakeJoint(1, 2));
    physics.joints.push_back(MakeJoint(3, 4));
    physics.joints.push_back(MakeJoint(4, 5));

    const DynamicChainDetectionDefaults defaults{};
    const DynamicChainDetectionResult detection = DetectDynamicChains(skeleton, &physics, defaults);

    ASSERT_EQ(detection.chains.size(), 2u);
    const DynamicChainDefinition* chainA = nullptr;
    const DynamicChainDefinition* chainB = nullptr;
    for (const DynamicChainDefinition& chain : detection.chains) {
        if (chain.rootBoneIndex == 0) {
            chainA = &chain;
        } else if (chain.rootBoneIndex == 3) {
            chainB = &chain;
        }
    }
    ASSERT_NE(chainA, nullptr);
    ASSERT_NE(chainB, nullptr);
    EXPECT_EQ(chainA->jointBoneIndices, (std::vector<std::int32_t>{ 1, 2 }));
    EXPECT_EQ(chainB->jointBoneIndices, (std::vector<std::int32_t>{ 4, 5 }));

    const std::set<std::int32_t> setA(chainA->jointBoneIndices.begin(), chainA->jointBoneIndices.end());
    const std::set<std::int32_t> setB(chainB->jointBoneIndices.begin(), chainB->jointBoneIndices.end());
    std::vector<std::int32_t> intersection;
    std::set_intersection(setA.begin(), setA.end(), setB.begin(), setB.end(), std::back_inserter(intersection));
    EXPECT_TRUE(intersection.empty());
}

TEST(DynamicChainDetectionTests, GraphUnreachableDynamicBodyBoneIndexAppearsInOrphanedDynamicBoneIndices)
{
    // Two Dynamic bodies jointed only to each other - no Static body
    // anywhere in the whole PhysicsData. Direct regression test for Culprit
    // F (PHASE0_MASTER_STRATEGY.md's own Revision Notes (v2)).
    SkeletonData skeleton;
    skeleton.bones.push_back(MakeBone(Vec3(0.0f, 0.0f, 0.0f), -1)); // 0
    skeleton.bones.push_back(MakeBone(Vec3(0.0f, 1.0f, 0.0f), 0)); // 1

    PhysicsData physics;
    physics.rigidBodies.push_back(MakeRigidBody(0, RigidBodyMotionType::Dynamic));
    physics.rigidBodies.push_back(MakeRigidBody(1, RigidBodyMotionType::Dynamic));
    physics.joints.push_back(MakeJoint(0, 1));

    const DynamicChainDetectionDefaults defaults{};
    const DynamicChainDetectionResult detection = DetectDynamicChains(skeleton, &physics, defaults);

    EXPECT_TRUE(detection.chains.empty());
    const std::set<std::int32_t> orphaned(
        detection.diagnostics.orphanedDynamicBoneIndices.begin(), detection.diagnostics.orphanedDynamicBoneIndices.end());
    EXPECT_EQ(orphaned, (std::set<std::int32_t>{ 0, 1 }));
}

TEST(DynamicChainDetectionTests, DynamicBodyWithNoReachableStaticAnchorIsOrphanedNotSimulated)
{
    // Deliberately the same underlying scenario as the test above (see this
    // campaign's PHASE3 doc: intentional redundancy insurance for the exact
    // guarantee Culprit F violated), built with different bone/rigid-body
    // numbering to keep the two tests independently reasoned-about.
    SkeletonData skeleton;
    skeleton.bones.push_back(MakeBone(Vec3(2.0f, 0.0f, 0.0f), -1)); // 0
    skeleton.bones.push_back(MakeBone(Vec3(2.0f, 1.0f, 0.0f), -1)); // 1 - unrelated parent, doesn't matter.

    PhysicsData physics;
    physics.rigidBodies.push_back(MakeRigidBody(1, RigidBodyMotionType::Dynamic));
    physics.rigidBodies.push_back(MakeRigidBody(0, RigidBodyMotionType::Dynamic));
    physics.joints.push_back(MakeJoint(0, 1));

    const DynamicChainDetectionDefaults defaults{};
    const DynamicChainDetectionResult detection = DetectDynamicChains(skeleton, &physics, defaults);

    EXPECT_TRUE(detection.chains.empty());
    const std::set<std::int32_t> orphaned(
        detection.diagnostics.orphanedDynamicBoneIndices.begin(), detection.diagnostics.orphanedDynamicBoneIndices.end());
    EXPECT_EQ(orphaned, (std::set<std::int32_t>{ 0, 1 }));
}

TEST(DynamicChainDetectionTests, CrossChainJointBetweenTwoDifferentAnchorsIsDroppedAndDiagnosed)
{
    SkeletonData skeleton;
    skeleton.bones.push_back(MakeBone(Vec3(0.0f, 0.0f, 0.0f), -1)); // 0 - anchor A
    skeleton.bones.push_back(MakeBone(Vec3(0.0f, 1.0f, 0.0f), 0)); // 1
    skeleton.bones.push_back(MakeBone(Vec3(0.0f, 2.0f, 0.0f), 1)); // 2
    skeleton.bones.push_back(MakeBone(Vec3(5.0f, 0.0f, 0.0f), -1)); // 3 - anchor B
    skeleton.bones.push_back(MakeBone(Vec3(5.0f, 1.0f, 0.0f), 3)); // 4
    skeleton.bones.push_back(MakeBone(Vec3(5.0f, 2.0f, 0.0f), 4)); // 5

    PhysicsData physics;
    physics.rigidBodies.push_back(MakeRigidBody(0, RigidBodyMotionType::Static));
    physics.rigidBodies.push_back(MakeRigidBody(1, RigidBodyMotionType::Dynamic));
    physics.rigidBodies.push_back(MakeRigidBody(2, RigidBodyMotionType::Dynamic));
    physics.rigidBodies.push_back(MakeRigidBody(3, RigidBodyMotionType::Static));
    physics.rigidBodies.push_back(MakeRigidBody(4, RigidBodyMotionType::Dynamic));
    physics.rigidBodies.push_back(MakeRigidBody(5, RigidBodyMotionType::Dynamic));
    physics.joints.push_back(MakeJoint(0, 1));
    physics.joints.push_back(MakeJoint(1, 2));
    physics.joints.push_back(MakeJoint(3, 4));
    physics.joints.push_back(MakeJoint(4, 5));
    physics.joints.push_back(MakeJoint(2, 4)); // Cross-chain brace - joint index 4.

    const DynamicChainDetectionDefaults defaults{};
    const DynamicChainDetectionResult detection = DetectDynamicChains(skeleton, &physics, defaults);

    ASSERT_EQ(detection.chains.size(), 2u);
    for (const DynamicChainDefinition& chain : detection.chains) {
        EXPECT_TRUE(chain.extraConstraints.empty());
    }
    EXPECT_EQ(detection.diagnostics.crossChainJointsDropped, (std::vector<std::int32_t>{ 4 }));
}

namespace {
// Shared fixture for the duplicate-bone-rigid-body-assignment tests below:
// bone 0 = static anchor, bones 1/2 = a two-joint dynamic chain, with TWO
// different RigidBody entries both attached to bone 1 (index 1, kept - the
// LOWER index; index 2, dropped).
void BuildDuplicateAssignmentFixture(SkeletonData& skeleton, PhysicsData& physics)
{
    skeleton.bones.push_back(MakeBone(Vec3(0.0f, 0.0f, 0.0f), -1)); // 0 - anchor
    skeleton.bones.push_back(MakeBone(Vec3(0.0f, 1.0f, 0.0f), 0)); // 1
    skeleton.bones.push_back(MakeBone(Vec3(0.0f, 2.0f, 0.0f), 1)); // 2

    physics.rigidBodies.push_back(MakeRigidBody(0, RigidBodyMotionType::Static));
    physics.rigidBodies.push_back(MakeRigidBody(1, RigidBodyMotionType::Dynamic, /*mass=*/2.5f, /*damping=*/0.1f)); // kept.
    physics.rigidBodies.push_back(MakeRigidBody(1, RigidBodyMotionType::Dynamic, /*mass=*/99.0f, /*damping=*/99.0f)); // dropped.
    physics.rigidBodies.push_back(MakeRigidBody(2, RigidBodyMotionType::Dynamic));
    physics.joints.push_back(MakeJoint(0, 1));
    physics.joints.push_back(MakeJoint(1, 3));
    // Also connect the DUPLICATE rigid body (index 2) to the anchor so it is
    // graph-reachable - and therefore actually ELIGIBLE - letting it reach
    // the duplicate-boneIndex tie-break at all (an unreachable body is
    // excluded before the tie-break ever runs, see Step C pass 2).
    physics.joints.push_back(MakeJoint(0, 2));
}
} // namespace

TEST(DynamicChainDetectionTests, DuplicateBoneRigidBodyAssignmentPicksLowestIndexDeterministically)
{
    SkeletonData skeleton;
    PhysicsData physics;
    BuildDuplicateAssignmentFixture(skeleton, physics);

    const DynamicChainDetectionDefaults defaults{};
    const DynamicChainDetectionResult detection = DetectDynamicChains(skeleton, &physics, defaults);

    ASSERT_EQ(detection.chains.size(), 1u);
    const DynamicChainDefinition& chain = detection.chains[0];
    ASSERT_EQ(chain.jointBoneIndices.size(), 2u);
    EXPECT_EQ(chain.jointBoneIndices[0], 1);
    EXPECT_EQ(chain.jointBoneIndices[1], 2);
    EXPECT_EQ(chain.parentJointIndex, (std::vector<std::int32_t>{ -1, 0 }));

    // The LOWER rigid-body index (mass 2.5/damping 0.1) must be the one used
    // to seed this joint's settings - never the dropped duplicate (99/99).
    ASSERT_EQ(chain.jointSettings.size(), 2u);
    EXPECT_NEAR(chain.jointSettings[0].mass, 2.5f, 1e-4f);
    EXPECT_NEAR(chain.jointSettings[0].damping, 0.1f, 1e-4f);
}

TEST(DynamicChainDetectionTests, DuplicateBoneRigidBodyAssignmentDropRecordedInDiagnostics)
{
    SkeletonData skeleton;
    PhysicsData physics;
    BuildDuplicateAssignmentFixture(skeleton, physics);

    const DynamicChainDetectionDefaults defaults{};
    const DynamicChainDetectionResult detection = DetectDynamicChains(skeleton, &physics, defaults);

    // Rigid body index 2 (the higher-index duplicate attached to bone 1) must
    // be recorded as dropped.
    EXPECT_EQ(detection.diagnostics.duplicateBoneRigidBodyAssignmentsDropped, (std::vector<std::int32_t>{ 2 }));
}

TEST(DynamicChainDetectionTests, UnattachedRigidBodiesAreExcludedFromEligibilityAndNeverCauseSpuriousDuplicateCollisions)
{
    SkeletonData skeleton;
    skeleton.bones.push_back(MakeBone(Vec3(0.0f, 0.0f, 0.0f), -1)); // 0 - anchor
    skeleton.bones.push_back(MakeBone(Vec3(0.0f, 1.0f, 0.0f), 0)); // 1
    skeleton.bones.push_back(MakeBone(Vec3(0.0f, 2.0f, 0.0f), 1)); // 2
    skeleton.bones.push_back(MakeBone(Vec3(0.0f, 3.5f, 0.0f), 2)); // 3

    PhysicsData baseline;
    baseline.rigidBodies.push_back(MakeRigidBody(0, RigidBodyMotionType::Static));
    baseline.rigidBodies.push_back(MakeRigidBody(1, RigidBodyMotionType::Dynamic));
    baseline.rigidBodies.push_back(MakeRigidBody(2, RigidBodyMotionType::Dynamic));
    baseline.rigidBodies.push_back(MakeRigidBody(3, RigidBodyMotionType::Dynamic));
    baseline.joints.push_back(MakeJoint(0, 1));
    baseline.joints.push_back(MakeJoint(1, 2));
    baseline.joints.push_back(MakeJoint(2, 3));

    PhysicsData withUnattached = baseline;
    withUnattached.rigidBodies.push_back(MakeRigidBody(-1, RigidBodyMotionType::Static));
    withUnattached.rigidBodies.push_back(MakeRigidBody(-1, RigidBodyMotionType::Dynamic));

    const DynamicChainDetectionDefaults defaults{};
    const DynamicChainDetectionResult baselineResult = DetectDynamicChains(skeleton, &baseline, defaults);
    const DynamicChainDetectionResult withUnattachedResult = DetectDynamicChains(skeleton, &withUnattached, defaults);

    ASSERT_EQ(baselineResult.chains.size(), withUnattachedResult.chains.size());
    for (std::size_t i = 0; i < baselineResult.chains.size(); ++i) {
        ExpectChainsEqual(baselineResult.chains[i], withUnattachedResult.chains[i]);
    }
    EXPECT_TRUE(withUnattachedResult.diagnostics.duplicateBoneRigidBodyAssignmentsDropped.empty());
    EXPECT_EQ(baselineResult.diagnostics.orphanedDynamicBoneIndices, withUnattachedResult.diagnostics.orphanedDynamicBoneIndices);
}

#ifdef NDEBUG

TEST(DynamicChainDetectionTests, CyclicSkeletonAncestryIsTreatedAsOrphanNotInfiniteLoop)
{
    // Bone 0's parent is bone 1, bone 1's parent is bone 0 - a malformed
    // cycle. Bone 0 has a Dynamic rigid body graph-reachable (via a Joint)
    // from a Static anchor attached to an unrelated bone 2 - graph-reachable,
    // but its REAL ancestry never crosses the anchor at all.
    SkeletonData skeleton;
    skeleton.bones.push_back(MakeBone(Vec3(0.0f, 0.0f, 0.0f), 1)); // 0
    skeleton.bones.push_back(MakeBone(Vec3(0.0f, 1.0f, 0.0f), 0)); // 1
    skeleton.bones.push_back(MakeBone(Vec3(5.0f, 0.0f, 0.0f), -1)); // 2 - anchor, unrelated in the hierarchy.

    PhysicsData physics;
    physics.rigidBodies.push_back(MakeRigidBody(2, RigidBodyMotionType::Static));
    physics.rigidBodies.push_back(MakeRigidBody(0, RigidBodyMotionType::Dynamic));
    physics.joints.push_back(MakeJoint(0, 1));

    const DynamicChainDetectionDefaults defaults{};
    const DynamicChainDetectionResult detection = DetectDynamicChains(skeleton, &physics, defaults);

    EXPECT_TRUE(detection.chains.empty());
    EXPECT_EQ(detection.diagnostics.orphanedDynamicBoneIndices, (std::vector<std::int32_t>{ 0 }));
}

#else

TEST(DynamicChainDetectionDeathTest, CyclicSkeletonAncestryTripsTheDiagnosticAssert)
{
    SkeletonData skeleton;
    skeleton.bones.push_back(MakeBone(Vec3(0.0f, 0.0f, 0.0f), 1)); // 0
    skeleton.bones.push_back(MakeBone(Vec3(0.0f, 1.0f, 0.0f), 0)); // 1
    skeleton.bones.push_back(MakeBone(Vec3(5.0f, 0.0f, 0.0f), -1)); // 2 - anchor, unrelated in the hierarchy.

    PhysicsData physics;
    physics.rigidBodies.push_back(MakeRigidBody(2, RigidBodyMotionType::Static));
    physics.rigidBodies.push_back(MakeRigidBody(0, RigidBodyMotionType::Dynamic));
    physics.joints.push_back(MakeJoint(0, 1));

    const DynamicChainDetectionDefaults defaults{};
    EXPECT_DEATH({ DetectDynamicChains(skeleton, &physics, defaults); }, "");
}

#endif

TEST(DynamicChainDetectionTests, OutOfRangeParentBoneIndexIsTreatedLikeNoParent)
{
    SkeletonData skeletonNegative;
    skeletonNegative.bones.push_back(MakeBone(Vec3(0.0f, 0.0f, 0.0f), -1)); // 0 - no parent.
    skeletonNegative.bones.push_back(MakeBone(Vec3(5.0f, 0.0f, 0.0f), -1)); // 1 - anchor.

    SkeletonData skeletonOutOfRange;
    skeletonOutOfRange.bones.push_back(MakeBone(Vec3(0.0f, 0.0f, 0.0f), 999)); // 0 - out-of-range parent.
    skeletonOutOfRange.bones.push_back(MakeBone(Vec3(5.0f, 0.0f, 0.0f), -1)); // 1 - anchor.

    PhysicsData physics;
    physics.rigidBodies.push_back(MakeRigidBody(0, RigidBodyMotionType::Dynamic));
    physics.rigidBodies.push_back(MakeRigidBody(1, RigidBodyMotionType::Static));
    physics.joints.push_back(MakeJoint(0, 1));

    const DynamicChainDetectionDefaults defaults{};
    const DynamicChainDetectionResult negativeResult = DetectDynamicChains(skeletonNegative, &physics, defaults);
    const DynamicChainDetectionResult outOfRangeResult = DetectDynamicChains(skeletonOutOfRange, &physics, defaults);

    EXPECT_TRUE(negativeResult.chains.empty());
    EXPECT_TRUE(outOfRangeResult.chains.empty());
    EXPECT_EQ(negativeResult.diagnostics.orphanedDynamicBoneIndices, outOfRangeResult.diagnostics.orphanedDynamicBoneIndices);
    EXPECT_EQ(negativeResult.diagnostics.orphanedDynamicBoneIndices, (std::vector<std::int32_t>{ 0 }));
}

TEST(DynamicChainDetectionTests, RunShorterThanMinimumChainLengthDetectsNothing)
{
    SkeletonData skeleton;
    skeleton.bones.push_back(MakeBone(Vec3(0.0f, 0.0f, 0.0f), -1)); // 0 - anchor
    skeleton.bones.push_back(MakeBone(Vec3(0.0f, 1.0f, 0.0f), 0)); // 1 - single joint, leaf

    PhysicsData physics;
    physics.rigidBodies.push_back(MakeRigidBody(0, RigidBodyMotionType::Static));
    physics.rigidBodies.push_back(MakeRigidBody(1, RigidBodyMotionType::Dynamic));
    physics.joints.push_back(MakeJoint(0, 1));

    DynamicChainDetectionDefaults defaults{};
    defaults.minimumChainLength = 2;
    const DynamicChainDetectionResult detection = DetectDynamicChains(skeleton, &physics, defaults);

    EXPECT_TRUE(detection.chains.empty());
    // "Too short" and "unreachable" are distinct concepts - a discarded
    // short chain must NOT retroactively appear as orphaned.
    EXPECT_TRUE(detection.diagnostics.orphanedDynamicBoneIndices.empty());
}

TEST(DynamicChainDetectionTests, NullPhysicsDataDetectsNothing)
{
    SkeletonData skeleton;
    skeleton.bones.push_back(MakeBone(Vec3(0.0f, 0.0f, 0.0f), -1));
    const DynamicChainDetectionDefaults defaults{};
    const DynamicChainDetectionResult detection = DetectDynamicChains(skeleton, nullptr, defaults);

    EXPECT_TRUE(detection.chains.empty());
    EXPECT_TRUE(detection.diagnostics.orphanedDynamicBoneIndices.empty());
    EXPECT_TRUE(detection.diagnostics.crossChainJointsDropped.empty());
    EXPECT_TRUE(detection.diagnostics.duplicateBoneRigidBodyAssignmentsDropped.empty());
}

namespace {
// Builds the spider-web fixture's PhysicsData from a logical body/joint list,
// permuted into arbitrary array slots - used to prove the whole algorithm's
// output is independent of PhysicsData::rigidBodies/joints' own storage
// order (only the LOGICAL boneIndex/motionType/topology matters).
PhysicsData BuildPermutedSpiderWebPhysicsData(
    const std::vector<std::size_t>& rigidBodySlotForLogicalIndex, const std::vector<std::size_t>& jointSlotForLogicalIndex)
{
    struct LogicalBody {
        std::int32_t boneIndex;
        RigidBodyMotionType motionType;
    };
    const std::vector<LogicalBody> logicalBodies = {
        { 0, RigidBodyMotionType::Static },
        { 1, RigidBodyMotionType::Dynamic },
        { 2, RigidBodyMotionType::Dynamic },
        { 3, RigidBodyMotionType::Dynamic },
        { 4, RigidBodyMotionType::Dynamic },
        { 5, RigidBodyMotionType::Dynamic },
        { 6, RigidBodyMotionType::Dynamic },
        { 7, RigidBodyMotionType::Dynamic },
        { 8, RigidBodyMotionType::Dynamic },
    };
    const std::vector<std::pair<int, int>> logicalJoints
        = { { 0, 1 }, { 0, 2 }, { 0, 3 }, { 0, 4 }, { 1, 5 }, { 2, 6 }, { 3, 7 }, { 4, 8 }, { 1, 2 } };

    PhysicsData physics;
    physics.rigidBodies.resize(logicalBodies.size());
    for (std::size_t logicalIdx = 0; logicalIdx < logicalBodies.size(); ++logicalIdx) {
        RigidBody body;
        body.boneIndex = logicalBodies[logicalIdx].boneIndex;
        body.motionType = logicalBodies[logicalIdx].motionType;
        physics.rigidBodies[rigidBodySlotForLogicalIndex[logicalIdx]] = body;
    }
    physics.joints.resize(logicalJoints.size());
    for (std::size_t logicalIdx = 0; logicalIdx < logicalJoints.size(); ++logicalIdx) {
        Joint joint;
        joint.rigidBodyAIndex = static_cast<std::int32_t>(rigidBodySlotForLogicalIndex[static_cast<std::size_t>(logicalJoints[logicalIdx].first)]);
        joint.rigidBodyBIndex = static_cast<std::int32_t>(rigidBodySlotForLogicalIndex[static_cast<std::size_t>(logicalJoints[logicalIdx].second)]);
        physics.joints[jointSlotForLogicalIndex[logicalIdx]] = joint;
    }
    return physics;
}
} // namespace

TEST(DynamicChainDetectionTests, ResultIsIdenticalRegardlessOfRigidBodyAndJointStorageOrder)
{
    SkeletonData skeleton;
    skeleton.bones.push_back(MakeBone(Vec3(0.0f, 0.0f, 0.0f), -1)); // 0
    skeleton.bones.push_back(MakeBone(Vec3(1.0f, 0.0f, 0.0f), 0)); // 1
    skeleton.bones.push_back(MakeBone(Vec3(-1.0f, 0.0f, 0.0f), 0)); // 2
    skeleton.bones.push_back(MakeBone(Vec3(0.0f, 0.0f, 1.0f), 0)); // 3
    skeleton.bones.push_back(MakeBone(Vec3(0.0f, 0.0f, -1.0f), 0)); // 4
    skeleton.bones.push_back(MakeBone(Vec3(1.0f, -1.0f, 0.0f), 1)); // 5
    skeleton.bones.push_back(MakeBone(Vec3(-1.0f, -1.0f, 0.0f), 2)); // 6
    skeleton.bones.push_back(MakeBone(Vec3(0.0f, -1.0f, 1.0f), 3)); // 7
    skeleton.bones.push_back(MakeBone(Vec3(0.0f, -1.0f, -1.0f), 4)); // 8

    const std::vector<std::size_t> naturalBodySlots = { 0, 1, 2, 3, 4, 5, 6, 7, 8 };
    const std::vector<std::size_t> naturalJointSlots = { 0, 1, 2, 3, 4, 5, 6, 7, 8 };
    const std::vector<std::size_t> reversedBodySlots = { 8, 7, 6, 5, 4, 3, 2, 1, 0 };
    const std::vector<std::size_t> reversedJointSlots = { 8, 7, 6, 5, 4, 3, 2, 1, 0 };

    PhysicsData naturalPhysics = BuildPermutedSpiderWebPhysicsData(naturalBodySlots, naturalJointSlots);
    PhysicsData reversedPhysics = BuildPermutedSpiderWebPhysicsData(reversedBodySlots, reversedJointSlots);

    const DynamicChainDetectionDefaults defaults{};
    const DynamicChainDetectionResult naturalResult = DetectDynamicChains(skeleton, &naturalPhysics, defaults);
    const DynamicChainDetectionResult reversedResult = DetectDynamicChains(skeleton, &reversedPhysics, defaults);

    ASSERT_EQ(naturalResult.chains.size(), reversedResult.chains.size());
    for (std::size_t i = 0; i < naturalResult.chains.size(); ++i) {
        ExpectChainsEqual(naturalResult.chains[i], reversedResult.chains[i]);
    }

    const std::set<std::int32_t> naturalOrphans(
        naturalResult.diagnostics.orphanedDynamicBoneIndices.begin(), naturalResult.diagnostics.orphanedDynamicBoneIndices.end());
    const std::set<std::int32_t> reversedOrphans(
        reversedResult.diagnostics.orphanedDynamicBoneIndices.begin(), reversedResult.diagnostics.orphanedDynamicBoneIndices.end());
    EXPECT_EQ(naturalOrphans, reversedOrphans);
    EXPECT_EQ(naturalResult.diagnostics.crossChainJointsDropped.size(), reversedResult.diagnostics.crossChainJointsDropped.size());
    EXPECT_EQ(naturalResult.diagnostics.duplicateBoneRigidBodyAssignmentsDropped.size(),
        reversedResult.diagnostics.duplicateBoneRigidBodyAssignmentsDropped.size());
}

} // namespace gte
