// Tier-1 tests for the per-group key memos of the cluster draw state. No device involved.

#include "Core/FeatureKit/ClusterDrawState.h"

#include <gtest/gtest.h>

#include <cstdint>

namespace gte::fx {
namespace {

ClusterDrawState::KeyQuery Query(std::uint64_t epoch)
{
    ClusterDrawState::KeyQuery query;
    query.group.epoch = epoch;
    query.group.mesh = MeshToken{ 3, 1 };
    query.group.material = MaterialToken{ 4, 1 };
    query.group.instanceCount = 50000;
    query.group.depthOnly = false;
    query.clusterCount = 22;
    query.generation = 0;
    return query;
}

TEST(ClusterKeyReuseTests, AKeyIsFoundOnlyForTheExactSameGroup)
{
    ClusterDrawState state("TestKeyMemoExact", { 1, 2 });
    const ClusterDrawState::KeyQuery base = Query(7);
    state.StoreSetKey(base, 1234);
    ASSERT_TRUE(state.FindSetKey(base).has_value());
    EXPECT_EQ(*state.FindSetKey(base), 1234u);

    const auto missing = [&](auto&& edit) {
        ClusterDrawState::KeyQuery other = base;
        edit(other);
        return !state.FindSetKey(other).has_value();
    };
    EXPECT_TRUE(missing([](ClusterDrawState::KeyQuery& q) { q.group.epoch = 8; }));
    EXPECT_TRUE(missing([](ClusterDrawState::KeyQuery& q) { q.group.mesh.index = 9; }));
    EXPECT_TRUE(missing([](ClusterDrawState::KeyQuery& q) { q.group.mesh.generation = 2; }));
    EXPECT_TRUE(missing([](ClusterDrawState::KeyQuery& q) { q.group.material.index = 9; }));
    EXPECT_TRUE(missing([](ClusterDrawState::KeyQuery& q) { q.group.instanceCount = 49999; }));
    EXPECT_TRUE(missing([](ClusterDrawState::KeyQuery& q) { q.group.depthOnly = true; }));
    EXPECT_TRUE(missing([](ClusterDrawState::KeyQuery& q) { q.clusterCount = 23; }));
    EXPECT_TRUE(missing([](ClusterDrawState::KeyQuery& q) { q.generation = 1; }));
}

TEST(ClusterKeyReuseTests, EpochZeroIsNeverStoredOrFound)
{
    ClusterDrawState state("TestKeyMemoZero", { 1, 2 });
    state.StoreSetKey(Query(0), 5);
    state.StoreHistoryKey(Query(0), 6);
    EXPECT_FALSE(state.FindSetKey(Query(0)).has_value());
    EXPECT_FALSE(state.FindHistoryKey(Query(0)).has_value());
}

TEST(ClusterKeyReuseTests, ANewEpochDropsTheOldKeys)
{
    ClusterDrawState state("TestKeyMemoEpoch", { 1, 2 });
    state.StoreSetKey(Query(1), 10);
    state.StoreSetKey(Query(2), 20);
    EXPECT_FALSE(state.FindSetKey(Query(1)).has_value());
    ASSERT_TRUE(state.FindSetKey(Query(2)).has_value());
    EXPECT_EQ(*state.FindSetKey(Query(2)), 20u);
}

TEST(ClusterKeyReuseTests, SetKeysAndHistoryKeysAreSeparate)
{
    ClusterDrawState state("TestKeyMemoKinds", { 1, 2 });
    state.StoreSetKey(Query(3), 11);
    EXPECT_FALSE(state.FindHistoryKey(Query(3)).has_value());
    state.StoreHistoryKey(Query(3), 22);
    EXPECT_EQ(*state.FindSetKey(Query(3)), 11u);
    EXPECT_EQ(*state.FindHistoryKey(Query(3)), 22u);
}

TEST(ClusterKeyReuseTests, ManyGroupsStayBoundedAndTheNewestIsKept)
{
    ClusterDrawState state("TestKeyMemoCap", { 1, 2 });
    for (std::uint32_t i = 0; i < 3 * ClusterDrawState::kMaxKeyMemos; ++i) {
        ClusterDrawState::KeyQuery query = Query(5);
        query.group.mesh.index = 100 + i;
        state.StoreSetKey(query, i);
        ASSERT_TRUE(state.FindSetKey(query).has_value());
        EXPECT_EQ(*state.FindSetKey(query), i);
    }
}

} // namespace
} // namespace gte::fx
