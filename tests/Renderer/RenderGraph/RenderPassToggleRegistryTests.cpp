// Unit tests for the generic, debugName-keyed built-in-pass toggle registry
// (src/Renderer/RenderGraph/RenderPassToggleRegistry.h). Entirely Tier-1 -
// no Vulkan device, no RenderGraphBuilder involved at all.

#include "Renderer/RenderGraph/RenderPassToggleRegistry.h"

#include <gtest/gtest.h>

namespace gte::rg {
namespace {

TEST(RenderPassToggleRegistryTest, NoteDeclaredWithOwnerOnBrandNewNameRegistersItEnabled)
{
    RenderPassToggleRegistry registry;

    registry.NoteDeclaredWithOwner("RenderOpaque", "TestFeature");

    EXPECT_TRUE(registry.IsEnabled("RenderOpaque"));
    const std::vector<RenderPassToggleState> all = registry.ListAll();
    ASSERT_EQ(all.size(), 1u);
    EXPECT_EQ(all[0].name, "RenderOpaque");
    EXPECT_TRUE(all[0].enabled);
    EXPECT_TRUE(all[0].everDeclaredThisSession);
    EXPECT_EQ(all[0].owningFeatureName, "TestFeature");
}

TEST(RenderPassToggleRegistryTest, NoteDeclaredWithOwnerCalledTwiceDoesNotDuplicateAndReflectsSetEnabled)
{
    RenderPassToggleRegistry registry;

    registry.NoteDeclaredWithOwner("DrawSkyBackground", "TestFeature"); // Frame 1.
    ASSERT_TRUE(registry.SetEnabled("DrawSkyBackground", false));
    registry.NoteDeclaredWithOwner("DrawSkyBackground", "TestFeature"); // Frame 2.

    EXPECT_FALSE(registry.IsEnabled("DrawSkyBackground"));
    const std::vector<RenderPassToggleState> all = registry.ListAll();
    ASSERT_EQ(all.size(), 1u); // No duplicate entry created.
    EXPECT_FALSE(all[0].enabled);
    EXPECT_TRUE(all[0].everDeclaredThisSession);
}

TEST(RenderPassToggleRegistryTest, DisablingThenNotingDeclaredLeavesIsEnabledFalse)
{
    RenderPassToggleRegistry registry;

    ASSERT_TRUE(registry.SetEnabled("RenderTransparent", false));
    registry.NoteDeclaredWithOwner("RenderTransparent", "TestFeature");

    EXPECT_FALSE(registry.IsEnabled("RenderTransparent"));
}

TEST(RenderPassToggleRegistryTest, SetEnabledFalseOnPresentIsRefusedByTheDenyList)
{
    RenderPassToggleRegistry registry;

    const bool result = registry.SetEnabled("Present", false);

    EXPECT_FALSE(result);
    EXPECT_TRUE(registry.IsEnabled("Present")); // Unchanged - still enabled.
}

TEST(RenderPassToggleRegistryTest, SetEnabledFalseOnClearViewTargetIsRefusedByTheDenyList)
{
    RenderPassToggleRegistry registry;

    const bool applied = registry.SetEnabled("ClearViewTarget", false);
    EXPECT_FALSE(applied);
    EXPECT_TRUE(registry.IsEnabled("ClearViewTarget"));
}

TEST(RenderPassToggleRegistryTest, SetEnabledOnNeverDeclaredNameCreatesAnEntryNotYetDeclaredThisSession)
{
    RenderPassToggleRegistry registry;

    ASSERT_TRUE(registry.SetEnabled("SomeFuturePass", false));

    const std::vector<RenderPassToggleState> all = registry.ListAll();
    ASSERT_EQ(all.size(), 1u);
    EXPECT_EQ(all[0].name, "SomeFuturePass");
    EXPECT_FALSE(all[0].enabled);
    EXPECT_FALSE(all[0].everDeclaredThisSession);
}

TEST(RenderPassToggleRegistryTest, IsEnabledOnACompletelyUnknownNameReturnsTrue)
{
    RenderPassToggleRegistry registry;

    EXPECT_TRUE(registry.IsEnabled("NeverSeenBefore"));
}

TEST(RenderPassToggleRegistryTest, NoteDeclaredWithOwnerOnAnEmptyNameRegistersNothing)
{
    RenderPassToggleRegistry registry;

    registry.NoteDeclaredWithOwner("", "TestFeature");
    EXPECT_TRUE(registry.ListAll().empty());
}

TEST(RenderPassToggleRegistryTest, ListAllReturnsEntriesSortedByNameLexically)
{
    RenderPassToggleRegistry registry;

    registry.NoteDeclaredWithOwner("RenderTransparent", "TestFeature");
    registry.NoteDeclaredWithOwner("AtmosphereComposite", "TestFeature");
    registry.NoteDeclaredWithOwner("GpuSkinning", "TestFeature");

    const std::vector<RenderPassToggleState> all = registry.ListAll();
    ASSERT_EQ(all.size(), 3u);
    EXPECT_EQ(all[0].name, "AtmosphereComposite");
    EXPECT_EQ(all[1].name, "GpuSkinning");
    EXPECT_EQ(all[2].name, "RenderTransparent");
}

TEST(RenderPassToggleRegistryTest, GhostEntryCountIsCappedAndOldestIsEvicted)
{
    RenderPassToggleRegistry registry;
    for (std::size_t i = 0; i < RenderPassToggleRegistry::kMaxGhostEntries; ++i) {
        ASSERT_TRUE(registry.SetEnabled("Ghost" + std::to_string(i), false));
    }
    ASSERT_TRUE(registry.HasEntry("Ghost0"));

    ASSERT_TRUE(registry.SetEnabled("OneMoreGhost", false));

    EXPECT_FALSE(registry.HasEntry("Ghost0")); // Oldest evicted.
    EXPECT_TRUE(registry.HasEntry("OneMoreGhost"));
    EXPECT_EQ(registry.ListAll().size(), RenderPassToggleRegistry::kMaxGhostEntries);
}

TEST(RenderPassToggleRegistryTest, PassThatActuallyRunsIsNeverEvictedAsAGhost)
{
    RenderPassToggleRegistry registry;
    ASSERT_TRUE(registry.SetEnabled("RealPass", true));
    registry.NoteDeclaredWithOwner("RealPass", "TestFeature"); // Promotes it out of ghost tracking.

    for (std::size_t i = 0; i < RenderPassToggleRegistry::kMaxGhostEntries; ++i) {
        registry.SetEnabled("Ghost" + std::to_string(i), false);
    }

    EXPECT_TRUE(registry.HasEntry("RealPass"));
}

} // namespace
} // namespace gte::rg
