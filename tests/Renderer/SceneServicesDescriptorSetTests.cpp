// Unit tests for Block 4 "Global Scene Services Descriptor Set" campaign
// (task_manager/better-render-pass-6). PHASE1
// (PHASE1_SCENE_SERVICE_TYPES_AND_BLACKBOARD_KEY.md) -
// SceneServiceBlackboardKey()'s per-(slot, view) distinctness guarantee.
// Entirely Tier-1 - no live VkDevice needed, mirroring
// Renderer/RenderGraph/RenderPipelineTests.cpp's own established style (a
// real rg::RenderViewId with zero Vulkan device involved).
//
// PHASE2 (PHASE2_SCENE_SERVICES_DESCRIPTOR_SET_CLASS.md) EXTENDS this SAME
// file with real, Tier-2 (live headless VkDevice) integration tests for the
// SceneServicesDescriptorSet CLASS itself, using the SAME
// HeadlessRenderGraphFixture (tests/Fakes/HeadlessRenderGraphFixture.h)
// every other real-device Renderer/RenderGraph test in this codebase already
// uses - GTEST_SKIP()-guarded exactly like every other HeadlessSurfaceProvider
// consumer whenever this machine's Vulkan driver/loader doesn't report
// VK_EXT_headless_surface.

#include "Renderer/SceneServicesDescriptorSet.h"

#include "../Fakes/HeadlessRenderGraphFixture.h"

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <string>
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

// --- PHASE2 (PHASE2_SCENE_SERVICES_DESCRIPTOR_SET_CLASS.md) ---------------
//
// Real, Tier-2 integration tests for the SceneServicesDescriptorSet class
// itself, against a real headless VkDevice (HeadlessRenderGraphFixture).

// 1. Construct a real SceneServicesDescriptorSet, Rewrite() for one view
// with every slot left all-defaulted (VK_NULL_HANDLE/VK_NULL_HANDLE) -
// confirms the returned VkDescriptorSet is non-null, and that the real,
// uploaded dummy resources themselves are valid/non-null and correctly
// typed (see this phase's own "Testability correction" for why this is the
// buildable equivalent of the source spec's Section 6 item 2 - there is no
// live-descriptor-set-introspection mechanism to use instead).
TEST(SceneServicesDescriptorSetTest, RewriteWithAllDefaultedSlotsReturnsAValidSetBackedByRealDummies)
{
    HeadlessRenderGraphFixture fixture;
    if (!fixture.IsUsable()) {
        GTEST_SKIP() << fixture.SkipReason();
    }

    SceneServicesDescriptorSet sceneServices(fixture.GetRenderer());

    std::array<SceneServicesDescriptorSet::ResolvedSlot, kSceneServiceSlotCount> resolved{};
    const VkDescriptorSet set = sceneServices.Rewrite(rg::RenderViewId::Named("Game"), resolved);

    EXPECT_NE(set, static_cast<VkDescriptorSet>(VK_NULL_HANDLE));

    EXPECT_NE(sceneServices.DummyImage2DTextureFor(SceneServiceSlot::ShadowMap).View(),
        static_cast<VkImageView>(VK_NULL_HANDLE));
    EXPECT_NE(sceneServices.DummyImage2DTextureFor(SceneServiceSlot::ShadowMap).Sampler(),
        static_cast<VkSampler>(VK_NULL_HANDLE));
    EXPECT_NE(sceneServices.DummyImage2DTextureFor(SceneServiceSlot::GIVolume).View(),
        static_cast<VkImageView>(VK_NULL_HANDLE));

    EXPECT_NE(sceneServices.DummyVolumetricFogTexture().View(), static_cast<VkImageView>(VK_NULL_HANDLE));
    EXPECT_NE(sceneServices.DummyVolumetricFogTexture().Sampler(), static_cast<VkSampler>(VK_NULL_HANDLE));
    // Proof it is genuinely a real, 1x1x1 3D volume, not a 2D texture
    // masquerading as one - no Vulkan-level image-type query is even
    // necessary once the test holds a true const VolumeTexture&.
    EXPECT_EQ(sceneServices.DummyVolumetricFogTexture().Depth(), 1);
    ResetSceneServiceRegistryForTesting();
}

// 2. Two different views, two different resolved inputs, two different
// VkDescriptorSet handles - PHASE0's global rule 5 (one VkDescriptorSet PER
// concurrently-active rg::RenderViewId, never one shared instance), proven
// as a plain handle-identity comparison.
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

    const VkDescriptorSet setA = sceneServices.Rewrite(viewA, resolvedA);
    const VkDescriptorSet setB = sceneServices.Rewrite(viewB, resolvedB);

    EXPECT_NE(setA, static_cast<VkDescriptorSet>(VK_NULL_HANDLE));
    EXPECT_NE(setB, static_cast<VkDescriptorSet>(VK_NULL_HANDLE));
    EXPECT_NE(sceneServices.DescriptorSetFor(viewA), sceneServices.DescriptorSetFor(viewB));
    EXPECT_EQ(sceneServices.DescriptorSetFor(viewA), setA);
    EXPECT_EQ(sceneServices.DescriptorSetFor(viewB), setB);
    ResetSceneServiceRegistryForTesting();
}

// 3. DescriptorSetFor() for a view Rewrite() was never called for reports
// VK_NULL_HANDLE, not some stale/default value.
TEST(SceneServicesDescriptorSetTest, DescriptorSetForAnUnseenViewIsNullHandle)
{
    HeadlessRenderGraphFixture fixture;
    if (!fixture.IsUsable()) {
        GTEST_SKIP() << fixture.SkipReason();
    }

    SceneServicesDescriptorSet sceneServices(fixture.GetRenderer());
    EXPECT_EQ(sceneServices.DescriptorSetFor(rg::RenderViewId::Named("NeverRewritten")),
        static_cast<VkDescriptorSet>(VK_NULL_HANDLE));
    ResetSceneServiceRegistryForTesting();
}

// --- Additive runtime slot registry ----------------------------------------
//
// RegisterSceneServiceSlot()'s own registration/idempotency/failure-memory/
// sealing contract - entirely Tier-1 (no live VkDevice needed) except the
// one Tier-2 case at the end, mirroring the Tier-1/Tier-2 split above.

TEST(SceneServiceSlotRegistryTest, SameNameReturnsSameIndexDifferentNameReturnsDifferentIndex)
{
    ResetSceneServiceRegistryForTesting();

    const std::uint32_t firstX = RegisterSceneServiceSlot("X", SceneServiceResourceKind::Image2D);
    const std::uint32_t secondX = RegisterSceneServiceSlot("X", SceneServiceResourceKind::Image2D);
    EXPECT_EQ(firstX, secondX);

    const std::uint32_t y = RegisterSceneServiceSlot("Y", SceneServiceResourceKind::Image2D);
    EXPECT_NE(firstX, y);
}

TEST(SceneServiceSlotRegistryTest, ReRegisteringAnExistingNameWithADifferentKindKeepsTheOriginalKind)
{
    ResetSceneServiceRegistryForTesting();

    const std::uint32_t index = RegisterSceneServiceSlot("X", SceneServiceResourceKind::Image2D);
    (void)RegisterSceneServiceSlot("X", SceneServiceResourceKind::Image3D);
    (void)RegisterSceneServiceSlot("X", SceneServiceResourceKind::Image3D);

    EXPECT_EQ(SceneServiceSlotResourceKind(index), SceneServiceResourceKind::Image2D);
}

TEST(SceneServiceSlotRegistryTest, APreferredIndexCollisionReturnsTheExistingOccupantsIndex)
{
    ResetSceneServiceRegistryForTesting();

    const std::uint32_t a = RegisterSceneServiceSlot("A", SceneServiceResourceKind::Image2D, 3u);
    const std::uint32_t b = RegisterSceneServiceSlot("B", SceneServiceResourceKind::Image2D, 3u);

    EXPECT_EQ(a, 3u);
    EXPECT_EQ(b, 3u);
    EXPECT_STREQ(SceneServiceSlotDebugName(3u), "A");
}

TEST(SceneServiceSlotRegistryTest, RepeatingAFailedPreferredIndexCollisionDoesNotCrash)
{
    ResetSceneServiceRegistryForTesting();

    (void)RegisterSceneServiceSlot("A", SceneServiceResourceKind::Image2D, 3u);
    (void)RegisterSceneServiceSlot("B", SceneServiceResourceKind::Image2D, 3u);
    const std::uint32_t secondB = RegisterSceneServiceSlot("B", SceneServiceResourceKind::Image2D, 3u);

    EXPECT_EQ(secondB, 3u);
}

TEST(SceneServiceSlotRegistryTest, RegisteringPastAFullRegistryReturnsInvalid)
{
    ResetSceneServiceRegistryForTesting();

    for (std::uint32_t i = 0; i < kSceneServiceSlotCount; ++i) {
        const std::string name = "Slot" + std::to_string(i);
        (void)RegisterSceneServiceSlot(name.c_str(), SceneServiceResourceKind::Image2D);
    }

    const std::uint32_t ninth = RegisterSceneServiceSlot("Ninth", SceneServiceResourceKind::Image2D);
    EXPECT_EQ(ninth, kInvalidSceneServiceSlotIndex);
}

TEST(SceneServiceSlotRegistryTest, RepeatingARegistrationAgainstAFullRegistryStillReturnsInvalid)
{
    ResetSceneServiceRegistryForTesting();

    for (std::uint32_t i = 0; i < kSceneServiceSlotCount; ++i) {
        const std::string name = "Slot" + std::to_string(i);
        (void)RegisterSceneServiceSlot(name.c_str(), SceneServiceResourceKind::Image2D);
    }

    (void)RegisterSceneServiceSlot("Ninth", SceneServiceResourceKind::Image2D);
    const std::uint32_t secondAttempt = RegisterSceneServiceSlot("Ninth", SceneServiceResourceKind::Image2D);
    EXPECT_EQ(secondAttempt, kInvalidSceneServiceSlotIndex);
}

TEST(SceneServiceSlotRegistryTest, ResetClearsEverySlotAndEveryFailureMemoryEntry)
{
    ResetSceneServiceRegistryForTesting();

    for (std::uint32_t i = 0; i < kSceneServiceSlotCount; ++i) {
        const std::string name = "Slot" + std::to_string(i);
        (void)RegisterSceneServiceSlot(name.c_str(), SceneServiceResourceKind::Image2D);
    }
    (void)RegisterSceneServiceSlot("Ninth", SceneServiceResourceKind::Image2D);

    ResetSceneServiceRegistryForTesting();
    EXPECT_EQ(RegisteredSceneServiceSlotCount(), 0u);

    // A name used before the reset gets a fresh registration path afterward.
    const std::uint32_t firstX = RegisterSceneServiceSlot("X", SceneServiceResourceKind::Image2D);
    const std::uint32_t secondX = RegisterSceneServiceSlot("X", SceneServiceResourceKind::Image2D);
    EXPECT_EQ(firstX, secondX);

    // A name that failed before the reset can attempt registration again with no leftover suppression.
    const std::uint32_t ninthAfterReset = RegisterSceneServiceSlot("Ninth", SceneServiceResourceKind::Image2D);
    EXPECT_NE(ninthAfterReset, kInvalidSceneServiceSlotIndex);
}

TEST(SceneServiceSlotRegistryTest, DebugNameIsCopiedNotAliasedToTheCallersBuffer)
{
    ResetSceneServiceRegistryForTesting();

    std::vector<char> buffer = { 'T', 'e', 'm', 'p', '\0' };
    const std::uint32_t index = RegisterSceneServiceSlot(buffer.data(), SceneServiceResourceKind::Image2D);

    for (char& c : buffer) {
        c = 'Z';
    }

    EXPECT_STREQ(SceneServiceSlotDebugName(index), "Temp");
}

TEST(SceneServiceSlotRegistryTest, NullOrEmptyDebugNameIsRefused)
{
    ResetSceneServiceRegistryForTesting();

    EXPECT_EQ(RegisterSceneServiceSlot(nullptr, SceneServiceResourceKind::Image2D), kInvalidSceneServiceSlotIndex);
    EXPECT_EQ(RegisterSceneServiceSlot("", SceneServiceResourceKind::Image2D), kInvalidSceneServiceSlotIndex);
}

TEST(SceneServiceSlotRegistryTest, OutOfRangePreferredIndexIsRefused)
{
    ResetSceneServiceRegistryForTesting();

    const std::uint32_t result =
        RegisterSceneServiceSlot("Z", SceneServiceResourceKind::Image2D, kSceneServiceSlotCount);
    EXPECT_EQ(result, kInvalidSceneServiceSlotIndex);
}

TEST(SceneServiceSlotRegistryTest, ReRegisteringAnExistingNameWithADifferentPreferredIndexKeepsTheOriginalIndex)
{
    ResetSceneServiceRegistryForTesting();

    const std::uint32_t first = RegisterSceneServiceSlot("W", SceneServiceResourceKind::Image2D, 2u);
    const std::uint32_t second = RegisterSceneServiceSlot("W", SceneServiceResourceKind::Image2D, 5u);
    EXPECT_EQ(first, 2u);
    EXPECT_EQ(second, 2u);

    const std::uint32_t third = RegisterSceneServiceSlot("W", SceneServiceResourceKind::Image2D, 5u);
    EXPECT_EQ(third, 2u);

    // Repeating the already-held index is ordinary silent success.
    const std::uint32_t fourth = RegisterSceneServiceSlot("W", SceneServiceResourceKind::Image2D, 2u);
    EXPECT_EQ(fourth, 2u);
}

// Tier-2: confirms the Runtime Sealing latch is wired into Rewrite() for
// real - once any real SceneServicesDescriptorSet has rewritten for any
// view, every later registration attempt is refused.
TEST(SceneServiceSlotRegistryTest, RegistrationIsRefusedAfterARealRewriteHasRunOnce)
{
    ResetSceneServiceRegistryForTesting();

    HeadlessRenderGraphFixture fixture;
    if (!fixture.IsUsable()) {
        GTEST_SKIP() << fixture.SkipReason();
    }

    SceneServicesDescriptorSet sceneServices(fixture.GetRenderer());
    std::array<SceneServicesDescriptorSet::ResolvedSlot, kSceneServiceSlotCount> resolved{};
    sceneServices.Rewrite(rg::RenderViewId::Named("Game"), resolved);

    const std::uint32_t result = RegisterSceneServiceSlot("PostSealName", SceneServiceResourceKind::Image2D);
    EXPECT_EQ(result, kInvalidSceneServiceSlotIndex);

    ResetSceneServiceRegistryForTesting();
}

} // namespace
} // namespace gte
