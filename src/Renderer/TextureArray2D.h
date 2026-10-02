#pragma once

#include "Memory/GpuMemoryTracker.h"
#include "TextureArrayTarget.h"
#include "Vulkan/VulkanAllocator.h"

#include <memory>

namespace gte {

// RAII wrapper around a real 2D (VK_IMAGE_TYPE_2D) Vulkan image with N array
// layers - either a plain Texture2DArray (isCubemap == false) or a cubemap /
// cubemap array (isCubemap == true, VK_IMAGE_VIEW_TYPE_CUBE or
// VK_IMAGE_VIEW_TYPE_CUBE_ARRAY) - better-render-pass-3 campaign, BLOCK 5
// (Array and Cubemap Texture Resources). Mirrors VolumeTexture.h's own
// image/view-creation MECHANICS (explicit `format` parameter, single mip
// level, a Target() accessor, GpuMemoryTracker registration as
// GpuResourceType::Texture - that enum has no 2D/3D/array/cube distinction
// at all, same precedent VolumeTexture already uses), with these deliberate
// differences:
//
// - VK_IMAGE_TYPE_2D, not _3D - this is a stack of N same-sized 2D layers,
//   never a true 3D (depth-addressable) image. Use VolumeTexture for that.
// - `arrayLayers` and `isCubemap` are genuinely new concerns VolumeTexture
//   has no equivalent of. `isCubemap` requires `arrayLayers` to be a
//   positive multiple of 6 and `width == height` - BOTH invariants are
//   validated with a real, unconditional, release-build `throw` one layer
//   up, in GpuResourceFactory::CreateTextureArray() (never here - this
//   constructor only carries a fast, loud, debug-only `assert()` of the
//   same two conditions, for in-development feedback; see that factory
//   method's own doc comment for why the release-safe enforcement belongs
//   there and not in this constructor).
// - `hasDepth` selects WHICH KIND of homogeneous image every layer of this
//   array is (VK_IMAGE_ASPECT_DEPTH_BIT + VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT
//   vs. VK_IMAGE_ASPECT_COLOR_BIT + VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT) -
//   this is NOT the same meaning `hasDepth` carries on RenderTexture/
//   TextureDesc (there, it means "also allocate a SECOND, separate
//   companion depth image"). There is only ever ONE image here, period.
//   This constructor's own `hasDepth`/`isCubemap` parameters have no
//   default - the caller (GpuResourceFactory::CreateTextureArray()) always
//   supplies both explicitly, already resolved.
// - Mirrors Texture2D/RenderTexture's OPT-IN `allowStorageImageAccess`
//   convention (never VolumeTexture's own unconditional "always storage +
//   sampled" policy - that fits VolumeTexture's own always-compute-written
//   use case, not this one, which is primarily a depth-format shadow array
//   and is never realistically a storage image). `allowStorageImageAccess`
//   is only ever meaningful when `hasDepth == false` - a debug-assert
//   guards against the combination `hasDepth && allowStorageImageAccess`,
//   since depth-format storage images are not realistically supported and
//   there is no legitimate caller intent to trust here.
// - `arrayLayers` (and width/height) are clamped to at least 1
//   UNCONDITIONALLY, in BOTH debug and release builds - a plain ternary
//   clamp, mirroring VolumeTexture.cpp's own `width > 0 ? width : 1`
//   pattern verbatim, never a debug-assert (an out-of-range value here is
//   just a degenerate-but-harmless 1-layer array, not a Vulkan API
//   violation).
// - Single mip level only - no mip-mapping support, matching every other
//   texture type in this engine today.
//
// SAMPLER POLICY - deliberately NOT a copy of VolumeTexture's trilinear,
// clamp-to-edge sampler (that fits VolumeTexture's own floating-point
// froxel-volume use case). This engine has NO existing depth-COMPARISON
// sampler anywhere today (no `compareEnable`/`VK_COMPARE_OP_*` usage exists
// anywhere in `src/`) - the engine's one pre-existing depth-sampling case
// (RenderTexture's own companion DepthBuffer, sampled via
// RenderTexture::Sampler()) already gets away with a plain linear,
// non-comparison sampler for BOTH its color and depth halves, and
// TextureArray2D follows that SAME existing precedent here, for BOTH the
// `hasDepth == true` and `hasDepth == false` case (one shared sampler
// policy either way: VK_FILTER_LINEAR min/mag, VK_SAMPLER_MIPMAP_MODE_LINEAR,
// clamp-to-edge all 3 axes, minLod=0, maxLod=0.25, compareEnable = VK_FALSE
// always). Whether a future shadow-cascade-consuming block wants a real
// hardware PCF comparison sampler (`compareEnable = VK_TRUE`,
// VK_COMPARE_OP_LESS) instead is a real, legitimate, OPEN design question -
// deciding it is explicitly OUT OF SCOPE for this class, exactly like the
// per-layer-view gap below.
//
// IMPORTANT, STATED PLAINLY: this class creates exactly ONE VkImageView,
// covering the WHOLE array/cube. There is no per-layer/per-face VkImageView
// anywhere in this class, and therefore no way for a graphics pass to
// render into one specific layer/face of this resource yet - a real
// cascade-shadow pass (rendering into layer i only) needs a
// `baseArrayLayer = i, layerCount = 1, viewType = VK_IMAGE_VIEW_TYPE_2D`
// view, which does not exist after this class. This is a deliberate,
// acknowledged, DEFERRED gap for a future block, not an oversight.
//
// Construct via Renderer::CreateTextureArray()/GpuResourceFactory::
// CreateTextureArray() (never directly) - exactly like every other GPU
// resource this engine creates.
class TextureArray2D {
public:
    // debugName is optional and purely cosmetic - same convention as
    // Buffer/RenderTexture/Texture2D/VolumeTexture's own debugName parameter
    // (see Buffer.h's doc comment for the full "who actually stores this"
    // story).
    //
    // `format` must already be a concrete, resolved VkFormat by the time it
    // reaches this constructor (never VK_FORMAT_UNDEFINED) - resolution
    // happens one layer up, in Renderer::CreateTextureArray().
    //
    // `hasDepth`/`isCubemap` have no default here - the caller
    // (GpuResourceFactory::CreateTextureArray()) always supplies both
    // explicitly, already resolved/validated.
    TextureArray2D(VmaAllocator allocator, std::shared_ptr<GpuMemoryTracker> tracker, VkDevice device, int width,
        int height, int arrayLayers, VkFormat format, bool hasDepth, bool isCubemap,
        bool allowStorageImageAccess = false, const char* debugName = nullptr);
    ~TextureArray2D();

    TextureArray2D(const TextureArray2D&) = delete;
    TextureArray2D& operator=(const TextureArray2D&) = delete;

    TextureArray2D(TextureArray2D&& other) noexcept;
    TextureArray2D& operator=(TextureArray2D&& other) noexcept;

    VkImage Image() const noexcept { return m_image; }
    // The one and only VkImageView this class creates - covers the WHOLE
    // array/cube (see this class's own header comment).
    VkImageView View() const noexcept { return m_imageView; }
    // Sampler suitable for reading this array/cubemap in a shader once
    // whichever pass writes it has finished - plain linear, clamp-to-edge,
    // no comparison (see this class's own header comment for why).
    VkSampler Sampler() const noexcept { return m_sampler; }
    int Width() const noexcept { return m_width; }
    int Height() const noexcept { return m_height; }
    std::uint32_t ArrayLayers() const noexcept { return m_arrayLayers; }
    VkFormat Format() const noexcept { return m_format; }
    bool HasDepth() const noexcept { return m_hasDepth; }
    bool IsCubemap() const noexcept { return m_isCubemap; }
    bool AllowsStorageImageAccess() const noexcept { return m_allowStorageImageAccess; }

    // Handle into the GpuMemoryTracker this TextureArray2D is registered
    // with - valid for this TextureArray2D's entire lifetime,
    // kInvalidGpuResourceHandle once moved-from.
    GpuResourceHandle Handle() const noexcept { return m_handle; }

    // Plain, non-owning view of this resource - see TextureArrayTarget.h.
    TextureArrayTarget Target() const noexcept;

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
    std::uint32_t m_arrayLayers = 1;
    VkFormat m_format = VK_FORMAT_UNDEFINED;
    bool m_hasDepth = false;
    bool m_isCubemap = false;
    bool m_allowStorageImageAccess = false;
};

} // namespace gte
