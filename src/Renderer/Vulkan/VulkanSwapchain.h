#pragma once

#include <volk.h>

#include <cstdint>
#include <vector>

namespace gte {

// RAII wrapper around a VkSwapchainKHR + its per-image VkImageViews. Owns
// both for its entire lifetime: created in the constructor, destroyed in the
// destructor. Does NOT own the VkPhysicalDevice/VkDevice/VkSurfaceKHR passed
// in - all three must outlive this swapchain.
//
// Supports in-place Recreate() (e.g. on window resize, or when
// vkAcquireNextImageKHR/vkQueuePresentKHR report VK_ERROR_OUT_OF_DATE_KHR),
// which tears down and rebuilds the swapchain and its image views.
class VulkanSwapchain {
public:
    VulkanSwapchain(VkPhysicalDevice physicalDevice,
                    VkDevice device,
                    VkSurfaceKHR surface,
                    std::uint32_t graphicsFamily,
                    std::uint32_t presentFamily,
                    int width,
                    int height);
    ~VulkanSwapchain();

    VulkanSwapchain(const VulkanSwapchain&) = delete;
    VulkanSwapchain& operator=(const VulkanSwapchain&) = delete;

    VulkanSwapchain(VulkanSwapchain&& other) noexcept;
    VulkanSwapchain& operator=(VulkanSwapchain&& other) noexcept;

    // Tears down and rebuilds the swapchain (and image views) for a new
    // window size. Call this on resize, and whenever acquire/present report
    // the swapchain is out of date or suboptimal.
    void Recreate(int width, int height);

    VkSwapchainKHR Native() const noexcept { return m_swapchain; }
    VkFormat ImageFormat() const noexcept { return m_imageFormat; }
    VkExtent2D Extent() const noexcept { return m_extent; }
    std::uint32_t ImageCount() const noexcept { return static_cast<std::uint32_t>(m_images.size()); }
    VkImage Image(std::uint32_t index) const { return m_images[index]; }
    VkImageView ImageView(std::uint32_t index) const { return m_imageViews[index]; }

private:
    void Create(int width, int height);
    void Destroy() noexcept;

    VkPhysicalDevice m_physicalDevice = VK_NULL_HANDLE;
    VkDevice m_device = VK_NULL_HANDLE;
    VkSurfaceKHR m_surface = VK_NULL_HANDLE;
    std::uint32_t m_graphicsFamily = 0;
    std::uint32_t m_presentFamily = 0;

    VkSwapchainKHR m_swapchain = VK_NULL_HANDLE;
    VkFormat m_imageFormat = VK_FORMAT_UNDEFINED;
    VkExtent2D m_extent{};
    std::vector<VkImage> m_images;
    std::vector<VkImageView> m_imageViews;
};

} // namespace gte
