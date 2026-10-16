// Tier-1 tests for the allocation-free binding cache key, the rewrite policy and entry staleness.

#include "Renderer/RenderGraph/RenderGraphComputeBindingCache.h"
#include "Renderer/RenderGraph/RenderGraphComputeBindingKey.h"
#include "Renderer/RenderGraph/RenderGraphPersistentResourceCache.h"

#include <gtest/gtest.h>

#include <string>
#include <unordered_map>
#include <vector>

namespace gte::rg {
namespace {

using Map = std::unordered_map<ComputeBindingKey, int, ComputeBindingKeyHash, ComputeBindingKeyEqual>;

VkDescriptorSetLayout FakeLayout(std::uintptr_t value)
{
    return reinterpret_cast<VkDescriptorSetLayout>(value);
}

TEST(ComputeBindingKeyTest, EqualContentInDifferentBuffersMatches)
{
    const std::string a = "Pass.A";
    const std::string b = "Pass.A";
    ASSERT_NE(a.data(), b.data());
    const ComputeBindingKeyEqual eq;
    const ComputeBindingKeyHash hash;
    const ComputeBindingKey owned{ a, ViewScope::GameView, FakeLayout(8) };
    const ComputeBindingKeyView view{ b, ViewScope::GameView, FakeLayout(8) };
    EXPECT_TRUE(eq(owned, view));
    EXPECT_TRUE(eq(view, owned));
    EXPECT_EQ(hash(owned), hash(view));
}

TEST(ComputeBindingKeyTest, ScopeLayoutAndNameDistinguishKeys)
{
    const ComputeBindingKeyEqual eq;
    const ComputeBindingKeyView base{ "Pass", ViewScope::GameView, FakeLayout(8) };
    EXPECT_FALSE(eq(base, ComputeBindingKeyView{ "Pass", ViewScope::SceneView, FakeLayout(8) }));
    EXPECT_FALSE(eq(base, ComputeBindingKeyView{ "Pass", ViewScope::GameView, FakeLayout(16) }));
    EXPECT_FALSE(eq(base, ComputeBindingKeyView{ "Pass2", ViewScope::GameView, FakeLayout(8) }));
}

TEST(ComputeBindingKeyTest, HashDiffersForDifferentScopeAndLayout)
{
    const ComputeBindingKeyHash hash;
    const std::size_t base = hash(ComputeBindingKeyView{ "Pass", ViewScope::GameView, FakeLayout(8) });
    EXPECT_NE(base, hash(ComputeBindingKeyView{ "Pass", ViewScope::SceneView, FakeLayout(8) }));
    EXPECT_NE(base, hash(ComputeBindingKeyView{ "Pass", ViewScope::GameView, FakeLayout(16) }));
}

TEST(ComputeBindingKeyTest, StringViewLookupFindsOwnedKey)
{
    Map map;
    map.emplace(ComputeBindingKey{ "Shadow.Mask.Dispatch", ViewScope::SceneView, FakeLayout(24) }, 7);

    const char* literal = "Shadow.Mask.Dispatch";
    const auto hit = map.find(ComputeBindingKeyView{ std::string_view(literal), ViewScope::SceneView, FakeLayout(24) });
    ASSERT_NE(hit, map.end());
    EXPECT_EQ(hit->second, 7);

    EXPECT_EQ(map.find(ComputeBindingKeyView{ literal, ViewScope::GameView, FakeLayout(24) }), map.end());
    EXPECT_EQ(map.find(ComputeBindingKeyView{ literal, ViewScope::SceneView, FakeLayout(32) }), map.end());
}

TEST(ComputeBindingKeyTest, InstanceIsPartOfEqualityAndHash)
{
    const ComputeBindingKeyEqual eq;
    const ComputeBindingKeyHash hash;
    const ComputeBindingKeyView base{ "Pass", ViewScope::Shared, FakeLayout(8), 1 };
    EXPECT_TRUE(eq(base, ComputeBindingKeyView{ "Pass", ViewScope::Shared, FakeLayout(8), 1 }));
    EXPECT_FALSE(eq(base, ComputeBindingKeyView{ "Pass", ViewScope::Shared, FakeLayout(8), 2 }));
    EXPECT_FALSE(eq(base, ComputeBindingKeyView{ "Pass", ViewScope::Shared, FakeLayout(8) }));
    EXPECT_NE(hash(base), hash(ComputeBindingKeyView{ "Pass", ViewScope::Shared, FakeLayout(8), 2 }));
}

TEST(ComputeBindingKeyTest, ManyInstancesOfOneNameAreManyEntries)
{
    Map map;
    for (std::uint64_t instance = 0; instance < 50; ++instance) {
        map.emplace(ComputeBindingKey{ "GpuSkinning.Dispatch", ViewScope::Shared, FakeLayout(8), instance },
            static_cast<int>(instance));
    }
    EXPECT_EQ(map.size(), 50u);
    const auto hit = map.find(ComputeBindingKeyView{ "GpuSkinning.Dispatch", ViewScope::Shared, FakeLayout(8), 17 });
    ASSERT_NE(hit, map.end());
    EXPECT_EQ(hit->second, 17);
}

TEST(DescriptorWritesNeedRewriteTest, IdenticalBufferWritesAreNotRewritten)
{
    const std::vector<DescriptorWrite> writes = {
        DescriptorWrite::StorageBuffer(0, reinterpret_cast<VkBuffer>(std::uintptr_t{ 16 })),
        DescriptorWrite::StorageBuffer(1, reinterpret_cast<VkBuffer>(std::uintptr_t{ 32 })) };
    EXPECT_TRUE(DescriptorWritesNeedRewrite({}, writes));
    EXPECT_FALSE(DescriptorWritesNeedRewrite(writes, writes));

    std::vector<DescriptorWrite> changed = writes;
    changed[1] = DescriptorWrite::StorageBuffer(1, reinterpret_cast<VkBuffer>(std::uintptr_t{ 48 }));
    EXPECT_TRUE(DescriptorWritesNeedRewrite(writes, changed));
}

TEST(DescriptorWritesNeedRewriteTest, ImageWritesAlwaysRewrite)
{
    const std::vector<DescriptorWrite> writes = {
        DescriptorWrite::StorageImage(0, reinterpret_cast<VkImageView>(std::uintptr_t{ 16 })) };
    EXPECT_TRUE(DescriptorWritesNeedRewrite(writes, writes));
}

// Entries of rigs that disappear are freed once they go unused for kDefaultStaleFrames.
TEST(ComputeBindingCacheStalenessTest, UnusedEntriesAreStaleOnlyPastTheThreshold)
{
    constexpr std::uint64_t kStale = RenderGraphComputeBindingCache::kDefaultStaleFrames;
    EXPECT_FALSE(IsStaleCacheEntry(10, 10 + kStale, kStale));
    EXPECT_TRUE(IsStaleCacheEntry(10, 10 + kStale + 1, kStale));
}

} // namespace
} // namespace gte::rg
