#pragma once

#include <volk.h>

#include <cstdint>
#include <vector>

namespace gte {

// RAII wrapper around every semaphore/fence Renderer needs to synchronize
// frames:
//   - `framesInFlight` sets of (image-available semaphore, in-flight fence)
//     for Present()'s swapchain acquire/submit pipeline.
//   - One render-finished semaphore per swapchain image (sized off the
//     swapchain's own image count, NOT framesInFlight - see
//     RenderFinishedSemaphore() below for why those can't share a count).
//   - One dedicated fence for RenderOffscreen()'s separate, synchronous
//     submit path (see Renderer.h's m_offscreenCommandBuffer comment for
//     why that path doesn't reuse the per-frame objects above).
//
// Created in the constructor, destroyed in the destructor - same
// convention as VulkanSwapchain/VulkanAllocator/etc. Does NOT own the
// VkDevice passed in; it must outlive this object.
//
// RecreateRenderFinishedSemaphores() is the one piece that must be redone
// whenever the swapchain itself is recreated (its image count can change
// across a resize) - see Renderer::RecreateSwapchain(). The per-frame
// objects and the offscreen fence never need to change after construction.
class VulkanFrameSync {
public:
    VulkanFrameSync(VkDevice device, std::uint32_t framesInFlight, std::uint32_t swapchainImageCount);
    ~VulkanFrameSync();

    VulkanFrameSync(const VulkanFrameSync&) = delete;
    VulkanFrameSync& operator=(const VulkanFrameSync&) = delete;

    VulkanFrameSync(VulkanFrameSync&& other) noexcept;
    VulkanFrameSync& operator=(VulkanFrameSync&& other) noexcept;

    // Rebuilds just the per-swapchain-image render-finished semaphores, for
    // a swapchain that was just recreated with a possibly-different image
    // count (see VulkanSwapchain::Recreate()). The per-frame objects
    // (image-available semaphores/in-flight fences) and the offscreen
    // fence are untouched.
    void RecreateRenderFinishedSemaphores(std::uint32_t swapchainImageCount);

    VkSemaphore ImageAvailableSemaphore(std::uint32_t frameIndex) const { return m_imageAvailableSemaphores[frameIndex]; }
    VkFence InFlightFence(std::uint32_t frameIndex) const { return m_inFlightFences[frameIndex]; }

    // One per swapchain image (not per frame-in-flight) - required so a
    // semaphore is never re-signaled while a previous present using it may
    // still be in flight. See VulkanSwapchain::ImageCount().
    VkSemaphore RenderFinishedSemaphore(std::uint32_t imageIndex) const { return m_renderFinishedSemaphores[imageIndex]; }

    VkFence OffscreenFence() const noexcept { return m_offscreenFence; }

private:
    void CreatePerFrameObjects(std::uint32_t framesInFlight);
    void CreateRenderFinishedSemaphores(std::uint32_t swapchainImageCount);
    void DestroyRenderFinishedSemaphores() noexcept;
    void Destroy() noexcept;

    VkDevice m_device = VK_NULL_HANDLE;

    std::vector<VkSemaphore> m_imageAvailableSemaphores;
    std::vector<VkFence> m_inFlightFences;
    std::vector<VkSemaphore> m_renderFinishedSemaphores;
    VkFence m_offscreenFence = VK_NULL_HANDLE;
};

} // namespace gte
