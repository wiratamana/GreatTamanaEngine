#pragma once

#include "../Renderer/Memory/GpuMemoryTracker.h"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace gte {

// One display-ready row for the Editor's "Memory" panel (see
// Panels/MemoryPanel.cpp) - a GpuMemoryTracker::Entry plus whatever debug
// name (if any) GpuMemoryTracker::GetDebugName() has for its handle, both
// already resolved into plain, ImGui-agnostic data. Deliberately kept free
// of ImGui/Renderer/live-Vulkan-device knowledge so BuildMemoryRows() below
// is Tier-1-testable exactly like the rest of this engine's pure ECS/math
// logic (see AGENTS.md, "Testability & Regression Safety") despite living
// under src/Editor/ - the same reasoning that already makes EditorCamera
// (src/Editor/EditorCamera.h) Tier-1-testable.
struct MemoryRow {
    GpuResourceHandle handle;
    std::string name; // Empty if the resource was never given a debug name.
    GpuResourceType type = GpuResourceType::Buffer;
    GpuMemoryLocation location = GpuMemoryLocation::GpuOnly;
    VkDeviceSize sizeBytes = 0;
};

// Builds one MemoryRow per entry in `entries`, naming each via `nameLookup`
// (in real use, Renderer::GetMemoryDebugName(); a test can supply any stand-
// in), sorted by sizeBytes descending, ties broken by handle index for a
// stable/reproducible order. Biggest-contributor-first is Unity's own
// Memory Profiler default sort, and is what actually answers "what's
// contributing to memory usage" at a glance rather than requiring the user
// to sort a raw list themselves every time.
std::vector<MemoryRow> BuildMemoryRows(const std::vector<GpuMemoryTracker::Entry>& entries,
    const std::function<std::string(GpuResourceHandle)>& nameLookup);

// Formats a byte count as a human-readable string ("512 B", "12.30 KB",
// "4.00 MB", "1.20 GB") - base-1024 (KiB/MiB/GiB magnitude, "KB"/"MB"/"GB"
// label, matching Unity's own Memory Profiler display convention). Pulled
// out as its own pure function so Panels/MemoryPanel.cpp and its test share
// the exact same formatting rather than risking the two silently drifting
// apart.
std::string FormatBytes(std::uint64_t bytes);

// Short, human-readable labels for the panel's "Type"/"Location" columns -
// pure lookups, kept here (rather than duplicated ad hoc inside
// Panels/MemoryPanel.cpp) so they're covered by the same test file as
// BuildMemoryRows()/FormatBytes() above.
const char* ToString(GpuResourceType type);
const char* ToString(GpuMemoryLocation location);

// One display-ready row for the Editor's "Memory" panel's VMA heap budget
// section - the REAL, driver-reported usage/budget for one Vulkan memory
// heap (see VulkanAllocator::GetHeapBudgets()/Renderer::GetVmaHeapBudgets()),
// as opposed to GpuMemoryTracker's tally of just this engine's own tracked
// Buffer/RenderTexture resources (MemoryRow above). Comparing the two is
// exactly how you tell whether GpuMemoryTracker's numbers plausibly account
// for everything Task Manager/a GPU tool would show for this heap, or
// whether something else (the swapchain's own images, ImGui's Vulkan
// backend, driver/loader overhead, ...) is unaccounted for.
struct HeapBudgetRow {
    std::uint32_t heapIndex = 0;
    VkDeviceSize vmaBlockBytes = 0;      // Bytes VMA itself has actually requested from the driver for this heap.
    std::uint32_t vmaBlockCount = 0;     // Number of separate VkDeviceMemory blocks VMA holds for this heap.
    std::uint32_t vmaAllocationCount = 0; // Number of individual sub-allocations (Buffer/RenderTexture, ...) living inside those blocks.
    VkDeviceSize usageBytes = 0;    // Driver-reported CURRENT usage of this heap by this process.
    VkDeviceSize budgetBytes = 0;   // Driver-reported ESTIMATED ceiling this process can use of this heap.
};

// Pure reshape of VulkanAllocator::GetHeapBudgets()'s raw VmaBudget entries
// into the plain rows above, in heap-index order - no live VmaAllocator
// needed (the caller already fetched `budgets`), so this stays
// Tier-1-testable like BuildMemoryRows() above.
std::vector<HeapBudgetRow> BuildHeapBudgetRows(const std::vector<VmaBudget>& budgets);

// Formats a block/sub-allocation summary for the "VMA Allocated" column -
// e.g. "64.00 MB across 1 block (3 sub-allocations)" - handling singular vs.
// plural wording correctly. This is exactly the story behind a
// GpuMemoryTracker total that looks much smaller than VMA's own block size:
// VMA doesn't call vkAllocateMemory per-resource (slow, and limited by
// maxMemoryAllocationCount) - it reserves whole blocks up front and
// sub-allocates individual resources out of them, so a block can visibly be
// mostly-unused headroom rather than a sign of a tracking gap. Pulled out as
// its own pure function (like FormatBytes() above) so Panels/MemoryPanel.cpp
// and its test share the exact same phrasing.
std::string FormatBlockSummary(VkDeviceSize bytes, std::uint32_t blockCount, std::uint32_t allocationCount);

} // namespace gte
