#include "Renderer/RenderGraph/RenderGraphGroupingCache.h"

#include <gtest/gtest.h>

namespace gte::rg {
namespace {

TEST(RenderGraphGroupingCacheTest, ResolveOnUnseenNameReturnsUngrouped)
{
    RenderGraphGroupingCache cache;
    EXPECT_EQ(cache.Resolve("NeverSeen"), "Ungrouped");
}

TEST(RenderGraphGroupingCacheTest, ObservedLabelSurvivesAfterTheRowStopsAppearing)
{
    RenderGraphGroupingCache cache;
    cache.Observe("RenderOpaque", "Geometry");
    EXPECT_EQ(cache.Resolve("RenderOpaque"), "Geometry");
    // Pass stops running this frame - caller never calls Observe() again -
    // label must still resolve correctly.
    EXPECT_EQ(cache.Resolve("RenderOpaque"), "Geometry");
}

TEST(RenderGraphGroupingCacheTest, ResetForTestingClearsEveryObservedLabel)
{
    RenderGraphGroupingCache cache;
    cache.Observe("RenderOpaque", "Geometry");
    EXPECT_EQ(cache.SizeForTesting(), 1u);

    cache.ResetForTesting();

    EXPECT_EQ(cache.SizeForTesting(), 0u);
    EXPECT_EQ(cache.Resolve("RenderOpaque"), "Ungrouped");
}

} // namespace
} // namespace gte::rg
