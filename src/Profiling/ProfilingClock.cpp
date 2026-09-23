#include "ProfilingClock.h"

// editor-core-separation-1 campaign, PHASE14
// (PHASE14_WINDOW_SDL_RELOCATION_TO_EDITOR.md) - Phase 11's own original fix
// hid SDL_GetPerformanceCounter()/SDL_GetPerformanceFrequency() behind this
// pair of functions, but STILL called the real SDL clock functions here,
// under the (at-the-time correct) assumption that gte_core would always
// link SDL3 anyway. Phase 14's own new archive-content regression test
// (tests/Build/SdlLinkageRegressionTests.cpp's
// GteCoreArchiveNoLongerReferencesRealSdl3FunctionSymbols) caught this as a
// real, live leak once gte_core actually tried to drop SDL3::SDL3 at link
// time for good - this file is now backed by std::chrono::steady_clock
// instead, a portable C++ standard-library facility needing zero extra
// link dependency at all (unlike SDL3, which is a real external DLL/import
// library). Semantics are preserved EXACTLY: GetProfilingPerformanceCounter()
// returns a monotonic tick count in NANOSECONDS (steady_clock's own
// resolution), and GetProfilingPerformanceFrequency() returns the fixed
// "ticks per second" divisor every caller already divides by
// (1,000,000,000 - one nanosecond's own reciprocal) - so every existing
// caller's `(now - start) * 1000 / frequency` millisecond conversion
// (ScopeTimer.h, JobScopeTimer.h, FrameProfiler.cpp, WorkerTimelineData.cpp)
// keeps producing byte-identical results with zero call-site change.
#include <chrono>

namespace gte::Profiling {

std::uint64_t GetProfilingPerformanceCounter() noexcept
{
    const auto now = std::chrono::steady_clock::now().time_since_epoch();
    return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(now).count());
}

std::uint64_t GetProfilingPerformanceFrequency() noexcept
{
    // steady_clock's own tick unit here is exactly one nanosecond (see
    // GetProfilingPerformanceCounter() above) - so "ticks per second" is
    // simply 1e9, a fixed constant, never a real runtime query.
    return static_cast<std::uint64_t>(1'000'000'000);
}

} // namespace gte::Profiling
