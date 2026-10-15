#include "RenderGraphTimestampPool.h"

#include "../Renderer.h"

#include <limits>
#include <stdexcept>

namespace gte::rg {

RenderGraphTimestampPool::RenderGraphTimestampPool(VkDevice device, Renderer& renderer,
    std::uint32_t graphicsQueueFamily, const GpuTimestampCapability& capability, std::uint32_t synchronousSlotBudget,
    std::uint32_t pipelinedSlotBudget, std::uint32_t pipelinedFramesInFlight)
    : m_device(device)
    , m_capability(capability)
    , m_pipelinedSlotBudget(pipelinedSlotBudget)
{
#if !GTE_ENABLE_PROFILER
    // The only place that reads GTE_ENABLE_PROFILER: a profiler-off build creates no query pool.
    // Every other method is inert whenever IsSupported() is false.
    m_capability.supported = false;
#endif

    if (!m_capability.supported) {
        return;
    }

    // Two independent fixed-size pools, one per regime; never resized.
    m_synchronousPool.emplace(m_device, synchronousSlotBudget * 2);
    m_pipelinedPool.emplace(m_device, pipelinedSlotBudget * pipelinedFramesInFlight * 2);

    // One-time reset of every slot in both pools, so first use is clean for the validation layer.
    WarmUpResetPool(*m_synchronousPool, renderer, graphicsQueueFamily);
    WarmUpResetPool(*m_pipelinedPool, renderer, graphicsQueueFamily);
}

void RenderGraphTimestampPool::WarmUpResetPool(
    VulkanQueryPool& pool, Renderer& renderer, std::uint32_t graphicsQueueFamily)
{
    VkCommandPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
    poolInfo.queueFamilyIndex = graphicsQueueFamily;

    VkCommandPool commandPool = VK_NULL_HANDLE;
    if (vkCreateCommandPool(m_device, &poolInfo, nullptr, &commandPool) != VK_SUCCESS) {
        throw std::runtime_error("RenderGraphTimestampPool: vkCreateCommandPool failed (warm-up reset)");
    }

    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool = commandPool;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = 1;

    VkCommandBuffer cmd = VK_NULL_HANDLE;
    if (vkAllocateCommandBuffers(m_device, &allocInfo, &cmd) != VK_SUCCESS) {
        vkDestroyCommandPool(m_device, commandPool, nullptr);
        throw std::runtime_error("RenderGraphTimestampPool: vkAllocateCommandBuffers failed (warm-up reset)");
    }

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    if (vkBeginCommandBuffer(cmd, &beginInfo) != VK_SUCCESS) {
        vkDestroyCommandPool(m_device, commandPool, nullptr);
        throw std::runtime_error("RenderGraphTimestampPool: vkBeginCommandBuffer failed (warm-up reset)");
    }

    // vkCmdResetQueryPool must be recorded OUTSIDE a dynamic rendering
    // instance - trivially true here, this throwaway command buffer never
    // begins one at all.
    vkCmdResetQueryPool(cmd, pool.Native(), 0, pool.SlotCount());

    if (vkEndCommandBuffer(cmd) != VK_SUCCESS) {
        vkDestroyCommandPool(m_device, commandPool, nullptr);
        throw std::runtime_error("RenderGraphTimestampPool: vkEndCommandBuffer failed (warm-up reset)");
    }

    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    VkFence fence = VK_NULL_HANDLE;
    if (vkCreateFence(m_device, &fenceInfo, nullptr, &fence) != VK_SUCCESS) {
        vkDestroyCommandPool(m_device, commandPool, nullptr);
        throw std::runtime_error("RenderGraphTimestampPool: vkCreateFence failed (warm-up reset)");
    }

    VkCommandBufferSubmitInfo cmdSubmitInfo{};
    cmdSubmitInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO;
    cmdSubmitInfo.commandBuffer = cmd;

    VkSubmitInfo2 submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2;
    submitInfo.commandBufferInfoCount = 1;
    submitInfo.pCommandBufferInfos = &cmdSubmitInfo;

    if (renderer.SubmitToGraphicsQueue(submitInfo, fence) != VK_SUCCESS) {
        vkDestroyFence(m_device, fence, nullptr);
        vkDestroyCommandPool(m_device, commandPool, nullptr);
        throw std::runtime_error("RenderGraphTimestampPool: vkQueueSubmit2 failed (warm-up reset)");
    }

    vkWaitForFences(m_device, 1, &fence, VK_TRUE, std::numeric_limits<std::uint64_t>::max());

    vkDestroyFence(m_device, fence, nullptr);
    vkDestroyCommandPool(m_device, commandPool, nullptr); // also frees `cmd`.
}

std::uint32_t RenderGraphTimestampPool::QueryBase(bool pipelined, std::uint32_t bufferIndex, std::int32_t slot) const noexcept
{
    if (!pipelined) {
        // Single-buffered - see this class's own header comment for why
        // no frame-in-flight multiplying is needed for the synchronous
        // regime (`bufferIndex` is always 0 there, ignored here).
        return static_cast<std::uint32_t>(slot) * 2;
    }
    return (bufferIndex * m_pipelinedSlotBudget + static_cast<std::uint32_t>(slot)) * 2;
}

void RenderGraphTimestampPool::WriteBegin(
    VkCommandBuffer cmd, bool pipelined, std::uint32_t bufferIndex, std::int32_t slot) noexcept
{
    if (!IsSupported() || !m_captureEnabled || slot == kNoNameSlot) {
        return;
    }
    const VkQueryPool pool = pipelined ? m_pipelinedPool->Native() : m_synchronousPool->Native();
    const std::uint32_t base = QueryBase(pipelined, bufferIndex, slot);
    vkCmdResetQueryPool(cmd, pool, base, 2);
    vkCmdWriteTimestamp2(cmd, VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT, pool, base);
}

void RenderGraphTimestampPool::WriteEnd(
    VkCommandBuffer cmd, bool pipelined, std::uint32_t bufferIndex, std::int32_t slot) noexcept
{
    if (!IsSupported() || !m_captureEnabled || slot == kNoNameSlot) {
        return;
    }
    const VkQueryPool pool = pipelined ? m_pipelinedPool->Native() : m_synchronousPool->Native();
    const std::uint32_t base = QueryBase(pipelined, bufferIndex, slot);
    vkCmdWriteTimestamp2(cmd, VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT, pool, base + 1);
}

RenderGraphTimestampPool::RawTicks RenderGraphTimestampPool::ReadBack(
    bool pipelined, std::uint32_t bufferIndex, std::int32_t slot) noexcept
{
    if (!IsSupported() || slot == kNoNameSlot) {
        return RawTicks{};
    }
    const VkQueryPool pool = pipelined ? m_pipelinedPool->Native() : m_synchronousPool->Native();
    const std::uint32_t base = QueryBase(pipelined, bufferIndex, slot);

    std::uint64_t ticks[2] = { 0, 0 };
    // No WAIT bit: the caller has already confirmed this submission is complete.
    vkGetQueryPoolResults(
        m_device, pool, base, 2, sizeof(ticks), ticks, sizeof(std::uint64_t), VK_QUERY_RESULT_64_BIT);
    return RawTicks{ ticks[0], ticks[1] };
}

} // namespace gte::rg
