#include "VulkanSwapchain.h"

#include "../../Core/Logging.h"

#include <algorithm>
#include <stdexcept>
#include <string>
#include <utility>

namespace gte {

namespace {

VkSurfaceFormatKHR ChooseSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& formats)
{
    for (const auto& format : formats) {
        if (format.format == VK_FORMAT_B8G8R8A8_UNORM &&
            format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            return format;
        }
    }
    return formats.front();
}

SupportedPresentModes SupportedModesOf(const std::vector<VkPresentModeKHR>& modes)
{
    SupportedPresentModes supported;
    for (const VkPresentModeKHR mode : modes) {
        supported.mailbox = supported.mailbox || mode == VK_PRESENT_MODE_MAILBOX_KHR;
        supported.immediate = supported.immediate || mode == VK_PRESENT_MODE_IMMEDIATE_KHR;
    }
    return supported;
}

VkPresentModeKHR ToVulkan(PresentMode mode) noexcept
{
    switch (mode) {
    case PresentMode::Mailbox: return VK_PRESENT_MODE_MAILBOX_KHR;
    case PresentMode::Immediate: return VK_PRESENT_MODE_IMMEDIATE_KHR;
    case PresentMode::Fifo: break;
    }
    return VK_PRESENT_MODE_FIFO_KHR;
}

VkExtent2D ChooseExtent(const VkSurfaceCapabilitiesKHR& capabilities, int width, int height)
{
    if (capabilities.currentExtent.width != 0xFFFFFFFFu) {
        return capabilities.currentExtent;
    }

    VkExtent2D extent;
    extent.width = std::clamp(static_cast<std::uint32_t>(width),
        capabilities.minImageExtent.width, capabilities.maxImageExtent.width);
    extent.height = std::clamp(static_cast<std::uint32_t>(height),
        capabilities.minImageExtent.height, capabilities.maxImageExtent.height);
    return extent;
}

} // namespace

VulkanSwapchain::VulkanSwapchain(VkPhysicalDevice physicalDevice,
                                  VkDevice device,
                                  VkSurfaceKHR surface,
                                  std::uint32_t graphicsFamily,
                                  std::uint32_t presentFamily,
                                  int width,
                                  int height,
                                  PresentPolicy policy)
    : m_physicalDevice(physicalDevice)
    , m_device(device)
    , m_surface(surface)
    , m_graphicsFamily(graphicsFamily)
    , m_presentFamily(presentFamily)
    , m_policy(policy)
{
    Create(width, height);
    GTE_LOG_INFO("Vulkan", std::string("present policy ") + PresentPolicyName(m_policy) + ", present mode "
            + PresentModeName(m_presentMode));
}

VulkanSwapchain::~VulkanSwapchain()
{
    Destroy();
}

VulkanSwapchain::VulkanSwapchain(VulkanSwapchain&& other) noexcept
    : m_physicalDevice(std::exchange(other.m_physicalDevice, VK_NULL_HANDLE))
    , m_device(std::exchange(other.m_device, VK_NULL_HANDLE))
    , m_surface(std::exchange(other.m_surface, VK_NULL_HANDLE))
    , m_graphicsFamily(other.m_graphicsFamily)
    , m_presentFamily(other.m_presentFamily)
    , m_policy(other.m_policy)
    , m_presentMode(other.m_presentMode)
    , m_swapchain(std::exchange(other.m_swapchain, VK_NULL_HANDLE))
    , m_imageFormat(other.m_imageFormat)
    , m_extent(other.m_extent)
    , m_images(std::move(other.m_images))
    , m_imageViews(std::move(other.m_imageViews))
{
}

VulkanSwapchain& VulkanSwapchain::operator=(VulkanSwapchain&& other) noexcept
{
    if (this != &other) {
        Destroy();
        m_physicalDevice = std::exchange(other.m_physicalDevice, VK_NULL_HANDLE);
        m_device = std::exchange(other.m_device, VK_NULL_HANDLE);
        m_surface = std::exchange(other.m_surface, VK_NULL_HANDLE);
        m_graphicsFamily = other.m_graphicsFamily;
        m_presentFamily = other.m_presentFamily;
        m_policy = other.m_policy;
        m_presentMode = other.m_presentMode;
        m_swapchain = std::exchange(other.m_swapchain, VK_NULL_HANDLE);
        m_imageFormat = other.m_imageFormat;
        m_extent = other.m_extent;
        m_images = std::move(other.m_images);
        m_imageViews = std::move(other.m_imageViews);
    }
    return *this;
}

void VulkanSwapchain::Recreate(int width, int height)
{
    // The caller has drained the device: no frame may still use the old images.
    Destroy();
    Create(width, height);
}

void VulkanSwapchain::Create(int width, int height)
{
    VkSurfaceCapabilitiesKHR capabilities{};
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(m_physicalDevice, m_surface, &capabilities);

    std::uint32_t formatCount = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(m_physicalDevice, m_surface, &formatCount, nullptr);
    std::vector<VkSurfaceFormatKHR> formats(formatCount);
    vkGetPhysicalDeviceSurfaceFormatsKHR(m_physicalDevice, m_surface, &formatCount, formats.data());

    std::uint32_t presentModeCount = 0;
    vkGetPhysicalDeviceSurfacePresentModesKHR(m_physicalDevice, m_surface, &presentModeCount, nullptr);
    std::vector<VkPresentModeKHR> presentModes(presentModeCount);
    vkGetPhysicalDeviceSurfacePresentModesKHR(m_physicalDevice, m_surface, &presentModeCount, presentModes.data());

    const VkSurfaceFormatKHR surfaceFormat = ChooseSurfaceFormat(formats);
    m_presentMode = ChoosePresentMode(m_policy, SupportedModesOf(presentModes));
    const VkPresentModeKHR presentMode = ToVulkan(m_presentMode);
    const VkExtent2D extent = ChooseExtent(capabilities, width, height);

    std::uint32_t imageCount = capabilities.minImageCount + 1;
    if (capabilities.maxImageCount > 0 && imageCount > capabilities.maxImageCount) {
        imageCount = capabilities.maxImageCount;
    }

    VkSwapchainCreateInfoKHR createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    createInfo.surface = m_surface;
    createInfo.minImageCount = imageCount;
    createInfo.imageFormat = surfaceFormat.format;
    createInfo.imageColorSpace = surfaceFormat.colorSpace;
    createInfo.imageExtent = extent;
    createInfo.imageArrayLayers = 1;
    createInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

    const std::uint32_t queueFamilyIndices[] = { m_graphicsFamily, m_presentFamily };
    if (m_graphicsFamily != m_presentFamily) {
        createInfo.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
        createInfo.queueFamilyIndexCount = 2;
        createInfo.pQueueFamilyIndices = queueFamilyIndices;
    } else {
        createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    }

    createInfo.preTransform = capabilities.currentTransform;
    createInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    createInfo.presentMode = presentMode;
    createInfo.clipped = VK_TRUE;
    createInfo.oldSwapchain = VK_NULL_HANDLE;

    const VkResult result = vkCreateSwapchainKHR(m_device, &createInfo, nullptr, &m_swapchain);
    if (result != VK_SUCCESS) {
        throw std::runtime_error("vkCreateSwapchainKHR failed (VkResult=" + std::to_string(result) + ")");
    }

    m_imageFormat = surfaceFormat.format;
    m_extent = extent;

    std::uint32_t actualImageCount = 0;
    vkGetSwapchainImagesKHR(m_device, m_swapchain, &actualImageCount, nullptr);
    m_images.resize(actualImageCount);
    vkGetSwapchainImagesKHR(m_device, m_swapchain, &actualImageCount, m_images.data());

    m_imageViews.resize(actualImageCount);
    for (std::uint32_t i = 0; i < actualImageCount; ++i) {
        VkImageViewCreateInfo viewInfo{};
        viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewInfo.image = m_images[i];
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = m_imageFormat;
        viewInfo.components = { VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY,
                                 VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY };
        viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        viewInfo.subresourceRange.baseMipLevel = 0;
        viewInfo.subresourceRange.levelCount = 1;
        viewInfo.subresourceRange.baseArrayLayer = 0;
        viewInfo.subresourceRange.layerCount = 1;

        if (vkCreateImageView(m_device, &viewInfo, nullptr, &m_imageViews[i]) != VK_SUCCESS) {
            throw std::runtime_error("vkCreateImageView failed while creating swapchain image views.");
        }
    }
}

void VulkanSwapchain::Destroy() noexcept
{
    for (VkImageView view : m_imageViews) {
        vkDestroyImageView(m_device, view, nullptr);
    }
    m_imageViews.clear();
    m_images.clear();

    if (m_swapchain != VK_NULL_HANDLE) {
        vkDestroySwapchainKHR(m_device, m_swapchain, nullptr);
        m_swapchain = VK_NULL_HANDLE;
    }
}

} // namespace gte
