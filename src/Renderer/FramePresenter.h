#pragma once

#include "AbandonedWork.h"
#include "FrameRecorder.h"
#include "GpuFrameRing.h"
#include "GpuTiming.h"
#include "Memory/GpuMemoryTracker.h"
#include "OffscreenFrameProtocol.h"
#include "OffscreenFrameTicket.h"
#include "RenderGraph/RenderGraphTypes.h"
#include "RenderTarget.h"
#include "RenderTexture.h"
#include "RenderTimingObserver.h"
#include "SwapchainCaptureService.h"
#include "Vulkan/VulkanAllocator.h"
#include "Vulkan/VulkanFrameSync.h"
#include "Vulkan/VulkanSwapchain.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

namespace gte::rg {
class CommandBuffer;
class RenderGraph;
class RenderGraphBuilder;
} // namespace gte::rg

namespace gte {

class OffscreenRecording;
class VulkanDevice;

// Owns the swapchain, frame-sync objects and command buffers; it talks to acquire, submit and present.
// Does not own the device, surface, queues or allocator.
// It points into the Renderer's VulkanDevice, so it is not movable.
class FramePresenter {
public:
    // `vulkanDevice` must outlive this object; all graphics-queue submits go through it.
    FramePresenter(VkPhysicalDevice physicalDevice, VkDevice device, VkSurfaceKHR surface,
        std::uint32_t graphicsQueueFamily, std::uint32_t presentQueueFamily, VkQueue presentQueue, int width,
        int height, VmaAllocator allocator, VkFormat depthFormat, std::shared_ptr<GpuMemoryTracker> memoryTracker,
        VulkanDevice& vulkanDevice, PresentPolicy presentPolicy);
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

    // See Renderer::RenderOffscreen(). Throws when a GPU wait fails; the fence is then left as it was.
    DrawStats RenderOffscreen(FrameRecorder& frameRecorder, RenderTexture& target,
        const std::array<float, 4>& clearColor, const std::function<void(rg::CommandBuffer&)>& recordExtra);

    // See Renderer::PresentViaRenderGraph(). Returns false without touching `graph` when no swapchain frame can
    // be recorded (minimized window, pending resize, just-recreated swapchain); otherwise runs a full
    // acquire, record, submit and present cycle.
    bool PresentViaRenderGraph(rg::RenderGraph& graph,
        const std::function<std::vector<rg::TextureHandle>(rg::RenderGraphBuilder&, rg::TextureHandle)>& build);

    // Requests a one-shot capture of the swapchain image, fulfilled two frames later with no added GPU stall.
    // No-op while one is pending.
    void RequestSwapchainCapture();

    // Returns and clears the capture that PresentViaRenderGraph() completed during its own latest call, or
    // nullopt. It cannot forward to SwapchainCaptureService directly: that needs the frame-in-flight index of
    // a specific moment inside PresentViaRenderGraph().
    std::optional<CapturedSwapchainPixels> TakeLastCompletedSwapchainCapture();

    // Frames that really reached the queue so far. The newest one carries this number.
    std::uint64_t SubmittedFrameCount() const noexcept { return m_submittedFrames; }

    // Waits (bounded) until the offscreen slot of the frame about to be built is free, then returns its ticket.
    // Repeats return the same ticket until that frame is submitted. nullopt when the wait did not complete:
    // nothing was opened and the caller skips the frame.
    std::optional<OffscreenFrameTicket> BeginOffscreenFrame();

    // Waits (bounded) for every offscreen frame in flight. Anything but Completed means the GPU may still use them.
    [[nodiscard]] BlockOutcome WaitAllOffscreen(BlockSite site, const char* detail) noexcept;

    // True when offscreen frames overlap; otherwise nothing is in flight while a frame is built.
    void SetOffscreenOverlap(bool overlapped) noexcept { m_offscreen.SetOverlapped(overlapped); }

    // Keeps a dropped GPU object alive until the offscreen frame being built, and every frame before it, is done.
    // Freed after a slot wait, a ring drain or the legacy offscreen wait that really completed.
    template <class T>
    void Retire(T&& object)
    {
        m_offscreen.Retire(std::forward<T>(object));
    }
    std::size_t RetiredPending() const noexcept { return m_offscreen.RetiredPending(); }
    // Frees every retired object when `gpuIdle`, else parks them in `abandoned`: the GPU may still use them.
    void ReleaseRetired(AbandonedWork& abandoned, bool gpuIdle) noexcept;

    // The frame slot whose host-written buffer copies the open offscreen recording reads; 0 when none is open.
    std::uint32_t OffscreenRecordSlot() const noexcept;
    // Slot a legacy (synchronous) recording reads; set before it opens.
    void SetLegacyRecordSlot(std::uint32_t slot) noexcept { m_legacyRecordSlot = slot; }

private:
    friend class OffscreenRecording;

    static constexpr std::uint32_t kFramesInFlight = kGpuFramesInFlight;
    static_assert(kFramesInFlight <= OffscreenFrameRing::kMaxSlots, "the offscreen ring would silently shrink");

    void CreateCommandObjects();
    // Drains the offscreen ring, waits (bounded) for the previous synchronous submit, then opens the legacy offscreen
    // command buffer. Throws on failure or when a recording is already open.
    VkCommandBuffer BeginOffscreenRecording();
    // Ends and submits the legacy offscreen command buffer, then waits (bounded). The fence is reset right before the submit.
    void EndOffscreenRecording();
    // Opens the command buffer of the ticket's slot and records the frame boundary barrier first. No wait: the slot
    // wait of BeginOffscreenFrame() proved it free. Throws on failure, when a recording is open or for a stale ticket.
    VkCommandBuffer BeginAsyncOffscreenRecording(const OffscreenFrameTicket& ticket);
    // Ends and submits the open slot command buffer without waiting. The slot fence is reset right before the submit.
    void EndOffscreenSlotRecording();
    // Drops the open recording without submitting it. Never call after a submit.
    void DiscardOffscreen() noexcept;
    // Resets `fence` and submits `cmd` without waiting. A failed submit marks the device lost and throws.
    void SubmitOffscreenBuffer(VkCommandBuffer cmd, VkFence fence, const char* what);
    // Submits the legacy offscreen command buffer and waits (bounded) for it. Throws on failure.
    void SubmitOffscreenAndWait(VkFence offscreenFence, const char* what);
    void RecreateSwapchain();
    // Returns an acquired but unsubmitted swapchain image (and its semaphore) after a failed frame.
    void RecoverAcquiredImage(std::uint32_t imageIndex, bool fenceReset, bool capturePending) noexcept;
    void Destroy() noexcept;

    VkDevice m_device = VK_NULL_HANDLE;
    VkQueue m_presentQueue = VK_NULL_HANDLE;
    std::uint32_t m_graphicsQueueFamily = 0;

    // The one choke point for graphics-queue submits; not owned.
    VulkanDevice* m_vulkanDevice = nullptr;

    VkFormat m_depthFormat = VK_FORMAT_UNDEFINED;
    std::shared_ptr<GpuMemoryTracker> m_memoryTracker;

    VulkanSwapchain m_swapchain;
    // Per-frame and per-image sync objects plus the offscreen fences; render-finished semaphores are rebuilt on
    // swapchain recreation.
    VulkanFrameSync m_frameSync;
    VkCommandPool m_commandPool = VK_NULL_HANDLE;
    std::array<VkCommandBuffer, kFramesInFlight> m_commandBuffers{};

    // Separate from the per-frame buffers so offscreen rendering never contends with the swapchain's.
    // The legacy buffer serves the synchronous callers; the slot buffers serve the overlapped main frame.
    VkCommandBuffer m_offscreenCommandBuffer = VK_NULL_HANDLE;
    std::array<VkCommandBuffer, kFramesInFlight> m_offscreenSlotCommandBuffers{};
    // Slot ring and retire queue; emptied by ReleaseRetired() before the device goes away.
    OffscreenFrameProtocol m_offscreen{ kFramesInFlight };

    // Which offscreen buffer is open: from its begin until it ends or is discarded.
    enum class OffscreenOpen : std::uint8_t { None, Legacy, Slot };
    OffscreenOpen m_offscreenOpen = OffscreenOpen::None;
    std::uint32_t m_legacyRecordSlot = 0;

    std::uint32_t m_currentFrame = 0;

    // Frames that really reached the queue, and which of them each slot holds (for the GPU timing read-back).
    std::uint64_t m_submittedFrames = 0;
    GpuFrameRing m_gpuFrames;

    bool m_resizeRequested = false;
    int m_pendingWidth = 0;
    int m_pendingHeight = 0;

    // See RequestSwapchainCapture()/TakeLastCompletedSwapchainCapture().
    SwapchainCaptureService m_swapchainCapture;

    // Set only inside PresentViaRenderGraph(), right after its per-slot fence wait.
    std::optional<CapturedSwapchainPixels> m_lastCompletedCapture;
};

} // namespace gte
