#include "GpuWaitService.h"

#include "../../Core/Logging.h"

#include <algorithm>
#include <exception>
#include <stdexcept>
#include <string>

namespace gte {

namespace {

BlockOutcome FromVkResult(VkResult result) noexcept
{
    switch (result) {
    case VK_SUCCESS:
        return BlockOutcome::Completed;
    case VK_TIMEOUT:
    case VK_NOT_READY:
        return BlockOutcome::TimedOut;
    case VK_ERROR_DEVICE_LOST:
        return BlockOutcome::DeviceLost;
    default:
        return BlockOutcome::Failed;
    }
}

std::uint64_t ToTicks(std::chrono::nanoseconds timeout) noexcept
{
    return static_cast<std::uint64_t>(std::max<std::int64_t>(0, timeout.count()));
}

} // namespace

GpuWaitService::GpuWaitService(VkDevice device, VkQueue graphicsQueue, std::mutex& queueMutex)
    : m_device(device)
    , m_queue(graphicsQueue)
    , m_queueMutex(queueMutex)
{
    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    if (vkCreateFence(m_device, &fenceInfo, nullptr, &m_drainFence) != VK_SUCCESS) {
        throw std::runtime_error("GpuWaitService: vkCreateFence failed (drain fence)");
    }
}

GpuWaitService::~GpuWaitService()
{
    if (WaitIdle(kIdleWaitTimeout, BlockSite::Shutdown, "GpuWaitService") == BlockOutcome::Completed) {
        vkDestroyFence(m_device, m_drainFence, nullptr);
    }
    // Otherwise: device lost, the pending drain fence is abandoned.
}

BlockOutcome GpuWaitService::WaitFence(VkFence fence, std::chrono::nanoseconds timeout, WaitClass waitClass,
    BlockSite site, const char* detail) noexcept
{
    if (m_policy.Lost()) {
        return BlockOutcome::DeviceLost;
    }
    BeginBlock(site, detail);
    const VkResult result = vkWaitForFences(m_device, 1, &fence, VK_TRUE, ToTicks(timeout));
    return EndBlock(waitClass, site, detail, FromVkResult(result));
}

BlockOutcome GpuWaitService::WaitIdle(std::chrono::nanoseconds timeout, BlockSite site, const char* detail) noexcept
{
    if (m_policy.Lost()) {
        return BlockOutcome::DeviceLost;
    }
    BeginBlock(site, detail);

    BlockOutcome raw = DrainGraphicsQueue(timeout);
    if (raw == BlockOutcome::Completed) {
        // The graphics queue is empty, so this waits for the other queues only. Queues are externally synchronized.
        const std::lock_guard queueLock(m_queueMutex);
        raw = FromVkResult(vkDeviceWaitIdle(m_device));
    }
    return EndBlock(WaitClass::Upload, site, detail, raw);
}

GpuWaitService::Acquired GpuWaitService::AcquireImage(VkSwapchainKHR swapchain, VkSemaphore imageAvailable,
    std::chrono::nanoseconds timeout, BlockSite site, const char* detail) noexcept
{
    Acquired acquired;
    if (m_policy.Lost()) {
        acquired.outcome = BlockOutcome::DeviceLost;
        acquired.result = VK_ERROR_DEVICE_LOST;
        return acquired;
    }

    BeginBlock(site, detail);
    acquired.result = vkAcquireNextImageKHR(
        m_device, swapchain, ToTicks(timeout), imageAvailable, VK_NULL_HANDLE, &acquired.imageIndex);

    // Out-of-date and suboptimal are answers, not wait failures; the caller reads `result`.
    const bool answered = acquired.result == VK_ERROR_OUT_OF_DATE_KHR || acquired.result == VK_SUBOPTIMAL_KHR;
    const BlockOutcome raw = answered ? BlockOutcome::Completed : FromVkResult(acquired.result);
    acquired.outcome = EndBlock(WaitClass::Frame, site, detail, raw);
    return acquired;
}

GpuWaitService::Presented GpuWaitService::Present(VkQueue queue, const VkPresentInfoKHR& info) noexcept
{
    Presented presented;
    if (m_policy.Lost()) {
        presented.outcome = BlockOutcome::DeviceLost;
        presented.result = VK_ERROR_DEVICE_LOST;
        return presented;
    }

    constexpr const char* kDetail = "present";
    BeginBlock(BlockSite::PresentQueue, kDetail);
    if (queue == m_queue) {
        const std::lock_guard queueLock(m_queueMutex);
        presented.result = vkQueuePresentKHR(queue, &info);
    } else {
        presented.result = vkQueuePresentKHR(queue, &info);
    }

    const bool answered = presented.result == VK_ERROR_OUT_OF_DATE_KHR || presented.result == VK_SUBOPTIMAL_KHR;
    const BlockOutcome raw = answered ? BlockOutcome::Completed : FromVkResult(presented.result);
    presented.outcome = EndBlock(WaitClass::Frame, BlockSite::PresentQueue, kDetail, raw);
    return presented;
}

void GpuWaitService::BeginBlock(BlockSite site, const char* detail) noexcept
{
    if (IRenderTimingObserver* observer = m_observer.load(std::memory_order_acquire)) {
        observer->OnBlockBegin(site, detail);
    }
}

BlockOutcome GpuWaitService::EndBlock(WaitClass waitClass, BlockSite site, const char* detail, BlockOutcome raw) noexcept
{
    const BlockOutcome outcome = m_policy.Judge(waitClass, raw);
    if (IRenderTimingObserver* observer = m_observer.load(std::memory_order_acquire)) {
        observer->OnBlockEnd(site, outcome);
    }
    if (outcome != BlockOutcome::Completed) {
        LogOnce(site, outcome, detail);
    }
    return outcome;
}

BlockOutcome GpuWaitService::DrainGraphicsQueue(std::chrono::nanoseconds timeout) noexcept
{
    const std::lock_guard drainLock(m_drainMutex);
    if (m_policy.Lost()) {
        return BlockOutcome::DeviceLost; // A timed-out drain leaves the fence pending; never reset it.
    }

    if (const VkResult reset = vkResetFences(m_device, 1, &m_drainFence); reset != VK_SUCCESS) {
        return FromVkResult(reset) == BlockOutcome::DeviceLost ? BlockOutcome::DeviceLost : BlockOutcome::Failed;
    }

    // An empty batch signals once everything submitted before it on this queue has finished.
    VkSubmitInfo2 empty{};
    empty.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2;
    VkResult submitted = VK_SUCCESS;
    {
        const std::lock_guard queueLock(m_queueMutex);
        submitted = vkQueueSubmit2(m_queue, 1, &empty, m_drainFence);
    }
    if (submitted != VK_SUCCESS) {
        return FromVkResult(submitted) == BlockOutcome::DeviceLost ? BlockOutcome::DeviceLost : BlockOutcome::Failed;
    }

    return FromVkResult(vkWaitForFences(m_device, 1, &m_drainFence, VK_TRUE, ToTicks(timeout)));
}

void GpuWaitService::LogOnce(BlockSite site, BlockOutcome outcome, const char* detail) noexcept
{
    const std::uint64_t bit =
        1ull << (static_cast<std::uint32_t>(site) * 3u + (static_cast<std::uint32_t>(outcome) - 1u) % 3u);
    if ((m_loggedBits.fetch_or(bit, std::memory_order_relaxed) & bit) != 0) {
        return;
    }
    try {
        GTE_LOG_ERROR("GpuWait",
            std::string("GpuWait(") + ToString(site) + (detail != nullptr && detail[0] != '\0' ? std::string(":") + detail : std::string())
                + ") " + ToString(outcome));
    } catch (...) {
        // Logging must never throw out of a wait.
    }
}

} // namespace gte
