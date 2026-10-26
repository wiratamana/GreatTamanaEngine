#pragma once

// Reads back swapchain pixels with no added GPU stall: the copy is recorded in the present pass and read
// one round later, after the existing per-slot fence wait. Buffers are created on first use and recreated
// when the swapchain changes. Does not own the VmaAllocator.

#include <volk.h>

#include "../Core/CapturedPixels.h"
#include "Buffer.h"
#include "Memory/GpuMemoryTracker.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

namespace gte {

// Tightly packed width*height*4 bytes.
using CapturedSwapchainPixels = CapturedPixels;

// FramePresenter only calls into this class; it records no copy or barrier itself.
class SwapchainCaptureService {
public:
    SwapchainCaptureService(VmaAllocator allocator, std::shared_ptr<GpuMemoryTracker> tracker,
        std::uint32_t framesInFlight);
    ~SwapchainCaptureService() = default;

    SwapchainCaptureService(const SwapchainCaptureService&) = delete;
    SwapchainCaptureService& operator=(const SwapchainCaptureService&) = delete;

    // Movable because FramePresenter is movable and holds this by value.
    SwapchainCaptureService(SwapchainCaptureService&&) = default;
    SwapchainCaptureService& operator=(SwapchainCaptureService&&) = default;

    // Called via Renderer when FrameCaptureBridge asks for a swapchain capture. No-op while one is pending:
    // only one request is tracked at a time.
    void RequestCapture();

    // Called right after graph.Execute(), before the PRESENT_SRC_KHR finalize. When a capture is requested it
    // (re)creates this slot's readback buffer, records the copy and its barriers, and marks the slot pending.
    // Returns true when it recorded anything; the finalize step must then start from TransferSrcOptimal.
    bool RecordCaptureIfRequested(
        VkCommandBuffer cmd, VkImage swapchainImage, VkExtent2D extent, VkFormat format, std::uint32_t frameInFlightIndex);

    // Called right after FramePresenter's own per-slot fence wait. Returns the slot's completed capture copied
    // out of the mapped buffer (clearing its pending flag), or nullopt when nothing was pending.
    std::optional<CapturedSwapchainPixels> TryTakeCompletedCapture(std::uint32_t frameInFlightIndex);

    // Drops the slot's pending capture whose copy was recorded but never submitted. The request is not re-armed.
    void AbandonPendingCapture(std::uint32_t frameInFlightIndex) noexcept;

    // Called from FramePresenter::RecreateSwapchain() on a real recreation: destroys every readback buffer (wrong
    // size now) and discards any pending capture; the bridge request then times out and the caller can retry.
    void NotifySwapchainRecreated();

private:
    struct Slot {
        std::optional<Buffer> readbackBuffer;
        bool pendingCapture = false;
        int capturedWidth = 0;
        int capturedHeight = 0;
        VkFormat capturedFormat = VK_FORMAT_UNDEFINED;
    };

    VmaAllocator m_allocator = VK_NULL_HANDLE;
    std::shared_ptr<GpuMemoryTracker> m_memoryTracker;
    bool m_captureRequested = false;
    std::vector<Slot> m_slots; // sized to framesInFlight
};

} // namespace gte
