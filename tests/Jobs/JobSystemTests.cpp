// Tier 1: gte::Jobs::JobSystem needs no live Vulkan device/SDL window/
// Editor/ImGui context to exercise - only real OS threads (when
// GTE_ENABLE_JOB_SYSTEM is ON) or plain synchronous inline calls (when it's
// OFF), either way safe to run on any machine/CI runner.
//
// JobSystem::Instance() is a process-wide singleton (same as
// Profiling::FrameProfiler::Instance() - see AGENTS.md, "Profiling", and
// JobSystem.h's own comment) - once constructed by the first test that
// touches it, the SAME worker pool (or the SAME "no pool, run inline"
// configuration) is reused by every other test in this binary. Nothing
// here depends on a pristine/first-use baseline, since every test only
// ever asserts on the OUTCOME of its own Schedule()/WaitForJobs() calls,
// never on some assumed-empty starting state.
#include "Jobs/JobSystem.h"

#include <gtest/gtest.h>

#include <atomic>
#include <cstddef>
#include <vector>

namespace gte::Jobs {
namespace {

TEST(JobSystemTests, WorkerCountIsAlwaysAtLeastOne)
{
    // A well-defined, non-zero value regardless of GTE_ENABLE_JOB_SYSTEM -
    // see JobSystem::WorkerCount()'s own header comment - so a future
    // caller (Phase 2's Dispatch()) can always safely divide work by it.
    EXPECT_GE(JobSystem::Instance().WorkerCount(), static_cast<std::size_t>(1));
}

TEST(JobSystemTests, DefaultConstructedHandleIsAlreadyComplete)
{
    JobHandle handle;
    EXPECT_TRUE(handle.IsComplete());

    // Waiting on a handle nothing was ever scheduled against must be an
    // immediate, correct no-op - never block.
    JobSystem::Instance().WaitForJobs(handle);
    EXPECT_TRUE(handle.IsComplete());
}

void SetFlagJob(void* payload)
{
    static_cast<std::atomic<bool>*>(payload)->store(true, std::memory_order_release);
}


TEST(JobSystemTests, ScheduleThenWaitActuallyRunsTheJob)
{
    std::atomic<bool> ran{ false };
    JobHandle handle;

    JobSystem::Instance().Schedule(&SetFlagJob, &ran, handle);
    JobSystem::Instance().WaitForJobs(handle);

    EXPECT_TRUE(handle.IsComplete());
    EXPECT_TRUE(ran.load(std::memory_order_acquire));
}

struct IndexWriteContext {
    std::vector<int>* slots;
    std::size_t index;
};

void WriteIndexJob(void* payload)
{
    const IndexWriteContext* context = static_cast<const IndexWriteContext*>(payload);
    (*context->slots)[context->index] = static_cast<int>(context->index);
}

TEST(JobSystemTests, ManyJobsAgainstOneSharedHandleAllCompleteWithoutCorruption)
{
    // Run several iterations (not just one) - real concurrency bugs
    // (missed writes, torn results) are exactly the kind of thing that can
    // pass on a single run by luck, per this campaign's own stress-repeat
    // discipline (see JOBSYSTEM_PHASE3_JOB_DEPENDENCIES_CONTINUATIONS.md,
    // Step 5).
    constexpr int kIterations = 20;
    constexpr std::size_t kJobCount = 256;

    for (int iteration = 0; iteration < kIterations; ++iteration) {
        std::vector<int> slots(kJobCount, -1);
        std::vector<IndexWriteContext> contexts(kJobCount);

        JobHandle handle;
        for (std::size_t i = 0; i < kJobCount; ++i) {
            contexts[i] = IndexWriteContext{ &slots, i };
            JobSystem::Instance().Schedule(&WriteIndexJob, &contexts[i], handle);
        }

        JobSystem::Instance().WaitForJobs(handle);
        ASSERT_TRUE(handle.IsComplete());

        for (std::size_t i = 0; i < kJobCount; ++i) {
            ASSERT_EQ(slots[i], static_cast<int>(i)) << "iteration=" << iteration << " index=" << i;
        }
    }
}

TEST(JobSystemTests, CopiedHandleObservesTheSameCompletionState)
{
    std::atomic<bool> ran{ false };
    JobHandle original;

    JobSystem::Instance().Schedule(&SetFlagJob, &ran, original);

    // A copy made WHILE the job may still be in flight shares the exact
    // same underlying completion state (JobHandle is backed by a
    // std::shared_ptr - see JobTypes.h) - waiting on either one is
    // equivalent.
    JobHandle copy = original;
    JobSystem::Instance().WaitForJobs(copy);

    EXPECT_TRUE(copy.IsComplete());
    EXPECT_TRUE(original.IsComplete());
    EXPECT_TRUE(ran.load(std::memory_order_acquire));
}

TEST(JobSystemTests, TwoIndependentHandlesEachOnlyWaitForTheirOwnJobs)
{
    std::atomic<bool> ranA{ false };
    std::atomic<bool> ranB{ false };

    JobHandle handleA;
    JobHandle handleB;
    JobSystem::Instance().Schedule(&SetFlagJob, &ranA, handleA);
    JobSystem::Instance().Schedule(&SetFlagJob, &ranB, handleB);

    JobSystem::Instance().WaitForJobs(handleA);
    EXPECT_TRUE(handleA.IsComplete());
    EXPECT_TRUE(ranA.load(std::memory_order_acquire));

    JobSystem::Instance().WaitForJobs(handleB);
    EXPECT_TRUE(handleB.IsComplete());
    EXPECT_TRUE(ranB.load(std::memory_order_acquire));
}

} // namespace
} // namespace gte::Jobs
