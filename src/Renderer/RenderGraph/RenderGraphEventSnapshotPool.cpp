#include "RenderGraphEventSnapshotPool.h"

#include <stdexcept>
#include <utility>

namespace gte::rg {

// Mirrors Texture2D.cpp's own ctor/dtor/move pattern - VMA image + VMA
// allocation + GpuMemoryTracker registration, never raw new/delete.
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
    // TRANSFER_DST: copied into every capture. SAMPLED: displayed later via
    // an ImGui/HTTP-readback descriptor. COLOR/DEPTH_ATTACHMENT mirrors the
    // source resource's own usage class - never actually rendered into here,
    // kept only so validation stays clean.
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

// Safe on a default-constructed or already-moved-from instance - every
// handle is VK_NULL_HANDLE in that state, so both guards below are no-ops.
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

    // Side-channel copy: barrier src to TRANSFER_SRC, barrier dst to
    // TRANSFER_DST, copy, then barrier src back to EXACTLY srcState - the
    // real pass graph's own tracked PhysicalTexture state never learns this
    // happened. This is the one rule that makes this safe to bolt onto an
    // already-correct barrier planner without corrupting it.
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
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_READ_BIT
    };
    EmitImageBarrier(cmd, dst.Image(), range, dstTransfer, dstRead);
    dst.State() = dstRead;
}

// Pre-sizes m_slots up front to its full, fixed capacity - the region
// protection this class exists for only holds if every physical area
// already exists before the first real capture ever runs. Every entry
// starts default-constructed (all-null handles, Extent() == {0,0}) -
// AcquireSlot() treats that as "first real use" and replaces it in place
// exactly like a genuine resize.
RenderGraphEventSnapshotPool::RenderGraphEventSnapshotPool(VmaAllocator allocator,
    std::shared_ptr<GpuMemoryTracker> tracker, VkDevice device, std::uint32_t slotBudget, std::uint32_t framesInFlight)
    : m_allocator(allocator)
    , m_tracker(std::move(tracker))
    , m_device(device)
    , m_slotBudget(slotBudget)
    , m_regionCount(framesInFlight + 1u) // "+1" - see this class's own header comment.
{
    m_slots.resize(static_cast<std::size_t>(slotBudget) * static_cast<std::size_t>(m_regionCount));
}

std::int32_t RenderGraphEventSnapshotPool::CaptureAfterPass(VkCommandBuffer cmd, VkImage srcImage,
    VkExtent2D srcExtent, VkFormat srcFormat, VkImageAspectFlags aspect, ResourceState srcState)
{
    if (!m_captureEnabled || srcImage == VK_NULL_HANDLE || m_nextSlot >= m_slotBudget) {
        return kNoNameSlot;
    }
    const std::int32_t slot = static_cast<std::int32_t>(m_nextSlot++);
    EventSnapshotResource& dst = AcquireSlot(srcExtent, srcFormat, aspect);
    CopyImageIntoSnapshot(
        cmd, dst, m_allocator, m_tracker, m_device, srcImage, srcExtent, srcFormat, aspect, srcState,
        "FrameDebuggerEventSnapshot");
    return slot;
}

EventSnapshotResource& RenderGraphEventSnapshotPool::AcquireSlot(
    VkExtent2D /*extent*/, VkFormat /*format*/, VkImageAspectFlags /*aspect*/)
{
    // Physical index = this call's own EXCLUSIVE region - m_slots is
    // pre-sized to m_slotBudget * m_regionCount in the constructor, so this
    // is always in-bounds; never grown/shrunk after that. The actual
    // (re)creation-if-needed decision is CopyImageIntoSnapshot()'s job, not
    // this method's - it always receives the right physical slot reference.
    const std::size_t idx =
        static_cast<std::size_t>(m_activeRegion) * m_slotBudget + static_cast<std::size_t>(m_nextSlot - 1);
    return m_slots[idx];
}

const EventSnapshotResource* RenderGraphEventSnapshotPool::ReadSnapshot(
    std::uint32_t region, std::int32_t slot) const noexcept
{
    if (slot < 0) {
        return nullptr;
    }
    const std::size_t idx = static_cast<std::size_t>(region) * m_slotBudget + static_cast<std::size_t>(slot);
    if (idx >= m_slots.size()) {
        return nullptr;
    }
    return &m_slots[idx];
}

} // namespace gte::rg
