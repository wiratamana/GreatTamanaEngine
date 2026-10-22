#include "MemoryPanelData.h"

#include "../Renderer/VulkanFormatInfo.h"
#include <algorithm>
#include <cstdio>

namespace gte {

std::vector<MemoryRow> BuildMemoryRows(const std::vector<GpuMemoryTracker::Entry>& entries,
    const std::function<std::string(GpuResourceHandle)>& nameLookup)
{
    std::vector<MemoryRow> rows;
    rows.reserve(entries.size());
    for (const GpuMemoryTracker::Entry& entry : entries) {
        MemoryRow row;
        row.handle = entry.handle;
        row.name = nameLookup ? nameLookup(entry.handle) : std::string();
        row.type = entry.record.type;
        row.location = entry.record.location;
        row.sizeBytes = entry.record.sizeBytes;
        row.format = FromVkFormat(entry.record.format);
        rows.push_back(std::move(row));
    }

    std::sort(rows.begin(), rows.end(), [](const MemoryRow& a, const MemoryRow& b) {
        if (a.sizeBytes != b.sizeBytes) {
            return a.sizeBytes > b.sizeBytes;
        }
        return a.handle.index < b.handle.index;
    });

    return rows;
}

std::string FormatBytes(std::uint64_t bytes)
{
    constexpr std::uint64_t kKiB = 1024ull;
    constexpr std::uint64_t kMiB = kKiB * 1024ull;
    constexpr std::uint64_t kGiB = kMiB * 1024ull;

    char buffer[64];
    if (bytes >= kGiB) {
        std::snprintf(buffer, sizeof(buffer), "%.2f GB", static_cast<double>(bytes) / static_cast<double>(kGiB));
    } else if (bytes >= kMiB) {
        std::snprintf(buffer, sizeof(buffer), "%.2f MB", static_cast<double>(bytes) / static_cast<double>(kMiB));
    } else if (bytes >= kKiB) {
        std::snprintf(buffer, sizeof(buffer), "%.2f KB", static_cast<double>(bytes) / static_cast<double>(kKiB));
    } else {
        std::snprintf(buffer, sizeof(buffer), "%llu B", static_cast<unsigned long long>(bytes));
    }
    return std::string(buffer);
}

const char* ToString(GpuResourceType type)
{
    switch (type) {
    case GpuResourceType::Buffer:
        return "Buffer";
    case GpuResourceType::Texture:
        return "Texture";
    }
    return "Unknown";
}

const char* ToString(GpuMemoryLocation location)
{
    switch (location) {
    case GpuMemoryLocation::GpuOnly:
        return "GPU only (device-local)";
    case GpuMemoryLocation::CpuOnly:
        return "CPU only (host-visible)";
    case GpuMemoryLocation::Shared:
        return "Shared (zero-copy)";
    }
    return "Unknown";
}

std::string FormatColumnLabel(GpuResourceType type, PixelFormat format)
{
    if (type == GpuResourceType::Buffer) {
        return "-"; // A format does not apply to a raw byte buffer.
    }
    switch (format) {
    case PixelFormat::BGRA8_UNorm:
        return "Texture (BGRA8 UNORM)";
    case PixelFormat::RGBA8_UNorm:
        return "Texture (RGBA8 UNORM)";
    case PixelFormat::RGBA8_SRGB:
        return "Texture (RGBA8 SRGB)";
    case PixelFormat::RGBA16_Float:
        return "Texture (RGBA16 Float)";
    case PixelFormat::R8_UNorm:
        return "Texture (R8 UNORM)";
    case PixelFormat::R32_Float:
        return "Texture (R32 Float)";
    case PixelFormat::D32_Float:
        return "Depth Texture (D32 Float, no stencil)";
    case PixelFormat::D32_Float_S8_UInt:
        return "Depth+Stencil Texture (D32 Float + S8 UInt)";
    case PixelFormat::D24_UNorm_S8_UInt:
        return "Depth+Stencil Texture (D24 UNORM + S8 UInt)";
    case PixelFormat::Undefined:
        break;
    }
    return "Other";
}

std::vector<HeapBudgetRow> BuildHeapBudgetRows(const std::vector<VmaBudget>& budgets)
{
    std::vector<HeapBudgetRow> rows;
    rows.reserve(budgets.size());
    for (std::size_t i = 0; i < budgets.size(); ++i) {
        const VmaBudget& budget = budgets[i];
        HeapBudgetRow row;
        row.heapIndex = static_cast<std::uint32_t>(i);
        row.vmaBlockBytes = budget.statistics.blockBytes;
        row.vmaBlockCount = budget.statistics.blockCount;
        row.vmaAllocationCount = budget.statistics.allocationCount;
        row.usageBytes = budget.usage;
        row.budgetBytes = budget.budget;
        rows.push_back(row);
    }
    return rows;
}

std::string FormatBlockSummary(uint64_t bytes, std::uint32_t blockCount, std::uint32_t allocationCount)
{
    char buffer[128];
    std::snprintf(buffer, sizeof(buffer), "%s across %u %s (%u %s)", FormatBytes(bytes).c_str(), blockCount,
        blockCount == 1 ? "block" : "blocks", allocationCount, allocationCount == 1 ? "sub-allocation" : "sub-allocations");
    return std::string(buffer);
}

} // namespace gte
