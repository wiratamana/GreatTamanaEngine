#pragma once

#include "Memory/GpuMemoryTracker.h"
#include "Vulkan/VulkanAllocator.h"

#include <memory>

namespace gte {

// RAII wrapper around a depth-attachment-only Vulkan image: owns the
// VkImage, its backing VmaAllocation, and a VkImageView for its entire
// lifetime - the depth-buffer counterpart to RenderTexture's color image,
// needed now that real (non-coplanar) 3D geometry - the built-in primitive
// shapes (Renderer/Primitives/PrimitiveMeshGenerator.h) - actually overlaps
// itself in screen space and needs correct depth-tested occlusion instead
// of "whichever triangle happened to be drawn last wins" (this engine's
// original hardcoded triangle demo was always flat/coplanar at the same
// depth, so this gap was invisible until real solid shapes existed).
//
// By DEFAULT, a DepthBuffer is never sampled (no VkSampler) and never
// displayed directly - it exists purely so a graphics pipeline has
// somewhere to depth-test/write against for one render target. Every
// Renderer-owned render target now has exactly one paired DepthBuffer, all
// built at the SAME format (VulkanDevice::PickDepthFormat(), surfaced as
// Renderer::DepthFormat()) - the exact same "one shared, runtime-negotiated
// format every pipeline/target agrees on" discipline
// Renderer::ColorFormat() already applies to color (see AGENTS.md, "Render
// Target Format Matching").
//
// Atmosphere Scattering + Aerial Perspective campaign, Phase 7
// (task_manager/atmosphere-scattering-1/ATMOSPHERE_PHASE7_SKY_BACKGROUND_AND_COMPOSITE_PASSES_v1.md)
// - added `allowSampledAccess` (default false, every existing call site
// unaffected): opts this DepthBuffer's image into ADDITIONALLY
// VK_IMAGE_USAGE_SAMPLED_BIT and creates a real, NEAREST-filtered
// VkSampler (a depth value must never be linearly blended across texels -
// mirrors Texture2D/RenderTexture's own `allowStorageImageAccess` opt-in
// convention, just for sampled access instead of storage-image access).
// Applied to the DepthBuffer backing the Game/Scene View's own RenderTexture
// companion depth, so the Aerial Perspective Composite pass (Phase 7) can
// bind it as `sampler2D sourceDepth` - read via
// `ResourceAccess::ShaderRead` with `isDepthResource=true`
// (RenderGraphBarrierPlanner's own RequiredStateFor() already treats this
// identically to a color ShaderRead - no barrier-planner changes needed).
//
// Registers itself with a GpuMemoryTracker exactly like Buffer/RenderTexture
// (see AGENTS.md, "GPU Resource Memory Tracking") - constructor AND
// Resize() (a genuinely new allocation) always (re)track a fresh handle.
class DepthBuffer {
public:
    // debugName is optional/Editor-only, same convention as Buffer/
    // RenderTexture.
    DepthBuffer(VmaAllocator allocator, std::shared_ptr<GpuMemoryTracker> tracker, VkDevice device, int width,
        int height, VkFormat format, const char* debugName = nullptr, bool allowSampledAccess = false);
    ~DepthBuffer();

    DepthBuffer(const DepthBuffer&) = delete;
    DepthBuffer& operator=(const DepthBuffer&) = delete;

    DepthBuffer(DepthBuffer&& other) noexcept;
    DepthBuffer& operator=(DepthBuffer&& other) noexcept;

    // Destroys and recreates the underlying image/view at a new size - same
    // "genuinely new VMA allocation, Handle() changes" contract as
    // RenderTexture::Resize(). Re-applies the exact same
    // allowSampledAccess/usage flags (and re-creates the sampler, if any)
    // at the new size, mirroring RenderTexture::Resize()'s own contract.
    void Resize(int width, int height);

    VkImage Image() const noexcept { return m_image; }
    VkImageView View() const noexcept { return m_imageView; }
    VkFormat Format() const noexcept { return m_format; }
    VkExtent2D Extent() const noexcept { return m_extent; }

    // Sampler suitable for reading this depth buffer in a shader as
    // `sampler2D`, once whichever pass wrote it has finished - VK_NULL_HANDLE
    // unless this DepthBuffer was constructed with allowSampledAccess = true.
    // NEAREST filtering (depth values must never be linearly blended across
    // texels).
    VkSampler Sampler() const noexcept { return m_sampler; }

    // True if this DepthBuffer's format also carries a stencil component
    // (e.g. VK_FORMAT_D24_UNORM_S8_UINT) - needed by FrameRecorder to pick
    // the right VkImageAspectFlags/VkImageLayout for its layout transitions
    // and rendering attachment. VulkanDevice::PickDepthFormat() always
    // prefers a depth-only format when the device supports one (the common
    // case), so this is normally false.
    bool HasStencilComponent() const noexcept;

    // Handle into the GpuMemoryTracker this DepthBuffer is registered with -
    // valid until the next Resize() (see above) or destruction.
    GpuResourceHandle Handle() const noexcept { return m_handle; }

private:
    void Create(int width, int height);
    void Destroy() noexcept;

    VmaAllocator m_allocator = VK_NULL_HANDLE;
    std::shared_ptr<GpuMemoryTracker> m_tracker;
    GpuResourceHandle m_handle;
    const char* m_debugName = nullptr;

    VkDevice m_device = VK_NULL_HANDLE;
    VkFormat m_format = VK_FORMAT_UNDEFINED;
    bool m_allowSampledAccess = false;

    VkImage m_image = VK_NULL_HANDLE;
    VmaAllocation m_allocation = VK_NULL_HANDLE;
    VkImageView m_imageView = VK_NULL_HANDLE;
    VkSampler m_sampler = VK_NULL_HANDLE;
    VkExtent2D m_extent{};
};

} // namespace gte
