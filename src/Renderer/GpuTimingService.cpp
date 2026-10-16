#include "GpuTimingService.h"

#include "Vulkan/VulkanDevice.h"

#include <limits>
#include <stdexcept>
#include <string>

namespace gte {

namespace {

// Whole-pool slot count: a start/end pair for each of the two offscreen slots.
constexpr std::uint32_t kTotalQuerySlotCount = 4;

} // namespace

GpuTimingService::GpuTimingService(VulkanDevice& vulkanDevice, const GpuTimestampCapability& capability)
    : m_device(vulkanDevice.Native())
    , m_capability(capability)
{
#if !GTE_ENABLE_PROFILER
    // A profiler-off build behaves as if the device had no timestamp support: no VkQueryPool is created.
    // This is the only place the class reads GTE_ENABLE_PROFILER; every other method is inert when unsupported.
    m_capability.supported = false;
#endif

    if (!m_capability.supported) {
        return;
    }

    m_queryPool.emplace(m_device, kTotalQuerySlotCount);

    // Every slot starts reset, so the per-call reset-then-write sequences need no first-use branch.
    WarmUpResetEntirePool(vulkanDevice);
}

void GpuTimingService::WarmUpResetEntirePool(VulkanDevice& vulkanDevice)
{
    VkCommandPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
    poolInfo.queueFamilyIndex = vulkanDevice.GraphicsQueueFamily();

    VkCommandPool commandPool = VK_NULL_HANDLE;
    if (vkCreateCommandPool(m_device, &poolInfo, nullptr, &commandPool) != VK_SUCCESS) {
        throw std::runtime_error("GpuTimingService: vkCreateCommandPool failed (warm-up reset)");
    }

    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool = commandPool;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = 1;

    VkCommandBuffer cmd = VK_NULL_HANDLE;
    if (vkAllocateCommandBuffers(m_device, &allocInfo, &cmd) != VK_SUCCESS) {
        vkDestroyCommandPool(m_device, commandPool, nullptr);
        throw std::runtime_error("GpuTimingService: vkAllocateCommandBuffers failed (warm-up reset)");
    }

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    if (vkBeginCommandBuffer(cmd, &beginInfo) != VK_SUCCESS) {
        vkDestroyCommandPool(m_device, commandPool, nullptr);
        throw std::runtime_error("GpuTimingService: vkBeginCommandBuffer failed (warm-up reset)");
    }

    // The reset must be recorded outside a dynamic rendering instance; this throwaway buffer never begins one.
    vkCmdResetQueryPool(cmd, m_queryPool->Native(), 0, m_queryPool->SlotCount());

    if (vkEndCommandBuffer(cmd) != VK_SUCCESS) {
        vkDestroyCommandPool(m_device, commandPool, nullptr);
        throw std::runtime_error("GpuTimingService: vkEndCommandBuffer failed (warm-up reset)");
    }

    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    VkFence fence = VK_NULL_HANDLE;
    if (vkCreateFence(m_device, &fenceInfo, nullptr, &fence) != VK_SUCCESS) {
        vkDestroyCommandPool(m_device, commandPool, nullptr);
        throw std::runtime_error("GpuTimingService: vkCreateFence failed (warm-up reset)");
    }

    VkCommandBufferSubmitInfo cmdSubmitInfo{};
    cmdSubmitInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO;
    cmdSubmitInfo.commandBuffer = cmd;

    VkSubmitInfo2 submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2;
    submitInfo.commandBufferInfoCount = 1;
    submitInfo.pCommandBufferInfos = &cmdSubmitInfo;

    if (vulkanDevice.SubmitToGraphicsQueue(submitInfo, fence) != VK_SUCCESS) {
        vkDestroyFence(m_device, fence, nullptr);
        vkDestroyCommandPool(m_device, commandPool, nullptr);
        throw std::runtime_error("GpuTimingService: vkQueueSubmit2 failed (warm-up reset)");
    }

    vkWaitForFences(m_device, 1, &fence, VK_TRUE, std::numeric_limits<std::uint64_t>::max());

    vkDestroyFence(m_device, fence, nullptr);
    vkDestroyCommandPool(m_device, commandPool, nullptr); // also frees `cmd`.
}

std::uint32_t GpuTimingService::OffscreenSlotBase(GpuTimingSlot slot) noexcept
{
    // Offscreen0 -> base 0, Offscreen1 -> base 2; never called with SwapchainPresent.
    return static_cast<std::uint32_t>(slot) * 2;
}

GpuTimingSample GpuTimingService::ReadResultAt(std::uint32_t slotBase) noexcept
{
    std::uint64_t ticks[2] = { 0, 0 };
    // No WAIT bit: callers only read after their own fence wait, so this is not a hidden second wait.
    vkGetQueryPoolResults(m_device, m_queryPool->Native(), slotBase, 2, sizeof(ticks), ticks, sizeof(std::uint64_t),
        VK_QUERY_RESULT_64_BIT);

    const double milliseconds =
        ConvertTimestampDeltaToMilliseconds(ticks[0], ticks[1], m_capability.timestampPeriodNs, m_capability.validBits);
    return GpuTimingSample{ GpuTimingSample::Status::Present, milliseconds };
}

void GpuTimingService::RecordOffscreenPassStart(VkCommandBuffer cmd, GpuTimingSlot slot) noexcept
{
    if (!IsSupported() || !m_captureEnabled) {
        return;
    }
    const std::uint32_t base = OffscreenSlotBase(slot);
    vkCmdResetQueryPool(cmd, m_queryPool->Native(), base, 2);
    vkCmdWriteTimestamp2(cmd, VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT, m_queryPool->Native(), base);
}

void GpuTimingService::RecordOffscreenPassEnd(VkCommandBuffer cmd, GpuTimingSlot slot) noexcept
{
    if (!IsSupported() || !m_captureEnabled) {
        return;
    }
    const std::uint32_t base = OffscreenSlotBase(slot);
    vkCmdWriteTimestamp2(cmd, VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT, m_queryPool->Native(), base + 1);
}

GpuTimingSample GpuTimingService::ReadOffscreenResultNow(GpuTimingSlot slot) noexcept
{
    // The offscreen path is fully synchronous, so there is no warm-up: hasWrittenData is always true here.
    const GpuTimingSample::Status status = ResolveGpuTimingStatus(IsSupported(), m_captureEnabled, /*hasWrittenData=*/true);
    GpuTimingSample sample{ status, 0.0 };
    if (status == GpuTimingSample::Status::Present) {
        sample = ReadResultAt(OffscreenSlotBase(slot));
    }
    m_lastKnown[static_cast<std::size_t>(slot)] = sample;
    return sample;
}

GpuTimingSample GpuTimingService::LastKnown(GpuTimingSlot slot) const noexcept
{
    return m_lastKnown[static_cast<std::size_t>(slot)];
}

} // namespace gte
