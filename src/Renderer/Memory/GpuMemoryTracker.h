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
// separately (below) - editor-core-separation-1 campaign, PHASE4: names are
// no longer "compiled out when GTE_ENABLE_EDITOR is OFF" (that macro/switch
// is being removed campaign-wide) - they are simply never STORED by this
// class at all, in any build; see SetDebugName()/SetDebugNameObserver()
// below.
//
// `format` is VK_FORMAT_UNDEFINED for a Buffer (format is meaningless for a
// raw byte buffer) and the resource's actual VkImageCreateInfo::format for a
// Texture (e.g. VK_FORMAT_B8G8R8A8_UNORM for a color RenderTexture,
// VK_FORMAT_D32_SFLOAT for a DepthBuffer) - a plain POD enum value, not a
// string/name, so it stays in this always-compiled record rather than being
// routed through the observer hook below (see AGENTS.md, "GPU Resource
// Memory Tracking" - only human-readable *names* are handled that way, not
// this kind of classification data, which the Editor's "Memory" panel needs
// in every build the panel itself is compiled into to show a "Format"
// column).
struct GpuResourceRecord {
    GpuResourceType type = GpuResourceType::Buffer;
    GpuMemoryLocation location = GpuMemoryLocation::GpuOnly;
    VkDeviceSize sizeBytes = 0;
    VkFormat format = VK_FORMAT_UNDEFINED;
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
//
// editor-core-separation-1 campaign, PHASE4
// (task_manager/editor-core-separation-1/PHASE4_GPU_MEMORY_TRACKER_BUCKET_A_EXTRACTION.md):
// this class carries ZERO `#if GTE_ENABLE_EDITOR` (or any other "is this an
// Editor build" macro) anywhere, and stores ZERO name/string data itself -
// human-readable debug names are now entirely an Editor-owned concern (see
// src/Editor/EditorGpuMemoryNameOverlay.h), which observes SetDebugName()/
// Untrack() calls via the always-compiled, unconditional hook below rather
// than this class keeping its own std::unordered_map<..., std::string>.
// This is the concrete, empirically-confirmed answer to this phase's own
// documented "real design fork" (does GpuMemoryTracker need a hook, or can
// the Editor-side overlay own everything): reading Buffer.cpp/DepthBuffer.cpp/
// Texture2D.cpp/VolumeTexture.cpp/RenderTexture.cpp confirmed SetDebugName()
// is always called immediately after Track(), from inside the exact
// gte_core-destined constructor that produced the handle - the Editor-side
// overlay has no way to intercept that moment on its own, so a hook here is
// unavoidable; storing the actual name strings, however, does NOT need to
// live here at all, hence the hook/hand-the-data-out shape below rather than
// keeping GpuMemoryTracker's own internal map merely macro-ungated.
class GpuMemoryTracker {
public:
    GpuMemoryTracker() = default;

    GpuMemoryTracker(const GpuMemoryTracker&) = delete;
    GpuMemoryTracker& operator=(const GpuMemoryTracker&) = delete;
    GpuMemoryTracker(GpuMemoryTracker&&) = delete;
    GpuMemoryTracker& operator=(GpuMemoryTracker&&) = delete;

    // Registers a newly created resource and returns the handle the caller
    // (Buffer/RenderTexture) must hold onto and pass back to Untrack() when
    // that exact allocation goes away. `format` is only meaningful for
    // GpuResourceType::Texture (VK_FORMAT_UNDEFINED, the default, is correct
    // for a Buffer - see GpuResourceRecord::format above).
    //
    // IMPORTANT: any lifecycle method that destroys and recreates the
    // underlying VMA allocation (e.g. RenderTexture::Resize()) is creating
    // a genuinely new allocation, and MUST Untrack() the old handle and
    // Track() a fresh one reflecting the new size/location as part of that
    // same operation - see AGENTS.md ("GPU resource memory tracking"). The
    // record here must always reflect the CURRENT actual allocation, never
    // a stale snapshot from whenever the resource was first constructed.
    GpuResourceHandle Track(GpuResourceType type, GpuMemoryLocation location, VkDeviceSize sizeBytes,
        VkFormat format = VK_FORMAT_UNDEFINED);

    // Removes a resource's record. Safe to call with an already-untracked
    // or otherwise invalid handle (no-op). Also notifies the installed
    // debug-name observer (if any) that `handle` should be forgotten - see
    // SetDebugNameObserver()/SetDebugName() below.
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

    // editor-core-separation-1 campaign, PHASE4 - always compiled, no macro
    // of any kind. A side-table OBSERVER hook: install one (via
    // SetDebugNameObserver() below) to be notified, in real time, of every
    // SetDebugName()/Untrack() call THIS tracker instance makes, so an
    // external type (e.g. the Editor-owned src/Editor/EditorGpuMemoryNameOverlay.h)
    // can maintain its OWN name storage keyed by GpuResourceHandle, without
    // this class storing a single byte of name/string data itself.
    // `userData` is an opaque pointer forwarded back verbatim on every call
    // (never interpreted by GpuMemoryTracker itself) - lets the observer
    // avoid a global/static lookup of its own if it doesn't want one.
    // `name` is nullptr specifically on an Untrack() notification (an
    // explicit "forget this handle" signal, never a real name to store) -
    // see Untrack() above and SetDebugName() below.
    using DebugNameObserver = void (*)(void* userData, GpuResourceHandle handle, const char* name);

    // Installs `observer` (plus `userData`, forwarded back on every call) as
    // this tracker's ONE debug-name observer. Safe to call more than once -
    // the LATEST call always wins (last-write-wins, documented, not
    // asserted - mirrors Core/LogSink.h's own InstallLogSink() precedent).
    // Pass nullptr to uninstall - the default, so a Player host that never
    // calls this pays only one branch (a null-pointer check) per
    // SetDebugName()/Untrack() call, and zero name-storage cost at all.
    void SetDebugNameObserver(DebugNameObserver observer, void* userData) noexcept
    {
        m_debugNameObserver = observer;
        m_debugNameObserverUserData = userData;
    }

    // Notifies the installed observer (if any) that `handle` should be
    // associated with the human-readable label `name`, displayed by the
    // Editor's "Memory" panel (see src/Editor/Panels/MemoryPanel.cpp) - a
    // complete no-op (beyond one or two cheap branches) for an
    // invalid/default-constructed handle, or when no observer is installed.
    // Called unconditionally (no `#if GTE_ENABLE_EDITOR` guard on either
    // side of this call anymore, anywhere in this engine) by
    // Buffer/DepthBuffer/Texture2D/VolumeTexture/RenderTexture's
    // constructors right after Track() - see AGENTS.md, "GPU Resource
    // Memory Tracking".
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
