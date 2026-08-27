#pragma once

#include "FrameProfiler.h"

#if GTE_ENABLE_PROFILER
#include "../Jobs/JobSystem.h"

#include <SDL3/SDL_timer.h>
#include <cstddef>
#include <cstdint>
#include <optional>
#endif

// Job System Phase 5 (Profiler Integration - Worker Timeline - see
// task_manager/job_system/JOBSYSTEM_PHASE5_PROFILER_INTEGRATION_WORKER_TIMELINE_v2.md
// and AGENTS.md, "Job System"): instruments the REST OF THE ENCLOSING SCOPE
// as one named CPU sample, attributed to whichever Job System worker thread
// is currently running it - the per-job-body counterpart of
// GTE_PROFILE_SCOPE (see ScopeTimer.h). This is the ONLY correct way to
// profile code running INSIDE a job body (a function passed to
// gte::Jobs::Dispatch()/Schedule()/ScheduleAfter()/DispatchAfter()) - per
// AGENTS.md's Job System Phase 4 thread-safety table,
// Profiling::FrameProfiler itself remains NEVER-safe to touch directly from
// a job body; GTE_PROFILE_JOB_SCOPE instead routes through
// FrameProfiler::RecordWorkerJobSample(), the ONE thread-safe write path
// this module exposes (Phase 5).
//
// NEVER call GTE_PROFILE_SCOPE (ScopeTimer.h) from inside a job body, and
// NEVER call GTE_PROFILE_JOB_SCOPE from the main thread - see
// gte::Jobs::JobSystem::WorkerIndexForCurrentThread()'s own comment: calling
// this from a thread that isn't a genuine Job System worker thread makes
// JobScopeTimer silently skip recording rather than attribute a scope to a
// fabricated worker index - a caller violating this rule doesn't crash, it
// simply produces no worker-timeline data for that one scope. This rule is
// only genuinely ENFORCED (nullopt returned for the main thread) when
// GTE_ENABLE_JOB_SYSTEM is ON, where a real, distinguishable worker thread
// actually exists - see WorkerIndexForCurrentThread()'s own comment for why
// a GTE_ENABLE_JOB_SYSTEM=OFF build cannot draw this same distinction (the
// calling thread and the one conceptual "worker" are, by design, identical
// in that configuration) and always attributes a recorded scope to worker 0
// instead.
//
// Compiles to a true empty no-op when GTE_ENABLE_PROFILER is OFF, exactly
// mirroring GTE_PROFILE_SCOPE/ScopeTimer's own compile-time gate.
#define GTE_PROFILE_JOB_SCOPE(name) \
    ::gte::Profiling::JobScopeTimer GTE_PROFILE_JOB_SCOPE_CONCAT(gteProfileJobScope_, __LINE__)(name)

#define GTE_PROFILE_JOB_SCOPE_CONCAT_INNER(a, b) a##b
#define GTE_PROFILE_JOB_SCOPE_CONCAT(a, b) GTE_PROFILE_JOB_SCOPE_CONCAT_INNER(a, b)

namespace gte::Profiling {

#if GTE_ENABLE_PROFILER

// RAII per-job-body CPU scope timer - see this header's own top comment and
// AGENTS.md ("Job System") for the full convention. `name` MUST be a string
// literal (or otherwise static-storage-duration) const char*, same
// requirement as ScopeTimer's own (see FrameProfiler::RecordWorkerJobSample()/
// RecordCpuScope()).
//
// Never allocates. Skips reading the clock/registering a worker-job sample
// entirely whenever EITHER the runtime capture-enabled flag
// (FrameProfiler::IsCaptureEnabled()) is false, OR the calling thread isn't
// a genuine Job System worker thread (see
// gte::Jobs::JobSystem::WorkerIndexForCurrentThread()) - so an ordinary,
// correctly-scoped call costs one branch plus one std::optional check when
// disabled/misused, never a clock read.
class JobScopeTimer {
public:
    explicit JobScopeTimer(const char* name) noexcept
        : m_name(name)
    {
        if (!FrameProfiler::Instance().IsCaptureEnabled()) {
            return;
        }

        const std::optional<std::size_t> workerIndex
            = ::gte::Jobs::JobSystem::Instance().WorkerIndexForCurrentThread();
        if (!workerIndex.has_value()) {
            // Not a genuine Job System worker thread - see this class's own
            // header comment above.
            return;
        }

        m_workerIndex = *workerIndex;
        m_active = true;
        m_startTicks = SDL_GetPerformanceCounter();
    }

    ~JobScopeTimer()
    {
        if (!m_active) {
            return;
        }
        const std::uint64_t endTicks = SDL_GetPerformanceCounter();
        const std::uint64_t frequency = SDL_GetPerformanceFrequency();
        const double elapsedMs = frequency != 0
            ? static_cast<double>(endTicks - m_startTicks) * 1000.0 / static_cast<double>(frequency)
            : 0.0;
        FrameProfiler::Instance().RecordWorkerJobSample(m_workerIndex, m_name, elapsedMs, m_startTicks);
    }

    JobScopeTimer(const JobScopeTimer&) = delete;
    JobScopeTimer& operator=(const JobScopeTimer&) = delete;
    JobScopeTimer(JobScopeTimer&&) = delete;
    JobScopeTimer& operator=(JobScopeTimer&&) = delete;

private:
    const char* m_name;
    bool m_active = false;
    std::size_t m_workerIndex = 0;
    std::uint64_t m_startTicks = 0;
};

#else // !GTE_ENABLE_PROFILER

// Compiled-out form: an empty type with a trivial constructor doing
// nothing at all - mirrors ScopeTimer's own GTE_ENABLE_PROFILER=OFF branch
// exactly (see ScopeTimer.h).
class JobScopeTimer {
public:
    explicit JobScopeTimer(const char*) noexcept { }

    JobScopeTimer(const JobScopeTimer&) = delete;
    JobScopeTimer& operator=(const JobScopeTimer&) = delete;
};

#endif // GTE_ENABLE_PROFILER

} // namespace gte::Profiling
