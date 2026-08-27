#include "JobSystem.h"

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
            std::lock_guard<std::mutex> lock(m_completionMutex);
            entry.state->pending.fetch_sub(1, std::memory_order_acq_rel);
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
    {
        // Same lock-bracketed-decrement requirement as WorkerLoop() above -
        // see that method's own comment for the full "why" (the exact same
        // lost-wakeup race is possible here too, however unlikely this
        // fallback path is to actually be hit in practice).
        std::lock_guard<std::mutex> lock(m_completionMutex);
        handle.m_state->pending.fetch_sub(1, std::memory_order_acq_rel);
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
    handle.m_state->pending.fetch_sub(1, std::memory_order_acq_rel);
}

void JobSystem::WaitForJobs(JobHandle& handle)
{
    // Schedule() above always finishes the job before returning in this
    // configuration, so `handle` is already complete by the time any
    // caller could reach here - nothing to wait for.
    (void)handle;
}

std::size_t JobSystem::WorkerCount() const noexcept
{
    // A well-defined, non-zero value - see this method's own header
    // comment - so a future caller can always safely divide work by it.
    return 1;
}

#endif // GTE_ENABLE_JOB_SYSTEM

} // namespace gte::Jobs
