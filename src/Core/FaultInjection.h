#pragma once

#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>

namespace gte {

enum class FaultKind : std::uint8_t { OffscreenThrow, PresentThrow, DeviceLost, MainThreadStall };

inline constexpr std::size_t kFaultKindCount = 4;
inline constexpr std::uint32_t kMaxFaultCount = 1000;
inline constexpr std::chrono::seconds kInjectedStallDuration{ 3 };

// Query text of a kind ("offscreen_throw", "present_throw", "device_lost", "main_thread_stall").
std::string_view FaultKindName(FaultKind kind) noexcept;
std::optional<FaultKind> ParseFaultKind(std::string_view text) noexcept;

#if GTE_ENABLE_NETWORK

// Armed fault counters. Any thread may arm or consume; each fault fires once per armed count.
class FaultInjection {
public:
    static FaultInjection& Instance() noexcept;

    // Replaces the armed count of `kind`.
    void Arm(FaultKind kind, std::uint32_t count) noexcept;

    // True when a fault was armed; consumes one count.
    bool Consume(FaultKind kind) noexcept;

    std::array<std::uint32_t, kFaultKindCount> Snapshot() const noexcept;

private:
    std::array<std::atomic<std::uint32_t>, kFaultKindCount> m_armed{};
};

inline bool FaultFires(FaultKind kind) noexcept
{
    return FaultInjection::Instance().Consume(kind);
}

#else

constexpr bool FaultFires(FaultKind) noexcept
{
    return false;
}

#endif

} // namespace gte
