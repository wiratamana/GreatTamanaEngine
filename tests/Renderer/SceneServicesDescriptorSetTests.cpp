// Unit tests for Block 4 "Global Scene Services Descriptor Set" campaign
// (task_manager/better-render-pass-6), PHASE1
// (PHASE1_SCENE_SERVICE_TYPES_AND_BLACKBOARD_KEY.md) -
// SceneServiceBlackboardKey()'s per-(slot, view) distinctness guarantee.
// Entirely Tier-1 - no live VkDevice needed, mirroring
// Renderer/RenderGraph/RenderPipelineTests.cpp's own established style (a
// real rg::RenderViewId with zero Vulkan device involved).

#include "Renderer/SceneServicesDescriptorSet.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <vector>

namespace gte {
namespace {

// PHASE0_MASTER_STRATEGY.md's own rule 6 (and this phase's own "What NOT to
// do") - the #1 regression this test exists to catch forever: a naive XOR/
// addition combiner, or keying by `slot` alone, can silently alias two
// DIFFERENT (slot, view) pairs onto the SAME blackboard key. Every one of
// the 8 slots, crossed with 3 distinct views (24 pairs total), must produce
// a pairwise-distinct key.
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
        const auto slot = static_cast<SceneServiceSlot>(slotIndex);
        for (const rg::RenderViewId& view : views) {
            keys.push_back(SceneServiceBlackboardKey(slot, view).hash);
        }
    }

    ASSERT_EQ(keys.size(), 24u);

    for (std::size_t i = 0; i < keys.size(); ++i) {
        for (std::size_t j = i + 1; j < keys.size(); ++j) {
            EXPECT_NE(keys[i], keys[j]) << "collision between pair index " << i << " and " << j;
        }
    }
}

// A focused, two-value sanity check on top of the all-pairs sweep above:
// the SAME slot, across two DIFFERENT views, must never alias - this is the
// exact cross-view data-corruption hazard (Game + Scene active together,
// same frame) Block 4's whole design exists to prevent.
TEST(SceneServiceBlackboardKeyTest, SameSlotDifferentViewsNeverAlias)
{
    const rg::RenderPassId gameKey = SceneServiceBlackboardKey(SceneServiceSlot::ShadowMap, rg::RenderViewId::Named("Game"));
    const rg::RenderPassId sceneKey =
        SceneServiceBlackboardKey(SceneServiceSlot::ShadowMap, rg::RenderViewId::Named("Scene"));

    EXPECT_FALSE(gameKey == sceneKey);
}

// A focused, two-value sanity check: the SAME view, across two DIFFERENT
// slots, must never alias either.
TEST(SceneServiceBlackboardKeyTest, SameViewDifferentSlotsNeverAlias)
{
    const rg::RenderViewId gameView = rg::RenderViewId::Named("Game");
    const rg::RenderPassId shadowKey = SceneServiceBlackboardKey(SceneServiceSlot::ShadowMap, gameView);
    const rg::RenderPassId fogKey = SceneServiceBlackboardKey(SceneServiceSlot::VolumetricFog, gameView);

    EXPECT_FALSE(shadowKey == fogKey);
}

// SceneServiceSlotResourceKind() - the ONE Image3D-kind slot among today's 3
// named slots is VolumetricFog; every other slot (named or reserved) is
// Image2D.
TEST(SceneServiceSlotResourceKindTest, OnlyVolumetricFogIsImage3D)
{
    EXPECT_EQ(SceneServiceSlotResourceKind(SceneServiceSlot::ShadowMap), SceneServiceResourceKind::Image2D);
    EXPECT_EQ(SceneServiceSlotResourceKind(SceneServiceSlot::GIVolume), SceneServiceResourceKind::Image2D);
    EXPECT_EQ(SceneServiceSlotResourceKind(SceneServiceSlot::VolumetricFog), SceneServiceResourceKind::Image3D);

    for (std::uint32_t slotIndex = 3; slotIndex < kSceneServiceSlotCount; ++slotIndex) {
        const auto slot = static_cast<SceneServiceSlot>(slotIndex);
        EXPECT_EQ(SceneServiceSlotResourceKind(slot), SceneServiceResourceKind::Image2D);
    }
}

} // namespace
} // namespace gte
