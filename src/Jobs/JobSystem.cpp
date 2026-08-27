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

// --- HOTFIX 2: background-thread registry (see JobSystem.h's own comments
// on RegisterBackgroundThread()/IsShuttingDown()/JoinAllBackgroundThreads())
// -----------------------------------------------------------------------
// Deliberately common to BOTH the GTE_ENABLE_JOB_SYSTEM=ON and =OFF
// configurations (defined here, outside either #if branch below) - a
// background fallback thread (JobContinuation.cpp's
// WatchDependencyWithFallback()) can be spawned regardless of that switch,
// since the overflow condition that triggers it (a dependency handle
// already having detail::kMaxWatchersPerHandle OTHER continuations
// registered against it) has nothing to do with whether a real worker-
// thread pool exists.

void JobSystem::RegisterBackgroundThread(std::thread thread, std::shared_ptr<std::atomic<bool>> completionFlag)
{
    if (m_shuttingDown.load(std::memory_order_acquire)) {
        // Too late to register normally - JoinAllBackgroundThreads() may
        // already have collected (and be joining) the registry by the time
        // this call happens. Join synchronously, right here, so `thread` is
        // never silently dropped from an already-collected registry -
        // see this method's own header comment.
        if (thread.joinable()) {
            thread.join();
        }
        if (completionFlag) {
            completionFlag->store(true, std::memory_order_release);
        }
        return;
    }

    std::lock_guard<std::mutex> lock(m_backgroundThreadsMutex);

    // Opportunistically join/discard any previously-registered thread that
    // has already finished (its own completionFlag set to true right before
    // it returned) before appending the new one - keeps this registry from
    // growing completely unbounded across a long-running process, without
    // requiring anything to proactively prune it the instant a thread
    // finishes.
    for (std::size_t i = 0; i < m_backgroundThreads.size();) {
        BackgroundThreadEntry& entry = m_backgroundThreads[i];
        if (entry.completionFlag && entry.completionFlag->load(std::memory_order_acquire)) {
            if (entry.thread.joinable()) {
                entry.thread.join();
            }
            m_backgroundThreads.erase(m_backgroundThreads.begin() + static_cast<std::ptrdiff_t>(i));
        } else {
            ++i;
        }
    }

    m_backgroundThreads.push_back(BackgroundThreadEntry{ std::move(thread), std::move(completionFlag) });
}

bool JobSystem::IsShuttingDown() const noexcept
{
    return m_shuttingDown.load(std::memory_order_acquire);
}

void JobSystem::JoinAllBackgroundThreads()
{
    std::vector<BackgroundThreadEntry> backgroundThreads;
    {
        std::lock_guard<std::mutex> lock(m_backgroundThreadsMutex);
        backgroundThreads = std::move(m_backgroundThreads);
    }
    for (BackgroundThreadEntry& entry : backgroundThreads) {
        if (entry.thread.joinable()) {
            entry.thread.join();
        }
    }
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
    // HOTFIX 2: mark shutdown BEFORE tearing down anything else, so any
    // background fallback thread (see RegisterBackgroundThread()) that
    // wakes up mid-teardown observes it (via IsShuttingDown()) and bails
    // out immediately, without calling back into JobSystem::Instance()
    // again - see JobContinuation.cpp's RunPollingFallbackJob().
    m_shuttingDown.store(true, std::memory_order_release);

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

    // HOTFIX 2: join every still-registered background fallback thread
    // too - previously these were fully detached, with zero lifecycle tie
    // to JobSystem at all, which meant a still-spinning one could call
    // JobSystem::Instance() during/after this singleton's own static
    // destruction (a real crash/UB hazard). By the time this call returns,
    // every background thread ever registered against this instance is
    // guaranteed to have already exited.
    JoinAllBackgroundThreads();
}

void JobSystem::WorkerLoop()
{
    detail::JobEntry entry;
    while (m_queue.WaitAndPop(entry)) {
        entry.fn(entry.payload);

        if (entry.state) {
            // The pending-count decrement (and, as of HOTFIX 8, the
            // watcher-list capture that goes with it) MUST be bracketed by
            // the SAME mutex WaitForJobs() holds while it checks its own
            // predicate (m_completionMutex) - even though `pending` is
            // itself atomic, an atomic write alone does NOT prevent a
            // classic condition_variable lost-wakeup race: a waiter could
            // check IsComplete() (see it as still false, while still
            // holding m_completionMutex), and then, before it actually
            // finishes registering itself as a waiter on
            // m_completionCondition, this decrement-then-notify_all()
            // sequence could run to completion on another thread and find
            // no one registered yet to wake - the waiter would then block
            // forever waiting for a notification that already happened
            // moments earlier. Holding m_completionMutex around the
            // decrement forces it to serialize against WaitForJobs()'s own
            // lock-held predicate check/wait-registration, closing that
            // window - see cppreference's own condition_variable::
            // notify_all() docs: "even though the shared variable is
            // atomic, it must be modified... while owning the mutex to
            // correctly publish the modification to the waiting thread."
            // (Verified: this exact race reproduced intermittently -
            // roughly 1 in 4 runs - under
            // JobSystemTests.ManyJobsAgainstOneSharedHandleAllCompleteWithoutCorruption's
            // 256-jobs-per-handle load before this fix; a --gtest_repeat
            // stress run showed zero hangs across 100+ iterations after
            // it.)
            //
            // HOTFIX 8 (see task_manager/job_system/JOBSYSTEM_HOTFIX_CODE_REVIEW_FINDINGS.md,
            // item 8, JOB_SYSTEM_HOTFIX8_COMPLETION_REPORT.md): the
            // decrement and the watcher-list capture now happen as ONE
            // operation, JobHandleState::CompleteOneAndTakeWatchers() -
            // previously the decrement happened here (under
            // m_completionMutex), and the watcher list was only captured/
            // cleared LATER, inside a separate FireWatchers() call guarded
            // by a DIFFERENT mutex (watcherMutex) - leaving a window
            // between "pending publicly reached zero" and "the watcher
            // list actually captured" that a concurrent Schedule() call
            // against the SAME handle (bumping pending back up for a
            // brand-new job) could race into, sweeping a brand-new
            // registration into THIS, OLDER completion's fired batch
            // instead of waiting for its own. Folding both into
            // CompleteOneAndTakeWatchers() (itself internally guarded by
            // watcherMutex) closes that race - see that method's own
            // comment for the full reasoning. `completedToZero` tells us
            // whether THIS decrement was the one that brought the handle
            // down to zero - exactly one thread ever observes this true
            // for a given handle's completion.
            std::array<detail::WatcherFunction, detail::kMaxWatchersPerHandle> firedFns{};
            std::array<void*, detail::kMaxWatchersPerHandle> firedContexts{};
            std::size_t firedCount = 0;
            bool completedToZero;
            {
                std::lock_guard<std::mutex> lock(m_completionMutex);
                completedToZero = entry.state->CompleteOneAndTakeWatchers(firedFns, firedContexts, firedCount);
            }

            if (completedToZero) {
                // Deliberately fired AFTER releasing m_completionMutex
                // above: a fired watcher (JobContinuation.cpp's
                // OnDependencyCleared()) may itself call back into
                // JobSystem::Schedule()/ScheduleAlreadyPending() - never
                // hold m_completionMutex while running arbitrary callback
                // code.
                for (std::size_t i = 0; i < firedCount; ++i) {
                    firedFns[i](firedContexts[i]);
                }
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
    // thread, rather than blocking Schedule() or dropping the job. This is
    // also the path a late Schedule()/ScheduleAlreadyPending() call takes
    // once JobQueue::TryPush() starts rejecting pushes after Shutdown()
    // (HOTFIX 2) - see JobQueue.cpp's own comment.
    fn(payload);
    // HOTFIX 8 (see task_manager/job_system/JOBSYSTEM_HOTFIX_CODE_REVIEW_FINDINGS.md,
    // item 8, JOB_SYSTEM_HOTFIX8_COMPLETION_REPORT.md): decrement+watcher-
    // capture folded into one JobHandleState-guarded operation,
    // CompleteOneAndTakeWatchers() - same lock-bracketed requirement as
    // WorkerLoop() above - see that method's own comment for the full "why"
    // (the exact same lost-wakeup race, AND the same watcher-batch race
    // item 8 describes, are both possible here too, however unlikely this
    // fallback path is to actually be hit in practice).
    std::array<detail::WatcherFunction, detail::kMaxWatchersPerHandle> firedFns{};
    std::array<void*, detail::kMaxWatchersPerHandle> firedContexts{};
    std::size_t firedCount = 0;
    bool completedToZero;
    {
        std::lock_guard<std::mutex> lock(m_completionMutex);
        completedToZero = handle.m_state->CompleteOneAndTakeWatchers(firedFns, firedContexts, firedCount);
    }
    if (completedToZero) {
        // See WorkerLoop()'s own comment on why this runs unlocked.
        for (std::size_t i = 0; i < firedCount; ++i) {
            firedFns[i](firedContexts[i]);
        }
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
    // HOTFIX 8: decrement+watcher-capture folded into one
    // JobHandleState-guarded operation - see WorkerLoop()'s own comment for
    // the full rationale (the same lost-wakeup AND watcher-batch races are
    // both possible here too, however unlikely this fallback path is to
    // actually be hit in practice).
    std::array<detail::WatcherFunction, detail::kMaxWatchersPerHandle> firedFns{};
    std::array<void*, detail::kMaxWatchersPerHandle> firedContexts{};
    std::size_t firedCount = 0;
    bool completedToZero;
    {
        std::lock_guard<std::mutex> lock(m_completionMutex);
        completedToZero = handle.m_state->CompleteOneAndTakeWatchers(firedFns, firedContexts, firedCount);
    }
    if (completedToZero) {
        for (std::size_t i = 0; i < firedCount; ++i) {
            firedFns[i](firedContexts[i]);
        }
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

JobSystem::~JobSystem()
{
    // See the GTE_ENABLE_JOB_SYSTEM=ON destructor above for the full
    // rationale - a background fallback thread can exist in THIS
    // configuration too (JobContinuation.cpp's overflow fallback has
    // nothing to do with GTE_ENABLE_JOB_SYSTEM), so the exact same HOTFIX 2
    // shutdown-then-join sequence is required here as well.
    m_shuttingDown.store(true, std::memory_order_release);
    JoinAllBackgroundThreads();
}

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
    // HOTFIX 8: decrement+watcher-capture folded into one
    // JobHandleState-guarded operation - no m_completionMutex exists in
    // this configuration (see this class's own header comment), but
    // CompleteOneAndTakeWatchers() is still safe to call without an outer
    // lock here since JobHandleState's own internal watcherMutex fully
    // guards the decrement+capture against a concurrent AddWatcher() call -
    // see that method's own comment for the full rationale.
    std::array<detail::WatcherFunction, detail::kMaxWatchersPerHandle> firedFns{};
    std::array<void*, detail::kMaxWatchersPerHandle> firedContexts{};
    std::size_t firedCount = 0;
    if (handle.m_state->CompleteOneAndTakeWatchers(firedFns, firedContexts, firedCount)) {
        for (std::size_t i = 0; i < firedCount; ++i) {
            firedFns[i](firedContexts[i]);
        }
    }
}

void JobSystem::ScheduleAlreadyPending(JobFunction fn, void* payload, JobHandle& handle)
{
    // Same "no increment here, the caller already accounted for it" contract
    // as the GTE_ENABLE_JOB_SYSTEM=ON branch above - see
    // JobHandle::AddPendingUnit()'s own comment.
    fn(payload);
    // HOTFIX 8: decrement+watcher-capture folded into one
    // JobHandleState-guarded operation - see CompleteOneAndTakeWatchers()'s
    // own comment, and JobSystem::WorkerLoop()'s, for the full rationale.
    std::array<detail::WatcherFunction, detail::kMaxWatchersPerHandle> firedFns{};
    std::array<void*, detail::kMaxWatchersPerHandle> firedContexts{};
    std::size_t firedCount = 0;
    if (handle.m_state->CompleteOneAndTakeWatchers(firedFns, firedContexts, firedCount)) {
        for (std::size_t i = 0; i < firedCount; ++i) {
            firedFns[i](firedContexts[i]);
        }
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
