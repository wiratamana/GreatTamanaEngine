// Tier 1: gte::Jobs::ComputeBatchRanges() is pure, deterministic math - no
// JobSystem singleton, no real threads, no scheduling involved at all. Kept
// deliberately separate from JobDispatchTests.cpp (which DOES exercise a
// real JobSystem::Instance()), mirroring this codebase's existing "pure math
// tested in isolation from the stateful thing that consumes it" convention
// (see AGENTS.md's DrawStats.h/FrameRecorder precedent) - see
// JOBSYSTEM_PHASE2_PARALLEL_FOR_DISPATCH.md, Step 3.1.
#include "Jobs/JobDispatch.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <vector>

namespace gte::Jobs {
namespace {

// Asserts the three universal invariants ComputeBatchRanges() must NEVER
// violate, for any input that produced a non-empty result: ranges are
// sorted/contiguous with no gaps, ranges never overlap, and the union of
// every range is exactly [0, itemCount). This is the single most important
// property this whole function exists to guarantee - Phase 6's real
// consumer (CPU vertex skinning) would silently corrupt or skip vertices if
// this were ever wrong.
void ExpectRangesExactlyCoverWithNoGapsOrOverlap(
    const std::vector<BatchRange>& ranges, std::uint32_t itemCount)
{
    std::uint32_t expectedNext = 0;
    for (const BatchRange& range : ranges) {
        EXPECT_EQ(range.beginIndex, expectedNext);
        EXPECT_LT(range.beginIndex, range.endIndex) << "a batch must never be empty";
        expectedNext = range.endIndex;
    }
    EXPECT_EQ(expectedNext, itemCount);
}

TEST(JobDispatchMathTests, ZeroItemCountProducesAnEmptyResult)
{
    const std::vector<BatchRange> ranges = ComputeBatchRanges(0, 8, 1);
    EXPECT_TRUE(ranges.empty());
}

TEST(JobDispatchMathTests, ItemCountSmallerThanFloorCollapsesToOneBatch)
{
    const std::vector<BatchRange> ranges = ComputeBatchRanges(/*itemCount=*/3, /*workerCount=*/8, /*minItemsPerBatch=*/8);
    ASSERT_EQ(ranges.size(), static_cast<std::size_t>(1));
    EXPECT_EQ(ranges[0].beginIndex, static_cast<std::uint32_t>(0));
    EXPECT_EQ(ranges[0].endIndex, static_cast<std::uint32_t>(3));
}

TEST(JobDispatchMathTests, ItemCountEqualToFloorStillCollapsesToOneBatch)
{
    // itemCount == minItemsPerBatch -> maxBatchesByFloor == 1, which is
    // still clamped to a single batch even with many workers available -
    // exactly one batch covering everything.
    const std::vector<BatchRange> ranges = ComputeBatchRanges(/*itemCount=*/8, /*workerCount=*/8, /*minItemsPerBatch=*/8);
    ASSERT_EQ(ranges.size(), static_cast<std::size_t>(1));
    EXPECT_EQ(ranges[0].beginIndex, static_cast<std::uint32_t>(0));
    EXPECT_EQ(ranges[0].endIndex, static_cast<std::uint32_t>(8));
}

TEST(JobDispatchMathTests, EvenlyDivisibleItemCountProducesEqualSizedBatches)
{
    const std::vector<BatchRange> ranges = ComputeBatchRanges(/*itemCount=*/1000, /*workerCount=*/10, /*minItemsPerBatch=*/1);
    ASSERT_EQ(ranges.size(), static_cast<std::size_t>(10));
    for (const BatchRange& range : ranges) {
        EXPECT_EQ(range.endIndex - range.beginIndex, static_cast<std::uint32_t>(100));
    }
    ExpectRangesExactlyCoverWithNoGapsOrOverlap(ranges, 1000);
}

TEST(JobDispatchMathTests, NonEvenlyDivisibleItemCountKeepsBatchSizesWithinOneOfEachOther)
{
    // 1003 items across 10 workers - not evenly divisible. Every batch's
    // size must differ from every other batch's by at most 1 (some get 101,
    // the rest get 100), and full coverage must still hold exactly.
    const std::vector<BatchRange> ranges = ComputeBatchRanges(/*itemCount=*/1003, /*workerCount=*/10, /*minItemsPerBatch=*/1);
    ASSERT_EQ(ranges.size(), static_cast<std::size_t>(10));

    std::uint32_t minSize = ranges.front().endIndex - ranges.front().beginIndex;
    std::uint32_t maxSize = minSize;
    for (const BatchRange& range : ranges) {
        const std::uint32_t size = range.endIndex - range.beginIndex;
        minSize = std::min(minSize, size);
        maxSize = std::max(maxSize, size);
    }
    EXPECT_LE(maxSize - minSize, static_cast<std::uint32_t>(1));
    ExpectRangesExactlyCoverWithNoGapsOrOverlap(ranges, 1003);
}

TEST(JobDispatchMathTests, NeverProducesMoreBatchesThanWorkerCount)
{
    // itemCount is large enough to allow many batches by the floor rule, but
    // workerCount itself must remain the hard ceiling - more batches than
    // available workers cannot run any more in parallel and would only add
    // scheduling overhead.
    const std::vector<BatchRange> ranges = ComputeBatchRanges(/*itemCount=*/10000, /*workerCount=*/4, /*minItemsPerBatch=*/1);
    EXPECT_LE(ranges.size(), static_cast<std::size_t>(4));
    ExpectRangesExactlyCoverWithNoGapsOrOverlap(ranges, 10000);
}

TEST(JobDispatchMathTests, WorkerCountOfOneProducesExactlyOneBatch)
{
    const std::vector<BatchRange> ranges = ComputeBatchRanges(/*itemCount=*/500, /*workerCount=*/1, /*minItemsPerBatch=*/1);
    ASSERT_EQ(ranges.size(), static_cast<std::size_t>(1));
    EXPECT_EQ(ranges[0].beginIndex, static_cast<std::uint32_t>(0));
    EXPECT_EQ(ranges[0].endIndex, static_cast<std::uint32_t>(500));
}

TEST(JobDispatchMathTests, ItemCountOfOneProducesExactlyOneSingleElementBatch)
{
    const std::vector<BatchRange> ranges = ComputeBatchRanges(/*itemCount=*/1, /*workerCount=*/8, /*minItemsPerBatch=*/1);
    ASSERT_EQ(ranges.size(), static_cast<std::size_t>(1));
    EXPECT_EQ(ranges[0].beginIndex, static_cast<std::uint32_t>(0));
    EXPECT_EQ(ranges[0].endIndex, static_cast<std::uint32_t>(1));
}

TEST(JobDispatchMathTests, ZeroWorkerCountIsTreatedAsOne)
{
    // A degenerate workerCount == 0 (should never happen in practice - see
    // JobSystem::WorkerCount()'s own "never zero" guarantee - but this
    // function must still degrade gracefully rather than dividing by zero
    // or producing zero batches for non-zero itemCount).
    const std::vector<BatchRange> ranges = ComputeBatchRanges(/*itemCount=*/50, /*workerCount=*/0, /*minItemsPerBatch=*/1);
    ASSERT_EQ(ranges.size(), static_cast<std::size_t>(1));
    ExpectRangesExactlyCoverWithNoGapsOrOverlap(ranges, 50);
}

TEST(JobDispatchMathTests, ZeroMinItemsPerBatchIsTreatedAsOne)
{
    const std::vector<BatchRange> withZeroFloor = ComputeBatchRanges(/*itemCount=*/100, /*workerCount=*/4, /*minItemsPerBatch=*/0);
    const std::vector<BatchRange> withOneFloor = ComputeBatchRanges(/*itemCount=*/100, /*workerCount=*/4, /*minItemsPerBatch=*/1);
    ASSERT_EQ(withZeroFloor.size(), withOneFloor.size());
    for (std::size_t i = 0; i < withZeroFloor.size(); ++i) {
        EXPECT_EQ(withZeroFloor[i].beginIndex, withOneFloor[i].beginIndex);
        EXPECT_EQ(withZeroFloor[i].endIndex, withOneFloor[i].endIndex);
    }
}

TEST(JobDispatchMathTests, FuzzAcrossManyTripleCombinationsAlwaysHoldsCoreInvariants)
{
    // A small, deterministic sweep across many (itemCount, workerCount,
    // minItemsPerBatch) triples, including the edge values this function's
    // own header comment calls out (workerCount == 1, minItemsPerBatch
    // larger than itemCount, itemCount == 1) - every single combination must
    // satisfy the same three universal invariants, plus "never more batches
    // than workerCount (when itemCount alone would have allowed more)".
    const std::uint32_t itemCounts[] = { 0, 1, 2, 3, 7, 8, 9, 100, 997, 1000, 100000 };
    const std::uint32_t workerCounts[] = { 1, 2, 3, 4, 8, 16 };
    const std::uint32_t minItemsPerBatchValues[] = { 1, 2, 5, 8, 64, 100000 };

    for (std::uint32_t itemCount : itemCounts) {
        for (std::uint32_t workerCount : workerCounts) {
            for (std::uint32_t minItemsPerBatch : minItemsPerBatchValues) {
                const std::vector<BatchRange> ranges = ComputeBatchRanges(itemCount, workerCount, minItemsPerBatch);

                if (itemCount == 0) {
                    EXPECT_TRUE(ranges.empty())
                        << "itemCount=" << itemCount << " workerCount=" << workerCount
                        << " minItemsPerBatch=" << minItemsPerBatch;
                    continue;
                }

                EXPECT_LE(ranges.size(), static_cast<std::size_t>(workerCount))
                    << "itemCount=" << itemCount << " workerCount=" << workerCount
                    << " minItemsPerBatch=" << minItemsPerBatch;

                std::uint32_t expectedNext = 0;
                for (const BatchRange& range : ranges) {
                    EXPECT_EQ(range.beginIndex, expectedNext)
                        << "itemCount=" << itemCount << " workerCount=" << workerCount
                        << " minItemsPerBatch=" << minItemsPerBatch;
                    EXPECT_LT(range.beginIndex, range.endIndex);
                    expectedNext = range.endIndex;
                }
                EXPECT_EQ(expectedNext, itemCount)
                    << "itemCount=" << itemCount << " workerCount=" << workerCount
                    << " minItemsPerBatch=" << minItemsPerBatch;
            }
        }
    }
}

} // namespace
} // namespace gte::Jobs
