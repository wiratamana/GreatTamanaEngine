// Tests: SceneServiceBlackboardKey() distinctness, the scene-service slot registry (Tier-1)
// and SceneServicesDescriptorSet on a headless VkDevice (Tier-2).

#include "Renderer/SceneServicesDescriptorSet.h"

#include "Renderer/GraphicsHandleRegistry.h" // Native() - this file compares DescriptorSetHandle values.

#include "../Fakes/HeadlessRenderGraphFixture.h"

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <string>
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

// The registry is process-wide: every test that touches it starts and ends with a clean table.
class SceneServiceRegistryTest : public ::testing::Test {
protected:
    void SetUp() override { ResetSceneServiceRegistryForTesting(); }
    void TearDown() override { ResetSceneServiceRegistryForTesting(); }
};

TEST_F(SceneServiceRegistryTest, ResourceKindDefaultsToImage2DUntilRegisteredOtherwise)
{
    EXPECT_EQ(SceneServiceSlotResourceKind(0u), SceneServiceResourceKind::Image2D);
    const std::uint32_t slot = RegisterSceneServiceSlot("Fog", SceneServiceResourceKind::Image3D, 2u);
    ASSERT_NE(slot, kInvalidSceneServiceSlotIndex);
    EXPECT_EQ(SceneServiceSlotResourceKind(slot), SceneServiceResourceKind::Image3D);
    EXPECT_EQ(SceneServiceSlotResourceKind(0u), SceneServiceResourceKind::Image2D);
}

TEST_F(SceneServiceRegistryTest, DepthFlagIsStoredPerSlotAndFalseForUnregisteredSlots)
{
    const std::uint32_t depthSlot =
        RegisterSceneServiceSlot("ShadowDepth", SceneServiceResourceKind::Image2D, std::nullopt, true);
    const std::uint32_t colorSlot = RegisterSceneServiceSlot("Fog", SceneServiceResourceKind::Image2D);
    ASSERT_NE(depthSlot, kInvalidSceneServiceSlotIndex);
    ASSERT_NE(colorSlot, kInvalidSceneServiceSlotIndex);

    EXPECT_TRUE(SceneServiceSlotIsDepthResource(depthSlot));
    EXPECT_FALSE(SceneServiceSlotIsDepthResource(colorSlot));
    EXPECT_FALSE(SceneServiceSlotIsDepthResource(kSceneServiceSlotCount - 1u));
    EXPECT_FALSE(SceneServiceSlotIsDepthResource(kInvalidSceneServiceSlotIndex));
}

TEST_F(SceneServiceRegistryTest, SameNameReturnsSameIndexDifferentNameReturnsDifferentIndex)
{
    const std::uint32_t firstX = RegisterSceneServiceSlot("X", SceneServiceResourceKind::Image2D);
    const std::uint32_t secondX = RegisterSceneServiceSlot("X", SceneServiceResourceKind::Image2D);
    EXPECT_EQ(firstX, secondX);

    const std::uint32_t y = RegisterSceneServiceSlot("Y", SceneServiceResourceKind::Image2D);
    EXPECT_NE(firstX, y);
}

TEST_F(SceneServiceRegistryTest, ReRegisteringAnExistingNameWithADifferentKindKeepsTheOriginalKind)
{
    const std::uint32_t index = RegisterSceneServiceSlot("X", SceneServiceResourceKind::Image2D);
    (void)RegisterSceneServiceSlot("X", SceneServiceResourceKind::Image3D);
    (void)RegisterSceneServiceSlot("X", SceneServiceResourceKind::Image3D);

    EXPECT_EQ(SceneServiceSlotResourceKind(index), SceneServiceResourceKind::Image2D);
}

TEST_F(SceneServiceRegistryTest, APreferredIndexCollisionReturnsTheExistingOccupantsIndex)
{
    const std::uint32_t a = RegisterSceneServiceSlot("A", SceneServiceResourceKind::Image2D, 3u);
    const std::uint32_t b = RegisterSceneServiceSlot("B", SceneServiceResourceKind::Image2D, 3u);

    EXPECT_EQ(a, 3u);
    EXPECT_EQ(b, 3u);
    // The occupant keeps its own properties; the refused newcomer never overwrites them.
    EXPECT_EQ(SceneServiceSlotResourceKind(3u), SceneServiceResourceKind::Image2D);
    EXPECT_EQ(RegisterSceneServiceSlot("A", SceneServiceResourceKind::Image2D), 3u);
}

TEST_F(SceneServiceRegistryTest, RepeatingAFailedPreferredIndexCollisionDoesNotCrash)
{
    (void)RegisterSceneServiceSlot("A", SceneServiceResourceKind::Image2D, 3u);
    (void)RegisterSceneServiceSlot("B", SceneServiceResourceKind::Image2D, 3u);
    const std::uint32_t secondB = RegisterSceneServiceSlot("B", SceneServiceResourceKind::Image2D, 3u);

    EXPECT_EQ(secondB, 3u);
}

TEST_F(SceneServiceRegistryTest, RegisteringPastAFullRegistryReturnsInvalid)
{
    for (std::uint32_t i = 0; i < kSceneServiceSlotCount; ++i) {
        const std::string name = "Slot" + std::to_string(i);
        (void)RegisterSceneServiceSlot(name.c_str(), SceneServiceResourceKind::Image2D);
    }

    const std::uint32_t ninth = RegisterSceneServiceSlot("Ninth", SceneServiceResourceKind::Image2D);
    EXPECT_EQ(ninth, kInvalidSceneServiceSlotIndex);
}

TEST_F(SceneServiceRegistryTest, RepeatingARegistrationAgainstAFullRegistryStillReturnsInvalid)
{
    for (std::uint32_t i = 0; i < kSceneServiceSlotCount; ++i) {
        const std::string name = "Slot" + std::to_string(i);
        (void)RegisterSceneServiceSlot(name.c_str(), SceneServiceResourceKind::Image2D);
    }

    (void)RegisterSceneServiceSlot("Ninth", SceneServiceResourceKind::Image2D);
    const std::uint32_t secondAttempt = RegisterSceneServiceSlot("Ninth", SceneServiceResourceKind::Image2D);
    EXPECT_EQ(secondAttempt, kInvalidSceneServiceSlotIndex);
}

TEST_F(SceneServiceRegistryTest, ResetClearsEverySlotAndEveryFailureMemoryEntry)
{
    for (std::uint32_t i = 0; i < kSceneServiceSlotCount; ++i) {
        const std::string name = "Slot" + std::to_string(i);
        (void)RegisterSceneServiceSlot(name.c_str(), SceneServiceResourceKind::Image3D, std::nullopt, true);
    }
    (void)RegisterSceneServiceSlot("Ninth", SceneServiceResourceKind::Image2D);

    ResetSceneServiceRegistryForTesting();

    // Slot properties are gone.
    EXPECT_EQ(SceneServiceSlotResourceKind(0u), SceneServiceResourceKind::Image2D);
    EXPECT_FALSE(SceneServiceSlotIsDepthResource(0u));

    // A name used before the reset gets a fresh registration path afterward.
    const std::uint32_t firstX = RegisterSceneServiceSlot("X", SceneServiceResourceKind::Image2D);
    const std::uint32_t secondX = RegisterSceneServiceSlot("X", SceneServiceResourceKind::Image2D);
    EXPECT_EQ(firstX, 0u);
    EXPECT_EQ(firstX, secondX);

    // A name that failed before the reset can register again with no leftover suppression.
    const std::uint32_t ninthAfterReset = RegisterSceneServiceSlot("Ninth", SceneServiceResourceKind::Image2D);
    EXPECT_NE(ninthAfterReset, kInvalidSceneServiceSlotIndex);
}

TEST_F(SceneServiceRegistryTest, DebugNameIsCopiedNotAliasedToTheCallersBuffer)
{
    std::vector<char> buffer = { 'T', 'e', 'm', 'p', '\0' };
    const std::uint32_t index = RegisterSceneServiceSlot(buffer.data(), SceneServiceResourceKind::Image2D);

    for (char& c : buffer) {
        c = 'Z';
    }

    // Same name as before the buffer was scribbled over: idempotent hit on the same slot.
    EXPECT_EQ(RegisterSceneServiceSlot("Temp", SceneServiceResourceKind::Image2D), index);
    const std::uint32_t other = RegisterSceneServiceSlot("Z", SceneServiceResourceKind::Image2D);
    EXPECT_NE(other, index);
}

TEST_F(SceneServiceRegistryTest, NullOrEmptyDebugNameIsRefused)
{
    EXPECT_EQ(RegisterSceneServiceSlot(nullptr, SceneServiceResourceKind::Image2D), kInvalidSceneServiceSlotIndex);
    EXPECT_EQ(RegisterSceneServiceSlot("", SceneServiceResourceKind::Image2D), kInvalidSceneServiceSlotIndex);
}

TEST_F(SceneServiceRegistryTest, OutOfRangePreferredIndexIsRefused)
{
    const std::uint32_t result =
        RegisterSceneServiceSlot("Z", SceneServiceResourceKind::Image2D, kSceneServiceSlotCount);
    EXPECT_EQ(result, kInvalidSceneServiceSlotIndex);
}

TEST_F(SceneServiceRegistryTest, ReRegisteringAnExistingNameWithADifferentPreferredIndexKeepsTheOriginalIndex)
{
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

// Rewrite() seals the registry for good: later registrations are refused.
TEST_F(SceneServiceRegistryTest, RegistrationIsRefusedAfterARealRewriteHasRunOnce)
{
    HeadlessRenderGraphFixture fixture;
    if (!fixture.IsUsable()) {
        GTEST_SKIP() << fixture.SkipReason();
    }

    SceneServicesDescriptorSet sceneServices(fixture.GetRenderer());
    std::array<SceneServicesDescriptorSet::ResolvedSlot, kSceneServiceSlotCount> resolved{};
    (void)sceneServices.Rewrite(rg::RenderViewId::Named("Game"), resolved);

    const std::uint32_t result = RegisterSceneServiceSlot("PostSealName", SceneServiceResourceKind::Image2D);
    EXPECT_EQ(result, kInvalidSceneServiceSlotIndex);
}

// Reset reopens a sealed registry.
TEST_F(SceneServiceRegistryTest, ResetReopensARegistrySealedByRewrite)
{
    HeadlessRenderGraphFixture fixture;
    if (!fixture.IsUsable()) {
        GTEST_SKIP() << fixture.SkipReason();
    }

    SceneServicesDescriptorSet sceneServices(fixture.GetRenderer());
    std::array<SceneServicesDescriptorSet::ResolvedSlot, kSceneServiceSlotCount> resolved{};
    (void)sceneServices.Rewrite(rg::RenderViewId::Named("Game"), resolved);

    ResetSceneServiceRegistryForTesting();
    EXPECT_NE(RegisterSceneServiceSlot("AfterReset", SceneServiceResourceKind::Image2D), kInvalidSceneServiceSlotIndex);
}

// Rewrite() with every slot defaulted returns a valid set backed by real dummy textures.
TEST_F(SceneServiceRegistryTest, RewriteWithAllDefaultedSlotsReturnsAValidSetBackedByRealDummies)
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
TEST_F(SceneServiceRegistryTest, RewriteForTwoDifferentViewsProducesTwoDifferentDescriptorSets)
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

TEST_F(SceneServiceRegistryTest, DescriptorSetForAnUnseenViewIsInvalid)
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
