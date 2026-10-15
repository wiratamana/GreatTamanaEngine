#pragma once

#include <volk.h>

namespace gte {

class ISurfaceProvider;

// RAII wrapper around a VkSurfaceKHR, created through ISurfaceProvider::CreateVulkanSurface. The
// VkInstance is not owned and must outlive the surface.
class VulkanSurface {
public:
    VulkanSurface(VkInstance instance, const ISurfaceProvider& surfaceProvider);
    ~VulkanSurface();

    VulkanSurface(const VulkanSurface&) = delete;
    VulkanSurface& operator=(const VulkanSurface&) = delete;

    VulkanSurface(VulkanSurface&& other) noexcept;
    VulkanSurface& operator=(VulkanSurface&& other) noexcept;

    VkSurfaceKHR Native() const noexcept { return m_surface; }

private:
    VkInstance m_instance = VK_NULL_HANDLE;
    VkSurfaceKHR m_surface = VK_NULL_HANDLE;
};

} // namespace gte
