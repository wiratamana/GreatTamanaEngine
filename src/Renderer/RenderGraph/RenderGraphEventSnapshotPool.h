#pragma once

// Cheap, always-on "what did this pass just write" capture: after a pass's
// real GPU work records, its color/depth write target is copied into a
// small, pooled scratch image instead of redrawing the whole scene again to
// reconstruct the same information. Mirrors RenderGraphTimestampPool's own
// shape exactly - same name-free, positional slot table, same two-layer
// on/off gate, same frame-in-flight-safe physical region split.

#include "RenderGraphBarrierPlanner.h"
#include "RenderGraphNameSlotTable.h"
#include "../Memory/GpuMemoryTracker.h"
#include "../Vulkan/VulkanAllocator.h"

#include <volk.h>
#include <cstdint>
#include <memory>
#include <vector>

namespace gte::rg {

// RAII-owned scratch 2D image a write target gets copied into. Mirrors
// Texture2D's own ownership shape (VmaAllocation + GpuMemoryTracker
// registration) - never raw new/delete, never a bare VkImage handle alone.
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

// Side-channel copy, reusable by anything that owns a scratch
// EventSnapshotResource: barriers `srcImage` to TRANSFER_SRC, barriers `dst`
// to TRANSFER_DST, copies, restores `srcImage` to EXACTLY `srcState`, and
// leaves `dst` in SHADER_READ_ONLY_OPTIMAL. The source image's own tracked
// barrier-planner state (if any) is never touched by this call - the next
// real pass's own barrier sees exactly what it would have seen if this copy
// never ran. (Re)creates `dst` in place first if its current size/format
// does not already match.
void CopyImageIntoSnapshot(VkCommandBuffer cmd, EventSnapshotResource& dst, VmaAllocator allocator,
    const std::shared_ptr<GpuMemoryTracker>& tracker, VkDevice device, VkImage srcImage, VkExtent2D srcExtent,
    VkFormat srcFormat, VkImageAspectFlags aspect, ResourceState& srcState, const char* debugName);

// Sibling of RenderGraphTimestampPool - same two-layer gate, same fixed,
// pre-grown slot pool (never per-capture allocation), same frame-in-flight
// protection: m_slots is pre-sized to slotBudget * regionCount, giving every
// region its own EXCLUSIVE physical area (indexed
// `region * slotBudget + logicalSlot`) - a slot is never replaced/destroyed
// while a different, still-possibly-in-flight region might still be reading
// it. Slots are POSITIONAL within one BeginFrame()/region (assigned by call
// order), not name-keyed: two writes from the same pass in one call, or two
// view-scope invocations of the same pass, naturally land in different
// slots with no collision bookkeeping needed at all.
//
// REGION, not bufferIndex: this engine pipelines across kFramesInFlight == 2
// frame-in-flight slots for ONE regime (PipelinedDeferredReadback), but also
// runs a COMPLETELY SEPARATE, non-pipelined regime (SynchronousImmediateReadback)
// every real frame. Both regimes call BeginFrame() on THIS SAME pool, in the
// same real frame, each with its own, textually identical bufferIndex range
// (0 for synchronous; 0..kFramesInFlight-1 for pipelined) - a flat
// `bufferIndex` alone would alias the synchronous regime's region with the
// pipelined regime's bufferIndex-0 region, letting the second Execute() call
// of a frame silently clobber scratch images the first one just produced.
// `RegionFor()` below assigns each regime its own exclusive sub-range so
// this can never happen - region 0 is reserved for the synchronous regime;
// regions [1, framesInFlight] are the pipelined regime's own bufferIndex 0..
// (framesInFlight - 1).
class RenderGraphEventSnapshotPool {
public:
    // `framesInFlight` must match FramePresenter::kFramesInFlight - this
    // pre-sizes m_slots to slotBudget * (framesInFlight + 1) in the
    // constructor body (the "+1" is the synchronous regime's own reserved
    // region - see this class's own doc comment above).
    RenderGraphEventSnapshotPool(VmaAllocator allocator, std::shared_ptr<GpuMemoryTracker> tracker, VkDevice device,
        std::uint32_t slotBudget, std::uint32_t framesInFlight);

    RenderGraphEventSnapshotPool(const RenderGraphEventSnapshotPool&) = delete;
    RenderGraphEventSnapshotPool& operator=(const RenderGraphEventSnapshotPool&) = delete;

    void SetCaptureEnabled(bool enabled) noexcept { m_captureEnabled = enabled; }
    bool IsCaptureEnabled() const noexcept { return m_captureEnabled; }

    // Call once at the top of ExecuteCompiledGraph(), before the pass loop.
    // `pipelined`/`bufferIndex` select this call's own exclusive region - see
    // RegionFor().
    void BeginFrame(bool pipelined, std::uint32_t bufferIndex) noexcept
    {
        m_activeRegion = RegionFor(pipelined, bufferIndex);
        m_nextSlot = 0;
    }

    // Copies `srcImage` (currently in `srcState`) into this call's own
    // region's pooled scratch slot, then restores `srcImage` back to
    // `srcState` - invisible to RenderGraphBarrierPlanner's own tracked
    // PhysicalTexture::colorState/depthState bookkeeping. Returns
    // kNoNameSlot if disarmed or this region's slot budget is exhausted.
    std::int32_t CaptureAfterPass(VkCommandBuffer cmd, VkImage srcImage, VkExtent2D srcExtent, VkFormat srcFormat,
        VkImageAspectFlags aspect, ResourceState srcState);

    // `region` must be RegionFor(pipelined, bufferIndex) using the SAME
    // pipelined/bufferIndex this slot was captured under
    // (FrameDebuggerEventSnapshotRef) - never cache the returned pointer/
    // view across a later BeginFrame() call for that same region, since
    // that physical area is legitimately replaced every framesInFlight real
    // captures.
    const EventSnapshotResource* ReadSnapshot(std::uint32_t region, std::int32_t slot) const noexcept;

    // Pure index helper - region 0 is the synchronous regime's own
    // reserved, single region; regions [1, framesInFlight] are the
    // pipelined regime's own bufferIndex 0..(framesInFlight - 1), offset by
    // one. See this class's own header comment for why the two regimes may
    // never share a region.
    std::uint32_t RegionFor(bool pipelined, std::uint32_t bufferIndex) const noexcept
    {
        return pipelined ? (1u + bufferIndex) : 0u;
    }

private:
    EventSnapshotResource& AcquireSlot(VkExtent2D extent, VkFormat format, VkImageAspectFlags aspect);

    VmaAllocator m_allocator = VK_NULL_HANDLE;
    std::shared_ptr<GpuMemoryTracker> m_tracker;
    VkDevice m_device = VK_NULL_HANDLE;
    bool m_captureEnabled = true;
    std::uint32_t m_slotBudget = 0;
    std::uint32_t m_regionCount = 1;
    std::uint32_t m_activeRegion = 0;
    std::uint32_t m_nextSlot = 0;
    // Pre-sized to slotBudget * regionCount in the constructor body - fixed
    // capacity, never grows/shrinks after that. Indexed as
    // `region * slotBudget + logicalSlot` (see AcquireSlot()).
    std::vector<EventSnapshotResource> m_slots;
};

} // namespace gte::rg
