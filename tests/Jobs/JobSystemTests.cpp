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
// never on some assumed-empty starting state. This also means the real
// singleton is NEVER actually destructed during a normal test run (it has
// process lifetime) - so HOTFIX 2's join-on-destruction behavior itself
// (see JOB_SYSTEM_HOTFIX2_COMPLETION_REPORT.md) is exercised here only
// insofar as RegisterBackgroundThread()/IsShuttingDown() behave correctly
// DURING normal (non-shutdown) operation; the destructor's own join
// sequence was verified manually (a real process exit while a polling
// fallback thread was still registered) rather than via an automated test,
// since nothing can force this particular Meyers singleton to destruct
// early without tearing down the whole test binary.
#include "Jobs/JobSystem.h"

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <cstddef>
#include <memory>
#include <thread>
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

// HOTFIX 2 regression coverage - see
// task_manager/job_system/JOBSYSTEM_HOTFIX_CODE_REVIEW_FINDINGS.md, item 2,
// and JOB_SYSTEM_HOTFIX2_COMPLETION_REPORT.md.

TEST(JobSystemTests, IsShuttingDownIsFalseDuringNormalOperation)
{
    // The real singleton is alive and well for the entire test binary's
    // run (see this file's own header comment) - IsShuttingDown() must
    // never report true outside of JobSystem's own destructor.
    EXPECT_FALSE(JobSystem::Instance().IsShuttingDown());
}

TEST(JobSystemTests, RegisterBackgroundThreadLetsARealThreadRunToCompletion)
{
    // Proves RegisterBackgroundThread() genuinely hands off a live,
    // running std::thread rather than blocking the caller or the thread
    // itself - a background fallback thread (JobContinuation.cpp's
    // WatchDependencyWithFallback()) must keep running normally after
    // being registered.
    auto completionFlag = std::make_shared<std::atomic<bool>>(false);
    std::atomic<bool> threadBodyRan{ false };

    std::thread backgroundThread([&threadBodyRan, completionFlag]() {
        threadBodyRan.store(true, std::memory_order_release);
        completionFlag->store(true, std::memory_order_release);
    });
    JobSystem::Instance().RegisterBackgroundThread(std::move(backgroundThread), completionFlag);

    // Bounded poll (never an unconditional/indefinite block) for the
    // thread to actually finish and flip its completion flag.
    bool completed = false;
    for (int i = 0; i < 500 && !completed; ++i) {
        completed = completionFlag->load(std::memory_order_acquire);
        if (!completed) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }

    EXPECT_TRUE(completed) << "RegisterBackgroundThread()'s thread never completed";
    EXPECT_TRUE(threadBodyRan.load(std::memory_order_acquire));
}

TEST(JobSystemTests, RegisterBackgroundThreadPrunesAlreadyFinishedEntriesOnNextCall)
{
    // Registers a quick-finishing thread and waits for it to signal
    // completion via its own flag, then registers a SECOND thread - that
    // second RegisterBackgroundThread() call's own opportunistic pruning
    // pass (see its header comment) should join/discard the first,
    // already-finished entry before appending the new one, rather than
    // letting the registry grow completely unbounded. Nothing here can
    // directly observe the registry's internal size, so this test's real
    // assertion is simply that back-to-back registration/pruning never
    // hangs or crashes, and that both threads are still given the chance
    // to run to completion correctly.
    auto firstFlag = std::make_shared<std::atomic<bool>>(false);
    std::thread first([firstFlag]() { firstFlag->store(true, std::memory_order_release); });
    JobSystem::Instance().RegisterBackgroundThread(std::move(first), firstFlag);

    for (int i = 0; i < 500 && !firstFlag->load(std::memory_order_acquire); ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    ASSERT_TRUE(firstFlag->load(std::memory_order_acquire));

    // A brief grace period so the first thread's own OS-level function has
    // actually returned (its flag is set right before that, not after) by
    // the time the second registration call below attempts to join it.
    std::this_thread::sleep_for(std::chrono::milliseconds(20));

    auto secondFlag = std::make_shared<std::atomic<bool>>(false);
    std::thread second([secondFlag]() { secondFlag->store(true, std::memory_order_release); });
    JobSystem::Instance().RegisterBackgroundThread(std::move(second), secondFlag);

    bool secondCompleted = false;
    for (int i = 0; i < 500 && !secondCompleted; ++i) {
        secondCompleted = secondFlag->load(std::memory_order_acquire);
        if (!secondCompleted) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }
    EXPECT_TRUE(secondCompleted) << "RegisterBackgroundThread()'s second thread never completed";
}

} // namespace
} // namespace gte::Jobs
