#pragma once

#include "Vulkan/VulkanDevice.h"
#include "Vulkan/VulkanInstance.h"
#include "Vulkan/VulkanSurface.h"
#include "Vulkan/VulkanSwapchain.h"

#include <array>
#include <cstdint>
#include <vector>

namespace gte {

class Window;

// Owns the entire Vulkan pipeline for a Window: instance, surface, device,
// swapchain, command buffers, and the per-frame synchronization objects
// needed to clear the swapchain and present it. Acquired piece-by-piece in
// the constructor (each piece itself RAII-owned - see Vulkan/*), released
// automatically in reverse order in the destructor.
//
// Public API is intentionally still just Clear()/Present(), matching the
// previous SDL_Renderer-backed version, so Game/main code did not need to
// change for this swap. Clear() only records the desired clear color;
// the actual clear happens as part of Present() (Vulkan has no equivalent
// of an immediate "clear now" call outside of a recorded command buffer).
class Renderer {
public:
    explicit Renderer(Window& window);
    ~Renderer();

    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;

    Renderer(Renderer&& other) noexcept;
    Renderer& operator=(Renderer&& other) noexcept;

    // Sets the color the swapchain image will be cleared to on the next
    // Present() call (0-255 per channel).
    void Clear(std::uint8_t r = 0, std::uint8_t g = 0, std::uint8_t b = 0, std::uint8_t a = 255);

    // Acquires the next swapchain image, records+submits a command buffer
    // that clears it to the last Clear() color, and presents it. Handles
    // swapchain recreation transparently (resize, or out-of-date/suboptimal
    // results from the driver).
    void Present();

    // Call when the window has been resized (e.g. from a WindowResized
    // event) - marks the swapchain dirty so the next Present() rebuilds it
    // at the new size instead of presenting into a stale-sized swapchain.
    void OnResize(int width, int height);

private:
    static constexpr std::uint32_t kMaxFramesInFlight = 2;

    void CreateCommandObjects();
    void CreateSyncObjects();
    void DestroySyncObjects() noexcept;
    void RecreateSwapchain();
    void RecordClearCommands(VkCommandBuffer cmd, std::uint32_t imageIndex);

    VulkanInstance m_instance;
    VulkanSurface m_surface;
    VulkanDevice m_device;
    VulkanSwapchain m_swapchain;

    VkCommandPool m_commandPool = VK_NULL_HANDLE;
    std::array<VkCommandBuffer, kMaxFramesInFlight> m_commandBuffers{};
    std::array<VkSemaphore, kMaxFramesInFlight> m_imageAvailableSemaphores{};
    std::array<VkFence, kMaxFramesInFlight> m_inFlightFences{};
    // One per swapchain image (not per frame-in-flight) - required so a
    // semaphore is never re-signaled while a previous present using it may
    // still be in flight. See VulkanSwapchain::ImageCount().
    std::vector<VkSemaphore> m_renderFinishedSemaphores;

    std::uint32_t m_currentFrame = 0;

    float m_clearColor[4] = { 0.0f, 0.0f, 0.0f, 1.0f };

    bool m_resizeRequested = false;
    int m_pendingWidth = 0;
    int m_pendingHeight = 0;
};

} // namespace gte
