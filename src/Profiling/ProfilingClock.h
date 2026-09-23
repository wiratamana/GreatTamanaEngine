#pragma once

#include <cstdint>

// `editor-core-separation-1` campaign, Phase 11 (see
// task_manager/editor-core-separation-1/PHASE11_PROFILING_SDL_CLOCK_LEAK_FIX.md):
// hides the two raw SDL clock calls `ScopeTimer.h`/`JobScopeTimer.h` used to
// call directly (`SDL_GetPerformanceCounter()`/`SDL_GetPerformanceFrequency()`
// from `<SDL3/SDL_timer.h>`) behind a tiny internal function pair declared
// here and defined ONLY in `ProfilingClock.cpp` - the same trick
// `src/Window/Window.h` already uses to keep `<SDL3/SDL.h>` out of any header
// a `gte_core` translation unit includes (see `Window.h`'s own comment).
// This header itself never includes any SDL header - only `ProfilingClock.cpp`
// does. Both call sites (`ScopeTimer.h`, `JobScopeTimer.h`) are behind
// `#if GTE_ENABLE_PROFILER` already; this header carries no such guard itself,
// since it has no dependency worth gating - it is cheap to include either way.
namespace gte::Profiling {

// Mirrors SDL_GetPerformanceCounter() exactly - a monotonic, high-resolution
// tick counter with no fixed unit (must be divided by
// GetProfilingPerformanceFrequency() to become seconds/milliseconds).
std::uint64_t GetProfilingPerformanceCounter() noexcept;

// Mirrors SDL_GetPerformanceFrequency() exactly - ticks per second for the
// counter above. Fixed for the life of the process.
std::uint64_t GetProfilingPerformanceFrequency() noexcept;

} // namespace gte::Profiling
