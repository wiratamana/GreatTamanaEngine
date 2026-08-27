#pragma once

#include "JobTypes.h"

#if GTE_ENABLE_JOB_SYSTEM
#include "JobQueue.h"

#include <condition_variable>
#include <mutex>
#include <thread>
#include <vector>
#endif

#include <cstddef>

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

private:
    JobSystem();
    ~JobSystem();

#if GTE_ENABLE_JOB_SYSTEM
    void WorkerLoop();

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
};

} // namespace gte::Jobs
