#pragma once

#include <volk.h>

// VMA's implementation section (compiled only where VMA_IMPLEMENTATION is
// defined before this header is first seen - see VulkanAllocator.cpp) picks
// between statically-linked, self-fetched, or externally-supplied Vulkan
// function pointers based on these two macros. This project resolves every
// Vulkan entry point dynamically through volk rather than linking a classic
// loader import lib (see FetchVulkan.cmake), so VMA is told to fetch its own
// function table (VMA_DYNAMIC_VULKAN_FUNCTIONS) starting from just
// vkGetInstanceProcAddr/vkGetDeviceProcAddr - the two entry points volk
// itself exposes as plain global function pointers once
// volkInitialize()/volkLoadInstance()/volkLoadDevice() have run (see
// VulkanAllocator.cpp). Defined here (guarded, so callers can override
// before including this header) rather than only in VulkanAllocator.cpp so
// every translation unit that sees vk_mem_alloc.h agrees on the same values.
#ifndef VMA_STATIC_VULKAN_FUNCTIONS
#define VMA_STATIC_VULKAN_FUNCTIONS 0
#endif
#ifndef VMA_DYNAMIC_VULKAN_FUNCTIONS
#define VMA_DYNAMIC_VULKAN_FUNCTIONS 1
#endif
#include <vk_mem_alloc.h>

#include <cstdint>

namespace gte {

// RAII wrapper around a VmaAllocator (Vulkan Memory Allocator) - created in
// the constructor, destroyed in the destructor. Does NOT own the
// VkInstance/VkPhysicalDevice/VkDevice passed in - all three must outlive
// this object (same convention as VulkanDevice/VulkanSwapchain not owning
// the instance/surface passed to them).
//
// This is the allocator every GPU resource type (RenderTexture today, future
// vertex/index/uniform/staging buffers) should create its VkImage/VkBuffer
// through via vmaCreateImage()/vmaCreateBuffer() instead of the manual
// FindMemoryType() + vkAllocateMemory()/vkBindImageMemory() dance those used
// to do by hand.
class VulkanAllocator {
public:
    VulkanAllocator(VkInstance instance, VkPhysicalDevice physicalDevice, VkDevice device, std::uint32_t apiVersion);
    ~VulkanAllocator();

    VulkanAllocator(const VulkanAllocator&) = delete;
    VulkanAllocator& operator=(const VulkanAllocator&) = delete;

    VulkanAllocator(VulkanAllocator&& other) noexcept;
    VulkanAllocator& operator=(VulkanAllocator&& other) noexcept;

    VmaAllocator Native() const noexcept { return m_allocator; }

private:
    void Destroy() noexcept;

    VmaAllocator m_allocator = VK_NULL_HANDLE;
};

} // namespace gte
