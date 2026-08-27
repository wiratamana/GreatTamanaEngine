#pragma once

#include "JobTypes.h"

#if GTE_ENABLE_JOB_SYSTEM
#include "JobQueue.h"

#include <condition_variable>
#endif

#include <atomic>
#include <cstddef>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>
#include <vector>

namespace gte::Jobs {

// The engine's general-purpose worker-thread pool. See AGENTS.md, "Job
// System", and
// task_manager/job_system/JOBSYSTEM_PHASE1_CORE_THREADING_FOUNDATION_v2.md
// for the full design/rationale this class follows.
//
// A Meyers singleton (Instance()) - the exact same lazy, first-call-
// initialized pattern Profiling::FrameProfiler::Instance() already uses in
// this codebase (see that class's own comment). This means the worker pool
// does NOT exist at all - not one std::thread is created - until the FIRST
// real Schedule() call anywhere in the engine happens to run. That is
// expected and fine: nothing in the engine calls Schedule() yet as of this
// phase (see this phase's own Definition of Done - "used by nothing else in
// the engine yet"), so a plain build/run of the engine today never spins up
// a single worker thread.
//
// This class ALWAYS compiles, unconditionally, regardless of
// GTE_ENABLE_JOB_SYSTEM - the same "the class stays available/testable even
// when its production behavior is gated off" precedent SdlMemoryTracker/
// FrameProfiler already established (see AGENTS.md) - only its INTERNAL
// BEHAVIOR differs per that switch (see Schedule()/WaitForJobs()/
// WorkerCount()'s own comments below); its public API shape never changes.
// When GTE_ENABLE_JOB_SYSTEM is OFF, no std::thread is ever created at all -
// Schedule() runs the job immediately, synchronously, on the calling
// thread - so a caller written against this API observes an identical
// contract (a handle eventually becomes complete; WaitForJobs() returns
// once it is) either way, just with different actual concurrency underneath.
class JobSystem {
public:
    static JobSystem& Instance();

    JobSystem(const JobSystem&) = delete;
    JobSystem& operator=(const JobSystem&) = delete;
    JobSystem(JobSystem&&) = delete;
    JobSystem& operator=(JobSystem&&) = delete;

    // Schedules `fn(payload)` to run - on a worker thread when
    // GTE_ENABLE_JOB_SYSTEM is ON, or immediately, INLINE, on the calling
    // thread when it's OFF (see JobSystem.cpp) - and registers it against
    // `handle` (handle.IsComplete() is false until this job, and every
    // other job ever scheduled against the same handle, has finished
    // running). `payload`'s lifetime is entirely the caller's
    // responsibility - never copied or freed by JobSystem.
    //
    // Never allocates on the heap in the steady-state path (the underlying
    // queue is fixed-capacity - see JobQueue) except for the rare,
    // explicitly-documented full-queue fallback, which itself doesn't
    // allocate either - it simply runs `fn` inline, synchronously, right
    // here, rather than blocking Schedule() or dropping the job.
    void Schedule(JobFunction fn, void* payload, JobHandle& handle);

    // Phase 3 (Job Dependencies / Continuations), internal use only by
    // JobContinuation.cpp's ScheduleAfter()/DispatchAfter(): runs
    // `fn(payload)` against `handle` exactly like Schedule() above, EXCEPT it
    // does NOT increment `handle`'s pending counter itself - the caller
    // already accounted for this one unit of work via
    // JobHandle::AddPendingUnit() at the moment it was first accepted as a
    // deferred continuation (see that method's own comment for why). Still
    // decrements `handle`'s pending counter exactly once, the same as
    // Schedule(), once this job actually finishes running - so the two
    // always balance out to exactly one net unit of work, regardless of how
    // long the deferral window was. Production/test code scheduling
    // ordinary, non-continuation work should always call Schedule()/
    // Dispatch() directly instead - this method exists solely so
    // JobContinuation.cpp never has to duplicate JobSystem's own queueing/
    // full-queue-fallback/watcher-firing logic a second time.
    void ScheduleAlreadyPending(JobFunction fn, void* payload, JobHandle& handle);

    // Blocks the CALLING thread until every job ever scheduled against
    // `handle` has finished running. An already-complete handle (nothing
    // was ever scheduled against it, or every job already finished by the
    // time this is called) returns immediately, without blocking at all.
    void WaitForJobs(JobHandle& handle);

    // How many worker threads this pool actually owns. When
    // GTE_ENABLE_JOB_SYSTEM is OFF, this is always 1 (see Schedule()'s own
    // "runs inline" behavior above) - a well-defined, non-zero value a
    // future caller (e.g. Phase 2's Dispatch(), splitting work across
    // WorkerCount() batches) can always safely divide work by, never zero.
    std::size_t WorkerCount() const noexcept;

    // Phase 5 (Profiler Integration - Worker Timeline - see
    // task_manager/job_system/JOBSYSTEM_PHASE5_PROFILER_INTEGRATION_WORKER_TIMELINE_v2.md):
    // returns the 0-based index of the worker thread currently executing,
    // IF AND ONLY IF the CALLING thread genuinely is one of this pool's own
    // worker threads - std::nullopt otherwise (the main thread, or a Phase 3
    // polling-fallback background thread). A real index is set exactly
    // once, at the very top of WorkerLoop() below (thread_local, never
    // reassigned afterward for the life of that thread).
    //
    // When GTE_ENABLE_JOB_SYSTEM is OFF, this ALWAYS returns `0` (never
    // std::nullopt) - mirroring WorkerCount()'s own "always >= 1, never 0"
    // contract above: there is no real worker-thread pool in that
    // configuration (Schedule() simply runs every job inline, on whichever
    // thread calls it), but there IS still exactly one conceptual "worker"
    // (WorkerCount() == 1), so a job body running via Schedule() in this
    // configuration is treated as running "as" that one worker, for the
    // duration of the call - this is what keeps Profiling::JobScopeTimer
    // still producing meaningful (if trivially single-row) worker-timeline
    // data in an OFF build, rather than permanently blank. Note this DOES
    // mean a caller in this configuration that (incorrectly, per AGENTS.md's
    // "never call GTE_PROFILE_JOB_SCOPE from the main thread" rule) uses
    // GTE_PROFILE_JOB_SCOPE directly from the main thread is indistinguishable
    // from a genuine job body - an unavoidable ambiguity of a configuration
    // where "the calling thread" and "the (only) worker" are, by design, the
    // exact same thread; this rule remains real and enforced whenever
    // GTE_ENABLE_JOB_SYSTEM is ON, where a real, distinguishable worker
    // thread actually exists.
    //
    // Used exclusively by Profiling::JobScopeTimer
    // (src/Profiling/JobScopeTimer.h) to attribute a recorded scope to the
    // worker that ran it - a std::nullopt return is what makes JobScopeTimer
    // skip recording entirely rather than attribute a scope to a fabricated
    // worker index, so a caller violating the "never from the main thread"
    // rule (in a GTE_ENABLE_JOB_SYSTEM=ON build) doesn't crash - it just
    // quietly produces no worker-timeline data for that one scope.
    std::optional<std::size_t> WorkerIndexForCurrentThread() const noexcept;

    // HOTFIX 2 (see task_manager/job_system/JOBSYSTEM_HOTFIX_CODE_REVIEW_FINDINGS.md,
    // item 2, and JOB_SYSTEM_HOTFIX2_COMPLETION_REPORT.md): registers a
    // background thread that is NOT part of the worker pool above - today,
    // exclusively the Phase 3 polling-fallback thread
    // (JobContinuation.cpp's WatchDependencyWithFallback(), spawned once a
    // single dependency handle already has detail::kMaxWatchersPerHandle
    // other continuations registered against it) - so JobSystem's own
    // destructor can JOIN it before this singleton finishes destructing,
    // rather than leaving it fully detached with zero lifecycle tie to
    // JobSystem at all (previously a real static-destruction-order hazard:
    // a still-spinning detached thread could call JobSystem::Instance()
    // during/after the Meyers singleton's own static destruction).
    //
    // `thread` is moved into an internal registry; `completionFlag` MUST be
    // set to `true` (via `std::memory_order_release`) by `thread`'s own
    // function, right before it returns - this is what lets a later call to
    // RegisterBackgroundThread() opportunistically join/discard already-
    // finished entries first, so this registry does not grow completely
    // unbounded across a long-running process even though nothing else ever
    // proactively prunes it. Safe to call from any thread, at any time.
    //
    // If this is called AFTER JobSystem's own destructor has already begun
    // tearing down (IsShuttingDown() already true) - a narrow, expected-rare
    // window, since JoinAllBackgroundThreads() may already have collected
    // the registry by then - `thread` is joined synchronously, right here,
    // instead of being stored, so it is never silently dropped from an
    // already-collected registry.
    void RegisterBackgroundThread(std::thread thread, std::shared_ptr<std::atomic<bool>> completionFlag);

    // True once JobSystem's destructor has begun tearing down this
    // instance. A background thread registered via RegisterBackgroundThread()
    // above (e.g. the Phase 3 polling fallback) MUST check this on every
    // iteration of its own poll loop and bail out immediately - WITHOUT
    // calling back into JobSystem::Instance() again for anything else - the
    // moment it observes this as true, so JobSystem's destructor can
    // actually finish joining it (see JoinAllBackgroundThreads()) instead of
    // either hanging forever (if the thread ignored this and its own
    // dependency never clears) or risking a static-destruction-order hazard
    // (if the thread kept calling back into JobSystem::Instance() with no
    // bound at all).
    bool IsShuttingDown() const noexcept;

private:
    JobSystem();
    ~JobSystem();

    // Common to both the GTE_ENABLE_JOB_SYSTEM=ON and =OFF configurations -
    // background fallback threads (see RegisterBackgroundThread() above) can
    // be spawned by JobContinuation.cpp regardless of that switch (the
    // overflow condition that spawns one has nothing to do with whether a
    // real worker-thread pool exists), so this must always be defined, and
    // both destructor bodies below must always call it.
    void JoinAllBackgroundThreads();

#if GTE_ENABLE_JOB_SYSTEM
    // Phase 5: takes this worker's own 0-based index, set into a
    // thread_local (JobSystem.cpp) at the very top of this function, for
    // WorkerIndexForCurrentThread() to read back later from this same
    // thread.
    void WorkerLoop(std::size_t workerIndex);

    // A single, process-wide "a job just finished" signal, shared by every
    // WaitForJobs() caller regardless of which handle they're each actually
    // waiting on - simpler than a per-handle condition variable, and
    // perfectly correct: every waiter re-checks its OWN handle's
    // IsComplete() predicate independently upon waking (via
    // std::condition_variable::wait()'s predicate overload), so being woken
    // by an unrelated handle's completion is harmless - just an immediate,
    // cheap re-check that goes right back to sleep if this waiter's own
    // handle isn't done yet.
    std::mutex m_completionMutex;
    std::condition_variable m_completionCondition;

    detail::JobQueue m_queue;
    std::vector<std::thread> m_workers;
#endif

    // HOTFIX 2: background-thread registry (see RegisterBackgroundThread()/
    // IsShuttingDown()/JoinAllBackgroundThreads() above) - deliberately
    // defined unconditionally, not inside the #if block above, since a
    // background fallback thread can exist regardless of
    // GTE_ENABLE_JOB_SYSTEM.
    struct BackgroundThreadEntry {
        std::thread thread;
        std::shared_ptr<std::atomic<bool>> completionFlag;
    };

    std::atomic<bool> m_shuttingDown{ false };
    std::mutex m_backgroundThreadsMutex;
    std::vector<BackgroundThreadEntry> m_backgroundThreads;
};

} // namespace gte::Jobs
