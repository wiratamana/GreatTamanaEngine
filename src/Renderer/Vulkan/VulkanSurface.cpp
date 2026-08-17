#include "VulkanSurface.h"

#include "../../Window/Window.h"

#include <utility>

namespace gte {

VulkanSurface::VulkanSurface(VkInstance instance, const Window& window)
    : m_instance(instance)
{
    // Window::CreateVulkanSurface() returns the SDL-typedef'd VkSurfaceKHR;
    // it is structurally identical to volk's/vulkan.h's own VkSurfaceKHR
    // (see the comment in Window.h), so no conversion is needed here beyond
    // the implicit pointer type match.
    m_surface = window.CreateVulkanSurface(instance);
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
