#include "RenderGraphTimestampPool.h"

#include <limits>
#include <stdexcept>

namespace gte::rg {

RenderGraphTimestampPool::RenderGraphTimestampPool(VkDevice device, VkQueue graphicsQueue,
    std::uint32_t graphicsQueueFamily, const GpuTimestampCapability& capability, std::uint32_t synchronousSlotBudget,
    std::uint32_t pipelinedSlotBudget, std::uint32_t pipelinedFramesInFlight)
    : m_device(device)
    , m_capability(capability)
    , m_pipelinedSlotBudget(pipelinedSlotBudget)
{
#if !GTE_ENABLE_PROFILER
    // Mirrors GpuTimingService's own constructor guard exactly (see
    // GpuTimingService.cpp) - a GTE_ENABLE_PROFILER=OFF build never
    // creates a VkQueryPool here either, regardless of what the device
    // itself reports. This is the ONLY place this class reads
    // GTE_ENABLE_PROFILER - every other method is unconditional and
    // simply inert whenever IsSupported() ends up false, whatever the
    // reason.
    m_capability.supported = false;
#endif

    if (!m_capability.supported) {
        return;
    }

    // Two INDEPENDENT pools, one per regime - see this class's own header
    // comment / RenderGraph.h's top-of-file comment for why the
    // synchronous-offscreen and pipelined-present regimes must never share
    // one slot range. Sized generously (kSynchronousTimingSlotBudget/
    // kPipelinedTimingSlotBudget, RenderGraph.h) up front and never
    // resized/recreated for this object's entire lifetime.
    m_synchronousPool.emplace(m_device, synchronousSlotBudget * 2);
    m_pipelinedPool.emplace(m_device, pipelinedSlotBudget * pipelinedFramesInFlight * 2);

    // Full, one-time, up-front reset of every slot in BOTH pools -
    // mirrors GpuTimingService::WarmUpResetEntirePool()'s own reasoning
    // exactly (validation-layer-friendly first use, even though the
    // Vulkan spec itself treats a freshly created query as already
    // "unavailable"/reset-equivalent).
    WarmUpResetPool(*m_synchronousPool, graphicsQueue, graphicsQueueFamily);
    WarmUpResetPool(*m_pipelinedPool, graphicsQueue, graphicsQueueFamily);
}

void RenderGraphTimestampPool::WarmUpResetPool(
    VulkanQueryPool& pool, VkQueue graphicsQueue, std::uint32_t graphicsQueueFamily)
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

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &cmd;

    if (vkQueueSubmit(graphicsQueue, 1, &submitInfo, fence) != VK_SUCCESS) {
        vkDestroyFence(m_device, fence, nullptr);
        vkDestroyCommandPool(m_device, commandPool, nullptr);
        throw std::runtime_error("RenderGraphTimestampPool: vkQueueSubmit failed (warm-up reset)");
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
    // VK_QUERY_RESULT_WAIT_BIT is not required here - the caller has
    // already confirmed (via its own, pre-existing synchronization) that
    // this exact submission is complete before calling this - mirrors
    // GpuTimingService::ReadResultAt()'s own identical omission/reasoning.
    vkGetQueryPoolResults(
        m_device, pool, base, 2, sizeof(ticks), ticks, sizeof(std::uint64_t), VK_QUERY_RESULT_64_BIT);
    return RawTicks{ ticks[0], ticks[1] };
}

} // namespace gte::rg
