#include "RenderGraphEventSnapshotPool.h"

#include "RecordingAccess.h"
#include "../Renderer.h"
#include "../VulkanFormatInfo.h"
#include "../../Core/Logging.h"

#include <atomic>
#include <stdexcept>
#include <utility>

namespace gte::rg {

// Owns a VMA image, its allocation and its GpuMemoryTracker entry.
EventSnapshotResource::EventSnapshotResource(VmaAllocator allocator, std::shared_ptr<GpuMemoryTracker> tracker,
    VkDevice device, VkExtent2D extent, VkFormat format, VkImageAspectFlags aspect, const char* debugName)
    : m_allocator(allocator)
    , m_tracker(std::move(tracker))
    , m_device(device)
    , m_extent(extent)
    , m_format(format)
{
    const bool isDepth = (aspect & VK_IMAGE_ASPECT_DEPTH_BIT) != 0;

    VkImageCreateInfo imageInfo{ VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.format = format;
    imageInfo.extent = { extent.width, extent.height, 1 };
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    // TRANSFER_DST: copied into on capture. SAMPLED: shown later by the UI / HTTP readback.
    // The attachment bit only mirrors the source's usage class (keeps validation clean).
    imageInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT
        | (isDepth ? VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT : VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT);
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    VmaAllocationCreateInfo allocCreateInfo{};
    allocCreateInfo.usage = VMA_MEMORY_USAGE_AUTO;

    VmaAllocationInfo allocationInfo{};
    if (vmaCreateImage(m_allocator, &imageInfo, &allocCreateInfo, &m_image, &m_allocation, &allocationInfo)
        != VK_SUCCESS) {
        throw std::runtime_error("EventSnapshotResource: vmaCreateImage failed.");
    }

    const GpuMemoryLocation location = ClassifyGpuMemoryLocation(m_allocator, m_allocation);
    m_handle = m_tracker->Track(GpuResourceType::Texture, location, allocationInfo.size, format);
    if (debugName != nullptr) {
        m_tracker->SetDebugName(m_handle, debugName);
    }

    VkImageViewCreateInfo viewInfo{ VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
    viewInfo.image = m_image;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = format;
    viewInfo.subresourceRange.aspectMask = aspect;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.layerCount = 1;

    if (vkCreateImageView(m_device, &viewInfo, nullptr, &m_imageView) != VK_SUCCESS) {
        m_tracker->Untrack(m_handle);
        vmaDestroyImage(m_allocator, m_image, m_allocation);
        throw std::runtime_error("EventSnapshotResource: vkCreateImageView failed.");
    }
}

EventSnapshotResource::~EventSnapshotResource()
{
    Destroy();
}

EventSnapshotResource::EventSnapshotResource(EventSnapshotResource&& other) noexcept
    : m_allocator(std::exchange(other.m_allocator, VK_NULL_HANDLE))
    , m_tracker(std::move(other.m_tracker))
    , m_handle(std::exchange(other.m_handle, kInvalidGpuResourceHandle))
    , m_device(std::exchange(other.m_device, VK_NULL_HANDLE))
    , m_image(std::exchange(other.m_image, VK_NULL_HANDLE))
    , m_allocation(std::exchange(other.m_allocation, VK_NULL_HANDLE))
    , m_imageView(std::exchange(other.m_imageView, VK_NULL_HANDLE))
    , m_extent(std::exchange(other.m_extent, VkExtent2D{}))
    , m_format(std::exchange(other.m_format, VK_FORMAT_UNDEFINED))
    , m_state(std::exchange(other.m_state, ResourceState{}))
{
}

EventSnapshotResource& EventSnapshotResource::operator=(EventSnapshotResource&& other) noexcept
{
    if (this != &other) {
        Destroy();
        m_allocator = std::exchange(other.m_allocator, VK_NULL_HANDLE);
        m_tracker = std::move(other.m_tracker);
        m_handle = std::exchange(other.m_handle, kInvalidGpuResourceHandle);
        m_device = std::exchange(other.m_device, VK_NULL_HANDLE);
        m_image = std::exchange(other.m_image, VK_NULL_HANDLE);
        m_allocation = std::exchange(other.m_allocation, VK_NULL_HANDLE);
        m_imageView = std::exchange(other.m_imageView, VK_NULL_HANDLE);
        m_extent = std::exchange(other.m_extent, VkExtent2D{});
        m_format = std::exchange(other.m_format, VK_FORMAT_UNDEFINED);
        m_state = std::exchange(other.m_state, ResourceState{});
    }
    return *this;
}

// Safe on a default-constructed or moved-from instance (all handles null).
void EventSnapshotResource::Destroy() noexcept
{
    if (m_imageView != VK_NULL_HANDLE) {
        vkDestroyImageView(m_device, m_imageView, nullptr);
        m_imageView = VK_NULL_HANDLE;
    }
    if (m_image != VK_NULL_HANDLE) {
        m_tracker->Untrack(m_handle);
        m_handle = kInvalidGpuResourceHandle;
        vmaDestroyImage(m_allocator, m_image, m_allocation);
        m_image = VK_NULL_HANDLE;
        m_allocation = VK_NULL_HANDLE;
    }
}

void CopyImageIntoSnapshot(VkCommandBuffer cmd, EventSnapshotResource& dst, VmaAllocator allocator,
    const std::shared_ptr<GpuMemoryTracker>& tracker, VkDevice device, VkImage srcImage, VkExtent2D srcExtent,
    VkFormat srcFormat, VkImageAspectFlags aspect, ResourceState& srcState, const char* debugName)
{
    if (dst.Extent().width != srcExtent.width || dst.Extent().height != srcExtent.height
        || dst.Format() != srcFormat) {
        dst = EventSnapshotResource(allocator, tracker, device, srcExtent, srcFormat, aspect, debugName);
    }

    const VkImageSubresourceRange range{ aspect, 0, 1, 0, 1 };

    // Side-channel copy: src goes to TRANSFER_SRC and back to EXACTLY srcState, so the graph's own
    // tracked state never sees it.
    const ResourceState srcTransfer{
        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_READ_BIT
    };
    const ResourceState dstTransfer{
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT
    };

    EmitImageBarrier(cmd, srcImage, range, srcState, srcTransfer);
    EmitImageBarrier(cmd, dst.Image(), range, dst.State(), dstTransfer);

    VkImageCopy2 region{ VK_STRUCTURE_TYPE_IMAGE_COPY_2 };
    region.srcSubresource = { aspect, 0, 0, 1 };
    region.dstSubresource = { aspect, 0, 0, 1 };
    region.extent = { srcExtent.width, srcExtent.height, 1 };

    VkCopyImageInfo2 copyInfo{ VK_STRUCTURE_TYPE_COPY_IMAGE_INFO_2 };
    copyInfo.srcImage = srcImage;
    copyInfo.srcImageLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    copyInfo.dstImage = dst.Image();
    copyInfo.dstImageLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    copyInfo.regionCount = 1;
    copyInfo.pRegions = &region;
    vkCmdCopyImage2(cmd, &copyInfo);

    EmitImageBarrier(cmd, srcImage, range, srcTransfer, srcState); // restore - invisible to the graph.

    const ResourceState dstRead{
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
        VK_ACCESS_2_SHADER_READ_BIT
    };
    EmitImageBarrier(cmd, dst.Image(), range, dstTransfer, dstRead);
    dst.State() = dstRead;
}

void CaptureAttachmentIntoSnapshot(const RecordingContext& cmd, Renderer& renderer, EventSnapshotResource& dst,
    const AttachmentWrite& write, const char* debugName)
{
    const VkFormat format = ToVkFormat(write.format);
    if (format == VK_FORMAT_UNDEFINED) {
        static std::atomic_flag logged;
        if (!logged.test_and_set()) {
            GTE_LOG_ERROR("FrameDebugger", "Event snapshot skipped: the attachment has an undefined pixel format.");
        }
        return;
    }
    const VkImageAspectFlags aspect = write.isDepth ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;
    ResourceState attachmentState = RequiredStateFor(
        write.isDepth ? ResourceAccess::DepthStencilAttachmentReadWrite : ResourceAccess::ColorAttachmentWrite,
        write.isDepth);
    CopyImageIntoSnapshot(RecordingAccess::Native(cmd), dst, renderer.GetVmaAllocator(), renderer.GetMemoryTracker(),
        renderer.GetImGuiVulkanBackendInfo().device, RecordingAccess::Image(write.image),
        ToVkExtent2D(write.extent), format, aspect, attachmentState, debugName);
}

} // namespace gte::rg
