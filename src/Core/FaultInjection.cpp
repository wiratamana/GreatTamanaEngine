#include "FaultInjection.h"

namespace gte {

namespace {

constexpr std::array<std::string_view, kFaultKindCount> kFaultKindNames = {
    "offscreen_throw", "present_throw", "device_lost", "main_thread_stall"
};

} // namespace

std::string_view FaultKindName(FaultKind kind) noexcept
{
    return kFaultKindNames[static_cast<std::size_t>(kind)];
}

std::optional<FaultKind> ParseFaultKind(std::string_view text) noexcept
{
    for (std::size_t i = 0; i < kFaultKindNames.size(); ++i) {
        if (kFaultKindNames[i] == text) {
            return static_cast<FaultKind>(i);
        }
    }
    return std::nullopt;
}

#if GTE_ENABLE_NETWORK

FaultInjection& FaultInjection::Instance() noexcept
{
    static FaultInjection instance;
    return instance;
}

void FaultInjection::Arm(FaultKind kind, std::uint32_t count) noexcept
{
    m_armed[static_cast<std::size_t>(kind)].store(count, std::memory_order_relaxed);
}

bool FaultInjection::Consume(FaultKind kind) noexcept
{
    std::atomic<std::uint32_t>& armed = m_armed[static_cast<std::size_t>(kind)];
    std::uint32_t current = armed.load(std::memory_order_relaxed);
    while (current > 0) {
        if (armed.compare_exchange_weak(current, current - 1, std::memory_order_relaxed)) {
            return true;
        }
    }
    return false;
}

std::array<std::uint32_t, kFaultKindCount> FaultInjection::Snapshot() const noexcept
{
    std::array<std::uint32_t, kFaultKindCount> counts{};
    for (std::size_t i = 0; i < kFaultKindCount; ++i) {
        counts[i] = m_armed[i].load(std::memory_order_relaxed);
    }
    return counts;
}

#endif

} // namespace gte
