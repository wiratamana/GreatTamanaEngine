#include "RenderGraphSnapshotFormatting.h"

#include <cstdio>
#include <cstddef>

namespace gte::rg {

// Never a fabricated "0.00 ms" for Absent or Unsupported.
std::string FormatGpuTiming(const GpuTimingSample& timing)
{
    switch (timing.status) {
    case GpuTimingSample::Status::Present: {
        char buffer[32];
        std::snprintf(buffer, sizeof(buffer), "%.2f ms", timing.milliseconds);
        return std::string(buffer);
    }
    case GpuTimingSample::Status::Unsupported:
        return "Unsupported";
    case GpuTimingSample::Status::Absent:
    default:
        return "N/A";
    }
}

std::string JoinNames(const std::vector<std::string>& names)
{
    if (names.empty()) {
        return "-";
    }
    std::string joined;
    for (const std::string& name : names) {
        if (!joined.empty()) {
            joined += ", ";
        }
        joined += name.empty() ? "(unnamed)" : name;
    }
    return joined;
}

// Name of the pass at a resource's first/last-use position (an index into the surviving prefix).
const char* ResolvePassNameAtSurvivingIndex(const RenderGraphSnapshot& snapshot, std::int32_t index)
{
    if (index < 0 || static_cast<std::size_t>(index) >= snapshot.passesInExecutionOrder.size()) {
        return "?";
    }
    const std::string& name = snapshot.passesInExecutionOrder[static_cast<std::size_t>(index)].name;
    return name.empty() ? "(unnamed)" : name.c_str();
}

// No default case, so a new enumerator warns here.
const char* ToString(ResourceKind kind) noexcept
{
    switch (kind) {
    case ResourceKind::Texture:
        return "Texture";
    case ResourceKind::Buffer:
        return "Buffer";
    case ResourceKind::VolumeTexture:
        return "VolumeTexture";
    case ResourceKind::TextureArray:
        return "TextureArray";
    }
    return "Unknown";
}

// No default case, so a new enumerator warns here.
const char* ToString(ViewScope scope) noexcept
{
    switch (scope) {
    case ViewScope::Shared:
        return "Shared";
    case ViewScope::GameView:
        return "GameView";
    case ViewScope::SceneView:
        return "SceneView";
    }
    return "Unknown";
}

} // namespace gte::rg
