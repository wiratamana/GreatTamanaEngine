#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>

namespace gte::Jobs {

// A job's raw function signature - a single opaque payload pointer, never a
// std::function (which would allocate/type-erase on the hot per-job path -
// see AGENTS.md, "Job System"). `payload`'s lifetime/ownership is entirely
// the CALLING code's responsibility: JobSystem never allocates, copies, or
// frees it - it is handed straight through from Schedule() to the job body
// and back out again.
using JobFunction = void (*)(void* payload);

namespace detail {

// Phase 3 (Job Dependencies / Continuations): a "call this when a handle's
// pending count reaches zero" callback - a plain function pointer + opaque
// context, mirroring JobFunction's own "no std::function, no type erasure on
// the per-registration path" convention above.
using WatcherFunction = void (*)(void* context);

// A small, fixed-capacity (never a growable container - the same "small,
// fixed, generously-sized capacity" convention JobQueue's own ring buffer
// already established) bound on how many DIFFERENT Phase 3 continuations may
// depend on the exact same single handle at once. See JobContinuation.cpp's
// own documented polling fallback for what happens once this is exceeded -
// a registration is never silently dropped.
constexpr std::size_t kMaxWatchersPerHandle = 8;

// The shared, heap-allocated (once, at JobHandle construction - see
// JobHandle's own comment below) piece of state a JobHandle and every job
// scheduled against it both point at. A plain atomic pending-job counter:
// JobSystem::Schedule() increments it once per job scheduled against this
// handle, and the worker (or, in a GTE_ENABLE_JOB_SYSTEM=OFF build, the
// calling thread itself - see JobSystem.cpp) that finishes running a job
// decrements it exactly once right after that job's JobFunction returns.
//
// Phase 3 adds a small, fixed-capacity list of "watcher" callbacks
// (AddWatcher()/CompleteOneAndTakeWatchers() below) - this is what lets ScheduleAfter()/
// DispatchAfter() (JobContinuation.h) be notified the INSTANT a dependency
// handle completes, rather than having to poll it.
struct JobHandleState {
    std::atomic<std::uint32_t> pending{ 0 };

    // Guards watcherFns/watcherContexts/watcherCount below. Contention here
    // is expected to be rare - only a genuine multi-stage dependency chain
    // (Phase 3) ever populates this list at all, and only while a dependency
    // handle is still in flight. Deliberately never held while actually
    // INVOKING a watcher callback (see CompleteOneAndTakeWatchers()) - a watcher body is
    // free to call back into this same handle (e.g. register another watcher)
    // without risking a self-deadlock.
    std::mutex watcherMutex;
    std::array<WatcherFunction, kMaxWatchersPerHandle> watcherFns{};
    std::array<void*, kMaxWatchersPerHandle> watcherContexts{};
    std::size_t watcherCount = 0;

    // Registers `fn(context)` to be called once `pending` reaches zero. If
    // `pending` is ALREADY zero at the moment this is called, `fn` is
    // invoked immediately, synchronously, right here (never stored) - this
    // is what makes an already-complete dependency degrade to zero
    // continuation bookkeeping (see JobContinuation.cpp's ScheduleAfter()).
    // Returns false, without storing anything, if this handle already has
    // kMaxWatchersPerHandle OTHER still-pending watchers registered - the
    // caller is responsible for a graceful fallback in that case (see
    // JobContinuation.cpp's WatchDependencyWithFallback()); this method
    // itself never blocks, grows its own storage, or silently drops a
    // registration it accepts.
    //
    // Correctness note: the pending-zero check (AddWatcher()) and the
    // pending-decrement-to-zero-plus-watcher-capture
    // (CompleteOneAndTakeWatchers(), below) both happen while holding this
    // SAME watcherMutex - this is what closes the exact class of
    // lost-wakeup race JobSystem::WorkerLoop()'s own
    // m_completionMutex-bracketed decrement already guards against for
    // WaitForJobs() (see AGENTS.md, "Job System"), AND (HOTFIX 8 - see
    // task_manager/job_system/JOBSYSTEM_HOTFIX_CODE_REVIEW_FINDINGS.md,
    // item 8) a SEPARATE race where a brand-new registration could be
    // mistakenly swept into an OLDER completion's fired-watcher batch:
    // whichever of {a registration, the decrement-to-zero that captures the
    // watcher list} acquires watcherMutex first is always fully observed by
    // the other, so a watcher registered for a NEW completion cycle can
    // never be included in an OLD cycle's fired batch, and a watcher can
    // never be registered "too late" to see a completion that raced it.
    //
    // HOTFIX 1: the immediate-fire ("already complete") branch must NEVER
    // invoke `fn` while still holding watcherMutex - mirrors
    // CompleteOneAndTakeWatchers()'s own "decide under the lock, fire after
    // releasing it" pattern below. Calling `fn` here under the lock
    // previously violated that same no-reentrancy contract: if the fired
    // continuation's own job body ever touched this same handle again
    // (e.g. registered another watcher against it, or - in the worst case -
    // the full-queue fallback ran the deferred job body inline, still
    // nested inside this call), it would try to re-lock this same
    // non-recursive watcherMutex on the same thread and deadlock.
    // Determining "already complete" under the lock and firing only after
    // releasing it closes that hole exactly the way
    // CompleteOneAndTakeWatchers() already does.
    bool AddWatcher(WatcherFunction fn, void* context)
    {
        bool alreadyComplete = false;
        {
            std::lock_guard<std::mutex> lock(watcherMutex);
            if (pending.load(std::memory_order_acquire) == 0) {
                alreadyComplete = true;
            } else if (watcherCount >= kMaxWatchersPerHandle) {
                return false;
            } else {
                watcherFns[watcherCount] = fn;
                watcherContexts[watcherCount] = context;
                ++watcherCount;
            }
        }
        if (alreadyComplete) {
            fn(context); // Invoked OUTSIDE watcherMutex - see this method's own comment above.
        }
        return true;
    }

    // HOTFIX 8 (see task_manager/job_system/JOBSYSTEM_HOTFIX_CODE_REVIEW_FINDINGS.md,
    // item 8, JOB_SYSTEM_HOTFIX8_COMPLETION_REPORT.md): merges the
    // pending-count decrement-to-zero check with the watcher-list
    // extraction into ONE operation, both guarded by the SAME watcherMutex
    // AddWatcher() already takes - replaces the old, separately-locked
    // FireWatchers(), which left a window open between "pending publicly
    // reached zero" and "the watcher list was actually captured/cleared":
    // a brand-new Schedule() call against the same handle (bumping pending
    // back up) racing into that window could see its own freshly-registered
    // watcher swept into the OLD completion's fired batch instead of
    // waiting for its OWN completion - firing a continuation far too
    // early. Folding the decrement into the same critical section as the
    // capture closes that race: AddWatcher() and this method are now fully
    // serialized against each other by watcherMutex, so whichever one runs
    // first is completely finished (list captured-and-cleared, or a new
    // watcher safely stored) before the other can observe any state at
    // all.
    //
    // Called by whichever thread performs the decrement of `pending`
    // (see JobSystem::WorkerLoop()/Schedule()/ScheduleAlreadyPending()'s
    // own full-queue-fallback paths, in every GTE_ENABLE_JOB_SYSTEM
    // configuration). Always decrements `pending` exactly once, mirroring
    // the old FireWatchers() call sites' own unconditional fetch_sub().
    // Returns true - and populates outFns/outContexts/outCount with
    // whatever was registered - only if THIS decrement was the one that
    // brought `pending` down to zero (the exact same "exactly one thread
    // ever observes this" guarantee fetch_sub()'s atomicity always
    // provided). The caller MUST invoke the returned watchers only AFTER
    // releasing whatever OUTER lock (e.g. JobSystem::m_completionMutex) it
    // held around this call - a fired watcher may itself call back into
    // JobSystem::Schedule()/ScheduleAlreadyPending(), so it must never run
    // while any lock relevant to job scheduling/completion is still held.
    bool CompleteOneAndTakeWatchers(std::array<WatcherFunction, kMaxWatchersPerHandle>& outFns,
        std::array<void*, kMaxWatchersPerHandle>& outContexts, std::size_t& outCount)
    {
        std::lock_guard<std::mutex> lock(watcherMutex);
        const std::uint32_t previous = pending.fetch_sub(1, std::memory_order_acq_rel);
        if (previous != 1) {
            outCount = 0;
            return false;
        }
        outCount = watcherCount;
        outFns = watcherFns;
        outContexts = watcherContexts;
        watcherCount = 0;
        return true;
    }
};

} // namespace detail

// A cheap-to-copy, opaque "when is this done" token - the ONE way calling
// code observes job completion (IsComplete()) or blocks for it
// (JobSystem::WaitForJobs()). Modeled directly on the shape
// JOBSYSTEM_PHASE1_CORE_THREADING_FOUNDATION_v2.md's own Step 1 code sketch
// specifies.
//
// Backed by a std::shared_ptr<detail::JobHandleState> - exactly ONE heap
// allocation, at JobHandle construction (via std::make_shared), never one
// per job scheduled against it. A single JobHandle is meant to be reused
// across MANY JobSystem::Schedule() calls (Phase 2's whole batch-dispatch
// design shares one JobHandle across every batch of a single Dispatch()
// call) - JobSystem::Schedule() itself never allocates on the heap, so the
// "zero heap allocation in the steady-state per-job path" guarantee this
// phase promises is about the SCHEDULING of a job, not the one-time cost of
// minting a fresh handle to schedule work against.
//
// A default-constructed JobHandle is already "complete" (pending == 0), so
// calling JobSystem::WaitForJobs() on one nothing was ever scheduled
// against is an immediate, correct no-op.
class JobHandle {
public:
    JobHandle()
        : m_state(std::make_shared<detail::JobHandleState>())
    {
    }

    // True once every job ever scheduled against this handle (there may be
    // zero, one, or many) has finished running. Safe to call from any
    // thread at any time - backed by a single atomic load.
    bool IsComplete() const noexcept { return m_state->pending.load(std::memory_order_acquire) == 0; }

    // Phase 3 (Job Dependencies / Continuations): registers `fn(context)` to
    // run once THIS handle becomes complete - see
    // detail::JobHandleState::AddWatcher()'s own comment for the
    // immediate-call/overflow behavior. Exposed here directly (rather than
    // only through a `friend` grant) since JobContinuation.cpp is an
    // ordinary consumer of JobHandle's own public surface, not an internal
    // detail of JobHandle itself.
    bool AddCompletionWatcher(detail::WatcherFunction fn, void* context) const
    {
        return m_state->AddWatcher(fn, context);
    }

    // HOTFIX 9 (see task_manager/job_system/JOBSYSTEM_HOTFIX_CODE_REVIEW_FINDINGS.md,
    // item 9, JOB_SYSTEM_HOTFIX9_COMPLETION_REPORT.md): true if `other`
    // shares the SAME underlying JobHandleState as this handle - i.e.
    // `other` either IS this handle, or is a COPY of it (a JobHandle copy
    // shares the same std::shared_ptr<detail::JobHandleState>, see this
    // class's own header comment above). Comparing two shared_ptrs with
    // `==` compares stored-pointer IDENTITY directly, never a deep/value
    // compare - exactly what's needed here - and needs no locking, since
    // m_state is only ever set once, at construction. Used by
    // JobContinuation.cpp's ScheduleAfter() to detect - and refuse to
    // honor - a dependency that is (or shares state with) its own output
    // handle, which would otherwise deadlock that handle against itself
    // forever (see that call site's own comment for the full reasoning).
    bool SharesStateWith(const JobHandle& other) const noexcept { return m_state == other.m_state; }

    // Phase 3, internal use only by JobContinuation.cpp's ScheduleAfter()/
    // DispatchAfter(): manually accounts for one unit of DEFERRED work this
    // handle now represents - work already ACCEPTED but not yet actually
    // pushed onto the job queue, because its own dependencies haven't
    // cleared yet. Paired 1:1 with JobSystem::ScheduleAlreadyPending()'s own
    // decrement once that deferred work actually finishes running.
    //
    // This - rather than a naive "increment, then later call Schedule()
    // which increments AGAIN" approach - is what keeps a deferred
    // continuation's handle continuously, correctly "incomplete" for the
    // ENTIRE window between ScheduleAfter()/DispatchAfter() being called and
    // the real work actually finishing, with no transient false-complete gap
    // a concurrent WaitForJobs() caller could ever observe (a
    // decrement-then-increment approach would risk exactly such a gap - see
    // JOBSYSTEM_PHASE3_JOB_DEPENDENCIES_CONTINUATIONS.md's own reasoning for
    // why this handle must be incomplete "from the instant ScheduleAfter()
    // returns", not lazily once the first dependency clears).
    void AddPendingUnit() const noexcept { m_state->pending.fetch_add(1, std::memory_order_acq_rel); }

private:
    std::shared_ptr<detail::JobHandleState> m_state;

    friend class JobSystem;
};

} // namespace gte::Jobs
