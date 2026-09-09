// Unit tests for Phase 1's pure, Tier-1-testable name -> physical-texture-
// snapshot table (src/Renderer/RenderGraph/RenderGraphDebugTextureRegistry.h)
// - see task_manager/network-impl-4/PHASE1_DEBUG_TEXTURE_REGISTRY_CORE_DATA_MODEL.md,
// Step 3.4, for the exact coverage list this file follows. Entirely Tier 1 -
// nothing here touches a live VkDevice; every Vulkan handle is a hand-
// fabricated value, mirroring RenderGraphBuilderTests.cpp/
// RenderGraphBarrierPlannerTests.cpp's own established idiom
// (reinterpret_cast<VkImage>(static_cast<std::uintptr_t>(...))).

#include "Renderer/RenderGraph/RenderGraphDebugTextureRegistry.h"

#include <gtest/gtest.h>

namespace gte::rg {
namespace {

// A fresh, fully-populated DebugTextureSnapshot with distinct fake color/
// depth handles derived from `colorImageTag` - mirrors
// RenderGraphBuilderTests.cpp's own MakeFakeSwapchainTarget()/
// MakeFakeGameViewTarget() role. `regime` is set via static_cast per
// RenderGraphDebugTextureRegistry.h's own closing note - this file
// deliberately does not include RenderGraph.h just to name an enumerator.
DebugTextureSnapshot MakeFakeSnapshot(const std::string& name, std::uintptr_t colorImageTag)
{
    DebugTextureSnapshot snapshot;
    snapshot.name = name;
    snapshot.regime = static_cast<ExecuteTimingMode>(0);

    snapshot.target.image = reinterpret_cast<VkImage>(colorImageTag);
    snapshot.target.imageView = reinterpret_cast<VkImageView>(colorImageTag + 1);
    snapshot.target.extent = VkExtent2D{ 1280, 720 };
    snapshot.target.format = VK_FORMAT_R8G8B8A8_UNORM;

    snapshot.hasDepth = true;
    snapshot.target.depthImage = reinterpret_cast<VkImage>(colorImageTag + 2);
    snapshot.target.depthImageView = reinterpret_cast<VkImageView>(colorImageTag + 3);
    snapshot.target.depthFormat = VK_FORMAT_D32_SFLOAT;

    snapshot.colorState.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    snapshot.colorState.stageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
    snapshot.colorState.accessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;

    snapshot.depthState.layout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
    snapshot.depthState.stageMask = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT;
    snapshot.depthState.accessMask = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

    snapshot.lastUpdatedFrameCounter = 42;

    return snapshot;
}

TEST(RenderGraphDebugTextureRegistryTest, FindByNameOnEmptyRegistryReturnsNullopt)
{
    RenderGraphDebugTextureRegistry registry;

    EXPECT_FALSE(registry.FindByName("GameView").has_value());
}

TEST(RenderGraphDebugTextureRegistryTest, ListAllOnEmptyRegistryReturnsEmptyVector)
{
    RenderGraphDebugTextureRegistry registry;

    EXPECT_TRUE(registry.ListAll().empty());
}

TEST(RenderGraphDebugTextureRegistryTest, UpsertThenFindByNameRoundTripsEveryField)
{
    RenderGraphDebugTextureRegistry registry;
    const DebugTextureSnapshot snapshot = MakeFakeSnapshot("GameView", 0x1000);

    registry.Upsert(snapshot);
    const std::optional<DebugTextureSnapshot> found = registry.FindByName("GameView");

    ASSERT_TRUE(found.has_value());
    EXPECT_EQ(found->name, snapshot.name);
    EXPECT_EQ(found->regime, snapshot.regime);
    EXPECT_EQ(found->target.image, snapshot.target.image);
    EXPECT_EQ(found->target.imageView, snapshot.target.imageView);
    EXPECT_EQ(found->target.extent.width, snapshot.target.extent.width);
    EXPECT_EQ(found->target.extent.height, snapshot.target.extent.height);
    EXPECT_EQ(found->target.format, snapshot.target.format);
    EXPECT_EQ(found->hasDepth, snapshot.hasDepth);
    EXPECT_EQ(found->target.depthImage, snapshot.target.depthImage);
    EXPECT_EQ(found->target.depthImageView, snapshot.target.depthImageView);
    EXPECT_EQ(found->target.depthFormat, snapshot.target.depthFormat);
    EXPECT_EQ(found->colorState, snapshot.colorState);
    EXPECT_EQ(found->depthState, snapshot.depthState);
    EXPECT_EQ(found->lastUpdatedFrameCounter, snapshot.lastUpdatedFrameCounter);
}

TEST(RenderGraphDebugTextureRegistryTest, SecondUpsertWithSameNameOverwritesInPlace)
{
    RenderGraphDebugTextureRegistry registry;
    registry.Upsert(MakeFakeSnapshot("GameView", 0x1000));
    registry.Upsert(MakeFakeSnapshot("GameView", 0x9000));

    EXPECT_EQ(registry.ListAll().size(), 1u);
    const std::optional<DebugTextureSnapshot> found = registry.FindByName("GameView");
    ASSERT_TRUE(found.has_value());
    EXPECT_EQ(found->target.image, reinterpret_cast<VkImage>(static_cast<std::uintptr_t>(0x9000)));
}

TEST(RenderGraphDebugTextureRegistryTest, TwoDifferentNamesBothAppearInFirstSeenOrder)
{
    RenderGraphDebugTextureRegistry registry;
    registry.Upsert(MakeFakeSnapshot("GameView", 0x1000));
    registry.Upsert(MakeFakeSnapshot("SceneView", 0x2000));

    const std::vector<DebugTextureSnapshot> all = registry.ListAll();
    ASSERT_EQ(all.size(), 2u);
    EXPECT_EQ(all[0].name, "GameView");
    EXPECT_EQ(all[1].name, "SceneView");
}

TEST(RenderGraphDebugTextureRegistryTest, ApplyColorStateOverrideOnExistingNameUpdatesOnlyColorState)
{
    RenderGraphDebugTextureRegistry registry;
    const DebugTextureSnapshot original = MakeFakeSnapshot("GameView", 0x1000);
    registry.Upsert(original);

    ResourceState newColorState;
    newColorState.layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    newColorState.stageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
    newColorState.accessMask = VK_ACCESS_2_SHADER_READ_BIT;

    registry.ApplyColorStateOverride("GameView", newColorState);

    const std::optional<DebugTextureSnapshot> found = registry.FindByName("GameView");
    ASSERT_TRUE(found.has_value());
    EXPECT_EQ(found->colorState, newColorState);

    // Every other field is unchanged - including lastUpdatedFrameCounter,
    // since a state correction is not a fresh capture of contents (a
    // regression here would silently make Locked Design Decision 4's
    // freshness metric wrong).
    EXPECT_EQ(found->name, original.name);
    EXPECT_EQ(found->regime, original.regime);
    EXPECT_EQ(found->target.image, original.target.image);
    EXPECT_EQ(found->hasDepth, original.hasDepth);
    EXPECT_EQ(found->depthState, original.depthState);
    EXPECT_EQ(found->lastUpdatedFrameCounter, original.lastUpdatedFrameCounter);
}

TEST(RenderGraphDebugTextureRegistryTest, ApplyColorStateOverrideOnUnknownNameIsSafeNoOp)
{
    RenderGraphDebugTextureRegistry registry;
    registry.Upsert(MakeFakeSnapshot("GameView", 0x1000));

    ResourceState newColorState;
    newColorState.layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    registry.ApplyColorStateOverride("DoesNotExist", newColorState);

    EXPECT_EQ(registry.ListAll().size(), 1u);
    EXPECT_FALSE(registry.FindByName("DoesNotExist").has_value());
}

} // namespace
} // namespace gte::rg
