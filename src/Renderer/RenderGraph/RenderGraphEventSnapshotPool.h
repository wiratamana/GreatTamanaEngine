#pragma once

// Gated capture of what a pass just wrote: the write target is copied into a pooled scratch image. Off by default;
// it only costs a copy and barrier while enabled (see RenderGraph::SetEventSnapshotCaptureEnabled()).

#include "RenderGraphBarrierPlanner.h"
#include "RenderGraphNameSlotTable.h"
#include "../Memory/GpuMemoryTracker.h"
#include "../Vulkan/VulkanAllocator.h"

#include <volk.h>
#include <cstdint>
#include <memory>
#include <vector>

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

// Fixed pool of slots, pre-sized to slotBudget * regionCount; each region owns an exclusive area so in-flight slots
// are never replaced. Region 0 is the synchronous regime, [1, framesInFlight] the pipelined buffer indices.
class RenderGraphEventSnapshotPool {
public:
    // `framesInFlight` must match FramePresenter::kFramesInFlight; one extra region is reserved for the synchronous regime.
    RenderGraphEventSnapshotPool(VmaAllocator allocator, std::shared_ptr<GpuMemoryTracker> tracker, VkDevice device,
        std::uint32_t slotBudget, std::uint32_t framesInFlight);

    RenderGraphEventSnapshotPool(const RenderGraphEventSnapshotPool&) = delete;
    RenderGraphEventSnapshotPool& operator=(const RenderGraphEventSnapshotPool&) = delete;

    void SetCaptureEnabled(bool enabled) noexcept { m_captureEnabled = enabled; }
    bool IsCaptureEnabled() const noexcept { return m_captureEnabled; }

    // Call once at the top of ExecuteCompiledGraph(); selects this call's exclusive region.
    void BeginFrame(bool pipelined, std::uint32_t bufferIndex) noexcept
    {
        m_activeRegion = RegionFor(pipelined, bufferIndex);
        m_nextSlot = 0;
    }

    // Copies `srcImage` into this region's scratch slot and restores it to `srcState`, invisible to the barrier
    // planner. Returns kNoNameSlot if disarmed or the region's slot budget is exhausted.
    std::int32_t CaptureAfterPass(VkCommandBuffer cmd, VkImage srcImage, VkExtent2D srcExtent, VkFormat srcFormat,
        VkImageAspectFlags aspect, ResourceState srcState);

    // `region` must be RegionFor() of the pipelined/bufferIndex the slot was captured under.
    // Do not cache the result across a later BeginFrame() for that region: the slot gets replaced.
    const EventSnapshotResource* ReadSnapshot(std::uint32_t region, std::int32_t slot) const noexcept;

    // Region 0 is the synchronous regime's; regions [1, framesInFlight] are the pipelined buffer indices.
    std::uint32_t RegionFor(bool pipelined, std::uint32_t bufferIndex) const noexcept
    {
        return pipelined ? (1u + bufferIndex) : 0u;
    }

private:
    EventSnapshotResource& AcquireSlot(VkExtent2D extent, VkFormat format, VkImageAspectFlags aspect);

    VmaAllocator m_allocator = VK_NULL_HANDLE;
    std::shared_ptr<GpuMemoryTracker> m_tracker;
    VkDevice m_device = VK_NULL_HANDLE;
    // Off by default in every build; only the editor's per-frame pump enables it.
    bool m_captureEnabled = false;
    std::uint32_t m_slotBudget = 0;
    std::uint32_t m_regionCount = 1;
    std::uint32_t m_activeRegion = 0;
    std::uint32_t m_nextSlot = 0;
    // Fixed capacity slotBudget * regionCount, indexed `region * slotBudget + logicalSlot`.
    std::vector<EventSnapshotResource> m_slots;
};

} // namespace gte::rg
