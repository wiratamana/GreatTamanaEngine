#pragma once

#include "RenderTarget.h"
#include "Vulkan/VulkanAllocator.h"

namespace gte {

// RAII wrapper around an off-screen, sampleable color-attachment Vulkan
// image: owns the VkImage, its backing VmaAllocation (GPU memory, sub-
// allocated by VMA - see Vulkan/VulkanAllocator.h), a VkImageView, and a
// VkSampler for its entire lifetime - created in the constructor, destroyed
// in the destructor. This is the engine's "camera render target" primitive
// (a Unity-style RenderTexture). Intended uses:
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
// lockstep with presentation.
class RenderTexture {
public:
    // format defaults to a plain 8-bit BGRA format, matching the common
    // swapchain format this engine already prefers (see
    // VulkanSwapchain.cpp's ChooseSurfaceFormat) - override if a caller
    // needs something else (e.g. an HDR intermediate format later).
    RenderTexture(VmaAllocator allocator, VkDevice device,
        int width, int height, VkFormat format = VK_FORMAT_B8G8R8A8_UNORM);
    ~RenderTexture();

    RenderTexture(const RenderTexture&) = delete;
    RenderTexture& operator=(const RenderTexture&) = delete;

    RenderTexture(RenderTexture&& other) noexcept;
    RenderTexture& operator=(RenderTexture&& other) noexcept;

    // Destroys and recreates the underlying image/view/sampler at a new
    // size (e.g. an Editor panel being resized). Contents are undefined
    // afterwards - Renderer::RenderOffscreen() always clears before
    // drawing anyway, so this is never a problem in practice.
    void Resize(int width, int height);

    // Describes this texture as a color-attachment render target, for
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

private:
    void Create(int width, int height);
    void Destroy() noexcept;

    VmaAllocator m_allocator = VK_NULL_HANDLE;
    VkDevice m_device = VK_NULL_HANDLE;
    VkFormat m_format = VK_FORMAT_B8G8R8A8_UNORM;

    VkImage m_image = VK_NULL_HANDLE;
    VmaAllocation m_allocation = VK_NULL_HANDLE;
    VkImageView m_imageView = VK_NULL_HANDLE;
    VkSampler m_sampler = VK_NULL_HANDLE;
    VkExtent2D m_extent{};
};

} // namespace gte
