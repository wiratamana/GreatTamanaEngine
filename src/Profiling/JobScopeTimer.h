#pragma once

#include "FrameProfiler.h"
#include "NameTable.h"
#include "ProfilingClock.h"
#include "ScopeTimer.h"

#if GTE_ENABLE_PROFILER
#include "../Jobs/JobSystem.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#endif

#if GTE_ENABLE_PROFILER

// Times the rest of a job body as one scope on the worker thread running it. `name` must be a string literal.
// Use it only inside job bodies; a call from any other thread records nothing.
#define GTE_PROFILE_JOB_SCOPE(name) \
    ::gte::Profiling::JobScopeTimer GTE_PROFILE_CONCAT(gteProfileJobScope_, __LINE__)(GTE_PROFILE_NAME_ID(name))

#else

#define GTE_PROFILE_JOB_SCOPE(name) static_assert(true)

#endif // GTE_ENABLE_PROFILER

namespace gte::Profiling {

#if GTE_ENABLE_PROFILER

// RAII job scope. Reads the clock only when capture is on and the thread is a job worker.
class JobScopeTimer {
public:
    explicit JobScopeTimer(NameId name) noexcept
        : m_name(name)
    {
        if (!FrameProfiler::Instance().IsCaptureEnabled()) {
            return;
        }
        const std::optional<std::size_t> workerIndex = ::gte::Jobs::JobSystem::Instance().WorkerIndexForCurrentThread();
        if (!workerIndex.has_value()) {
            return;
        }
        m_workerIndex = *workerIndex;
        m_active = true;
        m_startTicks = GetProfilingPerformanceCounter();
    }

    ~JobScopeTimer()
    {
        if (m_active) {
            FrameProfiler::Instance().RecordWorkerScope(
                m_workerIndex, m_name, m_startTicks, GetProfilingPerformanceCounter());
        }
    }

    JobScopeTimer(const JobScopeTimer&) = delete;
    JobScopeTimer& operator=(const JobScopeTimer&) = delete;

private:
    NameId m_name;
    bool m_active = false;
    std::size_t m_workerIndex = 0;
    std::uint64_t m_startTicks = 0;
};

#else

// Compiled-out form, so code that names the type still builds.
class JobScopeTimer {
public:
    explicit JobScopeTimer(NameId) noexcept { }

    JobScopeTimer(const JobScopeTimer&) = delete;
    JobScopeTimer& operator=(const JobScopeTimer&) = delete;
};

#endif // GTE_ENABLE_PROFILER

} // namespace gte::Profiling
