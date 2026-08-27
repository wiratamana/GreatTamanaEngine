#pragma once

#include "JobDispatch.h"
#include "JobTypes.h"

#include <cstdint>
#include <span>

namespace gte::Jobs {

// Phase 3 (Job Dependencies / Continuations) - see
// task_manager/job_system/JOBSYSTEM_PHASE3_JOB_DEPENDENCIES_CONTINUATIONS.md
// for the full design rationale, and AGENTS.md's "Job System" section for the
// summarized rules. Built ENTIRELY on top of Phase 1/2's existing
// JobSystem::Schedule()/WaitForJobs()/JobHandle and Dispatch() - no second
// scheduler, no second queue, no parallel bookkeeping duplicated.

// Schedules `fn(payload)` to run only once EVERY handle in `dependencies` has
// completed - never before. If `dependencies` is empty, or every handle in it
// is ALREADY complete at call time (the common case: a dependency that
// finished earlier in the same frame), this degrades to an ordinary
// JobSystem::Schedule() call with zero continuation bookkeeping at all.
//
// `handle` becomes "incomplete" (WaitForJobs(handle) blocks) THE INSTANT this
// call returns, even when nothing has been pushed onto the job queue yet
// (see JobHandle::AddPendingUnit()) - a caller may safely call
// WaitForJobs(handle) immediately after this returns and correctly block
// until `fn` has actually run, no matter how long `dependencies` take to
// clear.
//
// `dependencies` itself is only read during this call - no reference to the
// span is retained afterward. Each handle's own SHARED STATE (not the
// pointer, and not the span) is what a continuation watcher is registered
// against, so a `JobHandle*` in `dependencies` may safely go out of scope
// (or even be a different, unrelated JobHandle by the time it clears) as
// long as the shared state each JobHandle* pointed at when this call was
// made is what actually completes - in practice, always pass a JobHandle
// that outlives this call, per the same convention every other Job System
// API already follows.
//
// Explicit dependencies only - there is no automatic data-flow dependency
// inference anywhere in this module (see AGENTS.md).
void ScheduleAfter(
    JobFunction fn, void* payload, std::span<JobHandle* const> dependencies, JobHandle& handle);

// The Dispatch() (JobDispatch.h) equivalent - every batch job this dispatch
// would produce waits on `dependencies` before ANY of it may run. Same
// "handle is incomplete from the instant this call returns" contract as
// ScheduleAfter() above. `itemCount == 0` is the same immediate, job-free
// no-op Dispatch() itself already defines.
void DispatchAfter(BatchJobFunction fn, std::uint32_t itemCount, void* payload,
    std::span<JobHandle* const> dependencies, JobHandle& handle, std::uint32_t minItemsPerBatch = 1);

} // namespace gte::Jobs
