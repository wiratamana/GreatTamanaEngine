#include "VolumeTexture.h"

#include <cstdint>
#include <stdexcept>
#include <utility>

namespace gte {

VolumeTexture::VolumeTexture(VmaAllocator allocator, std::shared_ptr<GpuMemoryTracker> tracker, VkDevice device,
    int width, int height, int depth, VkFormat format, const char* debugName)
    : m_allocator(allocator)
    , m_tracker(std::move(tracker))
    , m_device(device)
    // Clamp to at least 1x1x1 - same defensive reasoning as Texture2D's own
    // constructor: a zero-sized VkImage is invalid.
    , m_width(width > 0 ? width : 1)
    , m_height(height > 0 ? height : 1)
    , m_depth(depth > 0 ? depth : 1)
    , m_format(format)
{
    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_3D;
    imageInfo.format = m_format;
    imageInfo.extent = { static_cast<std::uint32_t>(m_width), static_cast<std::uint32_t>(m_height),
        static_cast<std::uint32_t>(m_depth) };
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    // Always BOTH storage + sampled - see this class's own header comment
    // for why there is no "sampled-only" variant here, unlike Texture2D/
    // RenderTexture's opt-in allowStorageImageAccess. The caller
    // (GpuResourceFactory::CreateVolumeTexture()) is responsible for having
    // already confirmed `format` supports
    // VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT via SupportsStorageImageUsage()
    // before ever reaching here - this constructor unconditionally trusts
    // that check already happened (mirrors Texture2D's own division of
    // labor).
    imageInfo.usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    VmaAllocationCreateInfo allocCreateInfo{};
    allocCreateInfo.usage = VMA_MEMORY_USAGE_AUTO;

    VmaAllocationInfo allocationInfo{};
    if (vmaCreateImage(m_allocator, &imageInfo, &allocCreateInfo, &m_image, &m_allocation, &allocationInfo) !=
        VK_SUCCESS) {
        throw std::runtime_error("VolumeTexture: vmaCreateImage failed.");
    }

    // Registers this exact allocation - see AGENTS.md ("GPU resource memory
    // tracking"). GpuResourceType has no 2D-vs-3D distinction at all, so
    // GpuResourceType::Texture is the correct value to reuse here (see
    // GpuMemoryTracker.h).
    const GpuMemoryLocation location = ClassifyGpuMemoryLocation(m_allocator, m_allocation);
    m_handle = m_tracker->Track(GpuResourceType::Texture, location, allocationInfo.size, m_format);
#if GTE_ENABLE_EDITOR
    if (debugName != nullptr) {
        m_tracker->SetDebugName(m_handle, debugName);
    }
#else
    (void)debugName;
#endif

    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = m_image;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_3D;
    viewInfo.format = m_format;
    viewInfo.components = { VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY,
                             VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY };
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;

    if (vkCreateImageView(m_device, &viewInfo, nullptr, &m_imageView) != VK_SUCCESS) {
        m_tracker->Untrack(m_handle);
        vmaDestroyImage(m_allocator, m_image, m_allocation);
        throw std::runtime_error("VolumeTexture: vkCreateImageView failed.");
    }

    // Trilinear, clamp-to-edge in all three axes - see this class's own
    // header comment for why (a froxel volume must never wrap).
    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.minLod = 0.0f;
    samplerInfo.maxLod = 0.25f; // Single mip level only - matches Texture2D's own "clamp to what actually exists" spirit.

    if (vkCreateSampler(m_device, &samplerInfo, nullptr, &m_sampler) != VK_SUCCESS) {
        vkDestroyImageView(m_device, m_imageView, nullptr);
        m_tracker->Untrack(m_handle);
        vmaDestroyImage(m_allocator, m_image, m_allocation);
        throw std::runtime_error("VolumeTexture: vkCreateSampler failed.");
    }
}

VolumeTexture::~VolumeTexture()
{
    Destroy();
}

VolumeTexture::VolumeTexture(VolumeTexture&& other) noexcept
    : m_allocator(std::exchange(other.m_allocator, VK_NULL_HANDLE))
    , m_tracker(std::move(other.m_tracker))
    , m_handle(std::exchange(other.m_handle, kInvalidGpuResourceHandle))
    , m_device(std::exchange(other.m_device, VK_NULL_HANDLE))
    , m_image(std::exchange(other.m_image, VK_NULL_HANDLE))
    , m_allocation(std::exchange(other.m_allocation, VK_NULL_HANDLE))
    , m_imageView(std::exchange(other.m_imageView, VK_NULL_HANDLE))
    , m_sampler(std::exchange(other.m_sampler, VK_NULL_HANDLE))
    , m_width(std::exchange(other.m_width, 0))
    , m_height(std::exchange(other.m_height, 0))
    , m_depth(std::exchange(other.m_depth, 0))
    , m_format(std::exchange(other.m_format, VK_FORMAT_UNDEFINED))
{
}

VolumeTexture& VolumeTexture::operator=(VolumeTexture&& other) noexcept
{
    if (this != &other) {
        Destroy();
        m_allocator = std::exchange(other.m_allocator, VK_NULL_HANDLE);
        m_tracker = std::move(other.m_tracker);
        m_handle = std::exchange(other.m_handle, kInvalidGpuResourceHandle);
        m_device = std::exchange(other.m_device, VK_NULL_HANDLE);
        m_image = std::exchange(other.m_image, VK_NULL_HANDLE);
        m_allocation = std::exchange(other.m_allocation, VK_NULL_HANDLE);
        m_imageView = std::exchange(other.m_imageView, VK_NULL_HANDLE);
        m_sampler = std::exchange(other.m_sampler, VK_NULL_HANDLE);
        m_width = std::exchange(other.m_width, 0);
        m_height = std::exchange(other.m_height, 0);
        m_depth = std::exchange(other.m_depth, 0);
        m_format = std::exchange(other.m_format, VK_FORMAT_UNDEFINED);
    }
    return *this;
}

void VolumeTexture::Destroy() noexcept
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
        m_tracker->Untrack(m_handle);
        m_handle = kInvalidGpuResourceHandle;

        vmaDestroyImage(m_allocator, m_image, m_allocation);
        m_image = VK_NULL_HANDLE;
        m_allocation = VK_NULL_HANDLE;
    }
}

VolumeTarget VolumeTexture::Target() const noexcept
{
    VolumeTarget target;
    target.image = m_image;
    target.imageView = m_imageView;
    target.extent = VkExtent3D{ static_cast<std::uint32_t>(m_width), static_cast<std::uint32_t>(m_height),
        static_cast<std::uint32_t>(m_depth) };
    target.format = m_format;
    return target;
}

} // namespace gte
