#include "RenderGraphTimestampPool.h"

#include "../Renderer.h"
#include "RecordingAccess.h"

namespace gte::rg {

RenderGraphTimestampPool::RenderGraphTimestampPool(VkDevice device, Renderer& renderer,
    const GpuTimestampCapability& capability, std::uint32_t synchronousSlotBudget,
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
    WarmUpResetPool(*m_synchronousPool, renderer);
    WarmUpResetPool(*m_pipelinedPool, renderer);
}

void RenderGraphTimestampPool::WarmUpResetPool(VulkanQueryPool& pool, Renderer& renderer)
{
    // vkCmdResetQueryPool must be recorded outside a rendering instance; a one-shot never begins one.
    renderer.ImmediateSubmit(
        [&pool](CommandBuffer& cmd) {
            vkCmdResetQueryPool(RecordingAccess::Native(cmd), pool.Native(), 0, pool.SlotCount());
        },
        BlockSite::WarmUp, "timestamp warm-up");
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
