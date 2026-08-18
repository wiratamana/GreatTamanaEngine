#include "RenderTexture.h"

#include <cstdint>
#include <stdexcept>
#include <utility>

namespace gte {

namespace {

std::uint32_t FindMemoryType(VkPhysicalDevice physicalDevice, std::uint32_t typeBits, VkMemoryPropertyFlags properties)
{
    VkPhysicalDeviceMemoryProperties memProperties{};
    vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memProperties);

    for (std::uint32_t i = 0; i < memProperties.memoryTypeCount; ++i) {
        const bool typeSupported = (typeBits & (1u << i)) != 0;
        const bool hasProperties = (memProperties.memoryTypes[i].propertyFlags & properties) == properties;
        if (typeSupported && hasProperties) {
            return i;
        }
    }
    throw std::runtime_error("RenderTexture: failed to find a suitable Vulkan memory type.");
}

} // namespace

RenderTexture::RenderTexture(VkPhysicalDevice physicalDevice, VkDevice device, int width, int height, VkFormat format)
    : m_physicalDevice(physicalDevice)
    , m_device(device)
    , m_format(format)
{
    Create(width, height);
}

RenderTexture::~RenderTexture()
{
    Destroy();
}

RenderTexture::RenderTexture(RenderTexture&& other) noexcept
    : m_physicalDevice(std::exchange(other.m_physicalDevice, VK_NULL_HANDLE))
    , m_device(std::exchange(other.m_device, VK_NULL_HANDLE))
    , m_format(other.m_format)
    , m_image(std::exchange(other.m_image, VK_NULL_HANDLE))
    , m_memory(std::exchange(other.m_memory, VK_NULL_HANDLE))
    , m_imageView(std::exchange(other.m_imageView, VK_NULL_HANDLE))
    , m_sampler(std::exchange(other.m_sampler, VK_NULL_HANDLE))
    , m_extent(other.m_extent)
{
}

RenderTexture& RenderTexture::operator=(RenderTexture&& other) noexcept
{
    if (this != &other) {
        Destroy();
        m_physicalDevice = std::exchange(other.m_physicalDevice, VK_NULL_HANDLE);
        m_device = std::exchange(other.m_device, VK_NULL_HANDLE);
        m_format = other.m_format;
        m_image = std::exchange(other.m_image, VK_NULL_HANDLE);
        m_memory = std::exchange(other.m_memory, VK_NULL_HANDLE);
        m_imageView = std::exchange(other.m_imageView, VK_NULL_HANDLE);
        m_sampler = std::exchange(other.m_sampler, VK_NULL_HANDLE);
        m_extent = other.m_extent;
    }
    return *this;
}

void RenderTexture::Resize(int width, int height)
{
    Destroy();
    Create(width, height);
}

RenderTarget RenderTexture::Target() const noexcept
{
    RenderTarget target;
    target.image = m_image;
    target.imageView = m_imageView;
    target.extent = m_extent;
    target.format = m_format;
    return target;
}

void RenderTexture::Create(int width, int height)
{
    // Clamp to at least 1x1 - a docked Editor panel can transiently report
    // zero size while collapsed/hidden, and a zero-sized VkImage is invalid.
    m_extent.width = static_cast<std::uint32_t>(width > 0 ? width : 1);
    m_extent.height = static_cast<std::uint32_t>(height > 0 ? height : 1);

    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.format = m_format;
    imageInfo.extent = { m_extent.width, m_extent.height, 1 };
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    // COLOR_ATTACHMENT so Renderer::RenderOffscreen() can draw into it,
    // SAMPLED so it can be displayed later (e.g. an Editor panel wrapping
    // it in an ImGui descriptor set).
    imageInfo.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    if (vkCreateImage(m_device, &imageInfo, nullptr, &m_image) != VK_SUCCESS) {
        throw std::runtime_error("RenderTexture: vkCreateImage failed.");
    }

    VkMemoryRequirements memRequirements{};
    vkGetImageMemoryRequirements(m_device, m_image, &memRequirements);

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex =
        FindMemoryType(m_physicalDevice, memRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    if (vkAllocateMemory(m_device, &allocInfo, nullptr, &m_memory) != VK_SUCCESS) {
        vkDestroyImage(m_device, m_image, nullptr);
        m_image = VK_NULL_HANDLE;
        throw std::runtime_error("RenderTexture: vkAllocateMemory failed.");
    }

    if (vkBindImageMemory(m_device, m_image, m_memory, 0) != VK_SUCCESS) {
        throw std::runtime_error("RenderTexture: vkBindImageMemory failed.");
    }

    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = m_image;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = m_format;
    viewInfo.components = { VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY,
                             VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY };
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;

    if (vkCreateImageView(m_device, &viewInfo, nullptr, &m_imageView) != VK_SUCCESS) {
        throw std::runtime_error("RenderTexture: vkCreateImageView failed.");
    }

    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.minLod = 0.0f;
    samplerInfo.maxLod = 1.0f;

    if (vkCreateSampler(m_device, &samplerInfo, nullptr, &m_sampler) != VK_SUCCESS) {
        throw std::runtime_error("RenderTexture: vkCreateSampler failed.");
    }
}

void RenderTexture::Destroy() noexcept
{
    if (m_sampler != VK_NULL_HANDLE) {
        vkDestroySampler(m_device, m_sampler, nullptr);
        m_sampler = VK_NULL_HANDLE;
    }
    if (m_imageView != VK_NULL_HANDLE) {
        vkDestroyImageView(m_device, m_imageView, nullptr);
        m_imageView = VK_NULL_HANDLE;
    }
    if (m_image != VK_NULL_HANDLE) {
        vkDestroyImage(m_device, m_image, nullptr);
        m_image = VK_NULL_HANDLE;
    }
    if (m_memory != VK_NULL_HANDLE) {
        vkFreeMemory(m_device, m_memory, nullptr);
        m_memory = VK_NULL_HANDLE;
    }
}

} // namespace gte
