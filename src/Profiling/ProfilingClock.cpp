#include "ProfilingClock.h"

#include <chrono>

namespace gte::Profiling {

std::uint64_t GetProfilingPerformanceCounter() noexcept
{
    const auto now = std::chrono::steady_clock::now().time_since_epoch();
    return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(now).count());
}

} // namespace gte::Profiling
