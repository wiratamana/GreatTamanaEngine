#include "TextureArray2D.h"

#include <cassert>
#include <cstdint>
#include <stdexcept>
#include <utility>

namespace gte {

TextureArray2D::TextureArray2D(VmaAllocator allocator, std::shared_ptr<GpuMemoryTracker> tracker, VkDevice device,
    int width, int height, int arrayLayers, VkFormat format, bool hasDepth, bool isCubemap,
    bool allowStorageImageAccess, const char* debugName)
    : m_allocator(allocator)
    , m_tracker(std::move(tracker))
    , m_device(device)
    // Clamp to at least 1x1x1 layer, UNCONDITIONALLY, in both debug and
    // release builds - mirrors VolumeTexture.cpp's own
    // `width > 0 ? width : 1` pattern verbatim (a plain ternary clamp, not a
    // debug-assert: an out-of-range value here is just a degenerate-but-
    // harmless 1-layer array, not a Vulkan API violation).
    , m_width(width > 0 ? width : 1)
    , m_height(height > 0 ? height : 1)
    , m_arrayLayers(arrayLayers > 0 ? static_cast<std::uint32_t>(arrayLayers) : 1U)
    , m_format(format)
    , m_hasDepth(hasDepth)
    , m_isCubemap(isCubemap)
    , m_allowStorageImageAccess(allowStorageImageAccess)
{
    // Depth-format storage images are never realistically supported, and
    // there is no legitimate caller intent to trust here - see this class's
    // own header comment.
    assert(!(m_hasDepth && m_allowStorageImageAccess) &&
        "TextureArray2D: allowStorageImageAccess must never be combined with hasDepth.");

    // Fast, loud, in-development double-check of the two cubemap
    // invariants - the release-safe, unconditional enforcement of these
    // same two conditions lives one layer up, in
    // GpuResourceFactory::CreateTextureArray(), which must run BEFORE this
    // constructor is ever reached (see that method's own doc comment).
    assert((!m_isCubemap || (m_arrayLayers % 6 == 0 && m_arrayLayers >= 6)) &&
        "TextureArray2D: isCubemap requires arrayLayers to be a positive multiple of 6.");
    assert((!m_isCubemap || m_width == m_height) &&
        "TextureArray2D: isCubemap requires width == height.");

    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.format = m_format;
    imageInfo.extent = { static_cast<std::uint32_t>(m_width), static_cast<std::uint32_t>(m_height), 1 };
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = m_arrayLayers;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.flags = m_isCubemap ? VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT : 0;
    if (m_hasDepth) {
        imageInfo.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    } else {
        imageInfo.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        if (m_allowStorageImageAccess) {
            imageInfo.usage |= VK_IMAGE_USAGE_STORAGE_BIT;
        }
    }
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    VmaAllocationCreateInfo allocCreateInfo{};
    allocCreateInfo.usage = VMA_MEMORY_USAGE_AUTO;

    VmaAllocationInfo allocationInfo{};
    if (vmaCreateImage(m_allocator, &imageInfo, &allocCreateInfo, &m_image, &m_allocation, &allocationInfo) !=
        VK_SUCCESS) {
        throw std::runtime_error("TextureArray2D: vmaCreateImage failed.");
    }

    // Registers this exact allocation - see AGENTS.md ("GPU resource memory
    // tracking"). GpuResourceType has no 2D/3D/array/cube distinction at
    // all, so GpuResourceType::Texture is the correct value to reuse here
    // (see GpuMemoryTracker.h) - same precedent VolumeTexture already uses.
    const GpuMemoryLocation location = ClassifyGpuMemoryLocation(m_allocator, m_allocation);
    m_handle = m_tracker->Track(GpuResourceType::Texture, location, allocationInfo.size, m_format);
    if (debugName != nullptr) {
        m_tracker->SetDebugName(m_handle, debugName);
    }

    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = m_image;
    if (!m_isCubemap) {
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D_ARRAY;
    } else if (m_arrayLayers == 6) {
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_CUBE;
    } else {
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_CUBE_ARRAY;
    }
    viewInfo.format = m_format;
    viewInfo.components = { VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY,
                             VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY };
    // No stencil handling needed - unlike RenderTexture's DepthBuffer, this
    // is a simpler, single-aspect depth array.
    viewInfo.subresourceRange.aspectMask = m_hasDepth ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    // The WHOLE array/cube - this class's one whole-array view (see LayerView()
    // below for the per-layer counterpart).
    viewInfo.subresourceRange.layerCount = m_arrayLayers;

    if (vkCreateImageView(m_device, &viewInfo, nullptr, &m_imageView) != VK_SUCCESS) {
        m_tracker->Untrack(m_handle);
        vmaDestroyImage(m_allocator, m_image, m_allocation);
        throw std::runtime_error("TextureArray2D: vkCreateImageView failed.");
    }

    // One VK_IMAGE_VIEW_TYPE_2D view per layer, baseArrayLayer = i,
    // layerCount = 1 - reuses viewInfo's already-built components/aspectMask
    // so every per-layer view is byte-identical to the whole-array view
    // except viewType/baseArrayLayer/layerCount.
    m_layerViews.reserve(m_arrayLayers);
    for (std::uint32_t i = 0; i < m_arrayLayers; ++i) {
        VkImageViewCreateInfo layerViewInfo{};
        layerViewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        layerViewInfo.image = m_image;
        layerViewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        layerViewInfo.format = m_format;
        layerViewInfo.components = viewInfo.components;
        layerViewInfo.subresourceRange.aspectMask = viewInfo.subresourceRange.aspectMask;
        layerViewInfo.subresourceRange.baseMipLevel = 0;
        layerViewInfo.subresourceRange.levelCount = 1;
        layerViewInfo.subresourceRange.baseArrayLayer = i;
        layerViewInfo.subresourceRange.layerCount = 1;

        VkImageView layerView = VK_NULL_HANDLE;
        if (vkCreateImageView(m_device, &layerViewInfo, nullptr, &layerView) != VK_SUCCESS) {
            for (VkImageView existing : m_layerViews) {
                vkDestroyImageView(m_device, existing, nullptr);
            }
            vkDestroyImageView(m_device, m_imageView, nullptr);
            m_tracker->Untrack(m_handle);
            vmaDestroyImage(m_allocator, m_image, m_allocation);
            throw std::runtime_error("TextureArray2D: vkCreateImageView (per-layer) failed.");
        }
        m_layerViews.push_back(layerView);
    }

    // Plain linear, clamp-to-edge, no comparison - for BOTH the color and
    // depth case alike. See this class's own header comment for why this
    // engine's existing depth-sampling precedent (RenderTexture's own
    // DepthBuffer) is the correct template here, not VolumeTexture's
    // trilinear/compute-oriented sampler.
    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.minLod = 0.0f;
    samplerInfo.maxLod = 0.25f; // Single mip level only - matches VolumeTexture/Texture2D's own convention.
    samplerInfo.compareEnable = VK_FALSE;

    if (vkCreateSampler(m_device, &samplerInfo, nullptr, &m_sampler) != VK_SUCCESS) {
        for (VkImageView existing : m_layerViews) {
            vkDestroyImageView(m_device, existing, nullptr);
        }
        vkDestroyImageView(m_device, m_imageView, nullptr);
        m_tracker->Untrack(m_handle);
        vmaDestroyImage(m_allocator, m_image, m_allocation);
        throw std::runtime_error("TextureArray2D: vkCreateSampler failed.");
    }
}

TextureArray2D::~TextureArray2D()
{
    Destroy();
}

TextureArray2D::TextureArray2D(TextureArray2D&& other) noexcept
    : m_allocator(std::exchange(other.m_allocator, VK_NULL_HANDLE))
    , m_tracker(std::move(other.m_tracker))
    , m_handle(std::exchange(other.m_handle, kInvalidGpuResourceHandle))
    , m_device(std::exchange(other.m_device, VK_NULL_HANDLE))
    , m_image(std::exchange(other.m_image, VK_NULL_HANDLE))
    , m_allocation(std::exchange(other.m_allocation, VK_NULL_HANDLE))
    , m_imageView(std::exchange(other.m_imageView, VK_NULL_HANDLE))
    , m_layerViews(std::move(other.m_layerViews))
    , m_sampler(std::exchange(other.m_sampler, VK_NULL_HANDLE))
    , m_width(std::exchange(other.m_width, 0))
    , m_height(std::exchange(other.m_height, 0))
    , m_arrayLayers(std::exchange(other.m_arrayLayers, 0U))
    , m_format(std::exchange(other.m_format, VK_FORMAT_UNDEFINED))
    , m_hasDepth(std::exchange(other.m_hasDepth, false))
    , m_isCubemap(std::exchange(other.m_isCubemap, false))
    , m_allowStorageImageAccess(std::exchange(other.m_allowStorageImageAccess, false))
{
}

TextureArray2D& TextureArray2D::operator=(TextureArray2D&& other) noexcept
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
        m_layerViews = std::move(other.m_layerViews);
        m_sampler = std::exchange(other.m_sampler, VK_NULL_HANDLE);
        m_width = std::exchange(other.m_width, 0);
        m_height = std::exchange(other.m_height, 0);
        m_arrayLayers = std::exchange(other.m_arrayLayers, 0U);
        m_format = std::exchange(other.m_format, VK_FORMAT_UNDEFINED);
        m_hasDepth = std::exchange(other.m_hasDepth, false);
        m_isCubemap = std::exchange(other.m_isCubemap, false);
        m_allowStorageImageAccess = std::exchange(other.m_allowStorageImageAccess, false);
    }
    return *this;
}

void TextureArray2D::Destroy() noexcept
{
    if (m_sampler != VK_NULL_HANDLE) {
        vkDestroySampler(m_device, m_sampler, nullptr);
        m_sampler = VK_NULL_HANDLE;
    }
    for (VkImageView layerView : m_layerViews) {
        vkDestroyImageView(m_device, layerView, nullptr);
    }
    m_layerViews.clear();
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

TextureArrayTarget TextureArray2D::Target() const noexcept
{
    TextureArrayTarget target;
    target.image = m_image;
    target.imageView = m_imageView;
    target.extent = VkExtent2D{ static_cast<std::uint32_t>(m_width), static_cast<std::uint32_t>(m_height) };
    target.arrayLayers = m_arrayLayers;
    target.format = m_format;
    target.hasDepth = m_hasDepth;
    target.layerViews = m_layerViews.data();
    target.layerViewCount = static_cast<std::uint32_t>(m_layerViews.size());
    target.sampler = m_sampler;
    return target;
}

} // namespace gte
