#include "Texture.h"

#include <algorithm>
#include <stdexcept>
#include <utility>

namespace gte {

namespace {

bool IsDepthFormat(PixelFormat format) noexcept
{
    return format == PixelFormat::D32_Float || format == PixelFormat::D32_Float_S8_UInt
        || format == PixelFormat::D24_UNorm_S8_UInt;
}

bool FormatHasStencil(PixelFormat format) noexcept
{
    return format == PixelFormat::D32_Float_S8_UInt || format == PixelFormat::D24_UNorm_S8_UInt;
}

VkImageViewType ViewTypeFor(TextureDimension dimension, std::uint32_t layers) noexcept
{
    switch (dimension) {
    case TextureDimension::Tex3D:
        return VK_IMAGE_VIEW_TYPE_3D;
    case TextureDimension::Array2D:
        return VK_IMAGE_VIEW_TYPE_2D_ARRAY;
    case TextureDimension::Cube:
        return layers == 6 ? VK_IMAGE_VIEW_TYPE_CUBE : VK_IMAGE_VIEW_TYPE_CUBE_ARRAY;
    case TextureDimension::Tex2D:
        break;
    }
    return VK_IMAGE_VIEW_TYPE_2D;
}

VkImageViewCreateInfo MakeViewInfo(VkImage image, VkImageViewType type, VkFormat format, VkImageAspectFlags aspect,
    std::uint32_t baseLayer, std::uint32_t layerCount) noexcept
{
    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = image;
    viewInfo.viewType = type;
    viewInfo.format = format;
    viewInfo.subresourceRange = { aspect, 0, 1, baseLayer, layerCount };
    return viewInfo;
}

} // namespace

Texture::Texture(VmaAllocator allocator, std::shared_ptr<GpuMemoryTracker> tracker, VkDevice device,
    const TextureCreateInfo& info)
    : m_allocator(allocator)
    , m_tracker(std::move(tracker))
    , m_device(device)
    , m_info(info)
    , m_format(ToVkFormat(info.format))
    , m_depth(std::max(info.depth, 1))
    , m_layers(static_cast<std::uint32_t>(std::max(info.layers, 1)))
    , m_isDepth(IsDepthFormat(info.format))
    , m_hasStencil(FormatHasStencil(info.format))
{
    m_width = std::max(info.width, 1);
    m_height = std::max(info.height, 1);
    Create();
}

Texture::~Texture()
{
    Destroy();
}

Texture::Texture(Texture&& other) noexcept
    : m_allocator(std::exchange(other.m_allocator, VK_NULL_HANDLE))
    , m_tracker(std::move(other.m_tracker))
    , m_handle(std::exchange(other.m_handle, kInvalidGpuResourceHandle))
    , m_device(std::exchange(other.m_device, VK_NULL_HANDLE))
    , m_info(other.m_info)
    , m_format(std::exchange(other.m_format, VK_FORMAT_UNDEFINED))
    , m_width(std::exchange(other.m_width, 0))
    , m_height(std::exchange(other.m_height, 0))
    , m_depth(std::exchange(other.m_depth, 0))
    , m_layers(std::exchange(other.m_layers, 0U))
    , m_isDepth(other.m_isDepth)
    , m_hasStencil(other.m_hasStencil)
    , m_image(std::exchange(other.m_image, VK_NULL_HANDLE))
    , m_allocation(std::exchange(other.m_allocation, VK_NULL_HANDLE))
    , m_imageView(std::exchange(other.m_imageView, VK_NULL_HANDLE))
    , m_sampler(std::exchange(other.m_sampler, VK_NULL_HANDLE))
    , m_layerViews(std::move(other.m_layerViews))
{
    other.m_layerViews.clear();
}

Texture& Texture::operator=(Texture&& other) noexcept
{
    if (this != &other) {
        Destroy();
        m_allocator = std::exchange(other.m_allocator, VK_NULL_HANDLE);
        m_tracker = std::move(other.m_tracker);
        m_handle = std::exchange(other.m_handle, kInvalidGpuResourceHandle);
        m_device = std::exchange(other.m_device, VK_NULL_HANDLE);
        m_info = other.m_info;
        m_format = std::exchange(other.m_format, VK_FORMAT_UNDEFINED);
        m_width = std::exchange(other.m_width, 0);
        m_height = std::exchange(other.m_height, 0);
        m_depth = std::exchange(other.m_depth, 0);
        m_layers = std::exchange(other.m_layers, 0U);
        m_isDepth = other.m_isDepth;
        m_hasStencil = other.m_hasStencil;
        m_image = std::exchange(other.m_image, VK_NULL_HANDLE);
        m_allocation = std::exchange(other.m_allocation, VK_NULL_HANDLE);
        m_imageView = std::exchange(other.m_imageView, VK_NULL_HANDLE);
        m_sampler = std::exchange(other.m_sampler, VK_NULL_HANDLE);
        m_layerViews = std::move(other.m_layerViews);
        other.m_layerViews.clear();
    }
    return *this;
}

void Texture::Resize(int width, int height)
{
    Destroy();
    m_width = std::max(width, 1);
    m_height = std::max(height, 1);
    Create();
}

void Texture::Create()
{
    if (!m_info.createImage) {
        return;
    }
    try {
        const bool is3D = m_info.dimension == TextureDimension::Tex3D;
        const bool isLayered = m_info.dimension == TextureDimension::Array2D || m_info.dimension == TextureDimension::Cube;
        const std::uint32_t imageLayers = isLayered ? m_layers : 1U;

        VkImageCreateInfo imageInfo{};
        imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        imageInfo.flags = m_info.dimension == TextureDimension::Cube ? VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT : 0;
        imageInfo.imageType = is3D ? VK_IMAGE_TYPE_3D : VK_IMAGE_TYPE_2D;
        imageInfo.format = m_format;
        imageInfo.extent = { static_cast<std::uint32_t>(m_width), static_cast<std::uint32_t>(m_height),
            is3D ? static_cast<std::uint32_t>(m_depth) : 1U };
        imageInfo.mipLevels = 1;
        imageInfo.arrayLayers = imageLayers;
        imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
        imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
        if (m_info.sampled) {
            imageInfo.usage |= VK_IMAGE_USAGE_SAMPLED_BIT;
        }
        if (m_info.storage) {
            imageInfo.usage |= VK_IMAGE_USAGE_STORAGE_BIT;
        }
        if (m_info.transferSrc) {
            imageInfo.usage |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
        }
        if (m_info.transferDst) {
            imageInfo.usage |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        }
        if (m_info.renderTarget) {
            imageInfo.usage |= m_isDepth ? VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT : VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
        }
        imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

        VmaAllocationCreateInfo allocCreateInfo{};
        allocCreateInfo.usage = VMA_MEMORY_USAGE_AUTO;

        VmaAllocationInfo allocationInfo{};
        if (vmaCreateImage(m_allocator, &imageInfo, &allocCreateInfo, &m_image, &m_allocation, &allocationInfo)
            != VK_SUCCESS) {
            throw std::runtime_error("Texture: vmaCreateImage failed.");
        }

        // Re-tracked on every (re)create so the tracker always reflects the current allocation.
        m_handle = m_tracker->Track(GpuResourceType::Texture, ClassifyGpuMemoryLocation(m_allocator, m_allocation),
            allocationInfo.size, m_format);
        if (m_info.debugName != nullptr) {
            m_tracker->SetDebugName(m_handle, m_info.debugName);
        }

        // A sampled view must expose a single aspect, so stencil is only included when not sampled.
        VkImageAspectFlags aspect = VK_IMAGE_ASPECT_COLOR_BIT;
        if (m_isDepth) {
            aspect = VK_IMAGE_ASPECT_DEPTH_BIT;
            if (m_hasStencil && !m_info.sampled) {
                aspect |= VK_IMAGE_ASPECT_STENCIL_BIT;
            }
        }

        const VkImageViewCreateInfo viewInfo =
            MakeViewInfo(m_image, ViewTypeFor(m_info.dimension, m_layers), m_format, aspect, 0, imageLayers);
        if (vkCreateImageView(m_device, &viewInfo, nullptr, &m_imageView) != VK_SUCCESS) {
            throw std::runtime_error("Texture: vkCreateImageView failed.");
        }

        if (m_info.perLayerViews) {
            m_layerViews.reserve(imageLayers);
            for (std::uint32_t i = 0; i < imageLayers; ++i) {
                const VkImageViewCreateInfo layerInfo =
                    MakeViewInfo(m_image, VK_IMAGE_VIEW_TYPE_2D, m_format, aspect, i, 1);
                VkImageView layerView = VK_NULL_HANDLE;
                if (vkCreateImageView(m_device, &layerInfo, nullptr, &layerView) != VK_SUCCESS) {
                    throw std::runtime_error("Texture: vkCreateImageView (per-layer) failed.");
                }
                m_layerViews.push_back(layerView);
            }
        }

        if (m_info.filter != TextureFilter::None) {
            const VkFilter filter = m_info.filter == TextureFilter::Nearest ? VK_FILTER_NEAREST : VK_FILTER_LINEAR;
            VkSamplerCreateInfo samplerInfo{};
            samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
            samplerInfo.magFilter = filter;
            samplerInfo.minFilter = filter;
            samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
            samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
            samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
            samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
            samplerInfo.minLod = 0.0f;
            samplerInfo.maxLod = 1.0f;
            if (vkCreateSampler(m_device, &samplerInfo, nullptr, &m_sampler) != VK_SUCCESS) {
                throw std::runtime_error("Texture: vkCreateSampler failed.");
            }
        }
    } catch (...) {
        Destroy(); // A throwing constructor never runs the destructor, so unwind here.
        throw;
    }
}

void Texture::Destroy() noexcept
{
    if (m_device == VK_NULL_HANDLE) {
        return;
    }
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

} // namespace gte
