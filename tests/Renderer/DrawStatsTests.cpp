// Unit tests for Phase 3's pure draw-call/triangle-count accumulator
// (src/Renderer/DrawStats.h) - both entry points are covered here:
// AccumulateDrawStats() (the inline accumulator production code actually
// calls, from inside FrameRecorder::RecordFrame()'s existing loop - see
// FrameRecorder.cpp) and CountDrawStats() (the test-facing batch wrapper
// used by most cases below for convenience). No Vulkan/Renderer/live GPU
// device involved at all - see PHASE3_DRAW_CALL_TRIANGLE_COUNT_STRATEGY_v2.md,
// Step 3.7.

#include "Renderer/DrawStats.h"

#include <gtest/gtest.h>

#include <array>

namespace gte {
namespace {

TEST(DrawStatsTest, EmptyQueueProducesZeroedStats)
{
    const std::array<CountableDrawItem, 0> items{};
    const DrawStats stats = CountDrawStats(items);

    EXPECT_EQ(stats.drawCallCount, 0u);
    EXPECT_EQ(stats.triangleCount, 0u);
}

TEST(DrawStatsTest, OneNonIndexedDrawCountsVertexCountDividedByThree)
{
    const std::array<CountableDrawItem, 1> items{ { { false, 9, 0 } } };
    const DrawStats stats = CountDrawStats(items);

    EXPECT_EQ(stats.drawCallCount, 1u);
    EXPECT_EQ(stats.triangleCount, 3u);
}

TEST(DrawStatsTest, OneIndexedDrawCountsIndexCountDividedByThree)
{
    const std::array<CountableDrawItem, 1> items{ { { true, 0, 300 } } };
    const DrawStats stats = CountDrawStats(items);

    EXPECT_EQ(stats.drawCallCount, 1u);
    EXPECT_EQ(stats.triangleCount, 100u);
}

TEST(DrawStatsTest, MultipleMixedDrawsSumDrawCallsAndTriangles)
{
    const std::array<CountableDrawItem, 2> items{ {
        { false, 9, 0 }, // 1 draw call, 3 triangles
        { true, 0, 300 }, // 1 draw call, 100 triangles
    } };
    const DrawStats stats = CountDrawStats(items);

    EXPECT_EQ(stats.drawCallCount, 2u);
    EXPECT_EQ(stats.triangleCount, 103u);
}

TEST(DrawStatsTest, CountNotEvenlyDivisibleByThreeTruncates)
{
    const std::array<CountableDrawItem, 1> items{ { { true, 0, 10 } } };
    const DrawStats stats = CountDrawStats(items);

    EXPECT_EQ(stats.drawCallCount, 1u);
    EXPECT_EQ(stats.triangleCount, 3u); // 10 / 3 == 3, truncated - never a crash.
}

TEST(DrawStatsTest, ZeroVertexZeroIndexDegenerateDrawStillCountsTheDrawCall)
{
    const std::array<CountableDrawItem, 1> items{ { { false, 0, 0 } } };
    const DrawStats stats = CountDrawStats(items);

    EXPECT_EQ(stats.drawCallCount, 1u);
    EXPECT_EQ(stats.triangleCount, 0u);
}

TEST(DrawStatsTest, RepeatedAccumulateDrawStatsCallsAgreeWithCountDrawStats)
{
    // Production code (FrameRecorder::RecordFrame()) calls
    // AccumulateDrawStats() inline once per queued item - this proves that
    // sequence of direct calls into the SAME DrawStats produces an
    // identical result to calling CountDrawStats() once over the
    // equivalent list, so the two entry points can never silently diverge.
    DrawStats accumulated;
    AccumulateDrawStats(accumulated, false, 9, 0); // 1 draw call, 3 triangles
    AccumulateDrawStats(accumulated, true, 0, 300); // 1 draw call, 100 triangles
    AccumulateDrawStats(accumulated, true, 0, 10); // 1 draw call, 3 triangles (truncated)

    const std::array<CountableDrawItem, 3> items{ {
        { false, 9, 0 },
        { true, 0, 300 },
        { true, 0, 10 },
    } };
    const DrawStats batched = CountDrawStats(items);

    EXPECT_EQ(accumulated.drawCallCount, batched.drawCallCount);
    EXPECT_EQ(accumulated.triangleCount, batched.triangleCount);
    EXPECT_EQ(accumulated.drawCallCount, 3u);
    EXPECT_EQ(accumulated.triangleCount, 106u);
}

} // namespace
} // namespace gte
