#pragma once

// GPU timestamp source: one query pool per regime, one region per frame in flight, reset and read once per frame.
// Gated by GTE_ENABLE_PROFILER and SetCaptureEnabled(). Needs a real device.

#include "../GpuStampPlan.h"
#include "../GpuTiming.h"
#include "../Vulkan/VulkanQueryPool.h"

#include <volk.h>

#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace gte::rg {

class RenderGraphTimestampPool {
public:
    // `capability` is the device's timestamp support (profiling compiled out forces it off). The record budgets
    // size one frame's region of each regime.
    RenderGraphTimestampPool(VkDevice device, const GpuTimestampCapability& capability,
        std::uint32_t synchronousRecordBudget, std::uint32_t pipelinedRecordBudget, std::uint32_t framesInFlight);
    ~RenderGraphTimestampPool() = default;

    RenderGraphTimestampPool(const RenderGraphTimestampPool&) = delete;
    RenderGraphTimestampPool& operator=(const RenderGraphTimestampPool&) = delete;
    RenderGraphTimestampPool(RenderGraphTimestampPool&&) = delete;
    RenderGraphTimestampPool& operator=(RenderGraphTimestampPool&&) = delete;

    // False when the device lacks support or profiling is compiled out. Independent of IsCaptureEnabled().
    bool IsSupported() const noexcept { return m_capability.supported; }
    const GpuTimestampCapability& Capability() const noexcept { return m_capability; }

    void SetCaptureEnabled(bool enabled) noexcept { m_captureEnabled = enabled; }
    bool IsCaptureEnabled() const noexcept { return m_captureEnabled; }

    // Queries in one frame's region of a regime; 0 when unsupported.
    std::uint32_t QueryCount(bool pipelined) const noexcept;

    // Resets one frame's whole region. Record it before any stamp of that frame, outside a rendering instance.
    void ResetRegion(VkCommandBuffer cmd, bool pipelined, std::uint32_t bufferIndex) noexcept;

    // Writes one stamp after all earlier commands. No-op for a negative query or an unsupported pool.
    void WriteStamp(VkCommandBuffer cmd, bool pipelined, std::uint32_t bufferIndex, std::int32_t query) noexcept;

    // Reads the region with availability in ONE call. `values` and `available` get one entry per query.
    // The caller must know the submission is complete.
    GpuReadCall Read(bool pipelined, std::uint32_t bufferIndex, std::span<std::uint64_t> values,
        std::span<std::uint8_t> available) noexcept;

private:
    std::uint32_t RegionBase(bool pipelined, std::uint32_t bufferIndex) const noexcept;

    VkDevice m_device = VK_NULL_HANDLE;
    GpuTimestampCapability m_capability;
    bool m_captureEnabled = true;

    std::uint32_t m_synchronousQueryCount = 0;
    std::uint32_t m_pipelinedQueryCount = 0;
    std::uint32_t m_framesInFlight = 0;

    // Empty when !m_capability.supported.
    std::optional<VulkanQueryPool> m_synchronousPool;
    std::optional<VulkanQueryPool> m_pipelinedPool;

    // Value and availability pairs of one read; sized for the largest region.
    std::vector<std::uint64_t> m_scratch;
};

} // namespace gte::rg
