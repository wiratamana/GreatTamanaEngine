#pragma once

#include "DepthBuffer.h"
#include "Memory/GpuMemoryTracker.h"
#include "RenderTarget.h"
#include "Vulkan/VulkanAllocator.h"

#include <memory>

namespace gte {

// RAII wrapper around an off-screen, sampleable color-attachment Vulkan
// image: owns the VkImage, its backing VmaAllocation (GPU memory, sub-
// allocated by VMA - see Vulkan/VulkanAllocator.h), a VkImageView, a
// VkSampler, and its own companion DepthBuffer (see DepthBuffer.h) for its
// entire lifetime - created in the constructor, destroyed in the
// destructor. This is the engine's "camera render target" primitive (a
// Unity-style RenderTexture). Intended uses:
//
//   - Editor "Game"/"Scene" panels: a camera renders into one of these via
//     Renderer::RenderOffscreen(), then the Editor wraps it in an ImGui
//     descriptor set and displays it inside an ImGui::Image() panel. A
//     final/release build (no Editor compiled in) instead renders the same
//     scene straight into the swapchain via Renderer::Present(), fullscreen.
//   - Future off-screen effects (shadow maps, post-processing chains, etc).
//
// Does NOT own the VmaAllocator/VkDevice passed in - both must outlive this
// texture. Not tied to the swapchain or its frame-in-flight count: a
// RenderTexture is rendered into synchronously, on demand, via
// Renderer::RenderOffscreen() (see Renderer.h) rather than every frame in
// lockstep with presentation - which is also why one shared DepthBuffer per
// RenderTexture is safe (no frames-in-flight overlap to race against,
// unlike the swapchain's own per-swapchain-image DepthBuffers - see
// FramePresenter.h).
//
// Registers itself (and its companion DepthBuffer) with a GpuMemoryTracker
// (see Memory/GpuMemoryTracker.h) every time its underlying image is
// (re)created - constructor AND Resize() - so the engine's live memory
// picture always reflects the CURRENT actual allocation, never a stale
// snapshot from whenever this RenderTexture was first constructed. See
// AGENTS.md ("GPU resource memory tracking").
class RenderTexture {
public:
    // format defaults to a plain 8-bit BGRA format, matching the common
    // swapchain format this engine already prefers (see
    // VulkanSwapchain.cpp's ChooseSurfaceFormat) - override if a caller
    // needs something else (e.g. an HDR intermediate format later).
    // depthFormat should always be exactly Renderer::DepthFormat() - see
    // AGENTS.md ("Render Target Format Matching").
    //
    // debugName/depthDebugName are optional and Editor-only (see
    // literal) if provided: this RenderTexture stores each pointer itself
    // (not a copy) so Resize() can re-attach the same names to the fresh
    // handles it creates. debugName names the color image; depthDebugName
    // separately names its companion DepthBuffer (e.g. "GameView" /
    // "GameViewDepth") - left null (the default) if the depth side isn't
    // worth naming individually.
    //
    // allowStorageImageAccess (default false - every existing call site is
    // unaffected) opts this RenderTexture's color image into
    // VK_IMAGE_USAGE_STORAGE_BIT, the Vulkan mechanism behind an
    // `RWTexture` (see COMPUTE_PHASE1_RESOURCE_VOCABULARY_STRATEGY_v2.md) -
    // e.g. a compute shader `imageStore`s directly into it. The CALLER
    // (GpuResourceFactory::CreateRenderTexture(), never this constructor
    // itself - see FormatCapabilities.h) is responsible for first
    // confirming `format` actually supports
    // VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT via SupportsStorageImageUsage()
    // and throwing loudly if not; this constructor unconditionally trusts
    // that check already happened and just sets the usage flag. The flag
    // is remembered as a member (not just a constructor-local bool) so a
    // later Resize() (Destroy() + Create()) re-applies the exact same
    // usage flags at the new size, without needing to re-validate the
    // format's capability (format never changes across a Resize()).
    RenderTexture(VmaAllocator allocator, std::shared_ptr<GpuMemoryTracker> tracker, VkDevice device, int width,
        int height, VkFormat format = VK_FORMAT_B8G8R8A8_UNORM,
        VkFormat depthFormat = VK_FORMAT_D32_SFLOAT, const char* debugName = nullptr,
        const char* depthDebugName = nullptr, bool allowStorageImageAccess = false);
    ~RenderTexture();

    RenderTexture(const RenderTexture&) = delete;
    RenderTexture& operator=(const RenderTexture&) = delete;

    RenderTexture(RenderTexture&& other) noexcept;
    RenderTexture& operator=(RenderTexture&& other) noexcept;

    // Destroys and recreates the underlying color image/view/sampler AND
    // the companion DepthBuffer at a new size (e.g. an Editor panel being
    // resized). Contents are undefined afterwards - Renderer::RenderOffscreen()
    // always clears before drawing anyway, so this is never a problem in
    // practice. This is a genuinely new VMA allocation - Handle() returns a
    // DIFFERENT handle after this call (the old one is untracked, a new one
    // tracked with the new size) - never assume a RenderTexture's handle is
    // stable across a Resize().
    void Resize(int width, int height);

    // Describes this texture (color + depth) as a render target, for
    // Renderer::RenderOffscreen() to draw into.
    RenderTarget Target() const noexcept;

    VkImage Image() const noexcept { return m_image; }
    VkImageView View() const noexcept { return m_imageView; }
    // Sampler suitable for reading this texture in a shader/ImGui
    // descriptor once Renderer::RenderOffscreen() has finished drawing
    // into it (texture is left in VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
    // - see Renderer::RenderOffscreen).
    VkSampler Sampler() const noexcept { return m_sampler; }
    VkExtent2D Extent() const noexcept { return m_extent; }
    VkFormat Format() const noexcept { return m_format; }

    // Handle into the GpuMemoryTracker this RenderTexture's COLOR image is
    // registered with - valid until the next Resize() (see above) or
    // destruction. The companion DepthBuffer is tracked separately under
    // its own handle (see DepthBuffer::Handle()) - not exposed here since
    // nothing outside this class currently needs it individually.
    GpuResourceHandle Handle() const noexcept { return m_handle; }

    // Whether this RenderTexture's color image was created with
    // VK_IMAGE_USAGE_STORAGE_BIT (i.e. an `RWTexture`, see
    // COMPUTE_PHASE1_RESOURCE_VOCABULARY_STRATEGY_v2.md) - a future
    // descriptor-set builder (Phase 3) needs this to know whether this
    // texture is even eligible to be bound as a storage image.
    bool AllowsStorageImageAccess() const noexcept { return m_allowStorageImageAccess; }

private:
    void Create(int width, int height);
    void Destroy() noexcept;

    VmaAllocator m_allocator = VK_NULL_HANDLE;
    std::shared_ptr<GpuMemoryTracker> m_tracker;
    GpuResourceHandle m_handle;
    const char* m_debugName = nullptr;
    const char* m_depthDebugName = nullptr;

    VkDevice m_device = VK_NULL_HANDLE;
    VkFormat m_format = VK_FORMAT_B8G8R8A8_UNORM;
    VkFormat m_depthFormat = VK_FORMAT_D32_SFLOAT;
    bool m_allowStorageImageAccess = false;

    VkImage m_image = VK_NULL_HANDLE;
    VmaAllocation m_allocation = VK_NULL_HANDLE;
    VkImageView m_imageView = VK_NULL_HANDLE;
    VkSampler m_sampler = VK_NULL_HANDLE;
    VkExtent2D m_extent{};

    // This RenderTexture's own companion depth buffer - see the class
    // comment for why one shared DepthBuffer per RenderTexture (rather than
    // several, as the swapchain needs - see FramePresenter.h) is safe here.
    // std::unique_ptr so RenderTexture can still be constructed as a plain
    // aggregate-like RAII object without needing DepthBuffer to be
    // default-constructible - see Create()/Destroy().
    std::unique_ptr<DepthBuffer> m_depthBuffer;
};

} // namespace gte
