#pragma once

// GPU timestamp source for the render graph: one query pool per ExecuteTimingMode regime, indexed by
// the slot a RenderGraphNameSlotTable assigns to a pass name. Gated by GTE_ENABLE_PROFILER and
// SetCaptureEnabled(). Needs a real device.

#include "RenderGraphNameSlotTable.h"
#include "../GpuTiming.h"
#include "../Vulkan/VulkanQueryPool.h"

#include <volk.h>

#include <cstdint>
#include <optional>

namespace gte {
class Renderer;
} // namespace gte

namespace gte::rg {

class RenderGraphTimestampPool {
public:
    // Raw [begin, end) timestamp counters of one slot. Converting to milliseconds and deciding the
    // sample status is the caller's job.
    struct RawTicks {
        std::uint64_t begin = 0;
        std::uint64_t end = 0;
    };

    // `capability` is the device's timestamp support (profiling compiled out forces it off). `renderer` runs
    // a one-time warm-up reset. Frames in flight must equal kGpuTimingFramesInFlight.
    RenderGraphTimestampPool(VkDevice device, Renderer& renderer, const GpuTimestampCapability& capability,
        std::uint32_t synchronousSlotBudget, std::uint32_t pipelinedSlotBudget,
        std::uint32_t pipelinedFramesInFlight);
    ~RenderGraphTimestampPool() = default;

    RenderGraphTimestampPool(const RenderGraphTimestampPool&) = delete;
    RenderGraphTimestampPool& operator=(const RenderGraphTimestampPool&) = delete;
    RenderGraphTimestampPool(RenderGraphTimestampPool&&) = delete;
    RenderGraphTimestampPool& operator=(RenderGraphTimestampPool&&) = delete;

    // Whether this pool can ever produce timings: false when the device lacks support or profiling is
    // compiled out. Independent of IsCaptureEnabled().
    bool IsSupported() const noexcept { return m_capability.supported; }
    const GpuTimestampCapability& Capability() const noexcept { return m_capability; }

    // Runtime on/off switch, driven once per frame by RenderGraph.
    void SetCaptureEnabled(bool enabled) noexcept { m_captureEnabled = enabled; }
    bool IsCaptureEnabled() const noexcept { return m_captureEnabled; }

    // Resets the slot's two queries and writes a TOP_OF_PIPE timestamp into the first. No-op when
    // unsupported, capture is off, or slot == kNoNameSlot. Resetting here is safe because the caller
    // has already waited for the slot's previous use to finish.
    void WriteBegin(VkCommandBuffer cmd, bool pipelined, std::uint32_t bufferIndex, std::int32_t slot) noexcept;

    // Writes a BOTTOM_OF_PIPE timestamp into the slot's second query; same guard as WriteBegin().
    void WriteEnd(VkCommandBuffer cmd, bool pipelined, std::uint32_t bufferIndex, std::int32_t slot) noexcept;

    // Reads the slot's most recent [begin, end) ticks. The caller must know the submission is complete.
    // Returns zeros for kNoNameSlot or when unsupported.
    RawTicks ReadBack(bool pipelined, std::uint32_t bufferIndex, std::int32_t slot) noexcept;

private:
    std::uint32_t QueryBase(bool pipelined, std::uint32_t bufferIndex, std::int32_t slot) const noexcept;
    void WarmUpResetPool(VulkanQueryPool& pool, Renderer& renderer);

    VkDevice m_device = VK_NULL_HANDLE;
    GpuTimestampCapability m_capability;
    bool m_captureEnabled = true;

    std::uint32_t m_pipelinedSlotBudget = 0;

    // Empty when !m_capability.supported.
    std::optional<VulkanQueryPool> m_synchronousPool;
    std::optional<VulkanQueryPool> m_pipelinedPool;
};

} // namespace gte::rg
