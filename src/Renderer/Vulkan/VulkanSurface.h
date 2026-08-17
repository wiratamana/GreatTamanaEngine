#pragma once

#include <volk.h>

namespace gte {

class Window;

// RAII wrapper around a VkSurfaceKHR. Owns the underlying handle for its
// entire lifetime: created (via Window::CreateVulkanSurface) in the
// constructor, destroyed in the destructor. Does NOT own the VkInstance -
// the instance must outlive this surface.
class VulkanSurface {
public:
    VulkanSurface(VkInstance instance, const Window& window);
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
