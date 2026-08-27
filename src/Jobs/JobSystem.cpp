#include "JobSystem.h"

// <thread> is already pulled in transitively via JobSystem.h when
// GTE_ENABLE_JOB_SYSTEM is ON; the OFF branch below also needs
// std::this_thread::yield() (WaitForJobs()'s spin-wait - see that method's
// own comment for why a real wait is needed there even in this
// configuration), so include it directly and unconditionally here rather
// than relying on the ON-only transitive include.
#include <thread>

namespace gte::Jobs {

namespace {
// A generous, fixed capacity for the job queue - see JobQueue.h's own
// comment on why this is a fixed size rather than a growable container.
// Sized well above any workload this engine schedules as of this phase
// (nothing does yet); a future phase whose real workload legitimately needs
// more in-flight jobs than this should raise this constant deliberately,
// not silently rely on the graceful full-queue fallback as its normal path.
constexpr std::size_t kDefaultQueueCapacity = 4096;
} // namespace

JobSystem& JobSystem::Instance()
{
    // Meyers singleton - lazily constructed on the first call to
    // Instance() from anywhere in the process (see this class's own header
    // comment for why that matters). Function-local statics are
    // guaranteed thread-safe to initialize exactly once by the C++11
    // standard, so no external synchronization is needed here even though
    // multiple threads could race to call Instance() for the first time.
    static JobSystem instance;
    return instance;
}

#if GTE_ENABLE_JOB_SYSTEM

JobSystem::JobSystem()
    : m_queue(kDefaultQueueCapacity)
{
    // hardware_concurrency() may legitimately return 0 (the platform was
    // unable to detect it) - never spin up a zero-worker pool; a single
    // worker still means every Schedule()'d job eventually runs, just
    // serialized rather than in parallel.
    const unsigned int hardwareThreads = std::thread::hardware_concurrency();
    const std::size_t workerCount = hardwareThreads > 0 ? static_cast<std::size_t>(hardwareThreads) : 1;

    m_workers.reserve(workerCount);
    for (std::size_t i = 0; i < workerCount; ++i) {
        m_workers.emplace_back([this]() { WorkerLoop(); });
    }
}

JobSystem::~JobSystem()
{
    // Ask every worker to exit its WaitAndPop() loop, then join all of
    // them - RAII-clean shutdown, no explicit "please remember to call
    // Shutdown()" step required anywhere else in the engine (this runs
    // automatically at process exit, since JobSystem::Instance() is a
    // static with process lifetime).
    m_queue.Shutdown();
    for (std::thread& worker : m_workers) {
        if (worker.joinable()) {
            worker.join();
        }
    }
}

void JobSystem::WorkerLoop()
{
    detail::JobEntry entry;
    while (m_queue.WaitAndPop(entry)) {
        entry.fn(entry.payload);

        if (entry.state) {
            // The pending-count decrement MUST be bracketed by the SAME
            // mutex WaitForJobs() holds while it checks its own predicate
            // (m_completionMutex) - even though `pending` is itself atomic,
            // an atomic write alone does NOT prevent a classic
            // condition_variable lost-wakeup race: a waiter could check
            // IsComplete() (see it as still false, while still holding
            // m_completionMutex), and then, before it actually finishes
            // registering itself as a waiter on m_completionCondition,
            // this decrement-then-notify_all() sequence could run to
            // completion on another thread and find no one registered yet
            // to wake - the waiter would then block forever waiting for a
            // notification that already happened moments earlier. Holding
            // m_completionMutex around the decrement forces it to
            // serialize against WaitForJobs()'s own lock-held predicate
            // check/wait-registration, closing that window - see
            // cppreference's own condition_variable::notify_all() docs:
            // "even though the shared variable is atomic, it must be
            // modified... while owning the mutex to correctly publish the
            // modification to the waiting thread." (Verified: this exact
            // race reproduced intermittently - roughly 1 in 4 runs - under
            // JobSystemTests.ManyJobsAgainstOneSharedHandleAllCompleteWithoutCorruption's
            // 256-jobs-per-handle load before this fix; a --gtest_repeat
            // stress run showed zero hangs across 100+ iterations after
            // it.)
            //
            // `previousPending` (the value BEFORE this decrement) tells us
            // whether THIS decrement was the one that brought the handle
            // down to zero (previousPending == 1) - if so, this thread is
            // the one responsible for firing this handle's Phase 3
            // continuation watchers (see JobHandleState::FireWatchers()).
            // Exactly one thread ever observes previousPending == 1 for a
            // given handle's completion, since fetch_sub() is atomic.
            std::uint32_t previousPending;
            {
                std::lock_guard<std::mutex> lock(m_completionMutex);
                previousPending = entry.state->pending.fetch_sub(1, std::memory_order_acq_rel);
            }

            if (previousPending == 1) {
                // Deliberately fired AFTER releasing m_completionMutex above:
                // FireWatchers() takes its own, separate watcherMutex, and a
                // fired watcher (JobContinuation.cpp's OnDependencyCleared())
                // may itself call back into JobSystem::Schedule()/
                // ScheduleAlreadyPending() - never hold m_completionMutex
                // while running arbitrary callback code.
                entry.state->FireWatchers();
            }
        }

        // Wake every WaitForJobs() caller currently blocked, regardless of
        // which handle each is actually waiting on - see this class's own
        // header comment on why a single, shared condition variable is
        // correct here, not just simpler. Deliberately called AFTER the
        // lock_guard above has already released m_completionMutex - a
        // waiter woken here still has to re-acquire that same mutex before
        // its wait() call can return anyway, so holding it any longer here
        // would only add needless contention, not correctness.
        m_completionCondition.notify_all();

        entry = detail::JobEntry{}; // Release fn/payload/state before looping back to WaitAndPop().
    }
}

void JobSystem::Schedule(JobFunction fn, void* payload, JobHandle& handle)
{
    handle.m_state->pending.fetch_add(1, std::memory_order_acq_rel);

    detail::JobEntry entry{ fn, payload, handle.m_state };
    if (m_queue.TryPush(std::move(entry))) {
        return;
    }

    // Documented, graceful full-queue fallback (see JobQueue.h's own
    // comment) - run the job immediately, right here, on the calling
    // thread, rather than blocking Schedule() or dropping the job.
    fn(payload);
    std::uint32_t previousPending;
    {
        // Same lock-bracketed-decrement requirement as WorkerLoop() above -
        // see that method's own comment for the full "why" (the exact same
        // lost-wakeup race is possible here too, however unlikely this
        // fallback path is to actually be hit in practice).
        std::lock_guard<std::mutex> lock(m_completionMutex);
        previousPending = handle.m_state->pending.fetch_sub(1, std::memory_order_acq_rel);
    }
    if (previousPending == 1) {
        handle.m_state->FireWatchers(); // See WorkerLoop()'s own comment on why this runs unlocked.
    }
    m_completionCondition.notify_all();
}

void JobSystem::ScheduleAlreadyPending(JobFunction fn, void* payload, JobHandle& handle)
{
    // Unlike Schedule() above, deliberately does NOT increment
    // handle.m_state->pending here - the caller (JobContinuation.cpp's
    // ScheduleAfter()/DispatchAfter()) already accounted for this one unit
    // of work via JobHandle::AddPendingUnit() at the moment it was first
    // accepted as a deferred continuation - see that method's own comment.
    detail::JobEntry entry{ fn, payload, handle.m_state };
    if (m_queue.TryPush(std::move(entry))) {
        return;
    }

    // Same graceful full-queue fallback as Schedule() above.
    fn(payload);
    std::uint32_t previousPending;
    {
        std::lock_guard<std::mutex> lock(m_completionMutex);
        previousPending = handle.m_state->pending.fetch_sub(1, std::memory_order_acq_rel);
    }
    if (previousPending == 1) {
        handle.m_state->FireWatchers();
    }
    m_completionCondition.notify_all();
}

void JobSystem::WaitForJobs(JobHandle& handle)
{
    std::unique_lock<std::mutex> lock(m_completionMutex);
    m_completionCondition.wait(lock, [&handle]() { return handle.IsComplete(); });
}

std::size_t JobSystem::WorkerCount() const noexcept
{
    return m_workers.size();
}

#else // !GTE_ENABLE_JOB_SYSTEM

JobSystem::JobSystem() = default;
JobSystem::~JobSystem() = default;

void JobSystem::Schedule(JobFunction fn, void* payload, JobHandle& handle)
{
    // No worker pool exists in this build configuration - run the job
    // immediately, synchronously, on the calling thread. The handle's
    // pending count is still incremented/decremented around the call so
    // IsComplete()/WaitForJobs() behave identically from the caller's
    // point of view either way - GTE_ENABLE_JOB_SYSTEM only changes WHERE
    // and WHEN the work actually runs, never the public API's observable
    // contract.
    handle.m_state->pending.fetch_add(1, std::memory_order_acq_rel);
    fn(payload);
    const std::uint32_t previousPending = handle.m_state->pending.fetch_sub(1, std::memory_order_acq_rel);
    if (previousPending == 1) {
        handle.m_state->FireWatchers();
    }
}

void JobSystem::ScheduleAlreadyPending(JobFunction fn, void* payload, JobHandle& handle)
{
    // Same "no increment here, the caller already accounted for it" contract
    // as the GTE_ENABLE_JOB_SYSTEM=ON branch above - see
    // JobHandle::AddPendingUnit()'s own comment.
    fn(payload);
    const std::uint32_t previousPending = handle.m_state->pending.fetch_sub(1, std::memory_order_acq_rel);
    if (previousPending == 1) {
        handle.m_state->FireWatchers();
    }
}

void JobSystem::WaitForJobs(JobHandle& handle)
{
    // Historically (Phases 1-2) this could simply assume `handle` was
    // already complete by the time any caller reached here, since every
    // Schedule()/Dispatch() call in this configuration runs its job(s)
    // synchronously, to completion, before returning - so nothing was ever
    // left "in flight" for a caller on a different thread to wait for.
    //
    // That assumption no longer holds once Phase 3's ScheduleAfter()/
    // DispatchAfter() (JobContinuation.h) exist: JobHandle::AddPendingUnit()
    // can mark a handle incomplete well BEFORE the work that will eventually
    // complete it is actually scheduled - it only runs once that handle's
    // own dependencies clear, which may happen on a completely different
    // thread (e.g. another thread that is itself concurrently calling
    // Schedule()/ScheduleAlreadyPending() for the dependency). A caller
    // blocked in WaitForJobs() on such a handle genuinely has something to
    // wait for, even in this no-worker-pool configuration.
    //
    // There is no shared condition_variable to block on here (unlike the
    // GTE_ENABLE_JOB_SYSTEM=ON branch above) - spin-wait instead, yielding
    // the CPU between checks rather than busy-looping at full speed. This
    // is still zero-heap-allocation and correct regardless of which thread
    // (if any) actually completes `handle`.
    while (!handle.IsComplete()) {
        std::this_thread::yield();
    }
}

std::size_t JobSystem::WorkerCount() const noexcept
{
    // A well-defined, non-zero value - see this method's own header
    // comment - so a future caller can always safely divide work by it.
    return 1;
}

#endif // GTE_ENABLE_JOB_SYSTEM

} // namespace gte::Jobs
