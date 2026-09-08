// Unit tests for ApplyJointPhysicsOverrides() (task_manager/verlet-integration-11,
// PHASE2_RUNTIME_APPLICATION_ON_INSTANTIATION.md) -
// src/Physics/JointPhysicsOverrideApplication.h/.cpp. Pure/free function, no
// ECS/GPU/file I/O - mirrors DynamicChainDefinitionTests.cpp's own
// hand-built-fixture style.

#include "Physics/JointPhysicsOverrideApplication.h"

#include <gtest/gtest.h>

namespace gte {
namespace {

DynamicChainDefinition BuildTwoJointChain()
{
    DynamicChainDefinition chain;
    chain.rootBoneIndex = 0;
    chain.jointBoneIndices = { 1, 2 };
    chain.parentJointIndex = DynamicChainDefinition::MakeLinearParentIndices(2);
    chain.restLengths = { 1.0f, 1.0f };
    chain.jointSettings.assign(2, DynamicJointSettings{});
    return chain;
}

TEST(JointPhysicsOverrideApplicationTest, MatchingOverrideRewritesDampingStiffnessMass)
{
    std::vector<DynamicChainDefinition> chains{ BuildTwoJointChain() };
    const std::vector<JointPhysicsOverride> overrides{ { /*boneIndex=*/2, 0.9f, 0.05f, 3.0f } };

    ApplyJointPhysicsOverrides(chains, overrides);

    EXPECT_FLOAT_EQ(chains[0].jointSettings[0].damping, DynamicJointSettings{}.damping); // untouched (bone 1)
    EXPECT_FLOAT_EQ(chains[0].jointSettings[1].damping, 0.9f); // bone 2 - overridden
    EXPECT_FLOAT_EQ(chains[0].jointSettings[1].stiffness, 0.05f);
    EXPECT_FLOAT_EQ(chains[0].jointSettings[1].mass, 3.0f);
}

TEST(JointPhysicsOverrideApplicationTest, OverrideWithNoMatchingBoneIndexIsSilentlyIgnored)
{
    std::vector<DynamicChainDefinition> chains{ BuildTwoJointChain() };
    const std::vector<JointPhysicsOverride> overrides{ { /*boneIndex=*/999, 0.9f, 0.05f, 3.0f } };

    ApplyJointPhysicsOverrides(chains, overrides);

    EXPECT_FLOAT_EQ(chains[0].jointSettings[0].damping, DynamicJointSettings{}.damping);
    EXPECT_FLOAT_EQ(chains[0].jointSettings[1].damping, DynamicJointSettings{}.damping);
}

TEST(JointPhysicsOverrideApplicationTest, EmptyOverrideListLeavesEveryChainUnchanged)
{
    std::vector<DynamicChainDefinition> chains{ BuildTwoJointChain() };
    ApplyJointPhysicsOverrides(chains, {});
    EXPECT_FLOAT_EQ(chains[0].jointSettings[0].mass, DynamicJointSettings{}.mass);
    EXPECT_FLOAT_EQ(chains[0].jointSettings[1].mass, DynamicJointSettings{}.mass);
}

TEST(JointPhysicsOverrideApplicationTest, DuplicateBoneIndexOverridesLastEntryWins)
{
    std::vector<DynamicChainDefinition> chains{ BuildTwoJointChain() };
    const std::vector<JointPhysicsOverride> overrides{
        { 1, 0.1f, 0.1f, 1.0f },
        { 1, 0.7f, 0.6f, 5.0f }, // same boneIndex as above - this one must win.
    };

    ApplyJointPhysicsOverrides(chains, overrides);

    EXPECT_FLOAT_EQ(chains[0].jointSettings[0].damping, 0.7f);
    EXPECT_FLOAT_EQ(chains[0].jointSettings[0].mass, 5.0f);
}

TEST(JointPhysicsOverrideApplicationTest, AppliesAcrossMultipleChainsIndependently)
{
    DynamicChainDefinition chainA = BuildTwoJointChain();
    DynamicChainDefinition chainB = BuildTwoJointChain();
    chainB.rootBoneIndex = 10;
    chainB.jointBoneIndices = { 11, 12 }; // Disjoint bone indices from chainA - matches real DetectDynamicChains() invariant.
    std::vector<DynamicChainDefinition> chains{ chainA, chainB };

    const std::vector<JointPhysicsOverride> overrides{ { 11, 0.5f, 0.5f, 2.0f } };
    ApplyJointPhysicsOverrides(chains, overrides);

    EXPECT_FLOAT_EQ(chains[1].jointSettings[0].damping, 0.5f); // chainB's first joint (bone 11)
    EXPECT_FLOAT_EQ(chains[0].jointSettings[0].damping, DynamicJointSettings{}.damping); // chainA untouched
}

} // namespace
} // namespace gte
