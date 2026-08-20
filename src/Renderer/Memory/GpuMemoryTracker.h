#pragma once

#include "../Vulkan/VulkanAllocator.h"
#include "GpuResourceHandle.h"

#include <cstdint>
#include <vector>

#if GTE_ENABLE_EDITOR
#include <string>
#include <unordered_map>
#endif

namespace gte {

// What kind of GPU resource a tracked record describes.
enum class GpuResourceType : std::uint8_t {
    Buffer,
    Texture,
};

// Where a resource's memory ACTUALLY lives, as reported by VMA/the driver
// (VkMemoryPropertyFlags) for the allocation it actually got - not what was
// merely requested via BufferMemoryUsage. These can legitimately differ
// (e.g. a CpuToGpu request landing in a small dedicated host-visible+
// device-local heap vs. falling back to plain host-visible system RAM).
enum class GpuMemoryLocation : std::uint8_t {
    GpuOnly, // VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT only - not CPU-accessible at all.
    CpuOnly, // VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT only - system RAM, GPU reads over PCIe.
    Shared,  // Both bits set - same physical memory, zero-copy (UMA iGPU, or ReBAR/SAM on discrete GPUs).
};

// Given a live allocation, classifies which of the above it actually landed
// in. Cheap (a single VMA call, no allocation) - safe to call every time a
// resource is (re)created. See Buffer.cpp/RenderTexture.cpp for use.
GpuMemoryLocation ClassifyGpuMemoryLocation(VmaAllocator allocator, VmaAllocation allocation);

// Compact, POD, hot-path-friendly record - no strings, no debug info. This
// is what exists in EVERY build, including a final shipped game: just
// enough to answer "how much memory, of what kind, is live right now"
// essentially for free. See AGENTS.md for why names are handled completely
// separately (below, Editor-only).
struct GpuResourceRecord {
    GpuResourceType type = GpuResourceType::Buffer;
    GpuMemoryLocation location = GpuMemoryLocation::GpuOnly;
    VkDeviceSize sizeBytes = 0;
};

// Registry of every currently-live GPU allocation (Buffer/RenderTexture),
// addressed by a cheap GpuResourceHandle rather than a pointer or string.
// Backed by a dense slot-map array (indexed directly by handle.index, not a
// hash map) so both random lookup and full iteration over potentially
// thousands of live resources (e.g. the Editor's "Memory" panel - see
// src/Editor/Panels/MemoryPanel.cpp) stay fast and cache-friendly.
//
// Not thread-safe (matches the rest of this single-threaded engine).
//
// Deliberately non-copyable AND non-movable: always create exactly one and
// own it via std::shared_ptr (see Renderer), handing copies of that
// shared_ptr to every Buffer/RenderTexture it creates. That way tracking
// stays valid no matter how the owning Renderer/VulkanAllocator get moved
// around later - a Buffer/RenderTexture holding a raw pointer/reference to
// this class instead would risk dangling if the owner ever relocated.
class GpuMemoryTracker {
public:
    GpuMemoryTracker() = default;

    GpuMemoryTracker(const GpuMemoryTracker&) = delete;
    GpuMemoryTracker& operator=(const GpuMemoryTracker&) = delete;
    GpuMemoryTracker(GpuMemoryTracker&&) = delete;
    GpuMemoryTracker& operator=(GpuMemoryTracker&&) = delete;

    // Registers a newly created resource and returns the handle the caller
    // (Buffer/RenderTexture) must hold onto and pass back to Untrack() when
    // that exact allocation goes away.
    //
    // IMPORTANT: any lifecycle method that destroys and recreates the
    // underlying VMA allocation (e.g. RenderTexture::Resize()) is creating
    // a genuinely new allocation, and MUST Untrack() the old handle and
    // Track() a fresh one reflecting the new size/location as part of that
    // same operation - see AGENTS.md ("GPU resource memory tracking"). The
    // record here must always reflect the CURRENT actual allocation, never
    // a stale snapshot from whenever the resource was first constructed.
    GpuResourceHandle Track(GpuResourceType type, GpuMemoryLocation location, VkDeviceSize sizeBytes);

    // Removes a resource's record. Safe to call with an already-untracked
    // or otherwise invalid handle (no-op).
    void Untrack(GpuResourceHandle handle);

    struct Totals {
        VkDeviceSize totalBytes = 0;
        VkDeviceSize bufferBytes = 0;
        VkDeviceSize textureBytes = 0;
        VkDeviceSize gpuOnlyBytes = 0;
        VkDeviceSize cpuOnlyBytes = 0;
        VkDeviceSize sharedBytes = 0;
        std::size_t bufferCount = 0;
        std::size_t textureCount = 0;
    };
    // O(1) - maintained incrementally by Track()/Untrack(), never
    // recomputed by summing every live record.
    Totals GetTotals() const noexcept { return m_totals; }

    struct Entry {
        GpuResourceHandle handle;
        GpuResourceRecord record;
    };
    // Snapshot of every currently-live resource. Always available (even
    // outside the Editor) since it carries no names/strings - just cheap
    // PODs - consumed by the Editor's "Memory" panel (see
    // src/Editor/Panels/MemoryPanel.cpp).
    std::vector<Entry> GetAllResources() const;

#if GTE_ENABLE_EDITOR
    // Editor-only: attaches/reads a human-readable label for a handle,
    // displayed by the Editor's "Memory" panel (see
    // src/Editor/Panels/MemoryPanel.cpp). Stored in a completely
    // separate table from the hot GpuResourceRecord data above (see
    // AGENTS.md) - compiled out ENTIRELY (not just unused/empty) when
    // GTE_ENABLE_EDITOR is OFF, so a release build carries zero string cost
    // for this.
    void SetDebugName(GpuResourceHandle handle, const char* name);
    // Returns an empty string for an unnamed/invalid/unknown handle.
    const std::string& GetDebugName(GpuResourceHandle handle) const;
#endif

private:
    struct Slot {
        GpuResourceRecord record{};
        std::uint32_t generation = 0; // Persists across free/reuse cycles - never reset to 0.
        bool occupied = false;
    };

    static std::uint64_t PackHandle(GpuResourceHandle handle) noexcept
    {
        return (static_cast<std::uint64_t>(handle.index) << 32) | handle.generation;
    }

    void AddToTotals(const GpuResourceRecord& record);
    void RemoveFromTotals(const GpuResourceRecord& record);

    std::vector<Slot> m_slots;
    std::vector<std::uint32_t> m_freeList;
    Totals m_totals;

#if GTE_ENABLE_EDITOR
    std::unordered_map<std::uint64_t, std::string> m_debugNames;
#endif
};

} // namespace gte
