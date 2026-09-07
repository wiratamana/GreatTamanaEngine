// Unit tests for FindDynamicChainJointByBoneIndex() (src/Physics/
// DynamicChainDefinition.h) - the shared "given a bone index, which chain/
// joint-within-chain is it?" lookup added by task_manager/verlet-integration-5,
// PHASE1_CHAIN_LOOKUP_AND_SELECTION_FOUNDATION.md, shared by BoneViewerWindow
// (Phase 2) and InspectorPanel (Phase 3). Pure function - hand-built
// DynamicChainDefinition fixtures, no ECS/GPU/file I/O involved.

#include "Physics/DynamicChainDefinition.h"

#include <gtest/gtest.h>

namespace gte {
namespace {

std::vector<DynamicChainDefinition> MakeTwoChains()
{
    DynamicChainDefinition chain0;
    chain0.rootBoneIndex = 1;
    chain0.jointBoneIndices = { 2, 3, 4 };

    DynamicChainDefinition chain1;
    chain1.rootBoneIndex = 6;
    chain1.jointBoneIndices = { 7, 8 };

    return { chain0, chain1 };
}

} // namespace

TEST(DynamicChainDefinitionTests, MidChainBoneIndexResolvesToCorrectChainAndPosition)
{
    const std::vector<DynamicChainDefinition> chains = MakeTwoChains();

    const DynamicChainJointLocation location = FindDynamicChainJointByBoneIndex(chains, 3);
    EXPECT_TRUE(location.IsValid());
    EXPECT_EQ(location.chainIndex, 0);
    EXPECT_EQ(location.jointIndexInChain, 1);
}

TEST(DynamicChainDefinitionTests, SecondChainBoneIndexResolvesToCorrectChainAndPosition)
{
    const std::vector<DynamicChainDefinition> chains = MakeTwoChains();

    const DynamicChainJointLocation location = FindDynamicChainJointByBoneIndex(chains, 8);
    EXPECT_TRUE(location.IsValid());
    EXPECT_EQ(location.chainIndex, 1);
    EXPECT_EQ(location.jointIndexInChain, 1);
}

TEST(DynamicChainDefinitionTests, BoneIndexNotAJointOfAnyChainReturnsInvalid)
{
    const std::vector<DynamicChainDefinition> chains = MakeTwoChains();

    // Bone 5 sits between the two chains but is not a joint of either.
    const DynamicChainJointLocation notAJoint = FindDynamicChainJointByBoneIndex(chains, 5);
    EXPECT_FALSE(notAJoint.IsValid());
    EXPECT_EQ(notAJoint.chainIndex, -1);
    EXPECT_EQ(notAJoint.jointIndexInChain, -1);

    // A chain's own rootBoneIndex is the anchor, never a joint itself.
    const DynamicChainJointLocation rootBone = FindDynamicChainJointByBoneIndex(chains, 1);
    EXPECT_FALSE(rootBone.IsValid());
    const DynamicChainJointLocation otherRootBone = FindDynamicChainJointByBoneIndex(chains, 6);
    EXPECT_FALSE(otherRootBone.IsValid());
}

TEST(DynamicChainDefinitionTests, NegativeBoneIndexReturnsInvalidImmediately)
{
    const std::vector<DynamicChainDefinition> chains = MakeTwoChains();

    const DynamicChainJointLocation location = FindDynamicChainJointByBoneIndex(chains, -1);
    EXPECT_FALSE(location.IsValid());
    EXPECT_EQ(location.chainIndex, -1);
    EXPECT_EQ(location.jointIndexInChain, -1);
}

TEST(DynamicChainDefinitionTests, EmptyChainsListReturnsInvalidForAnyInput)
{
    const std::vector<DynamicChainDefinition> chains;

    EXPECT_FALSE(FindDynamicChainJointByBoneIndex(chains, 0).IsValid());
    EXPECT_FALSE(FindDynamicChainJointByBoneIndex(chains, 42).IsValid());
    EXPECT_FALSE(FindDynamicChainJointByBoneIndex(chains, -1).IsValid());
}

TEST(DynamicChainDefinitionTests, LocationIsNeverPartiallyValid)
{
    const std::vector<DynamicChainDefinition> chains = MakeTwoChains();

    const DynamicChainJointLocation found = FindDynamicChainJointByBoneIndex(chains, 2);
    EXPECT_TRUE(found.IsValid());
    EXPECT_GE(found.chainIndex, 0);
    EXPECT_GE(found.jointIndexInChain, 0);

    const DynamicChainJointLocation notFound = FindDynamicChainJointByBoneIndex(chains, 999);
    EXPECT_FALSE(notFound.IsValid());
    EXPECT_EQ(notFound.chainIndex, -1);
    EXPECT_EQ(notFound.jointIndexInChain, -1);
}

} // namespace gte
