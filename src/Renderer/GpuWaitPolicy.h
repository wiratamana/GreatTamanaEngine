#pragma once

// Vulkan-free timeout and device-lost policy for blocking GPU waits.

#include "RenderTimingObserver.h"

#include <atomic>
#include <chrono>
#include <cstdint>

namespace gte {

// Frame waits may be paused by a debugger, so a few timeouts are tolerated.
// Upload, one-shot and idle waits that fail leave work possibly running.
enum class WaitClass : std::uint8_t { Frame, Upload };

inline constexpr std::chrono::nanoseconds kFrameWaitTimeout = std::chrono::seconds(2);
inline constexpr std::chrono::nanoseconds kAcquireTimeout = std::chrono::seconds(2);
inline constexpr std::chrono::nanoseconds kUploadWaitTimeout = std::chrono::seconds(10);
inline constexpr std::chrono::nanoseconds kIdleWaitTimeout = std::chrono::seconds(10);

// Consecutive frame-wait timeouts that are tolerated; the next one marks the device lost.
inline constexpr std::uint32_t kToleratedFrameTimeouts = 3;

// Thread-safe. Folds raw wait results into the outcome a caller must act on.
class GpuWaitPolicy {
public:
    // Returns Completed, TimedOut, Failed, or DeviceLost once the device is considered lost.
    BlockOutcome Judge(WaitClass waitClass, BlockOutcome raw) noexcept;

    bool Lost() const noexcept { return m_lost.load(std::memory_order_acquire); }
    void MarkLost() noexcept { m_lost.store(true, std::memory_order_release); }
    std::uint32_t ConsecutiveFrameTimeouts() const noexcept
    {
        return m_frameTimeouts.load(std::memory_order_relaxed);
    }

private:
    std::atomic<bool> m_lost{ false };
    std::atomic<std::uint32_t> m_frameTimeouts{ 0 };
};

} // namespace gte
