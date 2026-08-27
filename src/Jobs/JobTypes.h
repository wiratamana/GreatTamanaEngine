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
// (AddWatcher()/FireWatchers() below) - this is what lets ScheduleAfter()/
// DispatchAfter() (JobContinuation.h) be notified the INSTANT a dependency
// handle completes, rather than having to poll it.
struct JobHandleState {
    std::atomic<std::uint32_t> pending{ 0 };

    // Guards watcherFns/watcherContexts/watcherCount below. Contention here
    // is expected to be rare - only a genuine multi-stage dependency chain
    // (Phase 3) ever populates this list at all, and only while a dependency
    // handle is still in flight. Deliberately never held while actually
    // INVOKING a watcher callback (see FireWatchers()) - a watcher body is
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
    // Correctness note: the pending-zero check and the watcher-list
    // mutation both happen while holding watcherMutex, and FireWatchers()
    // (below) also takes that same mutex before reading/clearing the list -
    // this is what closes the exact class of lost-wakeup race
    // JobSystem::WorkerLoop()'s own m_completionMutex-bracketed decrement
    // already guards against for WaitForJobs() (see AGENTS.md, "Job
    // System"): whichever of {a registration, the decrement-to-zero that
    // triggers FireWatchers()} acquires watcherMutex first is always
    // observed correctly by the other, so a watcher can never be registered
    // "too late" to see a completion that raced it.
    //
    // HOTFIX 1: the immediate-fire ("already complete") branch must NEVER
    // invoke `fn` while still holding watcherMutex - mirrors FireWatchers()'s
    // own "decide under the lock, fire after releasing it" pattern below.
    // Calling `fn` here under the lock previously violated that same
    // no-reentrancy contract: if the fired continuation's own job body ever
    // touched this same handle again (e.g. registered another watcher
    // against it, or - in the worst case - the full-queue fallback ran the
    // deferred job body inline, still nested inside this call), it would try
    // to re-lock this same non-recursive watcherMutex on the same thread and
    // deadlock. Determining "already complete" under the lock and firing
    // only after releasing it closes that hole exactly the way
    // FireWatchers() already does.
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

    // Called by whichever thread performs the decrement of `pending` down to
    // zero (see JobSystem::WorkerLoop()/Schedule()/ScheduleAlreadyPending()'s
    // own full-queue fallback paths) - fires every currently-registered
    // watcher exactly once, then clears the list, so a LATER reuse of this
    // same handle (see JobHandle's own "meant to be reused across many
    // Schedule() calls" precedent) starts from a clean slate.
    void FireWatchers()
    {
        std::array<WatcherFunction, kMaxWatchersPerHandle> fnsToFire{};
        std::array<void*, kMaxWatchersPerHandle> contextsToFire{};
        std::size_t count = 0;
        {
            std::lock_guard<std::mutex> lock(watcherMutex);
            count = watcherCount;
            fnsToFire = watcherFns;
            contextsToFire = watcherContexts;
            watcherCount = 0;
        }
        for (std::size_t i = 0; i < count; ++i) {
            fnsToFire[i](contextsToFire[i]);
        }
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
