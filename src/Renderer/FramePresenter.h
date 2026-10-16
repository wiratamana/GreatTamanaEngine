#pragma once

#include "DepthBuffer.h"
#include "FrameRecorder.h"
#include "GpuTimingService.h"
#include "Memory/GpuMemoryTracker.h"
#include "RenderGraph/RenderGraphTypes.h"
#include "RenderTarget.h"
#include "RenderTexture.h"
#include "SwapchainCaptureService.h"
#include "Vulkan/VulkanAllocator.h"
#include "Vulkan/VulkanFrameSync.h"
#include "Vulkan/VulkanSwapchain.h"

#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <vector>

namespace gte::rg {
class CommandBuffer;
class RenderGraph;
class RenderGraphBuilder;
} // namespace gte::rg

namespace gte {

class VulkanDevice;


// Owns the swapchain, frame-sync objects, command buffers and per-swapchain-image depth buffers; it talks to
// acquire, submit and present. Does not own the device, surface, queues or allocator.
// It points into the Renderer's VulkanDevice, so it is not movable.
class FramePresenter {
public:
    // `vulkanDevice` must outlive this object; all graphics-queue submits go through it.
    FramePresenter(VkPhysicalDevice physicalDevice, VkDevice device, VkSurfaceKHR surface,
        std::uint32_t graphicsQueueFamily, std::uint32_t presentQueueFamily, VkQueue graphicsQueue,
        VkQueue presentQueue, int width, int height, VmaAllocator allocator, VkFormat depthFormat,
        std::shared_ptr<GpuMemoryTracker> memoryTracker, std::shared_ptr<GpuTimingService> gpuTiming,
        VulkanDevice& vulkanDevice);
    ~FramePresenter();

    FramePresenter(const FramePresenter&) = delete;
    FramePresenter& operator=(const FramePresenter&) = delete;

    // Deleted because m_vulkanDevice points into the owning Renderer and would dangle after a move.
    FramePresenter(FramePresenter&&) = delete;
    FramePresenter& operator=(FramePresenter&&) = delete;

    // The color format the swapchain negotiated.
    VkFormat ColorFormat() const noexcept { return m_swapchain.ImageFormat(); }

    std::uint32_t ImageCount() const noexcept { return m_swapchain.ImageCount(); }

    static constexpr std::uint32_t FramesInFlight() noexcept { return kFramesInFlight; }

    void OnResize(int width, int height);

    // See Renderer::RenderOffscreen(). `timingSlot` brackets frameRecorder.RecordFrame() with GPU timestamps;
    // nullopt leaves the query slots untouched.
    DrawStats RenderOffscreen(FrameRecorder& frameRecorder, RenderTexture& target,
        std::optional<GpuTimingSlot> timingSlot, const std::array<float, 4>& clearColor,
        const std::function<void(rg::CommandBuffer&)>& recordExtra);

    // Waits on the offscreen command buffer's fence, resets and begins it, and returns it for the caller to
    // record render graph passes into. Pair with EndOffscreenRecording().
    VkCommandBuffer BeginOffscreenRecording();

    // Ends and submits the offscreen command buffer and blocks until the GPU finishes.
    void EndOffscreenRecording();

    // See Renderer::PresentViaRenderGraph(). Returns false without touching `graph` when no swapchain frame can
    // be recorded (minimized window, pending resize, just-recreated swapchain); otherwise runs a full
    // acquire, record, submit and present cycle.
    bool PresentViaRenderGraph(rg::RenderGraph& graph, bool needsSwapchainDepth,
        const std::function<std::vector<rg::TextureHandle>(rg::RenderGraphBuilder&, rg::TextureHandle)>& build);

    // Requests a one-shot capture of the swapchain image, fulfilled two frames later with no added GPU stall.
    // No-op while one is pending.
    void RequestSwapchainCapture();

    // Returns and clears the capture that PresentViaRenderGraph() completed during its own latest call, or
    // nullopt. It cannot forward to SwapchainCaptureService directly: that needs the frame-in-flight index of
    // a specific moment inside PresentViaRenderGraph().
    std::optional<CapturedSwapchainPixels> TakeLastCompletedSwapchainCapture();

private:
    static constexpr std::uint32_t kFramesInFlight = 2;

    void CreateCommandObjects();
    void CreateDepthBuffers();
    // Creates m_depthBuffers on first need; no-op once they exist.
    void EnsureDepthBuffersForSwapchain();
    void RecreateSwapchain();
    void Destroy() noexcept;

    VkDevice m_device = VK_NULL_HANDLE;
    VkQueue m_graphicsQueue = VK_NULL_HANDLE;
    VkQueue m_presentQueue = VK_NULL_HANDLE;
    std::uint32_t m_graphicsQueueFamily = 0;

    // The one choke point for graphics-queue submits; not owned.
    VulkanDevice* m_vulkanDevice = nullptr;

    // Needed to rebuild m_depthBuffers when the swapchain is recreated; not owned.
    VmaAllocator m_allocator = VK_NULL_HANDLE;
    VkFormat m_depthFormat = VK_FORMAT_UNDEFINED;
    std::shared_ptr<GpuMemoryTracker> m_memoryTracker;

    // The same service the Renderer owns; RenderOffscreen() brackets passes with it.
    std::shared_ptr<GpuTimingService> m_gpuTiming;

    VulkanSwapchain m_swapchain;
    // Per-frame and per-image sync objects plus the offscreen fence; render-finished semaphores are rebuilt on
    // swapchain recreation.
    VulkanFrameSync m_frameSync;

    // One DepthBuffer per swapchain image, empty until EnsureDepthBuffersForSwapchain() needs them: the
    // swapchain pass usually only draws UI, which is never depth-tested. Once created they are kept and rebuilt
    // with the swapchain, but a recreation never creates them from scratch.
    std::vector<DepthBuffer> m_depthBuffers;

    VkCommandPool m_commandPool = VK_NULL_HANDLE;
    std::array<VkCommandBuffer, kFramesInFlight> m_commandBuffers{};

    // Separate from the per-frame buffers so offscreen rendering never contends with the swapchain's.
    VkCommandBuffer m_offscreenCommandBuffer = VK_NULL_HANDLE;

    std::uint32_t m_currentFrame = 0;

    bool m_resizeRequested = false;
    int m_pendingWidth = 0;
    int m_pendingHeight = 0;

    // See RequestSwapchainCapture()/TakeLastCompletedSwapchainCapture().
    SwapchainCaptureService m_swapchainCapture;

    // Set only inside PresentViaRenderGraph(), right after its per-slot fence wait.
    std::optional<CapturedSwapchainPixels> m_lastCompletedCapture;
};

} // namespace gte
