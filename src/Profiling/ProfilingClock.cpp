#include "ProfilingClock.h"

// The ONLY file in src/Profiling/ that includes a real SDL header - see
// ProfilingClock.h's own comment for why this split exists.
#include <SDL3/SDL_timer.h>

namespace gte::Profiling {

std::uint64_t GetProfilingPerformanceCounter() noexcept
{
    return SDL_GetPerformanceCounter();
}

std::uint64_t GetProfilingPerformanceFrequency() noexcept
{
    return SDL_GetPerformanceFrequency();
}

} // namespace gte::Profiling
