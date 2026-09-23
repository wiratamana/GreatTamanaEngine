#pragma once

#include <volk.h>

namespace gte {

class ISurfaceProvider;

// RAII wrapper around a VkSurfaceKHR. Owns the underlying handle for its
// entire lifetime: created (via ISurfaceProvider::CreateVulkanSurface) in the
// constructor, destroyed in the destructor. Does NOT own the VkInstance -
// the instance must outlive this surface.
//
// editor-core-separation-1 campaign, PHASE10
// (task_manager/editor-core-separation-1/PHASE10_ISURFACEPROVIDER_INTERFACE_AND_WINDOW_INVERSION.md) -
// depends on the abstract ISurfaceProvider interface instead of the concrete
// Window class (Window implements it).
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
