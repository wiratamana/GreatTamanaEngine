#pragma once

#include <atomic>
#include <cstdint>
#include <memory>

namespace gte::Jobs {

// A job's raw function signature - a single opaque payload pointer, never a
// std::function (which would allocate/type-erase on the hot per-job path -
// see AGENTS.md, "Job System"). `payload`'s lifetime/ownership is entirely
// the CALLING code's responsibility: JobSystem never allocates, copies, or
// frees it - it is handed straight through from Schedule() to the job body
// and back out again.
using JobFunction = void (*)(void* payload);

namespace detail {

// The shared, heap-allocated (once, at JobHandle construction - see
// JobHandle's own comment below) piece of state a JobHandle and every job
// scheduled against it both point at. A plain atomic pending-job counter:
// JobSystem::Schedule() increments it once per job scheduled against this
// handle, and the worker (or, in a GTE_ENABLE_JOB_SYSTEM=OFF build, the
// calling thread itself - see JobSystem.cpp) that finishes running a job
// decrements it exactly once right after that job's JobFunction returns.
struct JobHandleState {
    std::atomic<std::uint32_t> pending{ 0 };
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

private:
    std::shared_ptr<detail::JobHandleState> m_state;

    friend class JobSystem;
};

} // namespace gte::Jobs
