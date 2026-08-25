#pragma once

#include "DepthBuffer.h"
#include "FrameRecorder.h"
#include "Memory/GpuMemoryTracker.h"
#include "RenderTarget.h"
#include "RenderTexture.h"
#include "Vulkan/VulkanAllocator.h"
#include "Vulkan/VulkanFrameSync.h"
#include "Vulkan/VulkanSwapchain.h"

#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <vector>

namespace gte {

// Owns the swapchain, every per-frame/per-image synchronization object (see
// VulkanFrameSync), the command buffers (one set per frame-in-flight for
// Present(), plus a dedicated one for RenderOffscreen() - see
// VulkanFrameSync's class comment for why the offscreen path doesn't share
// the per-frame objects), and one DepthBuffer PER SWAPCHAIN IMAGE (see
// DepthBuffer.h, allocated LAZILY - see m_depthBuffers below) needed to
// actually record and submit a frame's Vulkan work. This is the part of the
// old monolithic Renderer that literally talks to
// vkAcquireNextImageKHR/vkQueueSubmit/vkQueuePresentKHR -
// Present()/RenderOffscreen() below are moved here verbatim from the old
// Renderer::Present()/RenderOffscreen()/RecreateSwapchain().
//
// Depth buffers are indexed by SWAPCHAIN IMAGE INDEX (the same index
// vkAcquireNextImageKHR returns), not by frame-in-flight slot - this is
// deliberate, not arbitrary: vkAcquireNextImageKHR's own semaphore already
// guarantees a given swapchain image (and therefore whatever is paired with
// it) is genuinely free to write again before returning that index (see
// VulkanFrameSync::RenderFinishedSemaphore()'s own comment for the exact
// same reasoning applied to render-finished semaphores) - reusing that same
// guarantee for depth buffers avoids having to separately reason about
// frames-in-flight potentially overlapping on the GPU (kFramesInFlight can
// legitimately differ from the swapchain's own image count).
//
// Does NOT own the VkPhysicalDevice/VkDevice/VkSurfaceKHR/VkQueue/
// VmaAllocator handles passed in - all must outlive this object (same
// convention as VulkanSwapchain itself not owning the device/surface passed
// to it). DOES own its swapchain, frame-sync objects, per-swapchain-image
// DepthBuffers, and command pool/buffers for its entire lifetime.
//
// Present()/RenderOffscreen() both take a FrameRecorder& parameter rather
// than storing one, so this class never holds a reference/pointer to
// anything outside itself across calls - the only reason Renderer's own
// move constructor can stay a plain defaulted member-wise move (see
// Renderer.h) without risking a dangling cross-collaborator reference after
// a move.
class FramePresenter {
public:
    FramePresenter(VkPhysicalDevice physicalDevice, VkDevice device, VkSurfaceKHR surface,
        std::uint32_t graphicsQueueFamily, std::uint32_t presentQueueFamily, VkQueue graphicsQueue,
        VkQueue presentQueue, int width, int height, VmaAllocator allocator, VkFormat depthFormat,
        std::shared_ptr<GpuMemoryTracker> memoryTracker);
    ~FramePresenter();

    FramePresenter(const FramePresenter&) = delete;
    FramePresenter& operator=(const FramePresenter&) = delete;

    FramePresenter(FramePresenter&& other) noexcept;
    // Assumes the caller (Renderer::operator=(&&)) has already waited for
    // the device to go idle before this runs, since it may destroy this
    // presenter's current swapchain/command pool - see Renderer.cpp. This
    // class has no standalone way to know when that wait is safe to skip,
    // so it doesn't attempt it itself.
    FramePresenter& operator=(FramePresenter&& other) noexcept;

    // The color format this swapchain actually negotiated at runtime - see
    // Renderer::ColorFormat().
    VkFormat ColorFormat() const noexcept { return m_swapchain.ImageFormat(); }

    // Current swapchain image count - see Renderer::GetVulkanContextInfo().
    std::uint32_t ImageCount() const noexcept { return m_swapchain.ImageCount(); }

    // How many frames this presenter keeps in flight - see
    // Renderer::GetVulkanContextInfo().
    static constexpr std::uint32_t FramesInFlight() noexcept { return kFramesInFlight; }

    // See Renderer::OnResize().
    void OnResize(int width, int height);

    // See Renderer::Present(). frameRecorder supplies the clear color and
    // queued Submit() draws recorded into this frame - see FrameRecorder.h.
    // Returns std::nullopt on a call that recorded NOTHING this time (a
    // minimized window, a still-pending resize, or a just-recreated
    // swapchain) - std::nullopt is the correct, honest signal that the
    // "Present" GpuPass genuinely did not run this frame, distinct from
    // "ran and recorded zero queued draws" (a real DrawStats{0, 0}). See
    // PHASE3_DRAW_CALL_TRIANGLE_COUNT_STRATEGY_v2.md, Step 3.3.
    std::optional<DrawStats> Present(FrameRecorder& frameRecorder, const std::function<void(VkCommandBuffer)>& recordExtra);

    // See Renderer::RenderOffscreen(). Unlike Present() above, this
    // function has no early-return path today - it always has a real
    // DrawStats to return, never std::nullopt.
    DrawStats RenderOffscreen(FrameRecorder& frameRecorder, RenderTexture& target,
        const std::function<void(VkCommandBuffer)>& recordExtra);

private:
    static constexpr std::uint32_t kFramesInFlight = 2;

    void CreateCommandObjects();
    void CreateDepthBuffers();
    // Lazily creates m_depthBuffers (via CreateDepthBuffers()) the first
    // time they're actually needed - a no-op if they already exist. See
    // Present()/m_depthBuffers' own comment for why "actually needed" isn't
    // simply "always", unlike a RenderTexture's own companion DepthBuffer.
    void EnsureDepthBuffersForSwapchain();
    void RecreateSwapchain();
    void Destroy() noexcept;

    VkDevice m_device = VK_NULL_HANDLE;
    VkQueue m_graphicsQueue = VK_NULL_HANDLE;
    VkQueue m_presentQueue = VK_NULL_HANDLE;
    std::uint32_t m_graphicsQueueFamily = 0;

    // Needed to (re)build m_depthBuffers below whenever the swapchain
    // itself is (re)created - not owned (must outlive this object, same
    // convention as everything else this class doesn't own).
    VmaAllocator m_allocator = VK_NULL_HANDLE;
    VkFormat m_depthFormat = VK_FORMAT_UNDEFINED;
    std::shared_ptr<GpuMemoryTracker> m_memoryTracker;

    VulkanSwapchain m_swapchain;
    // Per-frame/per-image semaphores/fences, and the dedicated offscreen
    // fence - see VulkanFrameSync.h. Its per-swapchain-image semaphores are
    // rebuilt (RecreateRenderFinishedSemaphores()) whenever the swapchain
    // itself is recreated - see RecreateSwapchain().
    VulkanFrameSync m_frameSync;

    // One DepthBuffer per swapchain image (see the class comment for why
    // this is indexed by swapchain image index, not frame-in-flight slot).
    // Deliberately EMPTY until EnsureDepthBuffersForSwapchain() first
    // creates them (see Present()) - unlike a RenderTexture's own companion
    // DepthBuffer (always real geometry drawn into it), the swapchain's
    // Present() pass, in the common Editor case, draws NOTHING but Dear
    // ImGui's own (never depth-tested) chrome - Game's actual geometry is
    // consumed entirely by the two RenderOffscreen() calls into "Game"/
    // "Scene" before Present() ever runs (see Application::Run()). Rather
    // than unconditionally pay ~1 window-resolution-sized depth image PER
    // swapchain image for a pass that then never depth-tests anything, these
    // are only ever allocated once a frame is actually found to need one -
    // see FrameRecorder::HasQueuedDraws(). Once created, kept alive for the
    // rest of this FramePresenter's lifetime and rebuilt alongside the
    // swapchain in RecreateSwapchain() (same as m_frameSync's render-
    // finished semaphores) - but RecreateSwapchain() itself only rebuilds
    // them if they already existed, never creates them from scratch.
    std::vector<DepthBuffer> m_depthBuffers;

    VkCommandPool m_commandPool = VK_NULL_HANDLE;
    std::array<VkCommandBuffer, kFramesInFlight> m_commandBuffers{};

    // Separate command buffer for RenderOffscreen() (paired with
    // m_frameSync.OffscreenFence()), so off-screen rendering (Editor
    // panels) never contends with the swapchain's own per-frame-in-flight
    // command buffers/fences.
    VkCommandBuffer m_offscreenCommandBuffer = VK_NULL_HANDLE;

    std::uint32_t m_currentFrame = 0;

    bool m_resizeRequested = false;
    int m_pendingWidth = 0;
    int m_pendingHeight = 0;
};

} // namespace gte
