// Tests: SceneServiceBlackboardKey() distinctness (Tier-1) and SceneServicesDescriptorSet on a headless VkDevice (Tier-2).

#include "Renderer/SceneServicesDescriptorSet.h"

#include "Renderer/GraphicsHandleRegistry.h" // Native() - this file compares DescriptorSetHandle values.

#include "../Fakes/HeadlessRenderGraphFixture.h"

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <vector>

namespace gte {
namespace {

// A weak combiner (xor/add) or keying by slot alone could alias two (slot, view) pairs onto one key.
TEST(SceneServiceBlackboardKeyTest, Every824SlotViewPairProducesAPairwiseDistinctKey)
{
    const std::vector<rg::RenderViewId> views = {
        rg::RenderViewId::Shared(),
        rg::RenderViewId::Named("Game"),
        rg::RenderViewId::Named("Scene"),
    };

    std::vector<std::uint64_t> keys;
    keys.reserve(kSceneServiceSlotCount * views.size());

    for (std::uint32_t slotIndex = 0; slotIndex < kSceneServiceSlotCount; ++slotIndex) {
        for (const rg::RenderViewId& view : views) {
            keys.push_back(SceneServiceBlackboardKey(slotIndex, view).hash);
        }
    }

    ASSERT_EQ(keys.size(), 24u);

    for (std::size_t i = 0; i < keys.size(); ++i) {
        for (std::size_t j = i + 1; j < keys.size(); ++j) {
            EXPECT_NE(keys[i], keys[j]) << "collision between pair index " << i << " and " << j;
        }
    }
}

TEST(SceneServiceBlackboardKeyTest, SameSlotDifferentViewsNeverAlias)
{
    const rg::RenderPassId gameKey = SceneServiceBlackboardKey(0u, rg::RenderViewId::Named("Game"));
    const rg::RenderPassId sceneKey = SceneServiceBlackboardKey(0u, rg::RenderViewId::Named("Scene"));

    EXPECT_FALSE(gameKey == sceneKey);
}

TEST(SceneServiceBlackboardKeyTest, SameViewDifferentSlotsNeverAlias)
{
    const rg::RenderViewId gameView = rg::RenderViewId::Named("Game");
    const rg::RenderPassId firstKey = SceneServiceBlackboardKey(0u, gameView);
    const rg::RenderPassId secondKey = SceneServiceBlackboardKey(2u, gameView);

    EXPECT_FALSE(firstKey == secondKey);
}

// Rewrite() with every slot defaulted returns a valid set backed by real dummy textures.
TEST(SceneServicesDescriptorSetTest, RewriteWithAllDefaultedSlotsReturnsAValidSetBackedByRealDummies)
{
    HeadlessRenderGraphFixture fixture;
    if (!fixture.IsUsable()) {
        GTEST_SKIP() << fixture.SkipReason();
    }

    SceneServicesDescriptorSet sceneServices(fixture.GetRenderer());

    std::array<SceneServicesDescriptorSet::ResolvedSlot, kSceneServiceSlotCount> resolved{};
    const DescriptorSetHandle set = sceneServices.Rewrite(rg::RenderViewId::Named("Game"), resolved);

    EXPECT_NE(set, DescriptorSetHandle::Invalid);

    EXPECT_NE(sceneServices.DummyImage2DTextureFor(0u).View(), static_cast<VkImageView>(VK_NULL_HANDLE));
    EXPECT_NE(sceneServices.DummyImage2DTextureFor(0u).Sampler(), static_cast<VkSampler>(VK_NULL_HANDLE));
    EXPECT_NE(sceneServices.DummyImage2DTextureFor(1u).View(), static_cast<VkImageView>(VK_NULL_HANDLE));
}

// One descriptor set per concurrently active view, never one shared instance.
TEST(SceneServicesDescriptorSetTest, RewriteForTwoDifferentViewsProducesTwoDifferentDescriptorSets)
{
    HeadlessRenderGraphFixture fixture;
    if (!fixture.IsUsable()) {
        GTEST_SKIP() << fixture.SkipReason();
    }

    SceneServicesDescriptorSet sceneServices(fixture.GetRenderer());

    std::array<SceneServicesDescriptorSet::ResolvedSlot, kSceneServiceSlotCount> resolvedA{};
    std::array<SceneServicesDescriptorSet::ResolvedSlot, kSceneServiceSlotCount> resolvedB{};

    const rg::RenderViewId viewA = rg::RenderViewId::Named("Game");
    const rg::RenderViewId viewB = rg::RenderViewId::Named("Scene");

    const DescriptorSetHandle setA = sceneServices.Rewrite(viewA, resolvedA);
    const DescriptorSetHandle setB = sceneServices.Rewrite(viewB, resolvedB);

    EXPECT_NE(setA, DescriptorSetHandle::Invalid);
    EXPECT_NE(setB, DescriptorSetHandle::Invalid);
    EXPECT_NE(sceneServices.DescriptorSetFor(viewA), sceneServices.DescriptorSetFor(viewB));
    EXPECT_EQ(sceneServices.DescriptorSetFor(viewA), setA);
    EXPECT_EQ(sceneServices.DescriptorSetFor(viewB), setB);
}

TEST(SceneServicesDescriptorSetTest, DescriptorSetForAnUnseenViewIsInvalid)
{
    HeadlessRenderGraphFixture fixture;
    if (!fixture.IsUsable()) {
        GTEST_SKIP() << fixture.SkipReason();
    }

    SceneServicesDescriptorSet sceneServices(fixture.GetRenderer());
    EXPECT_EQ(sceneServices.DescriptorSetFor(rg::RenderViewId::Named("NeverRewritten")), DescriptorSetHandle::Invalid);
}

} // namespace
} // namespace gte
