#include "DepthBuffer.h"

#include <stdexcept>
#include <utility>

namespace gte {

DepthBuffer::DepthBuffer(VmaAllocator allocator, std::shared_ptr<GpuMemoryTracker> tracker, VkDevice device,
    int width, int height, VkFormat format, const char* debugName)
    : m_allocator(allocator)
    , m_tracker(std::move(tracker))
    , m_debugName(debugName)
    , m_device(device)
    , m_format(format)
{
    Create(width, height);
}

DepthBuffer::~DepthBuffer()
{
    Destroy();
}

DepthBuffer::DepthBuffer(DepthBuffer&& other) noexcept
    : m_allocator(std::exchange(other.m_allocator, VK_NULL_HANDLE))
    , m_tracker(std::move(other.m_tracker))
    , m_handle(std::exchange(other.m_handle, kInvalidGpuResourceHandle))
    , m_debugName(std::exchange(other.m_debugName, nullptr))
    , m_device(std::exchange(other.m_device, VK_NULL_HANDLE))
    , m_format(other.m_format)
    , m_image(std::exchange(other.m_image, VK_NULL_HANDLE))
    , m_allocation(std::exchange(other.m_allocation, VK_NULL_HANDLE))
    , m_imageView(std::exchange(other.m_imageView, VK_NULL_HANDLE))
    , m_extent(other.m_extent)
{
}

DepthBuffer& DepthBuffer::operator=(DepthBuffer&& other) noexcept
{
    if (this != &other) {
        Destroy();
        m_allocator = std::exchange(other.m_allocator, VK_NULL_HANDLE);
        m_tracker = std::move(other.m_tracker);
        m_handle = std::exchange(other.m_handle, kInvalidGpuResourceHandle);
        m_debugName = std::exchange(other.m_debugName, nullptr);
        m_device = std::exchange(other.m_device, VK_NULL_HANDLE);
        m_format = other.m_format;
        m_image = std::exchange(other.m_image, VK_NULL_HANDLE);
        m_allocation = std::exchange(other.m_allocation, VK_NULL_HANDLE);
        m_imageView = std::exchange(other.m_imageView, VK_NULL_HANDLE);
        m_extent = other.m_extent;
    }
    return *this;
}

void DepthBuffer::Resize(int width, int height)
{
    Destroy();
    Create(width, height);
}

bool DepthBuffer::HasStencilComponent() const noexcept
{
    return m_format == VK_FORMAT_D32_SFLOAT_S8_UINT || m_format == VK_FORMAT_D24_UNORM_S8_UINT ||
        m_format == VK_FORMAT_D16_UNORM_S8_UINT;
}

void DepthBuffer::Create(int width, int height)
{
    // Clamp to at least 1x1 - same reasoning as RenderTexture::Create() (a
    // docked Editor panel, or a minimized window, can transiently report a
    // zero size, and a zero-sized VkImage is invalid).
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
    imageInfo.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    VmaAllocationCreateInfo allocCreateInfo{};
    allocCreateInfo.usage = VMA_MEMORY_USAGE_AUTO;

    VmaAllocationInfo allocationInfo{};
    if (vmaCreateImage(m_allocator, &imageInfo, &allocCreateInfo, &m_image, &m_allocation, &allocationInfo) !=
        VK_SUCCESS) {
        throw std::runtime_error("DepthBuffer: vmaCreateImage failed.");
    }

    // See AGENTS.md ("GPU Resource Memory Tracking") - re-tracked here (not
    // just the constructor) so a Resize() (Destroy() + Create()) always
    // reflects the NEW size under a fresh handle, never a stale one.
    const GpuMemoryLocation location = ClassifyGpuMemoryLocation(m_allocator, m_allocation);
    m_handle = m_tracker->Track(GpuResourceType::Texture, location, allocationInfo.size, m_format);
#if GTE_ENABLE_EDITOR
    if (m_debugName != nullptr) {
        m_tracker->SetDebugName(m_handle, m_debugName);
    }
#endif

    const VkImageAspectFlags aspectMask =
        VK_IMAGE_ASPECT_DEPTH_BIT | (HasStencilComponent() ? VK_IMAGE_ASPECT_STENCIL_BIT : 0);

    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = m_image;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = m_format;
    viewInfo.components = { VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY,
                             VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY };
    viewInfo.subresourceRange.aspectMask = aspectMask;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;

    if (vkCreateImageView(m_device, &viewInfo, nullptr, &m_imageView) != VK_SUCCESS) {
        throw std::runtime_error("DepthBuffer: vkCreateImageView failed.");
    }
}

void DepthBuffer::Destroy() noexcept
{
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

} // namespace gte
