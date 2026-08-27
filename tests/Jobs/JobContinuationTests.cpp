// Tier 1: exercises a REAL gte::Jobs::JobSystem::Instance() (real worker
// threads when GTE_ENABLE_JOB_SYSTEM is ON, or the inline-synchronous
// fallback when it's OFF - either way safe on any machine/CI runner, no live
// Vulkan device/SDL window involved). Proves Job System Phase 3's
// ScheduleAfter()/DispatchAfter() (src/Jobs/JobContinuation.h) - see
// JOBSYSTEM_PHASE3_JOB_DEPENDENCIES_CONTINUATIONS.md, Step 3.5, for the full
// test plan this file follows.
//
// IMPORTANT (GTE_ENABLE_JOB_SYSTEM=OFF correctness note): when the Job
// System's real worker-thread pool is compiled out, JobSystem::Schedule()
// runs its job function IMMEDIATELY, SYNCHRONOUSLY, on whichever thread
// calls it (see JobSystem.cpp's OFF branch) - it does not return until the
// job body itself returns. A test that needs to hold a dependency handle
// "genuinely pending" for a controlled duration (most tests below) must
// therefore NEVER call Schedule() with such a job directly from the main
// test thread - that would deadlock forever in the OFF configuration (the
// calling thread blocks inside Schedule() waiting for a release flag only
// that SAME thread could ever set). Every such test instead spawns its OWN
// dedicated std::thread to make that Schedule() call (see
// StartHeldDependency() below) - this is what lets a dependency legitimately
// stay pending, from the calling test's point of view, whether
// GTE_ENABLE_JOB_SYSTEM is ON (Schedule() posts to a real worker queue and
// returns immediately; the spawned thread finishes quickly, independent of
// the job) or OFF (Schedule() runs synchronously on the SPAWNED thread,
// blocking only that thread, never the main test thread).
#include "Jobs/JobContinuation.h"
#include "Jobs/JobDispatch.h"
#include "Jobs/JobSystem.h"

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <span>
#include <thread>
#include <vector>

namespace gte::Jobs {
namespace {

void SetFlagJob(void* payload)
{
    static_cast<std::atomic<bool>*>(payload)->store(true, std::memory_order_release);
}

// Heap-allocated (freed by the job itself, right before it returns) context
// for HoldUntilReleasedJob - see StartHeldDependency()'s own comment for why
// this needs to be heap-allocated rather than referencing the spawning
// thread's own stack frame.
struct HoldUntilReleasedContext {
    std::atomic<bool>* releaseFlag;
    std::atomic<bool>* doneFlag;
};

// Busy-waits (yielding, never blocking a mutex - keeps this Tier 1 and free
// of any extra synchronization primitive beyond what JobSystem itself
// already provides) until `*releaseFlag` becomes true, then sets `*doneFlag`
// and frees its own heap-allocated context.
void HoldUntilReleasedJob(void* payload)
{
    HoldUntilReleasedContext* context = static_cast<HoldUntilReleasedContext*>(payload);
    while (!context->releaseFlag->load(std::memory_order_acquire)) {
        std::this_thread::yield();
    }
    context->doneFlag->store(true, std::memory_order_release);
    delete context;
}

// Spawns a dedicated background thread that Schedule()s HoldUntilReleasedJob
// against `handle` - see this file's own header comment for why this must
// run on its OWN thread rather than the caller's, in both build
// configurations. The context handed to the job is heap-allocated (`new`)
// specifically so it safely outlives this spawned thread's own lambda scope
// even when GTE_ENABLE_JOB_SYSTEM is ON (Schedule() returns almost
// immediately there, well before the job itself actually runs on a
// DIFFERENT worker thread later).
//
// The caller MUST spin-wait on `!handle.IsComplete()` before treating the
// dependency as genuinely pending (see every call site below) - spawning
// this thread only STARTS the process of incrementing `handle`'s pending
// counter; it does not synchronously guarantee the increment has happened
// by the time this function returns to the caller.
std::thread StartHeldDependency(JobHandle& handle, std::atomic<bool>* releaseFlag, std::atomic<bool>* doneFlag)
{
    return std::thread([&handle, releaseFlag, doneFlag]() {
        auto* context = new HoldUntilReleasedContext{ releaseFlag, doneFlag };
        JobSystem::Instance().Schedule(&HoldUntilReleasedJob, context, handle);
    });
}

// Spin-waits until `handle` has at least one unit of pending work registered
// against it - see StartHeldDependency()'s own comment for why every caller
// of it must do this before proceeding.
void WaitUntilPending(const JobHandle& handle)
{
    while (handle.IsComplete()) {
        std::this_thread::yield();
    }
}

TEST(JobContinuationTests, AlreadyCompleteDependencyFallsThroughToOrdinarySchedule)
{
    // The dependency finishes and is WaitForJobs()'d FIRST, so it is already
    // complete by the time ScheduleAfter() is called - proving the "falls
    // straight through to Schedule() with zero continuation bookkeeping"
    // fast path (see JobContinuation.cpp's ScheduleAfter()) is correct, not
    // just an unverified optimization.
    std::atomic<bool> dependencyRan{ false };
    JobHandle dependency;
    JobSystem::Instance().Schedule(&SetFlagJob, &dependencyRan, dependency);
    JobSystem::Instance().WaitForJobs(dependency);
    ASSERT_TRUE(dependency.IsComplete());

    std::atomic<bool> continuationRan{ false };
    JobHandle handle;
    JobHandle* dependencies[] = { &dependency };
    ScheduleAfter(&SetFlagJob, &continuationRan, dependencies, handle);

    JobSystem::Instance().WaitForJobs(handle);
    EXPECT_TRUE(handle.IsComplete());
    EXPECT_TRUE(continuationRan.load(std::memory_order_acquire));
}

struct ReentrantWatcherContext {
    JobHandle* handle;
    std::atomic<bool>* innerWatcherRan;
};

void InnerWatcherFn(void* payload)
{
    static_cast<std::atomic<bool>*>(payload)->store(true, std::memory_order_release);
}

void OuterWatcherFn(void* payload)
{
    ReentrantWatcherContext* context = static_cast<ReentrantWatcherContext*>(payload);
    // Reentrant call into the SAME handle, from INSIDE the first watcher's
    // own callback - this is exactly the HOTFIX 1 scenario (see
    // JOBSYSTEM_HOTFIX_CODE_REVIEW_FINDINGS.md, item 1): before the fix,
    // JobHandleState::AddWatcher() invoked this outer callback WHILE STILL
    // HOLDING watcherMutex, so this inner AddCompletionWatcher() call would
    // try to re-lock that same non-recursive mutex on the same thread and
    // hang forever.
    context->handle->AddCompletionWatcher(&InnerWatcherFn, context->innerWatcherRan);
}

// HOTFIX 1 regression test - see JOBSYSTEM_HOTFIX_CODE_REVIEW_FINDINGS.md,
// item 1: AddCompletionWatcher()'s "already complete" immediate-fire branch
// must never invoke the watcher callback while still holding the handle's
// internal watcherMutex. Proven here by actually reentering the SAME
// already-complete handle from inside a fired watcher, on a dedicated
// thread with a bounded wait rather than an unconditional join() - a
// regression back to the old locked-fire behavior would hang this thread
// forever (a non-recursive mutex re-locked by the same thread), which this
// test must fail loudly on instead of hanging the whole suite.
TEST(JobContinuationTests, ReentrantWatcherRegistrationOnAlreadyCompleteHandleDoesNotDeadlock)
{
    std::atomic<bool> dependencyRan{ false };
    JobHandle handle;
    JobSystem::Instance().Schedule(&SetFlagJob, &dependencyRan, handle);
    JobSystem::Instance().WaitForJobs(handle);
    ASSERT_TRUE(handle.IsComplete());

    std::atomic<bool> innerWatcherRan{ false };
    ReentrantWatcherContext context{ &handle, &innerWatcherRan };

    std::atomic<bool> outerCallReturned{ false };
    std::thread worker([&]() {
        handle.AddCompletionWatcher(&OuterWatcherFn, &context);
        outerCallReturned.store(true, std::memory_order_release);
    });

    for (int i = 0; i < 500 && !outerCallReturned.load(std::memory_order_acquire); ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }

    if (!outerCallReturned.load(std::memory_order_acquire)) {
        worker.detach();
        FAIL() << "AddCompletionWatcher() self-deadlocked on reentrant registration (HOTFIX 1 regression)";
    }
    worker.join();

    EXPECT_TRUE(innerWatcherRan.load(std::memory_order_acquire));
}

TEST(JobContinuationTests, EmptyDependencyListIsAnOrdinarySchedule)
{
    std::atomic<bool> continuationRan{ false };
    JobHandle handle;
    ScheduleAfter(&SetFlagJob, &continuationRan, std::span<JobHandle* const>{}, handle);

    JobSystem::Instance().WaitForJobs(handle);
    EXPECT_TRUE(handle.IsComplete());
    EXPECT_TRUE(continuationRan.load(std::memory_order_acquire));
}

TEST(JobContinuationTests, HandleStaysIncompleteUntilPendingDependencyClears)
{
    // Run several iterations - concurrent ordering bugs are exactly the
    // class of bug that can pass on a single run by luck (see AGENTS.md,
    // "Job System", and this campaign's own stress-repeat discipline).
    constexpr int kIterations = 25;
    for (int iteration = 0; iteration < kIterations; ++iteration) {
        std::atomic<bool> releaseDependency{ false };
        std::atomic<bool> dependencyDone{ false };
        JobHandle dependency;
        std::thread depScheduler = StartHeldDependency(dependency, &releaseDependency, &dependencyDone);
        WaitUntilPending(dependency);

        std::atomic<bool> continuationRan{ false };
        JobHandle handle;
        JobHandle* dependencies[] = { &dependency };
        ScheduleAfter(&SetFlagJob, &continuationRan, dependencies, handle);

        // The handle must be incomplete THE INSTANT ScheduleAfter() returns,
        // even though nothing has been pushed onto the queue yet on its
        // behalf - see JobHandle::AddPendingUnit()'s own comment.
        ASSERT_FALSE(handle.IsComplete()) << "iteration=" << iteration;

        // A waiter blocked on `handle` right now must not observe completion
        // until the dependency actually clears - proven by actually blocking
        // a real thread on it before releasing the dependency, rather than
        // just asserting IsComplete() on the main thread (which could pass
        // even with a broken implementation that races WaitForJobs()).
        std::atomic<bool> waitReturned{ false };
        std::thread waiter([&handle, &waitReturned]() {
            JobSystem::Instance().WaitForJobs(handle);
            waitReturned.store(true, std::memory_order_release);
        });

        // Deterministic (not timing-dependent): the continuation cannot have
        // run yet, and therefore the waiter cannot have returned yet either,
        // because the dependency job is still spinning on releaseDependency.
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        EXPECT_FALSE(continuationRan.load(std::memory_order_acquire)) << "iteration=" << iteration;
        EXPECT_FALSE(waitReturned.load(std::memory_order_acquire)) << "iteration=" << iteration;
        EXPECT_FALSE(handle.IsComplete()) << "iteration=" << iteration;

        releaseDependency.store(true, std::memory_order_release);
        waiter.join();
        depScheduler.join();

        EXPECT_TRUE(dependencyDone.load(std::memory_order_acquire)) << "iteration=" << iteration;
        EXPECT_TRUE(continuationRan.load(std::memory_order_acquire)) << "iteration=" << iteration;
        EXPECT_TRUE(waitReturned.load(std::memory_order_acquire)) << "iteration=" << iteration;
        EXPECT_TRUE(handle.IsComplete()) << "iteration=" << iteration;
    }
}

// Records, via a shared counter, whether the upstream dependency's own job
// already ran before this continuation runs - used by the strict-ordering
// test below to prove a continuation genuinely never starts before every
// one of its dependencies has finished, not just "usually" by scheduling
// luck.
struct OrderMarkerContext {
    std::atomic<int>* sharedValue;
    int expectedValueWhenRun;
    std::atomic<bool>* observedCorrectOrder;
};

void WriteMarkerJob(void* payload)
{
    static_cast<std::atomic<int>*>(payload)->store(1, std::memory_order_release);
}

void CheckMarkerJob(void* payload)
{
    const OrderMarkerContext* context = static_cast<const OrderMarkerContext*>(payload);
    const int observed = context->sharedValue->load(std::memory_order_acquire);
    context->observedCorrectOrder->store(observed == context->expectedValueWhenRun, std::memory_order_release);
}

TEST(JobContinuationTests, ContinuationNeverObservesDependencyAsUnfinished)
{
    constexpr int kIterations = 100;
    for (int iteration = 0; iteration < kIterations; ++iteration) {
        std::atomic<int> sharedValue{ 0 };
        std::atomic<bool> observedCorrectOrder{ false };

        JobHandle dependency;
        JobSystem::Instance().Schedule(&WriteMarkerJob, &sharedValue, dependency);

        OrderMarkerContext checkContext{ &sharedValue, 1, &observedCorrectOrder };
        JobHandle handle;
        JobHandle* dependencies[] = { &dependency };
        ScheduleAfter(&CheckMarkerJob, &checkContext, dependencies, handle);

        JobSystem::Instance().WaitForJobs(handle);
        EXPECT_TRUE(observedCorrectOrder.load(std::memory_order_acquire)) << "iteration=" << iteration;
    }
}

TEST(JobContinuationTests, FanInWaitsForEveryDependencyBeforeRunning)
{
    std::atomic<bool> releaseA{ false };
    std::atomic<bool> doneA{ false };
    JobHandle dependencyA;
    std::thread schedulerA = StartHeldDependency(dependencyA, &releaseA, &doneA);
    WaitUntilPending(dependencyA);

    std::atomic<bool> releaseB{ false };
    std::atomic<bool> doneB{ false };
    JobHandle dependencyB;
    std::thread schedulerB = StartHeldDependency(dependencyB, &releaseB, &doneB);
    WaitUntilPending(dependencyB);

    std::atomic<bool> continuationRan{ false };
    JobHandle handle;
    JobHandle* dependencies[] = { &dependencyA, &dependencyB };
    ScheduleAfter(&SetFlagJob, &continuationRan, dependencies, handle);

    ASSERT_FALSE(handle.IsComplete());

    // Releasing only ONE of the two dependencies must not be enough.
    releaseA.store(true, std::memory_order_release);
    while (!doneA.load(std::memory_order_acquire)) {
        std::this_thread::yield();
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    EXPECT_FALSE(continuationRan.load(std::memory_order_acquire));
    EXPECT_FALSE(handle.IsComplete());

    releaseB.store(true, std::memory_order_release);
    JobSystem::Instance().WaitForJobs(handle);

    EXPECT_TRUE(continuationRan.load(std::memory_order_acquire));
    EXPECT_TRUE(handle.IsComplete());

    schedulerA.join();
    schedulerB.join();
}

TEST(JobContinuationTests, FanOutRunsEveryDependentOnceTheSharedDependencyClears)
{
    constexpr int kContinuationCount = 5;

    std::atomic<bool> releaseDependency{ false };
    std::atomic<bool> dependencyDone{ false };
    JobHandle dependency;
    std::thread depScheduler = StartHeldDependency(dependency, &releaseDependency, &dependencyDone);
    WaitUntilPending(dependency);

    std::vector<std::atomic<bool>> continuationRan(kContinuationCount);
    std::vector<JobHandle> handles(kContinuationCount);
    for (int i = 0; i < kContinuationCount; ++i) {
        continuationRan[i].store(false, std::memory_order_relaxed);
        JobHandle* dependencies[] = { &dependency };
        ScheduleAfter(&SetFlagJob, &continuationRan[i], dependencies, handles[i]);
        EXPECT_FALSE(handles[i].IsComplete()) << "index=" << i;
    }

    releaseDependency.store(true, std::memory_order_release);
    for (int i = 0; i < kContinuationCount; ++i) {
        JobSystem::Instance().WaitForJobs(handles[i]);
        EXPECT_TRUE(continuationRan[i].load(std::memory_order_acquire)) << "index=" << i;
    }

    depScheduler.join();
}

TEST(JobContinuationTests, OverflowingWatcherCapacityStillRunsEveryContinuation)
{
    // Deliberately more dependents than detail::kMaxWatchersPerHandle - the
    // extras must fall back to the documented polling mechanism
    // (JobContinuation.cpp's WatchDependencyWithFallback()) rather than ever
    // being silently dropped.
    const int continuationCount = static_cast<int>(detail::kMaxWatchersPerHandle) + 3;

    std::atomic<bool> releaseDependency{ false };
    std::atomic<bool> dependencyDone{ false };
    JobHandle dependency;
    std::thread depScheduler = StartHeldDependency(dependency, &releaseDependency, &dependencyDone);
    WaitUntilPending(dependency);

    std::vector<std::atomic<bool>> continuationRan(continuationCount);
    std::vector<JobHandle> handles(continuationCount);
    for (int i = 0; i < continuationCount; ++i) {
        continuationRan[i].store(false, std::memory_order_relaxed);
        JobHandle* dependencies[] = { &dependency };
        ScheduleAfter(&SetFlagJob, &continuationRan[i], dependencies, handles[i]);
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    for (int i = 0; i < continuationCount; ++i) {
        EXPECT_FALSE(continuationRan[i].load(std::memory_order_acquire)) << "index=" << i;
    }

    releaseDependency.store(true, std::memory_order_release);
    for (int i = 0; i < continuationCount; ++i) {
        JobSystem::Instance().WaitForJobs(handles[i]);
        EXPECT_TRUE(continuationRan[i].load(std::memory_order_acquire)) << "index=" << i;
    }

    depScheduler.join();
}

struct WriteIndexRangeContext {
    std::vector<std::atomic<int>>* slots;
};

void WriteIndexRangeJob(std::uint32_t beginIndex, std::uint32_t endIndex, void* payload)
{
    const WriteIndexRangeContext* context = static_cast<const WriteIndexRangeContext*>(payload);
    for (std::uint32_t i = beginIndex; i < endIndex; ++i) {
        (*context->slots)[i].store(static_cast<int>(i), std::memory_order_relaxed);
    }
}

TEST(JobContinuationTests, DispatchAfterDefersEveryBatchUntilDependencyClears)
{
    constexpr std::uint32_t kItemCount = 777;

    std::atomic<bool> releaseDependency{ false };
    std::atomic<bool> dependencyDone{ false };
    JobHandle dependency;
    std::thread depScheduler = StartHeldDependency(dependency, &releaseDependency, &dependencyDone);
    WaitUntilPending(dependency);

    std::vector<std::atomic<int>> slots(kItemCount);
    for (std::uint32_t i = 0; i < kItemCount; ++i) {
        slots[i].store(-1, std::memory_order_relaxed);
    }
    WriteIndexRangeContext context{ &slots };

    JobHandle handle;
    JobHandle* dependencies[] = { &dependency };
    DispatchAfter(&WriteIndexRangeJob, kItemCount, &context, dependencies, handle, /*minItemsPerBatch=*/4);

    ASSERT_FALSE(handle.IsComplete());
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    for (std::uint32_t i = 0; i < kItemCount; ++i) {
        EXPECT_EQ(slots[i].load(std::memory_order_relaxed), -1) << "index=" << i;
    }
    EXPECT_FALSE(handle.IsComplete());

    releaseDependency.store(true, std::memory_order_release);
    JobSystem::Instance().WaitForJobs(handle);

    EXPECT_TRUE(handle.IsComplete());
    for (std::uint32_t i = 0; i < kItemCount; ++i) {
        EXPECT_EQ(slots[i].load(std::memory_order_relaxed), static_cast<int>(i)) << "index=" << i;
    }

    depScheduler.join();
}

TEST(JobContinuationTests, DispatchAfterZeroItemCountIsAnImmediateNoOp)
{
    std::atomic<bool> everRan{ false };
    auto neverCalled = [](std::uint32_t, std::uint32_t, void* payload) {
        static_cast<std::atomic<bool>*>(payload)->store(true, std::memory_order_release);
    };

    JobHandle dependency; // already complete - irrelevant, since itemCount == 0 short-circuits first.
    JobHandle handle;
    JobHandle* dependencies[] = { &dependency };
    DispatchAfter(neverCalled, /*itemCount=*/0, &everRan, dependencies, handle);

    EXPECT_TRUE(handle.IsComplete());
    JobSystem::Instance().WaitForJobs(handle);
    EXPECT_FALSE(everRan.load(std::memory_order_acquire));
}

// HOTFIX 3 regression test - see JOBSYSTEM_HOTFIX_CODE_REVIEW_FINDINGS.md,
// item 7: a use-after-free in ScheduleAfter()'s own dependency-registration
// loop, reachable any time 2+ dependencies are passed and at least one of
// them clears WHILE that loop is still iterating over the REST (plausible
// any time dependencies are real jobs racing to completion concurrently on
// worker threads, exactly as set up below). Before the fix,
// `unmetDependencyCount` was initialized to exactly `pendingDependencies.size()`
// (no "registration still in progress" sentinel), so two or more
// dependencies clearing before the loop finished registering every one of
// them could bring the count to zero - and `continuation` `delete`d - while
// a LATER loop iteration still held and dereferenced that same pointer.
// This test's real assertion is simply "this doesn't crash/hang", repeated
// many times (concurrent scheduling races are exactly the class of bug that
// can pass by luck on a single run - see AGENTS.md, "Job System") to
// maximize the chance of actually landing inside the registration window a
// dependency's own completion races against.
TEST(JobContinuationTests, ScheduleAfterSurvivesConcurrentDependencyCompletionDuringRegistration)
{
    constexpr int kIterations = 300;
    constexpr int kDependencyCount = 4;
    for (int iteration = 0; iteration < kIterations; ++iteration) {
        std::vector<JobHandle> dependencies(kDependencyCount);
        std::vector<std::atomic<bool>> dependencyRan(kDependencyCount);
        for (int i = 0; i < kDependencyCount; ++i) {
            dependencyRan[i].store(false, std::memory_order_relaxed);
            // Real jobs, scheduled just before ScheduleAfter() below - on a
            // real worker-thread pool (GTE_ENABLE_JOB_SYSTEM=ON) these race
            // to completion concurrently with ScheduleAfter()'s own
            // registration loop; even when it's OFF, Schedule() itself has
            // already run each job to completion by the time this loop
            // returns, so every dependency here is already complete before
            // ScheduleAfter() is even called - both configurations are
            // valuable, exercising different halves of this fix.
            JobSystem::Instance().Schedule(&SetFlagJob, &dependencyRan[i], dependencies[i]);
        }

        std::atomic<bool> continuationRan{ false };
        JobHandle handle;
        std::vector<JobHandle*> dependencyPtrs;
        dependencyPtrs.reserve(kDependencyCount);
        for (int i = 0; i < kDependencyCount; ++i) {
            dependencyPtrs.push_back(&dependencies[i]);
        }

        ScheduleAfter(&SetFlagJob, &continuationRan, dependencyPtrs, handle);

        JobSystem::Instance().WaitForJobs(handle);
        EXPECT_TRUE(handle.IsComplete()) << "iteration=" << iteration;
        EXPECT_TRUE(continuationRan.load(std::memory_order_acquire)) << "iteration=" << iteration;
        for (int i = 0; i < kDependencyCount; ++i) {
            EXPECT_TRUE(dependencyRan[i].load(std::memory_order_acquire))
                << "iteration=" << iteration << " dep=" << i;
        }
    }
}

} // namespace
} // namespace gte::Jobs
