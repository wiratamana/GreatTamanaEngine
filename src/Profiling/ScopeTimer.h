#pragma once

#include "FrameProfiler.h"

#if GTE_ENABLE_PROFILER
#include <SDL3/SDL_timer.h>
#include <cstdint>
#endif

// Instruments the REST OF THE ENCLOSING SCOPE as one named CPU sample -
// the primary, preferred way to add a new profiling call site (see
// AGENTS.md, "Profiling"). Constructs a local gte::Profiling::ScopeTimer
// bound to `name` (a string literal - see ScopeTimer's own class comment
// for why it must be) whose destructor fires at the natural end of the
// block it's declared in, exactly the same "acquire in constructor,
// release in destructor" RAII discipline this engine already mandates for
// every other resource (see AGENTS.md, "Coding Guidelines").
//
// Compiles to a true empty no-op (`((void)0)` - no clock read, no
// FrameProfiler call, nothing) when GTE_ENABLE_PROFILER is OFF - see
// ScopeTimer's own `#else` branch below, and PROFILER_STRATEGY_v2.md's
// Phase 0b for why this two-layer (compile-time here, runtime via
// FrameProfiler::IsCaptureEnabled()) on/off switch exists.
#define GTE_PROFILE_SCOPE(name) \
    ::gte::Profiling::ScopeTimer GTE_PROFILE_SCOPE_CONCAT(gteProfileScope_, __LINE__)(name)

#define GTE_PROFILE_SCOPE_CONCAT_INNER(a, b) a##b
#define GTE_PROFILE_SCOPE_CONCAT(a, b) GTE_PROFILE_SCOPE_CONCAT_INNER(a, b)

namespace gte::Profiling {

#if GTE_ENABLE_PROFILER

// RAII CPU scope timer - see AGENTS.md ("Profiling") for the full
// convention this class/macro follows. `name` MUST be a string literal
// (or otherwise static-storage-duration) const char* - it is compared
// against other scope names via pointer/strcmp() equality every time (see
// FrameProfiler::RecordCpuScope()), so a temporary/stack-lifetime string
// would be a use-after-free risk with zero benefit (the whole point of a
// literal is that its storage already outlives the entire program).
//
// Never allocates. Skips reading the clock entirely when the runtime
// capture-enabled flag (FrameProfiler::IsCaptureEnabled()) is false, so a
// disabled-but-compiled-in profiler costs exactly one branch per scope,
// never a clock read - see PROFILER_STRATEGY_v2.md, Step 1.3's overhead
// budget and Phase 0b.
class ScopeTimer {
public:
    explicit ScopeTimer(const char* name) noexcept
        : m_name(name)
    {
        if (FrameProfiler::Instance().IsCaptureEnabled()) {
            m_active = true;
            m_startTicks = SDL_GetPerformanceCounter();
        }
    }

    ~ScopeTimer()
    {
        if (!m_active) {
            return;
        }
        const std::uint64_t endTicks = SDL_GetPerformanceCounter();
        const std::uint64_t frequency = SDL_GetPerformanceFrequency();
        const double elapsedMs = frequency != 0
            ? static_cast<double>(endTicks - m_startTicks) * 1000.0 / static_cast<double>(frequency)
            : 0.0;
        FrameProfiler::Instance().RecordCpuScope(m_name, elapsedMs);
    }

    ScopeTimer(const ScopeTimer&) = delete;
    ScopeTimer& operator=(const ScopeTimer&) = delete;
    ScopeTimer(ScopeTimer&&) = delete;
    ScopeTimer& operator=(ScopeTimer&&) = delete;

private:
    const char* m_name;
    bool m_active = false;
    std::uint64_t m_startTicks = 0;
};

#else // !GTE_ENABLE_PROFILER

// Compiled-out form: an empty type with a trivial constructor doing
// nothing at all - the compiler has nothing left to even inline away.
// This is the "genuinely zero cost, not just small" branch a true
// minimal-size release build can opt into - see
// PROFILER_STRATEGY_v2.md, Phase 0b.
class ScopeTimer {
public:
    explicit ScopeTimer(const char*) noexcept { }

    ScopeTimer(const ScopeTimer&) = delete;
    ScopeTimer& operator=(const ScopeTimer&) = delete;
};

#endif // GTE_ENABLE_PROFILER

} // namespace gte::Profiling
