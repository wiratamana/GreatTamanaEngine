#pragma once

#include "Memory/GpuMemoryTracker.h"
#include "Vulkan/VulkanAllocator.h"

#include <memory>

namespace gte {

// RAII wrapper around an immutable, SAMPLED-ONLY 2D Vulkan image holding
// already-decoded RGBA8 pixel data (e.g. an imported PNG/JPEG asset) - the
// counterpart to RenderTexture (Renderer.h's resizable, cleared-and-drawn-
// into-every-frame "camera render target" primitive) for a plain, static,
// CPU-authored texture instead. Never resized/rewritten after construction -
// unlike RenderTexture there is no Resize(); a texture whose source file
// changed on disk becomes a brand new Texture2D, not a mutation of this one.
//
// Construct via Renderer::CreateTexture2D()/GpuResourceFactory::
// CreateTexture2D() (never directly) - those are what actually upload the
// pixel data via a temporary staging Buffer + ImmediateSubmit() (see
// GpuResourceFactory.cpp), the same "upload via staging" convention
// CreateDeviceLocalBuffer() already uses for vertex/index buffers. THIS
// constructor only creates the image/view/sampler (initially
// VK_IMAGE_LAYOUT_UNDEFINED, no pixel data in it yet) and registers it with
// GpuMemoryTracker - it does not itself touch pixel data or issue any
// upload/layout-transition commands, since it has no ImmediateSubmit()/
// command-pool access of its own (that lives one layer up, in
// GpuResourceFactory - same split RenderTexture/Buffer already use).
//
// Fixed at VK_FORMAT_R8G8B8A8_UNORM: this matches stb_image's 4-channel
// decode output byte-for-byte, and is purely for on-screen preview display
// (an Editor Inspector thumbnail today - see src/Editor/AssetPreviewTexture.h)
// rather than shader sampling that expects sRGB decoding, so UNORM (not
// SRGB) is what shows exactly the bytes the file contains.
//
// Registers with GpuMemoryTracker exactly like Buffer/RenderTexture (see
// AGENTS.md, "GPU Resource Memory Tracking") - an imported texture shows up
// in the Editor's "Memory" panel automatically, with zero extra bookkeeping.
class Texture2D {
public:
    // debugName is optional and Editor-only (see GpuMemoryTracker) - same
    // convention as Buffer/RenderTexture's own debugName parameter.
    //
    // allowStorageImageAccess (default false - every existing call site is
    // unaffected) opts this Texture2D's image into
    // VK_IMAGE_USAGE_STORAGE_BIT, the Vulkan mechanism behind an
    // `RWTexture` (see COMPUTE_PHASE1_RESOURCE_VOCABULARY_STRATEGY_v2.md).
    // The CALLER (GpuResourceFactory::CreateTexture2D(), never this
    // constructor itself - see Vulkan/FormatCapabilities.h) is responsible
    // for confirming VK_FORMAT_R8G8B8A8_UNORM (this class's own fixed
    // format) actually supports VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT via
    // SupportsStorageImageUsage() and throwing loudly if not - this
    // constructor unconditionally trusts that check already happened.
    Texture2D(VmaAllocator allocator, std::shared_ptr<GpuMemoryTracker> tracker, VkDevice device, int width,
        int height, const char* debugName = nullptr, bool allowStorageImageAccess = false);
    ~Texture2D();

    Texture2D(const Texture2D&) = delete;
    Texture2D& operator=(const Texture2D&) = delete;

    Texture2D(Texture2D&& other) noexcept;
    Texture2D& operator=(Texture2D&& other) noexcept;

    VkImage Image() const noexcept { return m_image; }
    VkImageView View() const noexcept { return m_imageView; }
    // Sampler suitable for reading this texture in a shader/ImGui descriptor
    // once GpuResourceFactory::CreateTexture2D() has finished uploading into
    // it (left in VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL - see
    // GpuResourceFactory.cpp).
    VkSampler Sampler() const noexcept { return m_sampler; }
    int Width() const noexcept { return m_width; }
    int Height() const noexcept { return m_height; }

    // Handle into the GpuMemoryTracker this Texture2D is registered with -
    // valid for this Texture2D's entire lifetime, kInvalidGpuResourceHandle
    // once moved-from.
    GpuResourceHandle Handle() const noexcept { return m_handle; }

    // Whether this Texture2D was created with VK_IMAGE_USAGE_STORAGE_BIT
    // (i.e. an `RWTexture`) - see RenderTexture::AllowsStorageImageAccess()'s
    // own comment for why a future descriptor-set builder needs this.
    bool AllowsStorageImageAccess() const noexcept { return m_allowStorageImageAccess; }

private:
    void Destroy() noexcept;

    VmaAllocator m_allocator = VK_NULL_HANDLE;
    std::shared_ptr<GpuMemoryTracker> m_tracker;
    GpuResourceHandle m_handle;

    VkDevice m_device = VK_NULL_HANDLE;
    VkImage m_image = VK_NULL_HANDLE;
    VmaAllocation m_allocation = VK_NULL_HANDLE;
    VkImageView m_imageView = VK_NULL_HANDLE;
    VkSampler m_sampler = VK_NULL_HANDLE;
    int m_width = 0;
    int m_height = 0;
    bool m_allowStorageImageAccess = false;
};

} // namespace gte
