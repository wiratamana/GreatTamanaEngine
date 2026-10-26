#pragma once

// The only place that blocks the CPU on the GPU: named, result-checked, observed, logged once per failure.
// Bounded except the final device idle and the present call (Vulkan has no timeout for them).

#include "../GpuWaitPolicy.h"
#include "../RenderTimingObserver.h"

#include <volk.h>

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

    struct Presented {
        BlockOutcome outcome = BlockOutcome::Failed;
        VkResult result = VK_ERROR_UNKNOWN; // Raw present result; out-of-date and suboptimal count as Completed.
    };

    // `queueMutex` is the lock every submit to `graphicsQueue` takes. Throws if the drain fence cannot be created.
    GpuWaitService(VkDevice device, VkQueue graphicsQueue, std::mutex& queueMutex);
    ~GpuWaitService();

    GpuWaitService(const GpuWaitService&) = delete;
    GpuWaitService& operator=(const GpuWaitService&) = delete;
    GpuWaitService(GpuWaitService&&) = delete;
    GpuWaitService& operator=(GpuWaitService&&) = delete;

    // The observer must outlive its installation and be thread-safe: waits report from any thread.
    // Pass nullptr to remove it.
    void SetObserver(IRenderTimingObserver* observer) noexcept { m_observer.store(observer, std::memory_order_release); }

    // Tells the observer about periodic work that is not a wait. `name` is a string literal.
    void ReportMarker(const char* name) noexcept
    {
        if (IRenderTimingObserver* observer = m_observer.load(std::memory_order_acquire)) {
            observer->OnMarker(name);
        }
    }

    [[nodiscard]] BlockOutcome WaitFence(VkFence fence, std::chrono::nanoseconds timeout, WaitClass waitClass,
        BlockSite site, const char* detail) noexcept;

    // Drains the graphics queue (bounded), then the rest of the device (not bounded).
    [[nodiscard]] BlockOutcome WaitIdle(std::chrono::nanoseconds timeout, BlockSite site, const char* detail) noexcept;

    [[nodiscard]] Acquired AcquireImage(VkSwapchainKHR swapchain, VkSemaphore imageAvailable,
        std::chrono::nanoseconds timeout, BlockSite site, const char* detail) noexcept;

    // Not bounded. Takes the graphics-queue lock when `queue` is the graphics queue.
    [[nodiscard]] Presented Present(VkQueue queue, const VkPresentInfoKHR& info) noexcept;

    bool DeviceLost() const noexcept { return m_policy.Lost(); }
    void MarkDeviceLost() noexcept { m_policy.MarkLost(); }
    std::uint32_t ConsecutiveFrameTimeouts() const noexcept { return m_policy.ConsecutiveFrameTimeouts(); }

private:
    static_assert(kBlockSiteCount * 3 <= 64, "one log-once bit per site and failure kind");

    void BeginBlock(BlockSite site, const char* detail) noexcept;
    BlockOutcome EndBlock(WaitClass waitClass, BlockSite site, const char* detail, BlockOutcome raw) noexcept;
    BlockOutcome DrainGraphicsQueue(std::chrono::nanoseconds timeout) noexcept;
    void LogOnce(BlockSite site, BlockOutcome outcome, const char* detail) noexcept;

    VkDevice m_device = VK_NULL_HANDLE;
    VkQueue m_queue = VK_NULL_HANDLE;
    std::mutex& m_queueMutex;
    VkFence m_drainFence = VK_NULL_HANDLE;
    std::mutex m_drainMutex;

    GpuWaitPolicy m_policy;
    std::atomic<IRenderTimingObserver*> m_observer{ nullptr };
    std::atomic<std::uint64_t> m_loggedBits{ 0 };
};

} // namespace gte
