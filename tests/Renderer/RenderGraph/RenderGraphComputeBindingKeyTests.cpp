// Tier-1 tests for the allocation-free binding cache key.

#include "Renderer/RenderGraph/RenderGraphComputeBindingKey.h"

#include <gtest/gtest.h>

#include <string>
#include <unordered_map>

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

} // namespace
} // namespace gte::rg
