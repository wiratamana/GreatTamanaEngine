#pragma once

#include "JobTypes.h"

#include <condition_variable>
#include <cstddef>
#include <memory>
#include <mutex>
#include <vector>

namespace gte::Jobs::detail {

// One queued unit of work: a plain function pointer + payload (see
// JobFunction, JobTypes.h) plus the JobHandleState it must decrement
// exactly once, right after it finishes running.
struct JobEntry {
    JobFunction fn = nullptr;
    void* payload = nullptr;
    std::shared_ptr<JobHandleState> state;
};

// Fixed-capacity MPMC ring buffer guarded by a mutex and condition variable. Capacity is set once.
// Schedule() pushes from any thread; every worker pops. A full queue is the caller's problem:
// it runs the job inline, so Push() never blocks, grows or drops silently.
// Always compiled, whatever GTE_ENABLE_JOB_SYSTEM says; only JobSystem's use of it is gated.
class JobQueue {
public:
    // `capacity` must be at least 1. Every slot is allocated once, up
    // front, in this constructor - never resized afterwards.
    explicit JobQueue(std::size_t capacity);

    JobQueue(const JobQueue&) = delete;
    JobQueue& operator=(const JobQueue&) = delete;
    JobQueue(JobQueue&&) = delete;
    JobQueue& operator=(JobQueue&&) = delete;

    // Attempts to enqueue `entry`. Returns false without modifying the
    // queue at all if it is currently at full capacity, OR if Shutdown()
    // has already been called (HOTFIX 2 - see
    // task_manager/job_system/JOBSYSTEM_HOTFIX_CODE_REVIEW_FINDINGS.md,
    // item 2: previously a push could succeed after Shutdown() was called,
    // silently stranding a job that no worker would ever pop, since workers
    // may have already exited their WaitAndPop() loop and been joined by
    // the time such a late push arrived) - the caller is responsible for a
    // graceful fallback in either case (see this class's own comment
    // above, and JobSystem::Schedule()'s full-queue fallback, which this
    // now also doubles as the "queue is shutting down" fallback for);
    // TryPush() itself never blocks, grows, or drops silently.
    bool TryPush(JobEntry entry);

    // Blocks the calling (worker) thread until either a job is available
    // (returns true, `outEntry` is populated with it) or Shutdown() has
    // been called AND the queue is empty (returns false - the worker's own
    // signal to exit its loop for good).
    bool WaitAndPop(JobEntry& outEntry);

    // Wakes every thread currently blocked in WaitAndPop() so each can
    // observe the shutdown request and return false. Called exactly once,
    // from JobSystem's destructor, before joining every worker thread.
    // Idempotent - safe to call more than once. Once called, every FUTURE
    // TryPush() call also starts returning false (see TryPush()'s own
    // comment) - an entry already queued before this call is still handed
    // out normally by WaitAndPop() first, per that method's own contract.
    void Shutdown();

private:
    std::mutex m_mutex;
    std::condition_variable m_condition;
    std::vector<JobEntry> m_slots; // Fixed-size ring buffer storage, sized once at construction.
    std::size_t m_head = 0; // Next slot WaitAndPop() reads from.
    std::size_t m_tail = 0; // Next slot TryPush() writes to.
    std::size_t m_count = 0;
    bool m_shuttingDown = false;
};

} // namespace gte::Jobs::detail
