#pragma once

#include <volk.h>

#include <cstdint>
#include <vector>

namespace gte {

// RAII owner of the synchronization objects frames need: per frame in flight an image-available semaphore, an
// in-flight fence and an offscreen slot fence; one render-finished semaphore per swapchain image; and one fence for
// the synchronous offscreen path. Does not own the VkDevice, which must outlive it.
class VulkanFrameSync {
public:
    VulkanFrameSync(VkDevice device, std::uint32_t framesInFlight, std::uint32_t swapchainImageCount);
    ~VulkanFrameSync();

    VulkanFrameSync(const VulkanFrameSync&) = delete;
    VulkanFrameSync& operator=(const VulkanFrameSync&) = delete;

    VulkanFrameSync(VulkanFrameSync&& other) noexcept;
    VulkanFrameSync& operator=(VulkanFrameSync&& other) noexcept;

    // Rebuilds only the per-swapchain-image render-finished semaphores after a swapchain recreate,
    // whose image count may change.
    void RecreateRenderFinishedSemaphores(std::uint32_t swapchainImageCount);

    VkSemaphore ImageAvailableSemaphore(std::uint32_t frameIndex) const { return m_imageAvailableSemaphores[frameIndex]; }
    VkFence InFlightFence(std::uint32_t frameIndex) const { return m_inFlightFences[frameIndex]; }

    // One per swapchain image, not per frame in flight, so a semaphore is never re-signaled while a
    // present using it may still be pending.
    VkSemaphore RenderFinishedSemaphore(std::uint32_t imageIndex) const { return m_renderFinishedSemaphores[imageIndex]; }

    // The legacy fence of the synchronous offscreen path (one command buffer, waited right after submit).
    VkFence OffscreenFence() const noexcept { return m_offscreenFence; }

    // One fence per offscreen ring slot, created signaled. Signals when the frame submitted into the slot is done.
    VkFence OffscreenSlotFence(std::uint32_t slot) const { return m_offscreenSlotFences[slot]; }

private:
    void CreatePerFrameObjects(std::uint32_t framesInFlight);
    void CreateRenderFinishedSemaphores(std::uint32_t swapchainImageCount);
    void DestroyRenderFinishedSemaphores() noexcept;
    void Destroy() noexcept;

    VkDevice m_device = VK_NULL_HANDLE;

    std::vector<VkSemaphore> m_imageAvailableSemaphores;
    std::vector<VkFence> m_inFlightFences;
    std::vector<VkFence> m_offscreenSlotFences;
    std::vector<VkSemaphore> m_renderFinishedSemaphores;
    VkFence m_offscreenFence = VK_NULL_HANDLE;
};

} // namespace gte
