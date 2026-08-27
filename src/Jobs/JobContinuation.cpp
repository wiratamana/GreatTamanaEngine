#include "JobContinuation.h"

#include "JobSystem.h"

#include <atomic>
#include <thread>
#include <vector>

namespace gte::Jobs {

namespace {

// Heap-allocated (see JobDispatch.cpp's own precedent for this exact kind of
// bounded, explicitly-documented exception to Phase 1's "zero heap
// allocation in the steady-state per-job path" rule) per-ScheduleAfter()-call
// bookkeeping: how many of the caller's dependencies are still outstanding,
// and what to actually run once every one of them clears. Freed by whichever
// watcher callback observes the LAST dependency clearing
// (OnDependencyCleared(), below) - exactly one call ever does, since
// unmetDependencyCount is only ever decremented, never re-incremented, and
// atomically guards against more than one caller observing the transition to
// zero.
struct PendingContinuation {
    JobFunction fn;
    void* payload;
    JobHandle handle; // copy - shares the SAME underlying state as the caller's own handle.
    std::atomic<std::uint32_t> unmetDependencyCount;
};

// Fired once per dependency, by whichever mechanism actually observed that
// dependency clear (a direct JobHandleState watcher, or the polling fallback
// below) - decrements the shared unmetDependencyCount, and once the LAST
// dependency clears, actually schedules the real, deferred work via
// JobSystem::ScheduleAlreadyPending() (never plain Schedule() - see
// PendingContinuation's own `handle` field comment and
// JobHandle::AddPendingUnit()'s doc for why).
void OnDependencyCleared(void* rawContinuation)
{
    PendingContinuation* continuation = static_cast<PendingContinuation*>(rawContinuation);
    if (continuation->unmetDependencyCount.fetch_sub(1, std::memory_order_acq_rel) == 1) {
        // We were the last dependency to clear.
        JobSystem::Instance().ScheduleAlreadyPending(continuation->fn, continuation->payload, continuation->handle);
        delete continuation;
    }
}

// The documented, deliberately-rare fallback for when a dependency handle
// already has detail::kMaxWatchersPerHandle OTHER continuations registered
// against it (JobHandle::AddCompletionWatcher() returned false) - rather
// than silently dropping this continuation, a small, DEDICATED background
// thread repeatedly polls the dependency until it completes, then calls
// OnDependencyCleared() itself. This costs one real OS thread a busy-yield
// loop for however long the dependency takes to actually finish - an
// accepted cost for a path expected to be hit only when a single handle has
// an unusually large fan-out of dependents (more than
// kMaxWatchersPerHandle), never in the common case - see
// JOBSYSTEM_PHASE3_JOB_DEPENDENCIES_CONTINUATIONS.md, 3.2/3.5, for the full
// rationale.
//
// Deliberately a raw std::thread, NEVER routed through
// JobSystem::Schedule()/Instance() - doing so would risk a real deadlock in
// a GTE_ENABLE_JOB_SYSTEM=OFF build (where Schedule() runs its job
// IMMEDIATELY, SYNCHRONOUSLY, on whichever thread calls it): if the
// dependency this fallback is polling can only ever be completed by
// something running concurrently on ANOTHER thread (the only way it could
// still be genuinely pending after AddCompletionWatcher() already found
// kMaxWatchersPerHandle other still-pending watchers ahead of it), a
// synchronous Schedule() call here would spin forever on the calling
// thread instead of yielding it back. A dedicated, detached thread makes
// this fallback's own "costs a background thread" contract true
// unconditionally, regardless of GTE_ENABLE_JOB_SYSTEM.
struct PollingFallbackContext {
    JobHandle dependency; // copy - independent of the caller's own dependency pointer's lifetime.
    PendingContinuation* continuation;
};

void RunPollingFallbackJob(void* rawContext)
{
    PollingFallbackContext* context = static_cast<PollingFallbackContext*>(rawContext);
    while (!context->dependency.IsComplete()) {
        std::this_thread::yield();
    }
    OnDependencyCleared(context->continuation);
    delete context;
}

// Registers `continuation` against `dependency`, falling back to a polling
// thread (above) if `dependency` already has the maximum number of watchers
// registered against it - a continuation registration is never silently
// dropped either way.
void WatchDependencyWithFallback(JobHandle& dependency, PendingContinuation* continuation)
{
    if (dependency.AddCompletionWatcher(&OnDependencyCleared, continuation)) {
        return; // Registered directly (or already complete and fired immediately) - the common case.
    }

    // Overflow - fall back to a dedicated, detached polling thread (see this
    // function's own header comment above for why this must be a raw
    // std::thread, never JobSystem::Schedule()). Detached: nothing ever
    // waits on the polling thread itself, only on `continuation->handle`,
    // which OnDependencyCleared()/ScheduleAlreadyPending() resolve
    // independently once every real dependency has cleared.
    auto* pollContext = new PollingFallbackContext{ dependency, continuation };
    std::thread(&RunPollingFallbackJob, pollContext).detach();
}

} // namespace

void ScheduleAfter(JobFunction fn, void* payload, std::span<JobHandle* const> dependencies, JobHandle& handle)
{
    // Determine which dependencies are not already complete - the common
    // case (every dependency already finished, or `dependencies` is empty)
    // needs zero continuation bookkeeping at all; this loop itself never
    // blocks or allocates.
    std::vector<JobHandle*> pendingDependencies;
    pendingDependencies.reserve(dependencies.size());
    for (JobHandle* dependency : dependencies) {
        if (dependency != nullptr && !dependency->IsComplete()) {
            pendingDependencies.push_back(dependency);
        }
    }

    if (pendingDependencies.empty()) {
        JobSystem::Instance().Schedule(fn, payload, handle);
        return;
    }

    // Mark `handle` as having one unit of DEFERRED work outstanding, right
    // now - see JobHandle::AddPendingUnit()'s own comment for why this must
    // happen before this function returns, not lazily once the first
    // dependency clears (closing a potential transient false-complete gap a
    // concurrent WaitForJobs() caller could otherwise observe).
    handle.AddPendingUnit();

    PendingContinuation* continuation = new PendingContinuation{
        fn, payload, handle, std::atomic<std::uint32_t>(static_cast<std::uint32_t>(pendingDependencies.size()))
    };

    for (JobHandle* dependency : pendingDependencies) {
        WatchDependencyWithFallback(*dependency, continuation);
    }
}

namespace {

// Bundles Dispatch()'s own parameters (JobDispatch.h) so a single
// JobFunction-shaped trampoline can defer the whole call until `dependencies`
// clear - see DispatchAfter() below.
struct DispatchAfterContext {
    BatchJobFunction fn;
    std::uint32_t itemCount;
    void* payload;
    std::uint32_t minItemsPerBatch;
    JobHandle handle; // copy - shares the SAME underlying state as the caller's own handle.
};

void RunDispatchAfterTrampoline(void* rawContext)
{
    DispatchAfterContext* context = static_cast<DispatchAfterContext*>(rawContext);
    const BatchJobFunction fn = context->fn;
    const std::uint32_t itemCount = context->itemCount;
    void* const payload = context->payload;
    const std::uint32_t minItemsPerBatch = context->minItemsPerBatch;
    JobHandle handle = context->handle; // copy out before freeing context - shares the same underlying state.
    delete context;

    // Dispatch() itself accounts for every batch job it schedules against
    // `handle` normally (via its own ordinary Schedule() increments) - this
    // trampoline's OWN placeholder pending unit (added by ScheduleAfter(),
    // below, before it deferred to this trampoline) is accounted for
    // separately, by ScheduleAlreadyPending()'s own post-run decrement once
    // this function returns.
    Dispatch(fn, itemCount, payload, handle, minItemsPerBatch);
}

} // namespace

void DispatchAfter(BatchJobFunction fn, std::uint32_t itemCount, void* payload,
    std::span<JobHandle* const> dependencies, JobHandle& handle, std::uint32_t minItemsPerBatch)
{
    if (itemCount == 0) {
        return; // Same immediate, job-free no-op as a dependency-free Dispatch() call.
    }

    // Reuses ScheduleAfter() directly against the SAME `handle` the caller
    // will eventually WaitForJobs() on: ScheduleAfter() itself adds exactly
    // one placeholder pending unit for "the trampoline hasn't run yet",
    // which nets out correctly once the trampoline actually runs Dispatch()
    // (which adds its own N batch-job pending units) and then returns
    // (letting ScheduleAlreadyPending() remove the one placeholder unit) -
    // see RunDispatchAfterTrampoline()'s own comment.
    auto* context = new DispatchAfterContext{ fn, itemCount, payload, minItemsPerBatch, handle };
    ScheduleAfter(&RunDispatchAfterTrampoline, context, dependencies, handle);
}

} // namespace gte::Jobs
