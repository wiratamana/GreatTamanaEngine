#pragma once

#include <volk.h>

#include <cstdint>

namespace gte {

// RAII wrapper around one VK_QUERY_TYPE_TIMESTAMP pool with a fixed slot count. A thin shell: what a
// slot index means is decided one layer up (GpuTimingService); callers use Native() directly.
// Does not own the VkDevice, which must outlive it.
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
