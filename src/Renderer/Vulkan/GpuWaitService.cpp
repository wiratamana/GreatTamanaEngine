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

std::uint64_t NowMicros() noexcept
{
    const auto now = std::chrono::steady_clock::now().time_since_epoch();
    return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(now).count());
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
    // On a lost device the drain fence may still be pending and quarantined fences are left alone.
    static_cast<void>(WaitIdle(kIdleWaitTimeout, BlockSite::Shutdown, "GpuWaitService"));
    if (m_drainFence != VK_NULL_HANDLE) {
        vkDestroyFence(m_device, m_drainFence, nullptr);
    }
}

BlockOutcome GpuWaitService::WaitFence(VkFence fence, std::chrono::nanoseconds timeout, WaitClass waitClass,
    BlockSite site, const char* detail) noexcept
{
    if (m_policy.Lost()) {
        return BlockOutcome::DeviceLost;
    }
    const std::uint64_t token = BeginBlock(site, detail);
    const VkResult result = vkWaitForFences(m_device, 1, &fence, VK_TRUE, ToTicks(timeout));
    return EndBlock(token, waitClass, site, detail, FromVkResult(result));
}

BlockOutcome GpuWaitService::WaitIdle(std::chrono::nanoseconds timeout, BlockSite site, const char* detail) noexcept
{
    if (m_policy.Lost()) {
        return BlockOutcome::DeviceLost;
    }
    const std::uint64_t token = BeginBlock(site, detail);

    BlockOutcome raw = DrainGraphicsQueue(timeout);
    if (raw == BlockOutcome::Completed) {
        // The graphics queue is empty, so this waits for the other queues only.
        raw = FromVkResult(vkDeviceWaitIdle(m_device));
    }

    const BlockOutcome outcome = EndBlock(token, WaitClass::Upload, site, detail, raw);
    if (outcome == BlockOutcome::Completed) {
        ReleaseQuarantine();
    }
    return outcome;
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

    const std::uint64_t token = BeginBlock(site, detail);
    acquired.result = vkAcquireNextImageKHR(
        m_device, swapchain, ToTicks(timeout), imageAvailable, VK_NULL_HANDLE, &acquired.imageIndex);

    // Out-of-date and suboptimal are answers, not wait failures; the caller reads `result`.
    const bool answered = acquired.result == VK_ERROR_OUT_OF_DATE_KHR || acquired.result == VK_SUBOPTIMAL_KHR;
    const BlockOutcome raw = answered ? BlockOutcome::Completed : FromVkResult(acquired.result);
    acquired.outcome = EndBlock(token, WaitClass::Frame, site, detail, raw);
    return acquired;
}

void GpuWaitService::Quarantine(VkFence fence) noexcept
{
    if (fence == VK_NULL_HANDLE) {
        return;
    }
    {
        const std::lock_guard lock(m_quarantineMutex);
        if (m_quarantineCount < m_quarantine.size()) {
            m_quarantine[m_quarantineCount++] = fence;
            return;
        }
    }
    // Full: the device is lost by now, so the handle is abandoned rather than destroyed under the GPU.
    LogOnce(BlockSite::ImmediateSubmit, BlockOutcome::Failed, "quarantine full");
}

GpuWaitService::LiveBlock GpuWaitService::CurrentBlock() const noexcept
{
    const std::uint64_t live = m_live.load(std::memory_order_acquire);
    if (live == 0) {
        return {};
    }
    return LiveBlock{ true, static_cast<BlockSite>((live & 0xFFu) - 1), (live >> 8) * 1000u };
}

std::uint64_t GpuWaitService::BeginBlock(BlockSite site, const char* detail) noexcept
{
    const std::uint64_t token = (NowMicros() << 8) | (static_cast<std::uint64_t>(site) + 1);
    m_live.store(token, std::memory_order_release);
    if (IRenderTimingObserver* observer = m_observer.load(std::memory_order_acquire)) {
        observer->OnBlockBegin(site, detail);
    }
    return token;
}

BlockOutcome GpuWaitService::EndBlock(
    std::uint64_t token, WaitClass waitClass, BlockSite site, const char* detail, BlockOutcome raw) noexcept
{
    const BlockOutcome outcome = m_policy.Judge(waitClass, raw);

    std::uint64_t expected = token;
    m_live.compare_exchange_strong(expected, 0, std::memory_order_acq_rel);
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
            std::string("GpuWait(") + ToString(site) + ") " + ToString(outcome)
                + (detail != nullptr && detail[0] != '\0' ? std::string(": ") + detail : std::string()));
    } catch (...) {
        // Logging must never throw out of a wait.
    }
}

void GpuWaitService::ReleaseQuarantine() noexcept
{
    const std::lock_guard lock(m_quarantineMutex);
    for (std::size_t i = 0; i < m_quarantineCount; ++i) {
        vkDestroyFence(m_device, m_quarantine[i], nullptr);
        m_quarantine[i] = VK_NULL_HANDLE;
    }
    m_quarantineCount = 0;
}

} // namespace gte
