#include "RenderGraphTimestampPool.h"

#include <algorithm>

namespace gte::rg {

RenderGraphTimestampPool::RenderGraphTimestampPool(VkDevice device, const GpuTimestampCapability& capability,
    std::uint32_t synchronousRecordBudget, std::uint32_t pipelinedRecordBudget, std::uint32_t framesInFlight)
    : m_device(device)
    , m_capability(capability)
    , m_framesInFlight(framesInFlight)
{
#if !GTE_ENABLE_PROFILER
    // The only place that reads GTE_ENABLE_PROFILER: a profiler-off build creates no query pool.
    m_capability.supported = false;
#endif

    if (!m_capability.supported) {
        return;
    }

    m_synchronousQueryCount = GpuQueryCount(synchronousRecordBudget);
    m_pipelinedQueryCount = GpuQueryCount(pipelinedRecordBudget);
    m_synchronousPool.emplace(m_device, m_synchronousQueryCount);
    m_pipelinedPool.emplace(m_device, m_pipelinedQueryCount * m_framesInFlight);
    m_scratch.resize(2 * static_cast<std::size_t>(std::max(m_synchronousQueryCount, m_pipelinedQueryCount)));
}

std::uint32_t RenderGraphTimestampPool::QueryCount(bool pipelined) const noexcept
{
    return pipelined ? m_pipelinedQueryCount : m_synchronousQueryCount;
}

std::uint32_t RenderGraphTimestampPool::RegionBase(bool pipelined, std::uint32_t bufferIndex) const noexcept
{
    return pipelined ? bufferIndex * m_pipelinedQueryCount : 0;
}

void RenderGraphTimestampPool::ResetRegion(VkCommandBuffer cmd, bool pipelined, std::uint32_t bufferIndex) noexcept
{
    if (!IsSupported() || (pipelined && bufferIndex >= m_framesInFlight)) {
        return;
    }
    const VkQueryPool pool = pipelined ? m_pipelinedPool->Native() : m_synchronousPool->Native();
    vkCmdResetQueryPool(cmd, pool, RegionBase(pipelined, bufferIndex), QueryCount(pipelined));
}

void RenderGraphTimestampPool::WriteStamp(
    VkCommandBuffer cmd, bool pipelined, std::uint32_t bufferIndex, std::int32_t query) noexcept
{
    if (!IsSupported() || query < 0 || static_cast<std::uint32_t>(query) >= QueryCount(pipelined)
        || (pipelined && bufferIndex >= m_framesInFlight)) {
        return;
    }
    const VkQueryPool pool = pipelined ? m_pipelinedPool->Native() : m_synchronousPool->Native();
    vkCmdWriteTimestamp2(cmd, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, pool,
        RegionBase(pipelined, bufferIndex) + static_cast<std::uint32_t>(query));
}

GpuReadCall RenderGraphTimestampPool::Read(bool pipelined, std::uint32_t bufferIndex,
    std::span<std::uint64_t> values, std::span<std::uint8_t> available) noexcept
{
    std::fill(available.begin(), available.end(), std::uint8_t{ 0 });
    if (!IsSupported()) {
        return GpuReadCall::Ok;
    }
    if (pipelined && bufferIndex >= m_framesInFlight) {
        return GpuReadCall::Failed;
    }

    const std::uint32_t count = QueryCount(pipelined);
    const VkQueryPool pool = pipelined ? m_pipelinedPool->Native() : m_synchronousPool->Native();

    // No WAIT bit: the caller has already confirmed this submission is complete. A pair is value, availability.
    const VkResult result = vkGetQueryPoolResults(m_device, pool, RegionBase(pipelined, bufferIndex), count,
        static_cast<std::size_t>(count) * 2 * sizeof(std::uint64_t), m_scratch.data(), 2 * sizeof(std::uint64_t),
        VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WITH_AVAILABILITY_BIT);
    if (result != VK_SUCCESS && result != VK_NOT_READY) {
        return GpuReadCall::Failed;
    }

    const std::size_t usable = std::min<std::size_t>({ count, values.size(), available.size() });
    for (std::size_t i = 0; i < usable; ++i) {
        values[i] = m_scratch[2 * i];
        available[i] = m_scratch[2 * i + 1] != 0 ? 1 : 0;
    }
    return result == VK_SUCCESS ? GpuReadCall::Ok : GpuReadCall::NotReady;
}

} // namespace gte::rg
