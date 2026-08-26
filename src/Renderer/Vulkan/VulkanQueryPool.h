#pragma once

#include <volk.h>

#include <cstdint>

namespace gte {

// Phase 4B (PHASE4_GPU_TIMESTAMP_QUERIES_STRATEGY_v2.md) - RAII wrapper
// around one VkQueryPool of type VK_QUERY_TYPE_TIMESTAMP, a fixed slot count
// for its entire lifetime (never resized/recreated - see that document's own
// design decision log).
//
// Deliberately a thin, "dumb" RAII shell with ZERO knowledge of what a given
// slot INDEX actually means semantically (which logical GpuTimingSlot/
// frame-in-flight index it belongs to) - that mapping lives one layer up, in
// GpuTimingService (see GpuTimingService.h), the same division of labor
// VulkanSwapchain already has relative to FramePresenter (FramePresenter
// uses VulkanSwapchain::Native() directly rather than every swapchain
// operation being individually wrapped). No Reset()/WriteTimestamp()/
// GetResults() convenience methods on purpose - GpuTimingService calls
// vkCmdResetQueryPool/vkCmdWriteTimestamp2/vkGetQueryPoolResults directly
// against Native() below, since those calls need GpuTimingService's own
// semantic slot-to-purpose mapping anyway; wrapping them here would just be
// an unnecessary extra indirection with no real encapsulation benefit.
//
// Created in the constructor, destroyed in the destructor - same convention
// as every other class under Vulkan/ (VulkanFrameSync, VulkanSwapchain,
// VulkanDevice, ...). Does NOT own the VkDevice passed in; it must outlive
// this object.
class VulkanQueryPool {
public:
    VulkanQueryPool(VkDevice device, std::uint32_t slotCount);
    ~VulkanQueryPool();

    VulkanQueryPool(const VulkanQueryPool&) = delete;
    VulkanQueryPool& operator=(const VulkanQueryPool&) = delete;

    VulkanQueryPool(VulkanQueryPool&& other) noexcept;
    VulkanQueryPool& operator=(VulkanQueryPool&& other) noexcept;

    VkQueryPool Native() const noexcept { return m_pool; }
    std::uint32_t SlotCount() const noexcept { return m_slotCount; }

private:
    void Destroy() noexcept;

    VkDevice m_device = VK_NULL_HANDLE;
    VkQueryPool m_pool = VK_NULL_HANDLE;
    std::uint32_t m_slotCount = 0;
};

} // namespace gte
