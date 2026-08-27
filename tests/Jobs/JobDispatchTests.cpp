// Tier 1: exercises a REAL gte::Jobs::JobSystem::Instance() (real worker
// threads when GTE_ENABLE_JOB_SYSTEM is ON, or the inline-synchronous
// fallback when it's OFF - either way safe on any machine/CI runner, no
// live Vulkan device/SDL window involved). Proves that batch-splitting math
// (JobDispatchMathTests.cpp) and real parallel execution COMPOSE correctly -
// see JOBSYSTEM_PHASE2_PARALLEL_FOR_DISPATCH.md, Step 3.4.
#include "Jobs/JobDispatch.h"
#include "Jobs/JobSystem.h"

#include <gtest/gtest.h>

#include <atomic>
#include <cstdint>
#include <vector>

namespace gte::Jobs {
namespace {

struct WriteIndexContext {
    std::vector<std::atomic<int>>* slots;
};

// Writes each index in [beginIndex, endIndex) with its own index value -
// every slot must end up written EXACTLY once, by EXACTLY one batch, once
// the whole Dispatch() has completed.
void WriteIndexRangeJob(std::uint32_t beginIndex, std::uint32_t endIndex, void* payload)
{
    const WriteIndexContext* context = static_cast<const WriteIndexContext*>(payload);
    for (std::uint32_t i = beginIndex; i < endIndex; ++i) {
        (*context->slots)[i].store(static_cast<int>(i), std::memory_order_relaxed);
    }
}

// Shared verification helper: dispatches WriteIndexRangeJob() across
// `itemCount` slots (each starting at a sentinel -1), waits for the whole
// dispatch, then asserts every single slot was written exactly once, with
// the exact value its own index implies - no slot untouched, no slot
// double-written/corrupted by two different batches racing on it.
void DispatchAndVerifyEveryIndexWrittenExactlyOnce(
    std::uint32_t itemCount, std::uint32_t minItemsPerBatch)
{
    std::vector<std::atomic<int>> slots(itemCount);
    for (std::uint32_t i = 0; i < itemCount; ++i) {
        slots[i].store(-1, std::memory_order_relaxed);
    }

    WriteIndexContext context{ &slots };
    JobHandle handle;
    Dispatch(&WriteIndexRangeJob, itemCount, &context, handle, minItemsPerBatch);
    JobSystem::Instance().WaitForJobs(handle);

    ASSERT_TRUE(handle.IsComplete());
    for (std::uint32_t i = 0; i < itemCount; ++i) {
        ASSERT_EQ(slots[i].load(std::memory_order_relaxed), static_cast<int>(i))
            << "itemCount=" << itemCount << " minItemsPerBatch=" << minItemsPerBatch << " index=" << i;
    }
}

TEST(JobDispatchTests, ZeroItemCountIsAnImmediateNoOp)
{
    JobHandle handle;
    std::atomic<bool> everRan{ false };

    auto neverCalled = [](std::uint32_t, std::uint32_t, void* payload) {
        static_cast<std::atomic<bool>*>(payload)->store(true, std::memory_order_release);
    };

    Dispatch(neverCalled, /*itemCount=*/0, &everRan, handle);

    // Nothing was ever scheduled - the handle stays exactly as complete as a
    // fresh JobHandle already is, and WaitForJobs() must not block.
    EXPECT_TRUE(handle.IsComplete());
    JobSystem::Instance().WaitForJobs(handle);
    EXPECT_FALSE(everRan.load(std::memory_order_acquire));
}

TEST(JobDispatchTests, EvenlyDivisibleWorkloadCompletesWithFullCoverage)
{
    DispatchAndVerifyEveryIndexWrittenExactlyOnce(/*itemCount=*/1024, /*minItemsPerBatch=*/1);
}

TEST(JobDispatchTests, NonEvenlyDivisiblePrimeSizedWorkloadCompletesWithFullCoverage)
{
    // A deliberately prime itemCount, to maximize the chance of an uneven
    // batch split exercising ComputeBatchRanges()'s remainder-distribution
    // path for real, under genuine concurrent execution.
    DispatchAndVerifyEveryIndexWrittenExactlyOnce(/*itemCount=*/1009, /*minItemsPerBatch=*/1);
}

TEST(JobDispatchTests, WorkloadSmallerThanMinItemsPerBatchStillCompletesFully)
{
    // itemCount is deliberately much smaller than minItemsPerBatch - this
    // exercises the "collapses to a single batch" path (ComputeBatchRanges())
    // for real, under a genuine Dispatch()/WaitForJobs() round-trip rather
    // than only via the pure-math test.
    DispatchAndVerifyEveryIndexWrittenExactlyOnce(/*itemCount=*/5, /*minItemsPerBatch=*/1000);
}

TEST(JobDispatchTests, SingleItemWorkloadCompletes)
{
    DispatchAndVerifyEveryIndexWrittenExactlyOnce(/*itemCount=*/1, /*minItemsPerBatch=*/1);
}

TEST(JobDispatchTests, RepeatedDispatchesAgainstFreshHandlesAllCompleteIndependently)
{
    // Stress-repeat several independent Dispatch() calls in a row (a new
    // JobHandle each time) - same "never trust a single passing run for
    // genuinely concurrent code" discipline this campaign already applies
    // elsewhere (see AGENTS.md, "Job System").
    constexpr int kIterations = 25;
    for (int iteration = 0; iteration < kIterations; ++iteration) {
        DispatchAndVerifyEveryIndexWrittenExactlyOnce(/*itemCount=*/777, /*minItemsPerBatch=*/4);
    }
}

} // namespace
} // namespace gte::Jobs
