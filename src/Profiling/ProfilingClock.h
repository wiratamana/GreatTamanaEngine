#pragma once

#include <cstdint>

// `editor-core-separation-1` campaign, Phase 11 (see
// task_manager/editor-core-separation-1/PHASE11_PROFILING_SDL_CLOCK_LEAK_FIX.md):
// hides the two raw clock calls `ScopeTimer.h`/`JobScopeTimer.h` use behind a
// tiny internal function pair declared here and defined ONLY in
// `ProfilingClock.cpp` - the same trick `src/Window/Window.h` used to keep
// `<SDL3/SDL.h>` out of any header a `gte_core` translation unit includes
// (see `Window.h`'s own comment) BEFORE Phase 14 relocated `Window.cpp`
// itself into `gte_editor` entirely.
//
// Phase 14 (PHASE14_WINDOW_SDL_RELOCATION_TO_EDITOR.md) changed WHAT backs
// these two functions: originally `SDL_GetPerformanceCounter()`/
// `SDL_GetPerformanceFrequency()` (from `<SDL3/SDL_timer.h>`), now
// `std::chrono::steady_clock` - see `ProfilingClock.cpp`'s own comment for
// why. This header itself never included any SDL header even before that
// change, and still doesn't - only `ProfilingClock.cpp` ever needed to.
// Both call sites (`ScopeTimer.h`, `JobScopeTimer.h`) are behind
// `#if GTE_ENABLE_PROFILER` already; this header carries no such guard itself,
// since it has no dependency worth gating - it is cheap to include either way.
namespace gte::Profiling {

// A monotonic, high-resolution tick counter with no fixed unit (must be
// divided by GetProfilingPerformanceFrequency() to become seconds/
// milliseconds) - mirrors SDL_GetPerformanceCounter()'s own contract exactly
// (Phase 14: now backed by std::chrono::steady_clock, not SDL).
std::uint64_t GetProfilingPerformanceCounter() noexcept;

// Ticks per second for the counter above. Fixed for the life of the process
// - mirrors SDL_GetPerformanceFrequency()'s own contract exactly (Phase 14:
// now a fixed compile-time constant, not a runtime SDL query).
std::uint64_t GetProfilingPerformanceFrequency() noexcept;

} // namespace gte::Profiling
