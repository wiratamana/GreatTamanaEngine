// Unit tests for DetectDynamicChains() (src/Physics/DynamicChainDetection.h) -
// the chain-auto-detection algorithm this campaign's Phase 4 adds
// (verlet-integration-1, PHASE4_PARAMETER_AUTHORING_AND_DATA_DRIVEN_CONFIG.md,
// 3.2). Pure function - hand-built SkeletonData/PhysicsData fixtures, no
// ECS/GPU/file I/O involved.

#include "Physics/DynamicChainDetection.h"

#include <gtest/gtest.h>

namespace gte {
namespace {

Bone MakeBone(Vec3 position, std::int32_t parentBoneIndex, bool deformAfterPhysics)
{
    Bone bone;
    bone.position = position;
    bone.parentBoneIndex = parentBoneIndex;
    bone.deformAfterPhysics = deformAfterPhysics;
    return bone;
}

} // namespace

TEST(DynamicChainDetectionTests, SimpleThreeJointChainDetectedWithCorrectRootAndOrderAndRestLengths)
{
    // root(0, not flagged) -> chainRoot(1, not flagged) -> joint1(2) ->
    // joint2(3) -> joint3(4), all three joints flagged.
    SkeletonData skeleton;
    skeleton.bones.push_back(MakeBone(Vec3(0.0f, 0.0f, 0.0f), -1, false)); // 0
    skeleton.bones.push_back(MakeBone(Vec3(0.0f, 1.0f, 0.0f), 0, false)); // 1 - chain root/anchor
    skeleton.bones.push_back(MakeBone(Vec3(0.0f, 2.0f, 0.0f), 1, true)); // 2 - joint1
    skeleton.bones.push_back(MakeBone(Vec3(0.0f, 3.5f, 0.0f), 2, true)); // 3 - joint2 (1.5 units from joint1)
    skeleton.bones.push_back(MakeBone(Vec3(0.0f, 5.5f, 0.0f), 3, true)); // 4 - joint3 (2.0 units from joint2)

    const DynamicChainDetectionDefaults defaults{};
    const std::vector<DynamicChainDefinition> chains = DetectDynamicChains(skeleton, nullptr, defaults);

    ASSERT_EQ(chains.size(), 1u);
    const DynamicChainDefinition& chain = chains[0];
    EXPECT_EQ(chain.rootBoneIndex, 1);
    ASSERT_EQ(chain.jointBoneIndices.size(), 3u);
    EXPECT_EQ(chain.jointBoneIndices[0], 2);
    EXPECT_EQ(chain.jointBoneIndices[1], 3);
    EXPECT_EQ(chain.jointBoneIndices[2], 4);

    ASSERT_EQ(chain.restLengths.size(), 3u);
    EXPECT_NEAR(chain.restLengths[0], 1.0f, 1e-4f);
    EXPECT_NEAR(chain.restLengths[1], 1.5f, 1e-4f);
    EXPECT_NEAR(chain.restLengths[2], 2.0f, 1e-4f);
}

TEST(DynamicChainDetectionTests, BranchingRunDetectsTwoSeparateChainsEachStartingAtItsOwnBranchChild)
{
    // root(0) -> chainRoot(1, not flagged) -> joint1(2, flagged) -> {
    //   joint2a(3, flagged) -> joint3a(4, flagged),
    //   joint2b(5, flagged) -> joint3b(6, flagged),
    // }
    SkeletonData skeleton;
    skeleton.bones.push_back(MakeBone(Vec3(0.0f, 0.0f, 0.0f), -1, false)); // 0
    skeleton.bones.push_back(MakeBone(Vec3(0.0f, 1.0f, 0.0f), 0, false)); // 1
    skeleton.bones.push_back(MakeBone(Vec3(0.0f, 2.0f, 0.0f), 1, true)); // 2 - joint1 (branch point)
    skeleton.bones.push_back(MakeBone(Vec3(1.0f, 2.0f, 0.0f), 2, true)); // 3 - joint2a
    skeleton.bones.push_back(MakeBone(Vec3(2.0f, 2.0f, 0.0f), 3, true)); // 4 - joint3a
    skeleton.bones.push_back(MakeBone(Vec3(-1.0f, 2.0f, 0.0f), 2, true)); // 5 - joint2b
    skeleton.bones.push_back(MakeBone(Vec3(-2.0f, 2.0f, 0.0f), 5, true)); // 6 - joint3b

    const DynamicChainDetectionDefaults defaults{};
    const std::vector<DynamicChainDefinition> chains = DetectDynamicChains(skeleton, nullptr, defaults);

    ASSERT_EQ(chains.size(), 2u);
    for (const DynamicChainDefinition& chain : chains) {
        EXPECT_EQ(chain.rootBoneIndex, 2); // Both anchor to the branch bone itself.
        ASSERT_EQ(chain.jointBoneIndices.size(), 2u);
    }
    // Order not asserted across chains (both discovered by ascending bone
    // index) - but each chain's own joint order must be root-to-tip.
    EXPECT_EQ(chains[0].jointBoneIndices[0], 3);
    EXPECT_EQ(chains[0].jointBoneIndices[1], 4);
    EXPECT_EQ(chains[1].jointBoneIndices[0], 5);
    EXPECT_EQ(chains[1].jointBoneIndices[1], 6);
}

TEST(DynamicChainDetectionTests, RunShorterThanMinimumChainLengthDetectsNothing)
{
    SkeletonData skeleton;
    skeleton.bones.push_back(MakeBone(Vec3(0.0f, 0.0f, 0.0f), -1, false)); // 0
    skeleton.bones.push_back(MakeBone(Vec3(0.0f, 1.0f, 0.0f), 0, false)); // 1 - chain root
    skeleton.bones.push_back(MakeBone(Vec3(0.0f, 2.0f, 0.0f), 1, true)); // 2 - single joint, leaf

    DynamicChainDetectionDefaults defaults{};
    defaults.minimumChainLength = 2;
    const std::vector<DynamicChainDefinition> chains = DetectDynamicChains(skeleton, nullptr, defaults);

    EXPECT_TRUE(chains.empty());
}

TEST(DynamicChainDetectionTests, MatchingDynamicRigidBodyOverridesOnlyItsOwnJointMassAndDamping)
{
    SkeletonData skeleton;
    skeleton.bones.push_back(MakeBone(Vec3(0.0f, 0.0f, 0.0f), -1, false)); // 0
    skeleton.bones.push_back(MakeBone(Vec3(0.0f, 1.0f, 0.0f), 0, false)); // 1 - chain root
    skeleton.bones.push_back(MakeBone(Vec3(0.0f, 2.0f, 0.0f), 1, true)); // 2 - joint1
    skeleton.bones.push_back(MakeBone(Vec3(0.0f, 3.0f, 0.0f), 2, true)); // 3 - joint2

    PhysicsData physics;
    RigidBody body;
    body.boneIndex = 2; // matches joint1 only
    body.motionType = RigidBodyMotionType::Dynamic;
    body.mass = 3.5f;
    body.linearDamping = 0.42f;
    physics.rigidBodies.push_back(body);

    const DynamicChainDetectionDefaults defaults{};
    const std::vector<DynamicChainDefinition> chains = DetectDynamicChains(skeleton, &physics, defaults);

    ASSERT_EQ(chains.size(), 1u);
    ASSERT_EQ(chains[0].jointSettings.size(), 2u);
    EXPECT_NEAR(chains[0].jointSettings[0].mass, 3.5f, 1e-4f);
    EXPECT_NEAR(chains[0].jointSettings[0].damping, 0.42f, 1e-4f);
    // Sibling joint2 keeps the pure defaults - no RigidBody matched it.
    EXPECT_NEAR(chains[0].jointSettings[1].mass, defaults.defaultJointSettings.mass, 1e-4f);
    EXPECT_NEAR(chains[0].jointSettings[1].damping, defaults.defaultJointSettings.damping, 1e-4f);
}

TEST(DynamicChainDetectionTests, DegenerateRootBoneFlaggedAsPhysicsProducesZeroChainsFromThatRunButOthersStillDetected)
{
    SkeletonData skeleton;
    // Bone 0 is the skeleton's own literal root (parentBoneIndex == -1) AND
    // flagged deformAfterPhysics == true - no ancestor exists to anchor a
    // chain to, so this whole run must be discarded, never emitted with
    // rootBoneIndex == -1.
    skeleton.bones.push_back(MakeBone(Vec3(0.0f, 0.0f, 0.0f), -1, true)); // 0 - degenerate.
    skeleton.bones.push_back(MakeBone(Vec3(0.0f, 1.0f, 0.0f), 0, true)); // 1 - would-be continuation, also discarded.

    // A second, entirely separate, PROPERLY anchored chain elsewhere in the
    // same skeleton must still be detected normally.
    skeleton.bones.push_back(MakeBone(Vec3(5.0f, 0.0f, 0.0f), -1, false)); // 2 - a second, unrelated root.
    skeleton.bones.push_back(MakeBone(Vec3(5.0f, 1.0f, 0.0f), 2, false)); // 3 - its own chain root/anchor.
    skeleton.bones.push_back(MakeBone(Vec3(5.0f, 2.0f, 0.0f), 3, true)); // 4 - joint1
    skeleton.bones.push_back(MakeBone(Vec3(5.0f, 3.0f, 0.0f), 4, true)); // 5 - joint2

    const DynamicChainDetectionDefaults defaults{};
    const std::vector<DynamicChainDefinition> chains = DetectDynamicChains(skeleton, nullptr, defaults);

    ASSERT_EQ(chains.size(), 1u);
    EXPECT_EQ(chains[0].rootBoneIndex, 3);
    ASSERT_EQ(chains[0].jointBoneIndices.size(), 2u);
    EXPECT_EQ(chains[0].jointBoneIndices[0], 4);
    EXPECT_EQ(chains[0].jointBoneIndices[1], 5);
}

TEST(DynamicChainDetectionTests, EmptySkeletonDetectsNothing)
{
    SkeletonData skeleton;
    const DynamicChainDetectionDefaults defaults{};
    EXPECT_TRUE(DetectDynamicChains(skeleton, nullptr, defaults).empty());
}

} // namespace gte
