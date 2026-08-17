#pragma once

#include <volk.h>

#include <cstdint>

namespace gte {

// RAII wrapper around a chosen VkPhysicalDevice + the VkDevice (logical
// device) created for it, plus the graphics/present queues pulled from that
// device. Owns the VkDevice for its entire lifetime: created in the
// constructor, destroyed in the destructor. Does NOT own the VkInstance or
// VkSurfaceKHR passed in - both must outlive this device.
//
// Selection picks the first suitable discrete GPU (falling back to any
// suitable device) that has:
//   - a queue family supporting VK_QUEUE_GRAPHICS_BIT
//   - a queue family with presentation support for the given surface
//     (may be the same family as graphics)
//   - VK_KHR_swapchain support
//   - VkPhysicalDeviceVulkan13Features::dynamicRendering support (used
//     instead of classic VkRenderPass/VkFramebuffer - simpler, and what
//     Dear ImGui's Vulkan backend supports natively)
class VulkanDevice {
public:
    VulkanDevice(VkInstance instance, VkSurfaceKHR surface);
    ~VulkanDevice();

    VulkanDevice(const VulkanDevice&) = delete;
    VulkanDevice& operator=(const VulkanDevice&) = delete;

    VulkanDevice(VulkanDevice&& other) noexcept;
    VulkanDevice& operator=(VulkanDevice&& other) noexcept;

    VkPhysicalDevice Physical() const noexcept { return m_physicalDevice; }
    VkDevice Native() const noexcept { return m_device; }

    VkQueue GraphicsQueue() const noexcept { return m_graphicsQueue; }
    VkQueue PresentQueue() const noexcept { return m_presentQueue; }
    std::uint32_t GraphicsQueueFamily() const noexcept { return m_graphicsFamily; }
    std::uint32_t PresentQueueFamily() const noexcept { return m_presentFamily; }

private:
    void PickPhysicalDevice(VkInstance instance, VkSurfaceKHR surface);
    void CreateLogicalDevice();
    void Destroy() noexcept;

    VkPhysicalDevice m_physicalDevice = VK_NULL_HANDLE;
    VkDevice m_device = VK_NULL_HANDLE;

    VkQueue m_graphicsQueue = VK_NULL_HANDLE;
    VkQueue m_presentQueue = VK_NULL_HANDLE;
    std::uint32_t m_graphicsFamily = 0;
    std::uint32_t m_presentFamily = 0;
};

} // namespace gte
