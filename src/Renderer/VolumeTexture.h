#pragma once

#include "Memory/GpuMemoryTracker.h"
#include "VolumeTarget.h"
#include "Vulkan/VulkanAllocator.h"

#include <memory>

namespace gte {

// RAII wrapper around a real 3D (VK_IMAGE_TYPE_3D) Vulkan image - the
// Atmosphere Scattering campaign's froxel/voxel-grid GPU resource type
// (Phase 2 - ATMOSPHERE_PHASE2_VOLUME_TEXTURE_RENDERGRAPH_SUPPORT_v1.md),
// mirroring Texture2D.h's exact shape with these deliberate differences:
//
// - Takes an EXPLICIT `format` parameter (unlike Texture2D's fixed
//   VK_FORMAT_R8G8B8A8_UNORM) - the aerial-perspective volume needs a
//   higher-precision floating-point format (e.g.
//   VK_FORMAT_R16G16B16A16_SFLOAT) to store scattering values that can
//   exceed [0, 1] and need float precision.
// - ALWAYS storage+sampled capable (VK_IMAGE_USAGE_STORAGE_BIT |
//   VK_IMAGE_USAGE_SAMPLED_BIT, unconditionally) - unlike Texture2D/
//   RenderTexture's opt-in `allowStorageImageAccess`, a VolumeTexture in
//   this campaign is ALWAYS written by a compute pass and ALWAYS later
//   sampled, so there is no "sampled-only" variant worth supporting.
// - VK_IMAGE_VIEW_TYPE_3D image view; a trilinear (VK_FILTER_LINEAR
//   min/mag/mip) sampler clamped to edge (VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE)
//   in all three axes - a froxel volume must never wrap, and clamping at
//   the near/far/edge froxels is the physically correct behavior.
// - Single mip level only - no mip-mapping support at all (this campaign's
//   froxel volume never needs one).
//
// Construct via Renderer::CreateVolumeTexture()/GpuResourceFactory::
// CreateVolumeTexture() (never directly) - exactly like Texture2D/
// RenderTexture, the FORMAT-CAPABILITY CHECK (Vulkan/FormatCapabilities.h's
// SupportsStorageImageUsage()) belongs in that factory layer, NOT this
// constructor - this constructor unconditionally trusts that check already
// happened (see GpuResourceFactory::CreateVolumeTexture()'s own doc
// comment - mirrors Texture2D's identical "constructor trusts, factory
// checks" division of labor).
//
// Registers with GpuMemoryTracker exactly like Buffer/RenderTexture/
// Texture2D (see AGENTS.md, "GPU Resource Memory Tracking") - as
// GpuResourceType::Texture (that enum has no 2D-vs-3D distinction at all -
// see GpuMemoryTracker.h).
//
// A VolumeTexture is imported into a gte::rg::RenderGraph via
// RenderGraphBuilder::ImportVolumeTexture() - which takes THIS class's own
// Target() accessor (a plain, non-owning VolumeTarget - see VolumeTarget.h),
// never the owning VolumeTexture object itself, exactly mirroring
// RenderTexture::Target()/RenderGraphBuilder::ImportTexture()'s existing
// convention. There is deliberately no CreateVolumeTexture()
// (pooled/transient) render-graph counterpart yet - see
// ATMOSPHERE_PHASE2_VOLUME_TEXTURE_RENDERGRAPH_SUPPORT_v1.md's own Step 3.3/
// this campaign's Phase 2 completion report for why this was deliberately
// deferred rather than speculatively built.
class VolumeTexture {
public:
    // debugName is optional and Editor-only (see GpuMemoryTracker) - same
    // convention as Buffer/RenderTexture/Texture2D's own debugName
    // parameter.
    VolumeTexture(VmaAllocator allocator, std::shared_ptr<GpuMemoryTracker> tracker, VkDevice device, int width,
        int height, int depth, VkFormat format, const char* debugName = nullptr);
    ~VolumeTexture();

    VolumeTexture(const VolumeTexture&) = delete;
    VolumeTexture& operator=(const VolumeTexture&) = delete;

    VolumeTexture(VolumeTexture&& other) noexcept;
    VolumeTexture& operator=(VolumeTexture&& other) noexcept;

    VkImage Image() const noexcept { return m_image; }
    VkImageView View() const noexcept { return m_imageView; }
    // Sampler suitable for reading this volume texture in a shader once the
    // pass that writes it has finished (trilinear, clamp-to-edge in all
    // three axes - see this class's own comment above).
    VkSampler Sampler() const noexcept { return m_sampler; }
    int Width() const noexcept { return m_width; }
    int Height() const noexcept { return m_height; }
    int Depth() const noexcept { return m_depth; }
    VkFormat Format() const noexcept { return m_format; }

    // Handle into the GpuMemoryTracker this VolumeTexture is registered
    // with - valid for this VolumeTexture's entire lifetime,
    // kInvalidGpuResourceHandle once moved-from.
    GpuResourceHandle Handle() const noexcept { return m_handle; }

    // Plain, non-owning view of this resource for
    // RenderGraphBuilder::ImportVolumeTexture() - mirrors
    // RenderTexture::Target() exactly (see VolumeTarget.h).
    VolumeTarget Target() const noexcept;

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
    int m_depth = 0;
    VkFormat m_format = VK_FORMAT_UNDEFINED;
};

} // namespace gte
