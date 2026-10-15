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
        return; // Already pending; only one request is tracked at a time.
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

    const uint64_t size = uint64_t(extent.width) * extent.height * 4;
    if (!slot.readbackBuffer.has_value() || slot.readbackBuffer->Size() != size) {
        // Defensive: NotifySwapchainRecreated() normally resets the buffers before a size mismatch can occur.
        slot.readbackBuffer.reset();
        slot.readbackBuffer.emplace(m_allocator, m_memoryTracker, size, BufferUsage::TransferDst,
            BufferMemoryUsage::GpuToCpu, "SwapchainCaptureReadback");
    }

    const VkImageSubresourceRange colorRange{ VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };

    // The image comes out of the present pass's color-attachment write, not a sampled texture.
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

    // The caller's fence wait only proves the copy executed; a transfer-write to host-read dependency is still
    // needed for host visibility. `layout` is ignored for buffer barriers, so UNDEFINED is a placeholder.
    const rg::ResourceState transferWriteState{
        VK_IMAGE_LAYOUT_UNDEFINED, VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT
    };
    const rg::ResourceState hostReadState{ VK_IMAGE_LAYOUT_UNDEFINED, VK_PIPELINE_STAGE_2_HOST_BIT, VK_ACCESS_2_HOST_READ_BIT };
    rg::EmitBufferBarrier(cmd, slot.readbackBuffer->Native(), 0, size, transferWriteState, hostReadState);

    // No transition back: the caller's finalize step treats TransferSrcOptimal as its previous state.

    slot.pendingCapture = true;
    slot.capturedWidth = static_cast<int>(extent.width);
    slot.capturedHeight = static_cast<int>(extent.height);
    slot.capturedFormat = format;
    m_captureRequested = false; // The request is satisfied; the copy is now in flight.

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
    // A capture request caught by a resize is dropped; the bridge's own timeout reports it to the caller.
    m_captureRequested = false;
}

} // namespace gte
