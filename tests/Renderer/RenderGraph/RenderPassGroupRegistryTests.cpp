// Unit tests for render-pass-7 campaign's PHASE2
// (task_manager/render-pass-7/PHASE2_PASS_GROUP_REGISTRY.md) - the new, generic Core
// facility (src/Renderer/RenderGraph/RenderPassGroupRegistry.h) that lets any Layer-2
// module register a Frame-Debugger-tree heading for its own RenderPassTag. Entirely
// Tier 1 - no Vulkan device involved anywhere in this file. Every test calls
// ResetPassGroupRegistryForTesting() first, since this registry is genuinely global,
// process-lifetime state - GTest does not guarantee test ordering/isolation otherwise.

#include "Renderer/RenderGraph/RenderPassGroupRegistry.h"

#include <gtest/gtest.h>

namespace gte::rg {
namespace {

TEST(RenderPassGroupRegistryTest, RegisteringOneTagMakesCountOneAndRoundTripsFields)
{
    ResetPassGroupRegistryForTesting();

    RegisterPassGroupLabel(RenderPassTag{ 0x1 }, "Compute LUT");

    ASSERT_EQ(PassGroupLabelCount(), 1u);
    EXPECT_EQ(PassGroupLabelTagAt(0).bit, 0x1u);
    EXPECT_STREQ(PassGroupLabelUiHeadingAt(0), "Compute LUT");
}

TEST(RenderPassGroupRegistryTest, FindPassGroupIndexForTagsReturnsCorrectIndexForExactBit)
{
    ResetPassGroupRegistryForTesting();

    RegisterPassGroupLabel(RenderPassTag{ 0x1 }, "Compute LUT");

    const std::optional<std::size_t> found = FindPassGroupIndexForTags(0x1);
    ASSERT_TRUE(found.has_value());
    EXPECT_EQ(*found, 0u);
}

TEST(RenderPassGroupRegistryTest, FindPassGroupIndexForTagsReturnsNulloptForUnrelatedBit)
{
    ResetPassGroupRegistryForTesting();

    RegisterPassGroupLabel(RenderPassTag{ 0x1 }, "Compute LUT");

    EXPECT_FALSE(FindPassGroupIndexForTags(0x2).has_value());
    EXPECT_FALSE(FindPassGroupIndexForTags(0x0).has_value());
}

TEST(RenderPassGroupRegistryTest, FindPassGroupIndexForTagsResolvesMaskWithOnlyOneRegisteredBit)
{
    ResetPassGroupRegistryForTesting();

    RegisterPassGroupLabel(RenderPassTag{ 0x2 }, "Compute LUT");

    // Mask carries several bits (0x1 unregistered, 0x2 registered, 0x8 unregistered) - only
    // the one registered bit should resolve.
    const std::optional<std::size_t> found = FindPassGroupIndexForTags(0x1 | 0x2 | 0x8);
    ASSERT_TRUE(found.has_value());
    EXPECT_EQ(*found, 0u);
}

TEST(RenderPassGroupRegistryTest, RegisteringSameTagTwiceWithSameHeadingIsIdempotent)
{
    ResetPassGroupRegistryForTesting();

    RegisterPassGroupLabel(RenderPassTag{ 0x1 }, "Compute LUT");
    RegisterPassGroupLabel(RenderPassTag{ 0x1 }, "Compute LUT");

    EXPECT_EQ(PassGroupLabelCount(), 1u);
    EXPECT_STREQ(PassGroupLabelUiHeadingAt(0), "Compute LUT");
}

TEST(RenderPassGroupRegistryTest, RegisteringSameTagTwiceWithDifferentHeadingUpdatesInPlace)
{
    ResetPassGroupRegistryForTesting();

    RegisterPassGroupLabel(RenderPassTag{ 0x1 }, "Compute LUT");
    RegisterPassGroupLabel(RenderPassTag{ 0x1 }, "Compute LUT (Renamed)");

    // Still exactly one entry (content updated, not a second one appended).
    EXPECT_EQ(PassGroupLabelCount(), 1u);
    EXPECT_STREQ(PassGroupLabelUiHeadingAt(0), "Compute LUT (Renamed)");
}

TEST(RenderPassGroupRegistryTest, TwoDifferentTagsPreserveRegistrationOrder)
{
    ResetPassGroupRegistryForTesting();

    RegisterPassGroupLabel(RenderPassTag{ 0x1 }, "Compute LUT");
    RegisterPassGroupLabel(RenderPassTag{ 0x2 }, "GPU Skinning");

    ASSERT_EQ(PassGroupLabelCount(), 2u);
    EXPECT_EQ(PassGroupLabelTagAt(0).bit, 0x1u);
    EXPECT_STREQ(PassGroupLabelUiHeadingAt(0), "Compute LUT");
    EXPECT_EQ(PassGroupLabelTagAt(1).bit, 0x2u);
    EXPECT_STREQ(PassGroupLabelUiHeadingAt(1), "GPU Skinning");
}

TEST(RenderPassGroupRegistryTest, FindPassGroupIndexForTagsFirstRegisteredWinsWhenMaskCarriesBoth)
{
    ResetPassGroupRegistryForTesting();

    RegisterPassGroupLabel(RenderPassTag{ 0x1 }, "Compute LUT");
    RegisterPassGroupLabel(RenderPassTag{ 0x2 }, "GPU Skinning");

    const std::optional<std::size_t> found = FindPassGroupIndexForTags(0x1 | 0x2);
    ASSERT_TRUE(found.has_value());
    EXPECT_EQ(*found, 0u); // The FIRST-registered entry (0x1, "Compute LUT") wins.
}

TEST(RenderPassGroupRegistryTest, ResetForTestingActuallyClearsEverything)
{
    ResetPassGroupRegistryForTesting();

    RegisterPassGroupLabel(RenderPassTag{ 0x1 }, "Compute LUT");
    RegisterPassGroupLabel(RenderPassTag{ 0x2 }, "GPU Skinning");
    ASSERT_EQ(PassGroupLabelCount(), 2u);

    ResetPassGroupRegistryForTesting();

    EXPECT_EQ(PassGroupLabelCount(), 0u);
}

} // namespace
} // namespace gte::rg
