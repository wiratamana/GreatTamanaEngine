#pragma once

#include "../Vulkan/VulkanAllocator.h"
#include "GpuResourceHandle.h"

#include <cstdint>
#include <vector>

namespace gte {

// What kind of GPU resource a tracked record describes.
enum class GpuResourceType : std::uint8_t {
    Buffer,
    Texture,
};

// Where a resource's memory actually lives, as reported by VMA for the allocation it got.
// This can differ from the requested BufferMemoryUsage.
enum class GpuMemoryLocation : std::uint8_t {
    GpuOnly, // VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT only - not CPU-accessible at all.
    CpuOnly, // VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT only - system RAM, GPU reads over PCIe.
    Shared,  // Both bits set - same physical memory, zero-copy (UMA iGPU, or ReBAR/SAM on discrete GPUs).
};

// Classifies where a live allocation actually landed. One cheap VMA call, safe to call on every (re)create.
GpuMemoryLocation ClassifyGpuMemoryLocation(VmaAllocator allocator, VmaAllocation allocation);

// Compact POD record kept in every build; names are never stored here (see SetDebugNameObserver()).
// `format` is VK_FORMAT_UNDEFINED for a Buffer and the image format for a Texture.
struct GpuResourceRecord {
    GpuResourceType type = GpuResourceType::Buffer;
    GpuMemoryLocation location = GpuMemoryLocation::GpuOnly;
    uint64_t sizeBytes = 0;
    VkFormat format = VK_FORMAT_UNDEFINED;
};

// Registry of every live GPU allocation, addressed by GpuResourceHandle (dense slot map, not thread-safe).
// Non-copyable and non-movable: owned via shared_ptr so resources never hold a dangling pointer to it.
class GpuMemoryTracker {
public:
    GpuMemoryTracker() = default;

    GpuMemoryTracker(const GpuMemoryTracker&) = delete;
    GpuMemoryTracker& operator=(const GpuMemoryTracker&) = delete;
    GpuMemoryTracker(GpuMemoryTracker&&) = delete;
    GpuMemoryTracker& operator=(GpuMemoryTracker&&) = delete;

    // Registers a new resource; the caller must pass the returned handle to Untrack() when it dies. A resize that
    // recreates the allocation must Untrack() and Track() again so the record matches the current allocation.
    GpuResourceHandle Track(GpuResourceType type, GpuMemoryLocation location, uint64_t sizeBytes,
        VkFormat format = VK_FORMAT_UNDEFINED);

    // Removes a resource's record and tells the debug-name observer to forget it.
    // No-op for an already-untracked or invalid handle.
    void Untrack(GpuResourceHandle handle);

    struct Totals {
        uint64_t totalBytes = 0;
        uint64_t bufferBytes = 0;
        uint64_t textureBytes = 0;
        uint64_t gpuOnlyBytes = 0;
        uint64_t cpuOnlyBytes = 0;
        uint64_t sharedBytes = 0;
        std::size_t bufferCount = 0;
        std::size_t textureCount = 0;
    };
    // O(1): maintained incrementally by Track()/Untrack().
    Totals GetTotals() const noexcept { return m_totals; }

    struct Entry {
        GpuResourceHandle handle;
        GpuResourceRecord record;
    };
    // Snapshot of every live resource: cheap PODs, no names.
    std::vector<Entry> GetAllResources() const;

    // Observer for SetDebugName()/Untrack() calls, so an external type can keep names keyed by handle.
    // `userData` is passed back verbatim; `name` is nullptr on an Untrack() notification ("forget this handle").
    using DebugNameObserver = void (*)(void* userData, GpuResourceHandle handle, const char* name);

    // Installs the one debug-name observer (last call wins). nullptr uninstalls and is the default: a
    // host without an observer pays one null check per SetDebugName()/Untrack() and stores no names.
    void SetDebugNameObserver(DebugNameObserver observer, void* userData) noexcept
    {
        m_debugNameObserver = observer;
        m_debugNameObserverUserData = userData;
    }

    // Associates `handle` with a human-readable label via the installed observer. No-op for an invalid
    // handle or without an observer. Called by every resource constructor right after Track().
    void SetDebugName(GpuResourceHandle handle, const char* name);

private:
    struct Slot {
        GpuResourceRecord record{};
        std::uint32_t generation = 0; // Persists across free/reuse cycles - never reset to 0.
        bool occupied = false;
    };

    void AddToTotals(const GpuResourceRecord& record);
    void RemoveFromTotals(const GpuResourceRecord& record);

    std::vector<Slot> m_slots;
    std::vector<std::uint32_t> m_freeList;
    Totals m_totals;

    DebugNameObserver m_debugNameObserver = nullptr;
    void* m_debugNameObserverUserData = nullptr;
};

} // namespace gte
