#pragma once

#include <cstdint>

namespace gte::Profiling {

// Ticks are nanoseconds from std::chrono::steady_clock. No platform library is linked for timing.
inline constexpr double kTicksPerMillisecond = 1.0e6;

constexpr double TicksToMilliseconds(std::uint64_t ticks) noexcept
{
    return static_cast<double>(ticks) / kTicksPerMillisecond;
}

// Monotonic tick counter, safe to call from any thread.
std::uint64_t GetProfilingPerformanceCounter() noexcept;

} // namespace gte::Profiling
