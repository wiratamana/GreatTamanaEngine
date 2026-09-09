#include "SwapchainCaptureService.h"

#include "RenderGraph/RenderGraphBarrierPlanner.h"

#include <cstring>

namespace gte {

SwapchainCaptureService::SwapchainCaptureService(
    VmaAllocator allocator, std::shared_ptr<GpuMemoryTracker> tracker, std::uint32_t framesInFlight)
    : m_allocator(allocator)
    , m_memoryTracker(std::move(tracker))
    , m_slots(framesInFlight)
{
}

void SwapchainCaptureService::RequestCapture()
{
    if (m_captureRequested) {
        return; // Already pending - FrameCaptureBridge never lets this happen anyway (Phase 2).
    }
    m_captureRequested = true;
}

bool SwapchainCaptureService::RecordCaptureIfRequested(
    VkCommandBuffer cmd, VkImage swapchainImage, VkExtent2D extent, VkFormat format, std::uint32_t frameInFlightIndex)
{
    if (!m_captureRequested) {
        return false;
    }

    Slot& slot = m_slots[frameInFlightIndex];

    const VkDeviceSize size = VkDeviceSize(extent.width) * extent.height * 4;
    if (!slot.readbackBuffer.has_value() || slot.readbackBuffer->Size() != size) {
        // In practice NotifySwapchainRecreated() will already have reset
        // every slot's buffer by the time a real size mismatch could ever
        // happen here - this check is retained purely as cheap defensive
        // belt-and-braces, not the primary rebuild path (see the phase
        // document's own Step 3.2, point 3).
        slot.readbackBuffer.reset();
        slot.readbackBuffer.emplace(m_allocator, m_memoryTracker, size, VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            BufferMemoryUsage::GpuToCpu, "SwapchainCaptureReadback");
    }

    const VkImageSubresourceRange colorRange{ VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };

    // The swapchain image is coming out of the "Present" render-graph
    // pass's own color-attachment write, not a sampled texture - so the
    // "previous" state here is ColorAttachmentWrite, not ShaderRead (unlike
    // Phase 3's Renderer::CaptureRenderTexturePixels(), which reads a
    // RenderTexture that's always left in SHADER_READ_ONLY_OPTIMAL).
    const rg::ResourceState previous = rg::RequiredStateFor(rg::ResourceAccess::ColorAttachmentWrite, false);
    const rg::ResourceState transferSrcState{
        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_READ_BIT
    };
    rg::EmitImageBarrier(cmd, swapchainImage, colorRange, previous, transferSrcState);

    VkBufferImageCopy region{};
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.layerCount = 1;
    region.imageExtent = { extent.width, extent.height, 1 };
    vkCmdCopyImageToBuffer(
        cmd, swapchainImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, slot.readbackBuffer->Native(), 1, &region);

    // REQUIRED: a fence wait alone (TryTakeCompletedCapture()'s caller's own
    // per-slot vkWaitForFences) guarantees this copy finished EXECUTING, but
    // the Vulkan spec still requires an explicit memory dependency whose
    // destination stage/access includes HOST_BIT/HOST_READ_BIT before a
    // device write is guaranteed VISIBLE to a later host read - see the
    // phase document's own Step 2, point 6 and Step 3.2, point 4. `layout`
    // is ignored for buffer barriers (see ResourceState's own doc comment
    // in RenderGraphBarrierPlanner.h) - VK_IMAGE_LAYOUT_UNDEFINED is simply
    // the required placeholder value.
    const rg::ResourceState transferWriteState{
        VK_IMAGE_LAYOUT_UNDEFINED, VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT
    };
    const rg::ResourceState hostReadState{ VK_IMAGE_LAYOUT_UNDEFINED, VK_PIPELINE_STAGE_2_HOST_BIT, VK_ACCESS_2_HOST_READ_BIT };
    rg::EmitBufferBarrier(cmd, slot.readbackBuffer->Native(), 0, size, transferWriteState, hostReadState);

    // There is NO "transition back" step for the image afterward - the
    // caller's own subsequent manual finalize block (FramePresenter's
    // existing PRESENT_SRC_KHR transition) is what takes the image the rest
    // of the way, treating TransferSrcOptimal as its own "previous" state
    // for this one call.

    slot.pendingCapture = true;
    slot.capturedWidth = static_cast<int>(extent.width);
    slot.capturedHeight = static_cast<int>(extent.height);
    slot.capturedFormat = format;
    m_captureRequested = false; // The REQUEST is satisfied - the copy is now in flight, not "still wanted".

    return true;
}

std::optional<CapturedSwapchainPixels> SwapchainCaptureService::TryTakeCompletedCapture(std::uint32_t frameInFlightIndex)
{
    Slot& slot = m_slots[frameInFlightIndex];
    if (!slot.pendingCapture) {
        return std::nullopt;
    }
    slot.pendingCapture = false;

    const std::size_t byteCount = static_cast<std::size_t>(slot.capturedWidth) * slot.capturedHeight * 4;

    CapturedSwapchainPixels result;
    result.pixels.resize(byteCount);
    std::memcpy(result.pixels.data(), slot.readbackBuffer->MappedData(), byteCount);
    result.width = slot.capturedWidth;
    result.height = slot.capturedHeight;
    result.format = slot.capturedFormat;
    return result;
}

void SwapchainCaptureService::NotifySwapchainRecreated()
{
    for (Slot& slot : m_slots) {
        slot.readbackBuffer.reset();
        slot.pendingCapture = false;
    }
    // A resize-in-progress capture request is simply dropped - the network
    // thread's own fixed timeout in FrameCaptureBridge (Phase 2) is what
    // ultimately surfaces this to the caller as a timeout, without this
    // service needing its own separate "failed" signal path back to
    // Application (see this class's own NotifySwapchainRecreated() doc
    // comment in the header).
    m_captureRequested = false;
}

} // namespace gte
