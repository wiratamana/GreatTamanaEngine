#include "VulkanSurface.h"

#include "../../Core/ISurfaceProvider.h"

#include <utility>

namespace gte {

VulkanSurface::VulkanSurface(VkInstance instance, const ISurfaceProvider& surfaceProvider)
    : m_instance(instance)
{
    // The provider's VkSurfaceKHR is structurally identical to volk's (see Core/ISurfaceProvider.h): no conversion.
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
