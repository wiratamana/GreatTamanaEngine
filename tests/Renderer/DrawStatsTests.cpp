// Tier-1 tests for the draw-call/triangle accumulators in src/Renderer/DrawStats.h.

#include "Renderer/DrawStats.h"

#include <gtest/gtest.h>

namespace gte {
namespace {

TEST(DrawStatsTest, DefaultStatsAreZero)
{
    const DrawStats stats;

    EXPECT_EQ(stats.drawCallCount, 0u);
    EXPECT_EQ(stats.triangleCount, 0u);
    EXPECT_EQ(stats.indirectDrawCount, 0u);
}

TEST(DrawStatsTest, NonIndexedDrawCountsVertexCountDividedByThree)
{
    DrawStats stats;
    AccumulateDrawStats(stats, false, 9, 0);

    EXPECT_EQ(stats.drawCallCount, 1u);
    EXPECT_EQ(stats.triangleCount, 3u);
}

TEST(DrawStatsTest, IndexedDrawCountsIndexCountDividedByThree)
{
    DrawStats stats;
    AccumulateDrawStats(stats, true, 0, 300);

    EXPECT_EQ(stats.drawCallCount, 1u);
    EXPECT_EQ(stats.triangleCount, 100u);
}

TEST(DrawStatsTest, MixedDrawsSumDrawCallsAndTriangles)
{
    DrawStats stats;
    AccumulateDrawStats(stats, false, 9, 0); // 1 draw call, 3 triangles
    AccumulateDrawStats(stats, true, 0, 300); // 1 draw call, 100 triangles
    AccumulateDrawStats(stats, true, 0, 10); // 1 draw call, 3 triangles (truncated)

    EXPECT_EQ(stats.drawCallCount, 3u);
    EXPECT_EQ(stats.triangleCount, 106u);
}

TEST(DrawStatsTest, DegenerateDrawStillCountsTheDrawCall)
{
    DrawStats stats;
    AccumulateDrawStats(stats, false, 0, 0);

    EXPECT_EQ(stats.drawCallCount, 1u);
    EXPECT_EQ(stats.triangleCount, 0u);
}

TEST(DrawStatsTest, AccumulateIndirectDrawStatsIncrementsOnlyIndirectDrawCount)
{
    DrawStats stats;
    AccumulateIndirectDrawStats(stats);
    AccumulateIndirectDrawStats(stats);
    AccumulateIndirectDrawStats(stats);

    EXPECT_EQ(stats.indirectDrawCount, 3u);
    EXPECT_EQ(stats.drawCallCount, 0u);
    EXPECT_EQ(stats.triangleCount, 0u);
}

TEST(DrawStatsTest, DirectAndIndirectAccumulatorsAreIndependent)
{
    DrawStats stats;
    AccumulateDrawStats(stats, true, 0, 300);
    AccumulateIndirectDrawStats(stats);

    EXPECT_EQ(stats.drawCallCount, 1u);
    EXPECT_EQ(stats.triangleCount, 100u);
    EXPECT_EQ(stats.indirectDrawCount, 1u);
}

} // namespace
} // namespace gte
