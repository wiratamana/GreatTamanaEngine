// Unit tests for Phase 1's pure, Tier-1-testable name -> physical-volume-
// texture-snapshot table
// (src/Renderer/RenderGraph/RenderGraphDebugVolumeTextureRegistry.h) - see
// task_manager/network-impl-6/PHASE1_DEBUG_VOLUME_TEXTURE_REGISTRY.md,
// Step 3.3, for the exact coverage list this file follows (a direct mirror
// of RenderGraphDebugTextureRegistryTests.cpp, adjusted for the absence of a
// depth half). Entirely Tier 1 - nothing here touches a live VkDevice; every
// Vulkan handle is a hand-fabricated value, mirroring
// RenderGraphDebugTextureRegistryTests.cpp's own established idiom
// (reinterpret_cast<VkImage>(static_cast<std::uintptr_t>(...))).

#include "Renderer/RenderGraph/RenderGraphDebugVolumeTextureRegistry.h"

#include <gtest/gtest.h>

namespace gte::rg {
namespace {

// A fresh, fully-populated DebugVolumeTextureSnapshot with a distinct fake
// image handle derived from `imageTag` - mirrors
// RenderGraphDebugTextureRegistryTests.cpp's own MakeFakeSnapshot() role.
// `regime` is set via static_cast per RenderGraphDebugVolumeTextureRegistry.h's
// own closing note - this file deliberately does not include RenderGraph.h
// just to name an enumerator.
DebugVolumeTextureSnapshot MakeFakeSnapshot(const std::string& name, std::uintptr_t imageTag)
{
    DebugVolumeTextureSnapshot snapshot;
    snapshot.name = name;
    snapshot.regime = static_cast<ExecuteTimingMode>(0);

    snapshot.target.image = reinterpret_cast<VkImage>(imageTag);
    snapshot.target.imageView = reinterpret_cast<VkImageView>(imageTag + 1);
    snapshot.target.extent = VkExtent3D{ 128, 128, 32 };
    snapshot.target.format = VK_FORMAT_R16G16B16A16_SFLOAT;

    snapshot.state.layout = VK_IMAGE_LAYOUT_GENERAL;
    snapshot.state.stageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
    snapshot.state.accessMask = VK_ACCESS_2_SHADER_WRITE_BIT;

    snapshot.lastUpdatedFrameCounter = 42;

    return snapshot;
}

TEST(RenderGraphDebugVolumeTextureRegistryTest, FindByNameOnEmptyRegistryReturnsNullopt)
{
    RenderGraphDebugVolumeTextureRegistry registry;

    EXPECT_FALSE(registry.FindByName("AtmosphereAerialPerspectiveVolume_GameView").has_value());
}

TEST(RenderGraphDebugVolumeTextureRegistryTest, ListAllOnEmptyRegistryReturnsEmptyVector)
{
    RenderGraphDebugVolumeTextureRegistry registry;

    EXPECT_TRUE(registry.ListAll().empty());
}

TEST(RenderGraphDebugVolumeTextureRegistryTest, UpsertThenFindByNameRoundTripsEveryField)
{
    RenderGraphDebugVolumeTextureRegistry registry;
    const DebugVolumeTextureSnapshot snapshot = MakeFakeSnapshot("AtmosphereAerialPerspectiveVolume_GameView", 0x1000);

    registry.Upsert(snapshot);
    const std::optional<DebugVolumeTextureSnapshot> found = registry.FindByName("AtmosphereAerialPerspectiveVolume_GameView");

    ASSERT_TRUE(found.has_value());
    EXPECT_EQ(found->name, snapshot.name);
    EXPECT_EQ(found->regime, snapshot.regime);
    EXPECT_EQ(found->target.image, snapshot.target.image);
    EXPECT_EQ(found->target.imageView, snapshot.target.imageView);
    EXPECT_EQ(found->target.extent.width, snapshot.target.extent.width);
    EXPECT_EQ(found->target.extent.height, snapshot.target.extent.height);
    EXPECT_EQ(found->target.extent.depth, snapshot.target.extent.depth);
    EXPECT_EQ(found->target.format, snapshot.target.format);
    EXPECT_EQ(found->state, snapshot.state);
    EXPECT_EQ(found->lastUpdatedFrameCounter, snapshot.lastUpdatedFrameCounter);
}

TEST(RenderGraphDebugVolumeTextureRegistryTest, SecondUpsertWithSameNameOverwritesInPlace)
{
    RenderGraphDebugVolumeTextureRegistry registry;
    registry.Upsert(MakeFakeSnapshot("AtmosphereAerialPerspectiveVolume_GameView", 0x1000));
    registry.Upsert(MakeFakeSnapshot("AtmosphereAerialPerspectiveVolume_GameView", 0x9000));

    EXPECT_EQ(registry.ListAll().size(), 1u);
    const std::optional<DebugVolumeTextureSnapshot> found = registry.FindByName("AtmosphereAerialPerspectiveVolume_GameView");
    ASSERT_TRUE(found.has_value());
    EXPECT_EQ(found->target.image, reinterpret_cast<VkImage>(static_cast<std::uintptr_t>(0x9000)));
}

TEST(RenderGraphDebugVolumeTextureRegistryTest, TwoDifferentNamesBothAppearInFirstSeenOrder)
{
    RenderGraphDebugVolumeTextureRegistry registry;
    registry.Upsert(MakeFakeSnapshot("AtmosphereAerialPerspectiveVolume_GameView", 0x1000));
    registry.Upsert(MakeFakeSnapshot("AtmosphereAerialPerspectiveVolume_SceneView", 0x2000));

    const std::vector<DebugVolumeTextureSnapshot> all = registry.ListAll();
    ASSERT_EQ(all.size(), 2u);
    EXPECT_EQ(all[0].name, "AtmosphereAerialPerspectiveVolume_GameView");
    EXPECT_EQ(all[1].name, "AtmosphereAerialPerspectiveVolume_SceneView");
}

TEST(RenderGraphDebugVolumeTextureRegistryTest, UpsertOnExistingNameDoesNotMoveItsFirstSeenPosition)
{
    RenderGraphDebugVolumeTextureRegistry registry;
    registry.Upsert(MakeFakeSnapshot("AtmosphereAerialPerspectiveVolume_GameView", 0x1000));
    registry.Upsert(MakeFakeSnapshot("AtmosphereAerialPerspectiveVolume_SceneView", 0x2000));
    // Re-upsert the FIRST name - it must stay first, not move to the end.
    registry.Upsert(MakeFakeSnapshot("AtmosphereAerialPerspectiveVolume_GameView", 0x3000));

    const std::vector<DebugVolumeTextureSnapshot> all = registry.ListAll();
    ASSERT_EQ(all.size(), 2u);
    EXPECT_EQ(all[0].name, "AtmosphereAerialPerspectiveVolume_GameView");
    EXPECT_EQ(all[0].target.image, reinterpret_cast<VkImage>(static_cast<std::uintptr_t>(0x3000)));
    EXPECT_EQ(all[1].name, "AtmosphereAerialPerspectiveVolume_SceneView");
}

TEST(RenderGraphDebugVolumeTextureRegistryTest, ApplyStateOverrideOnExistingNameUpdatesOnlyState)
{
    RenderGraphDebugVolumeTextureRegistry registry;
    const DebugVolumeTextureSnapshot original = MakeFakeSnapshot("AtmosphereAerialPerspectiveVolume_GameView", 0x1000);
    registry.Upsert(original);

    ResourceState newState;
    newState.layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    newState.stageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
    newState.accessMask = VK_ACCESS_2_SHADER_READ_BIT;

    registry.ApplyStateOverride("AtmosphereAerialPerspectiveVolume_GameView", newState);

    const std::optional<DebugVolumeTextureSnapshot> found = registry.FindByName("AtmosphereAerialPerspectiveVolume_GameView");
    ASSERT_TRUE(found.has_value());
    EXPECT_EQ(found->state, newState);

    // Every other field is unchanged - including lastUpdatedFrameCounter,
    // since a state correction is not a fresh capture of contents (a
    // regression here would silently make the "how many frames old" freshness
    // metric wrong).
    EXPECT_EQ(found->name, original.name);
    EXPECT_EQ(found->regime, original.regime);
    EXPECT_EQ(found->target.image, original.target.image);
    EXPECT_EQ(found->lastUpdatedFrameCounter, original.lastUpdatedFrameCounter);
}

TEST(RenderGraphDebugVolumeTextureRegistryTest, ApplyStateOverrideOnUnknownNameIsSafeNoOp)
{
    RenderGraphDebugVolumeTextureRegistry registry;
    registry.Upsert(MakeFakeSnapshot("AtmosphereAerialPerspectiveVolume_GameView", 0x1000));

    ResourceState newState;
    newState.layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    registry.ApplyStateOverride("DoesNotExist", newState);

    EXPECT_EQ(registry.ListAll().size(), 1u);
    EXPECT_FALSE(registry.FindByName("DoesNotExist").has_value());
}

} // namespace
} // namespace gte::rg
