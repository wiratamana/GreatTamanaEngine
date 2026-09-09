#pragma once

// network-impl-2 campaign, Phase 4
// (PHASE4_SWAPCHAIN_PIPELINED_CAPTURE_SERVICE.md) - the harder, pipelined
// counterpart of Phase 3's Renderer::CaptureRenderTexturePixels(). Reads
// back the REAL swapchain image's pixels out of FramePresenter's
// kFramesInFlight == 2, pipelined Present regime, with ZERO added GPU stall,
// by piggy-backing on synchronization FramePresenter::PresentViaRenderGraph()
// already performs for an unrelated, pre-existing reason (its own per-slot
// vkWaitForFences call) - the exact same "no new GPU wait, ever" discipline
// AGENTS.md's GPU-timestamp-queries section locks in for GpuTimingService,
// applied here to pixel data instead of timing data.
//
// Structurally modeled directly on GpuTimingService (same file's neighbor,
// same "FramePresenter only ever calls INTO this, never issues the raw
// Vulkan calls itself" division of labor) - see PHASE4's own Step 2, point 3
// for why this is a design-SHAPE precedent to copy, not a "this exact
// pattern is already proven working for Present" claim: GpuTimingService's
// OWN Present-specific trio (RecordPresentPassStart/End,
// ReadPresentResultIfAvailable) is dead code today, never actually called
// from FramePresenter.cpp - this class is the FIRST real, exercised
// production consumer of "read one round later, keyed by frame-in-flight
// index, right after the existing per-slot fence wait" applied directly
// inside FramePresenter.cpp itself.
//
// Buffers are allocated LAZILY - only once RequestCapture() is called for
// the very first time (PHASE0_MASTER_STRATEGY.md's own Locked Design
// Decision #7) - and destroyed/recreated (never resized in place, Buffer
// has no Resize()) whenever the swapchain's own extent changes (see
// NotifySwapchainRecreated() below).
//
// Does NOT own the VmaAllocator/VkDevice passed in - both must outlive this
// object, same convention as every other Vulkan/*-adjacent class here.

#include <volk.h>

#include "Buffer.h"
#include "Memory/GpuMemoryTracker.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

namespace gte {

struct CapturedSwapchainPixels {
    std::vector<std::uint8_t> pixels; // tightly packed, width*height*4 bytes
    int width = 0;
    int height = 0;
    VkFormat format = VK_FORMAT_UNDEFINED;
};

// FramePresenter only ever calls INTO this class, at the two specific
// points documented on each method below - it never issues
// vkCmdCopyImageToBuffer/vkCmdPipelineBarrier2 itself for this purpose.
class SwapchainCaptureService {
public:
    SwapchainCaptureService(VmaAllocator allocator, std::shared_ptr<GpuMemoryTracker> tracker,
        std::uint32_t framesInFlight);
    ~SwapchainCaptureService() = default;

    SwapchainCaptureService(const SwapchainCaptureService&) = delete;
    SwapchainCaptureService& operator=(const SwapchainCaptureService&) = delete;

    // Movable (unlike copyable) - FramePresenter itself is move-constructible/
    // move-assignable (see FramePresenter.h/.cpp), and owns this as a plain
    // value member, so this class must be too. Every member (VmaAllocator,
    // shared_ptr, bool, std::vector<Slot>) is trivially move-safe.
    SwapchainCaptureService(SwapchainCaptureService&&) = default;
    SwapchainCaptureService& operator=(SwapchainCaptureService&&) = default;

    // Called from Application::Run() (indirectly, via Renderer - see
    // FramePresenter/Renderer's own thin forwarding methods) when
    // FrameCaptureBridge::IsCaptureRequested(Swapchain) is true. A safe
    // no-op if a capture is ALREADY pending (this service only ever tracks
    // ONE in-flight request at a time - FrameCaptureBridge's own "already
    // pending -> 503, never queued" rule, Phase 2, is what guarantees
    // RequestCapture() is never called again before the previous request
    // has been fully resolved).
    void RequestCapture();

    // Called from FramePresenter::PresentViaRenderGraph(), ONLY right after
    // graph.Execute() returns and BEFORE the existing manual PRESENT_SRC_KHR
    // finalize block (see PHASE4's own Step 2, point 2). If a capture is
    // currently requested, (re)creates this frame-in-flight slot's readback
    // buffer if needed (matching `extent`), records the
    // ColorAttachmentWrite -> TransferSrcOptimal barrier + vkCmdCopyImageToBuffer
    // + the buffer's own transfer-write -> host-read visibility barrier
    // (REQUIRED, not optional - see Step 3.2, point 4 of the phase
    // document), and marks this slot "captured, pending read". Returns true
    // if it recorded anything (meaning the caller's own subsequent finalize
    // step must transition FROM TransferSrcOptimal, not
    // ColorAttachmentWrite) - false otherwise (nothing recorded, caller's
    // existing behavior is unchanged).
    bool RecordCaptureIfRequested(
        VkCommandBuffer cmd, VkImage swapchainImage, VkExtent2D extent, VkFormat format, std::uint32_t frameInFlightIndex);

    // Called from FramePresenter::PresentViaRenderGraph(), ONLY right after
    // its own vkWaitForFences() call for `frameInFlightIndex` returns (see
    // PHASE4's own Step 2, point 3) - i.e. BEFORE this same slot's buffer
    // might be reused/destroyed by a resize this same call. If this slot
    // has a completed pending capture, copies it out of the buffer's mapped
    // memory into a plain std::vector and returns it (clearing the pending
    // flag) - std::nullopt if nothing was pending for this slot.
    std::optional<CapturedSwapchainPixels> TryTakeCompletedCapture(std::uint32_t frameInFlightIndex);

    // Called from FramePresenter::RecreateSwapchain() whenever the
    // swapchain is actually recreated (a real resize, not merely a resize
    // REQUEST that's still pending due to a minimized window) - destroys
    // every existing readback buffer (they're the wrong size now) and
    // discards ANY currently-pending capture (there is no safe way to
    // finish a copy whose source image no longer exists at that size/
    // identity) - the corresponding FrameCaptureBridge request, if any,
    // will simply time out and the caller can retry; this is an accepted,
    // rare edge case (a resize racing an in-flight screenshot request), not
    // one this service tries to paper over with a synthetic "just-resized,
    // please retry immediately" fast-fail path.
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
