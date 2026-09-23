#include "VulkanSurface.h"

#include "../../Core/ISurfaceProvider.h"

#include <utility>

namespace gte {

VulkanSurface::VulkanSurface(VkInstance instance, const ISurfaceProvider& surfaceProvider)
    : m_instance(instance)
{
    // ISurfaceProvider::CreateVulkanSurface() returns the SDL-typedef'd (or,
    // for a future headless/Player implementation, whatever-else-typedef'd)
    // VkSurfaceKHR; it is structurally identical to volk's/vulkan.h's own
    // VkSurfaceKHR (see the comment in Core/ISurfaceProvider.h), so no
    // conversion is needed here beyond the implicit pointer type match.
    m_surface = surfaceProvider.CreateVulkanSurface(instance);
}

VulkanSurface::~VulkanSurface()
{
    if (m_surface != VK_NULL_HANDLE) {
        vkDestroySurfaceKHR(m_instance, m_surface, nullptr);
        m_surface = VK_NULL_HANDLE;
    }
}

VulkanSurface::VulkanSurface(VulkanSurface&& other) noexcept
    : m_instance(std::exchange(other.m_instance, VK_NULL_HANDLE))
    , m_surface(std::exchange(other.m_surface, VK_NULL_HANDLE))
{
}

VulkanSurface& VulkanSurface::operator=(VulkanSurface&& other) noexcept
{
    if (this != &other) {
        if (m_surface != VK_NULL_HANDLE) {
            vkDestroySurfaceKHR(m_instance, m_surface, nullptr);
        }
        m_instance = std::exchange(other.m_instance, VK_NULL_HANDLE);
        m_surface = std::exchange(other.m_surface, VK_NULL_HANDLE);
    }
    return *this;
}

} // namespace gte
