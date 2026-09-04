// Unit tests for DynamicChainRigCache (src/Game/Physics/DynamicChainRigCache.h)
// - PhysicsSystem's own path-keyed cache of detected dynamic bone chains
// (verlet-integration-1, PHASE4_PARAMETER_AUTHORING_AND_DATA_DRIVEN_CONFIG.md,
// 3.3). Trivial Register()/TryGet()/TryGetMutable() round-trip, mirroring
// SkeletalRigCacheTests.cpp's own shape - Tier 1, no ECS/GPU dependency.

#include "Game/Physics/DynamicChainRigCache.h"

#include <gtest/gtest.h>

namespace gte {
namespace {

DynamicChainRigCache::ModelEntry BuildSampleEntry()
{
    DynamicChainRigCache::ModelEntry entry;

    Bone root;
    root.position = Vec3(0.0f, 0.0f, 0.0f);
    root.parentBoneIndex = -1;
    entry.skeleton.bones.push_back(root);

    DynamicChainDefinition chain;
    chain.rootBoneIndex = 0;
    chain.jointBoneIndices = { 1 };
    chain.jointSettings = { DynamicJointSettings{} };
    chain.restLengths = { 1.0f };
    entry.chains.push_back(chain);

    return entry;
}

} // namespace

TEST(DynamicChainRigCacheTests, RegisterThenTryGetRoundTripsBothSkeletonAndChains)
{
    DynamicChainRigCache cache;
    cache.Register("Model.gta", BuildSampleEntry());

    const DynamicChainRigCache::ModelEntry* found = cache.TryGet("Model.gta");
    ASSERT_NE(found, nullptr);
    ASSERT_EQ(found->skeleton.bones.size(), 1u);
    ASSERT_EQ(found->chains.size(), 1u);
    EXPECT_EQ(found->chains[0].rootBoneIndex, 0);
    ASSERT_EQ(found->chains[0].jointBoneIndices.size(), 1u);
    EXPECT_EQ(found->chains[0].jointBoneIndices[0], 1);
}

TEST(DynamicChainRigCacheTests, UnknownPathReturnsNullptr)
{
    DynamicChainRigCache cache;
    cache.Register("Model.gta", BuildSampleEntry());

    EXPECT_EQ(cache.TryGet("SomeOtherModel.gta"), nullptr);
}

TEST(DynamicChainRigCacheTests, TryGetMutableAllowsLiveEditOfJointSettingsAndTryGetSeesTheEdit)
{
    DynamicChainRigCache cache;
    cache.Register("Model.gta", BuildSampleEntry());

    DynamicChainRigCache::ModelEntry* mutableEntry = cache.TryGetMutable("Model.gta");
    ASSERT_NE(mutableEntry, nullptr);
    mutableEntry->chains[0].jointSettings[0].damping = 0.75f;

    const DynamicChainRigCache::ModelEntry* found = cache.TryGet("Model.gta");
    ASSERT_NE(found, nullptr);
    EXPECT_NEAR(found->chains[0].jointSettings[0].damping, 0.75f, 1e-4f);
}

TEST(DynamicChainRigCacheTests, TryGetMutableOnUnknownPathReturnsNullptr)
{
    DynamicChainRigCache cache;
    EXPECT_EQ(cache.TryGetMutable("Unknown.gta"), nullptr);
}

} // namespace gte
