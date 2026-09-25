// Unit tests for the editor-core-separation-8 campaign's PHASE1
// (task_manager/editor-core-separation-8/
// PHASE1_BUILTIN_PASS_TOGGLE_REGISTRY_AND_CHOKEPOINT.md) new, generic,
// debugName-keyed built-in-pass toggle registry
// (src/Renderer/RenderGraph/RenderPassToggleRegistry.h). Entirely Tier-1 -
// no Vulkan device, no RenderGraphBuilder involved at all, mirroring this
// same folder's other small, pure-data-structure test files (e.g.
// RenderGraphNameSlotTableTests.cpp).

#include "Renderer/RenderGraph/RenderPassToggleRegistry.h"

#include <gtest/gtest.h>

namespace gte::rg {
namespace {

TEST(RenderPassToggleRegistryTest, NoteDeclaredAndCheckEnabledOnBrandNewNameReturnsTrueAndRegistersIt)
{
    RenderPassToggleRegistry registry;

    const bool enabled = registry.NoteDeclaredAndCheckEnabled("RenderOpaque");

    EXPECT_TRUE(enabled);
    const std::vector<RenderPassToggleState> all = registry.ListAll();
    ASSERT_EQ(all.size(), 1u);
    EXPECT_EQ(all[0].name, "RenderOpaque");
    EXPECT_TRUE(all[0].enabled);
    EXPECT_TRUE(all[0].everDeclaredThisSession);
}

TEST(RenderPassToggleRegistryTest, NoteDeclaredAndCheckEnabledCalledTwiceDoesNotDuplicateAndReflectsSetEnabled)
{
    RenderPassToggleRegistry registry;

    EXPECT_TRUE(registry.NoteDeclaredAndCheckEnabled("DrawSkyBackground")); // Frame 1.
    ASSERT_TRUE(registry.SetEnabled("DrawSkyBackground", false));
    const bool enabledOnFrame2 = registry.NoteDeclaredAndCheckEnabled("DrawSkyBackground"); // Frame 2.

    EXPECT_FALSE(enabledOnFrame2);
    const std::vector<RenderPassToggleState> all = registry.ListAll();
    ASSERT_EQ(all.size(), 1u); // No duplicate entry created.
    EXPECT_FALSE(all[0].enabled);
    EXPECT_TRUE(all[0].everDeclaredThisSession);
}

TEST(RenderPassToggleRegistryTest, DisablingThenNotingDeclaredReturnsFalse)
{
    RenderPassToggleRegistry registry;

    ASSERT_TRUE(registry.SetEnabled("RenderTransparent", false));

    EXPECT_FALSE(registry.NoteDeclaredAndCheckEnabled("RenderTransparent"));
}

TEST(RenderPassToggleRegistryTest, SetEnabledFalseOnPresentIsRefusedByTheDenyList)
{
    RenderPassToggleRegistry registry;

    const bool result = registry.SetEnabled("Present", false);

    EXPECT_FALSE(result);
    EXPECT_TRUE(registry.IsEnabled("Present")); // Unchanged - still enabled.
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

TEST(RenderPassToggleRegistryTest, NoteDeclaredAndCheckEnabledOnAnEmptyNameReturnsTrueAndRegistersNothing)
{
    RenderPassToggleRegistry registry;

    EXPECT_TRUE(registry.NoteDeclaredAndCheckEnabled(""));
    EXPECT_TRUE(registry.ListAll().empty());
}

TEST(RenderPassToggleRegistryTest, ListAllReturnsEntriesSortedByNameLexically)
{
    RenderPassToggleRegistry registry;

    registry.NoteDeclaredAndCheckEnabled("RenderTransparent");
    registry.NoteDeclaredAndCheckEnabled("AtmosphereComposite");
    registry.NoteDeclaredAndCheckEnabled("GpuSkinning");

    const std::vector<RenderPassToggleState> all = registry.ListAll();
    ASSERT_EQ(all.size(), 3u);
    EXPECT_EQ(all[0].name, "AtmosphereComposite");
    EXPECT_EQ(all[1].name, "GpuSkinning");
    EXPECT_EQ(all[2].name, "RenderTransparent");
}

} // namespace
} // namespace gte::rg
