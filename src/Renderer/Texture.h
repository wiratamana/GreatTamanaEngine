#pragma once

#include "../Core/Extent.h"
#include "../Core/PixelFormat.h"
#include "../Core/TextureDimension.h"
#include "Memory/GpuMemoryTracker.h"
#include "VulkanFormatInfo.h"
#include "Vulkan/VulkanAllocator.h"

#include <cstdint>
#include <memory>
#include <vector>

namespace gte {

enum class TextureFilter : std::uint8_t {
    None, // No sampler is created.
    Linear,
    Nearest,
};

// Everything needed to create any GPU image. Aspect (color/depth/stencil) comes from `format`.
struct TextureCreateInfo {
    TextureDimension dimension = TextureDimension::Tex2D;
    int width = 1;
    int height = 1;
    int depth = 1;  // Z slices, Tex3D only.
    int layers = 1; // Array2D/Cube only.
    PixelFormat format = PixelFormat::RGBA8_UNorm;
    bool renderTarget = false; // Color or depth attachment, depending on `format`.
    bool sampled = true;
    bool storage = false;
    bool transferSrc = false;
    bool transferDst = false;
    bool perLayerViews = false; // One extra 2D view per layer (see LayerView()).
    bool createImage = true;    // false: only records size/format, creates no Vulkan objects (depth-only RenderTexture).
    TextureFilter filter = TextureFilter::Linear;
    const char* debugName = nullptr; // Stored by pointer - must outlive the Texture.
};

// The one RAII owner of a Vulkan image: image + VMA allocation + views + sampler + memory tracking.
// Every other texture class derives from this and only chooses a TextureCreateInfo.
class Texture {
public:
    Texture(VmaAllocator allocator, std::shared_ptr<GpuMemoryTracker> tracker, VkDevice device,
        const TextureCreateInfo& info);
    virtual ~Texture();

    Texture(const Texture&) = delete;
    Texture& operator=(const Texture&) = delete;
    Texture(Texture&& other) noexcept;
    Texture& operator=(Texture&& other) noexcept;

    // Destroys and recreates the image at a new size. Handle() changes; contents are undefined.
    virtual void Resize(int width, int height);

    int Width() const noexcept { return m_width; }
    int Height() const noexcept { return m_height; }
    int Depth() const noexcept { return m_depth; }
    std::uint32_t ArrayLayers() const noexcept { return m_layers; }
    Extent2D Extent() const noexcept
    {
        return Extent2D{ static_cast<std::uint32_t>(m_width), static_cast<std::uint32_t>(m_height) };
    }
    PixelFormat Format() const noexcept { return FromVkFormat(m_format); }
    bool HasStencilComponent() const noexcept { return m_hasStencil; }
    GpuResourceHandle Handle() const noexcept { return m_handle; }

    // Opaque identity of the image view: changes when the view is recreated, 0 when there is none.
    std::uint64_t ViewId() const noexcept { return reinterpret_cast<std::uintptr_t>(m_imageView); }

    // Renderer-internal: raw Vulkan objects.
    VkImage Image() const noexcept { return m_image; }
    VkImageView View() const noexcept { return m_imageView; }
    VkSampler Sampler() const noexcept { return m_sampler; }
    VkFormat NativeFormat() const noexcept { return m_format; }
    VkImageView LayerView(std::uint32_t layerIndex) const noexcept
    {
        return layerIndex < m_layerViews.size() ? m_layerViews[layerIndex] : VK_NULL_HANDLE;
    }

protected:
    const std::vector<VkImageView>& LayerViews() const noexcept { return m_layerViews; }
    bool IsDepth() const noexcept { return m_isDepth; }

private:
    void Create();
    void Destroy() noexcept;

    VmaAllocator m_allocator = VK_NULL_HANDLE;
    std::shared_ptr<GpuMemoryTracker> m_tracker;
    GpuResourceHandle m_handle = kInvalidGpuResourceHandle;
    VkDevice m_device = VK_NULL_HANDLE;
    TextureCreateInfo m_info;

    VkFormat m_format = VK_FORMAT_UNDEFINED;
    int m_width = 0;
    int m_height = 0;
    int m_depth = 1;
    std::uint32_t m_layers = 1;
    bool m_isDepth = false;
    bool m_hasStencil = false;

    VkImage m_image = VK_NULL_HANDLE;
    VmaAllocation m_allocation = VK_NULL_HANDLE;
    VkImageView m_imageView = VK_NULL_HANDLE;
    VkSampler m_sampler = VK_NULL_HANDLE;
    std::vector<VkImageView> m_layerViews;
};

} // namespace gte
