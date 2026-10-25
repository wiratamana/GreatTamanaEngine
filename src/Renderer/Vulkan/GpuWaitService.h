#pragma once

// The only place that blocks the CPU on the GPU. Every wait is bounded, named by a site, result-checked,
// reported to the observer and logged once per distinct failure.

#include "../GpuWaitPolicy.h"
#include "../RenderTimingObserver.h"

#include <volk.h>

#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <mutex>

namespace gte {

class GpuWaitService {
public:
    struct Acquired {
        BlockOutcome outcome = BlockOutcome::Failed;
        VkResult result = VK_ERROR_UNKNOWN; // Raw acquire result; out-of-date and suboptimal count as Completed.
        std::uint32_t imageIndex = 0;
    };

    // The wait currently in progress on any thread, for a watchdog. Best effort with concurrent waits.
    struct LiveBlock {
        bool active = false;
        BlockSite site = BlockSite::Offscreen;
        std::uint64_t startNs = 0; // steady_clock, microsecond resolution.
    };

    // `queueMutex` is the lock every submit to `graphicsQueue` takes. Throws if the drain fence cannot be created.
    GpuWaitService(VkDevice device, VkQueue graphicsQueue, std::mutex& queueMutex);
    ~GpuWaitService();

    GpuWaitService(const GpuWaitService&) = delete;
    GpuWaitService& operator=(const GpuWaitService&) = delete;
    GpuWaitService(GpuWaitService&&) = delete;
    GpuWaitService& operator=(GpuWaitService&&) = delete;

    // The observer must outlive its installation; pass nullptr to remove it.
    void SetObserver(IRenderTimingObserver* observer) noexcept { m_observer.store(observer, std::memory_order_release); }

    [[nodiscard]] BlockOutcome WaitFence(VkFence fence, std::chrono::nanoseconds timeout, WaitClass waitClass,
        BlockSite site, const char* detail) noexcept;

    // Drains the graphics queue with a bounded fence wait, then the rest of the device.
    [[nodiscard]] BlockOutcome WaitIdle(std::chrono::nanoseconds timeout, BlockSite site, const char* detail) noexcept;

    [[nodiscard]] Acquired AcquireImage(VkSwapchainKHR swapchain, VkSemaphore imageAvailable,
        std::chrono::nanoseconds timeout, BlockSite site, const char* detail) noexcept;

    // Takes a fence the GPU may still signal. It is destroyed after a later idle wait completes.
    void Quarantine(VkFence fence) noexcept;

    bool DeviceLost() const noexcept { return m_policy.Lost(); }
    void MarkDeviceLost() noexcept { m_policy.MarkLost(); }
    LiveBlock CurrentBlock() const noexcept;

private:
    static constexpr std::size_t kQuarantineCapacity = 32;
    static_assert(kBlockSiteCount * 3 <= 64, "one log-once bit per site and failure kind");

    std::uint64_t BeginBlock(BlockSite site, const char* detail) noexcept;
    BlockOutcome EndBlock(std::uint64_t token, WaitClass waitClass, BlockSite site, const char* detail,
        BlockOutcome raw) noexcept;
    BlockOutcome DrainGraphicsQueue(std::chrono::nanoseconds timeout) noexcept;
    void LogOnce(BlockSite site, BlockOutcome outcome, const char* detail) noexcept;
    void ReleaseQuarantine() noexcept;

    VkDevice m_device = VK_NULL_HANDLE;
    VkQueue m_queue = VK_NULL_HANDLE;
    std::mutex& m_queueMutex;
    VkFence m_drainFence = VK_NULL_HANDLE;
    std::mutex m_drainMutex;

    GpuWaitPolicy m_policy;
    std::atomic<IRenderTimingObserver*> m_observer{ nullptr };
    std::atomic<std::uint64_t> m_live{ 0 }; // (start microseconds << 8) | (site + 1); 0 = idle.
    std::atomic<std::uint64_t> m_loggedBits{ 0 };

    std::mutex m_quarantineMutex;
    std::array<VkFence, kQuarantineCapacity> m_quarantine{};
    std::size_t m_quarantineCount = 0;
};

} // namespace gte
