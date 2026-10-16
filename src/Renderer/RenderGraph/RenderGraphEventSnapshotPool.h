#pragma once

// Scratch images the Frame Debugger copies a pass's write target into, and the copy helpers.

#include "RecordingContext.h"
#include "RenderGraphBarrierPlanner.h"
#include "RenderGraphNameSlotTable.h"
#include "../../Core/FrameDebuggerEventSink.h"
#include "../Memory/GpuMemoryTracker.h"
#include "../Vulkan/VulkanAllocator.h"

#include <volk.h>
#include <cstdint>
#include <memory>
#include <vector>

namespace gte {
class Renderer;
}

namespace gte::rg {

// RAII scratch 2D image (VMA allocation plus memory-tracker registration) that a write target is copied into.
class EventSnapshotResource {
public:
    EventSnapshotResource() = default;
    EventSnapshotResource(VmaAllocator allocator, std::shared_ptr<GpuMemoryTracker> tracker, VkDevice device,
        VkExtent2D extent, VkFormat format, VkImageAspectFlags aspect, const char* debugName);
    ~EventSnapshotResource();

    EventSnapshotResource(const EventSnapshotResource&) = delete;
    EventSnapshotResource& operator=(const EventSnapshotResource&) = delete;
    EventSnapshotResource(EventSnapshotResource&& other) noexcept;
    EventSnapshotResource& operator=(EventSnapshotResource&& other) noexcept;

    VkImage Image() const noexcept { return m_image; }
    VkImageView View() const noexcept { return m_imageView; }
    VkExtent2D Extent() const noexcept { return m_extent; }
    VkFormat Format() const noexcept { return m_format; }
    ResourceState& State() noexcept { return m_state; } // tracked solely by whoever last copied into this.

private:
    void Destroy() noexcept;

    VmaAllocator m_allocator = VK_NULL_HANDLE;
    std::shared_ptr<GpuMemoryTracker> m_tracker;
    GpuResourceHandle m_handle{};
    VkDevice m_device = VK_NULL_HANDLE;
    VkImage m_image = VK_NULL_HANDLE;
    VmaAllocation m_allocation = VK_NULL_HANDLE;
    VkImageView m_imageView = VK_NULL_HANDLE;
    VkExtent2D m_extent{};
    VkFormat m_format = VK_FORMAT_UNDEFINED;
    ResourceState m_state;
};

// Copies `srcImage` into scratch `dst` (recreated if size/format differ), restores `srcImage` to EXACTLY `srcState`
// and leaves `dst` SHADER_READ_ONLY_OPTIMAL. The source's tracked barrier state is never touched.
void CopyImageIntoSnapshot(VkCommandBuffer cmd, EventSnapshotResource& dst, VmaAllocator allocator,
    const std::shared_ptr<GpuMemoryTracker>& tracker, VkDevice device, VkImage srcImage, VkExtent2D srcExtent,
    VkFormat srcFormat, VkImageAspectFlags aspect, ResourceState& srcState, const char* debugName);

// Vulkan-free entry for the Frame Debugger: same copy as above for the attachment `write` named, taking the
// allocator, tracker and device from `renderer`.
void CaptureAttachmentIntoSnapshot(const RecordingContext& cmd, Renderer& renderer, EventSnapshotResource& dst,
    const AttachmentWrite& write, const char* debugName);

} // namespace gte::rg
