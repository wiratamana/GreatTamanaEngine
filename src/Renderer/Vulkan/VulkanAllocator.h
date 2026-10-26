#pragma once

#include <volk.h>

// This project resolves Vulkan entry points through volk, so VMA fetches its own function table
// (dynamic) from vkGetInstanceProcAddr/vkGetDeviceProcAddr. Defined here, overridable, so every
// translation unit seeing vk_mem_alloc.h agrees.
#ifndef VMA_STATIC_VULKAN_FUNCTIONS
#define VMA_STATIC_VULKAN_FUNCTIONS 0
#endif
#ifndef VMA_DYNAMIC_VULKAN_FUNCTIONS
#define VMA_DYNAMIC_VULKAN_FUNCTIONS 1
#endif
#include <vk_mem_alloc.h>

#include <cstdint>
#include <vector>

namespace gte {

// RAII wrapper around a VmaAllocator. Does not own the instance, physical device or device; all three
// must outlive it. GPU resources create their images and buffers through it.
class VulkanAllocator {
public:
    VulkanAllocator(VkInstance instance, VkPhysicalDevice physicalDevice, VkDevice device, std::uint32_t apiVersion);
    ~VulkanAllocator();

    VulkanAllocator(const VulkanAllocator&) = delete;
    VulkanAllocator& operator=(const VulkanAllocator&) = delete;

    VulkanAllocator(VulkanAllocator&& other) noexcept;
    VulkanAllocator& operator=(VulkanAllocator&& other) noexcept;

    VmaAllocator Native() const noexcept { return m_allocator; }

    // Drops the allocator without destroying it. For a lost device with leaked allocations still live.
    void AbandonOnDestroy() noexcept { m_abandon = true; }

    // One VmaBudget per memory heap: the driver-reported usage and budget (an estimate without
    // VK_EXT_memory_budget). Cheap enough to call every frame.
    std::vector<VmaBudget> GetHeapBudgets() const;

private:
    void Destroy() noexcept;

    VmaAllocator m_allocator = VK_NULL_HANDLE;
    bool m_abandon = false;
};

} // namespace gte
