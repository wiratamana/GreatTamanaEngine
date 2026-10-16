#pragma once

#include "../Core/PixelFormat.h"
#include "../Renderer/Memory/GpuMemoryTracker.h"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace gte {

// One display-ready row of the "Memory" panel: a GpuMemoryTracker entry plus its resolved debug name.
// Plain data, free of ImGui and device knowledge, so BuildMemoryRows() is unit-testable.
struct MemoryRow {
    GpuResourceHandle handle;
    std::string name; // Empty if the resource was never given a debug name.
    GpuResourceType type = GpuResourceType::Buffer;
    GpuMemoryLocation location = GpuMemoryLocation::GpuOnly;
    uint64_t sizeBytes = 0;
    // Undefined for a buffer, or for a texture format the engine has no PixelFormat for.
    PixelFormat format = PixelFormat::Undefined;
};

// Builds one row per entry, named through `nameLookup`, sorted by size descending (ties by handle index).
std::vector<MemoryRow> BuildMemoryRows(const std::vector<GpuMemoryTracker::Entry>& entries,
    const std::function<std::string(GpuResourceHandle)>& nameLookup);

// Human-readable byte count: "512 B", "12.30 KB", "4.00 MB", "1.20 GB" (base 1024).
std::string FormatBytes(std::uint64_t bytes);

// Labels for the panel's "Type" and "Location" columns.
const char* ToString(GpuResourceType type);
const char* ToString(GpuMemoryLocation location);

// Label for the "Format" column: "-" for a buffer, "Other" for a texture format without a PixelFormat.
std::string FormatColumnLabel(GpuResourceType type, PixelFormat format);

// One display-ready row of the heap budget section: driver-reported usage and budget of one memory heap,
// next to what the allocator itself holds. Compare it with the tracked total to spot untracked memory.
struct HeapBudgetRow {
    std::uint32_t heapIndex = 0;
    uint64_t vmaBlockBytes = 0;          // Bytes the allocator requested from the driver for this heap.
    std::uint32_t vmaBlockCount = 0;     // Number of memory blocks the allocator holds for this heap.
    std::uint32_t vmaAllocationCount = 0; // Number of sub-allocations living inside those blocks.
    uint64_t usageBytes = 0;             // Driver-reported current usage of this heap by this process.
    uint64_t budgetBytes = 0;            // Driver-reported estimated ceiling for this process.
};

// Reshapes the allocator's raw budget entries into rows, in heap-index order.
std::vector<HeapBudgetRow> BuildHeapBudgetRows(const std::vector<VmaBudget>& budgets);

// "64.00 MB across 1 block (3 sub-allocations)", with correct singular and plural wording.
std::string FormatBlockSummary(uint64_t bytes, std::uint32_t blockCount, std::uint32_t allocationCount);

} // namespace gte
