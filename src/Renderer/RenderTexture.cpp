#include "RenderTexture.h"

#include <cstdint>
#include <stdexcept>
#include <utility>

namespace gte {

RenderTexture::RenderTexture(VmaAllocator allocator, std::shared_ptr<GpuMemoryTracker> tracker, VkDevice device,
    int width, int height, VkFormat format, VkFormat depthFormat, const char* debugName, const char* depthDebugName)
    : m_allocator(allocator)
    , m_tracker(std::move(tracker))
    , m_debugName(debugName)
    , m_depthDebugName(depthDebugName)
    , m_device(device)
    , m_format(format)
    , m_depthFormat(depthFormat)
{
    Create(width, height);
}

RenderTexture::~RenderTexture()
{
    Destroy();
}

RenderTexture::RenderTexture(RenderTexture&& other) noexcept
    : m_allocator(std::exchange(other.m_allocator, VK_NULL_HANDLE))
    , m_tracker(std::move(other.m_tracker))
    , m_handle(std::exchange(other.m_handle, kInvalidGpuResourceHandle))
    , m_debugName(std::exchange(other.m_debugName, nullptr))
    , m_depthDebugName(std::exchange(other.m_depthDebugName, nullptr))
    , m_device(std::exchange(other.m_device, VK_NULL_HANDLE))
    , m_format(other.m_format)
    , m_depthFormat(other.m_depthFormat)
    , m_image(std::exchange(other.m_image, VK_NULL_HANDLE))
    , m_allocation(std::exchange(other.m_allocation, VK_NULL_HANDLE))
    , m_imageView(std::exchange(other.m_imageView, VK_NULL_HANDLE))
    , m_sampler(std::exchange(other.m_sampler, VK_NULL_HANDLE))
    , m_extent(other.m_extent)
    , m_depthBuffer(std::move(other.m_depthBuffer))
{
}

RenderTexture& RenderTexture::operator=(RenderTexture&& other) noexcept
{
    if (this != &other) {
        Destroy();
        m_allocator = std::exchange(other.m_allocator, VK_NULL_HANDLE);
        m_tracker = std::move(other.m_tracker);
        m_handle = std::exchange(other.m_handle, kInvalidGpuResourceHandle);
        m_debugName = std::exchange(other.m_debugName, nullptr);
        m_depthDebugName = std::exchange(other.m_depthDebugName, nullptr);
        m_device = std::exchange(other.m_device, VK_NULL_HANDLE);
        m_format = other.m_format;
        m_depthFormat = other.m_depthFormat;
        m_image = std::exchange(other.m_image, VK_NULL_HANDLE);
        m_allocation = std::exchange(other.m_allocation, VK_NULL_HANDLE);
        m_imageView = std::exchange(other.m_imageView, VK_NULL_HANDLE);
        m_sampler = std::exchange(other.m_sampler, VK_NULL_HANDLE);
        m_extent = other.m_extent;
        m_depthBuffer = std::move(other.m_depthBuffer);
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
    if (m_depthBuffer) {
        target.depthImage = m_depthBuffer->Image();
        target.depthImageView = m_depthBuffer->View();
        target.depthFormat = m_depthBuffer->Format();
        target.depthHasStencil = m_depthBuffer->HasStencilComponent();
    }
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

    // VMA_MEMORY_USAGE_AUTO lets VMA pick the right memory type from the
    // image's usage flags (device-local here, since it's a color attachment)
    // - replaces the manual FindMemoryType()/vkAllocateMemory()/
    // vkBindImageMemory() dance this used to do by hand.
    VmaAllocationCreateInfo allocCreateInfo{};
    allocCreateInfo.usage = VMA_MEMORY_USAGE_AUTO;

    VmaAllocationInfo allocationInfo{};
    if (vmaCreateImage(m_allocator, &imageInfo, &allocCreateInfo, &m_image, &m_allocation, &allocationInfo) !=
        VK_SUCCESS) {
        throw std::runtime_error("RenderTexture: vmaCreateImage failed.");
    }

    // Registers this exact allocation - see AGENTS.md ("GPU resource
    // memory tracking"). Called from here (not just the constructor) so a
    // Resize() (Destroy() + Create()) always re-tracks with a fresh handle
    // reflecting the NEW size - the tracker never holds a stale record.
    const GpuMemoryLocation location = ClassifyGpuMemoryLocation(m_allocator, m_allocation);
    m_handle = m_tracker->Track(GpuResourceType::Texture, location, allocationInfo.size, m_format);
#if GTE_ENABLE_EDITOR
    if (m_debugName != nullptr) {
        m_tracker->SetDebugName(m_handle, m_debugName);
    }
#endif

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

    // This RenderTexture's own companion depth buffer - see the class
    // comment for why one (rather than several, as the swapchain needs) is
    // safe here. Named via m_depthDebugName (if the caller supplied one) so
    // it shows up as an identifiable engine-owned texture in the Editor's
    // "Memory" panel instead of "(unnamed)" - see RenderTexture's
    // constructor comment for the naming convention (e.g. "GameView" /
    // "GameViewDepth").
    m_depthBuffer = std::make_unique<DepthBuffer>(
        m_allocator, m_tracker, m_device, width, height, m_depthFormat, m_depthDebugName);
}

void RenderTexture::Destroy() noexcept
{
    m_depthBuffer.reset();

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

} // namespace gte
