#pragma once

#include <cstdint>

namespace gte::Profiling {

// Hand-driven clock for tests. One tick is one nanosecond, like the real clock.
class ManualClock {
public:
    static constexpr std::uint64_t kTicksPerMillisecond = 1'000'000;

    constexpr std::uint64_t NowTicks() const noexcept { return m_now; }
    constexpr void Set(std::uint64_t ticks) noexcept { m_now = ticks; }
    constexpr void AdvanceTicks(std::uint64_t ticks) noexcept { m_now += ticks; }
    constexpr void AdvanceMilliseconds(std::uint64_t ms) noexcept { m_now += ms * kTicksPerMillisecond; }

private:
    std::uint64_t m_now = 0;
};

} // namespace gte::Profiling
