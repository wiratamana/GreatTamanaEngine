#include "RenderGraphSnapshotFormatting.h"

#include <cstdio>
#include <cstddef>

namespace gte::rg {

// Relocated verbatim (character-for-character body) from
// RenderGraphPanel.cpp's own anonymous-namespace FormatGpuTiming() - see
// that function's original doc comment there for the "why" (mirrors
// Panels/ProfilerPanel.cpp's own FormatGpuTimingLine() convention exactly -
// never a fabricated "0.00 ms" for Absent/Unsupported, see AGENTS.md,
// "Profiling").
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

// Relocated verbatim from RenderGraphPanel.cpp's own anonymous namespace.
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

// Relocated (body unchanged) and RENAMED from RenderGraphPanel.cpp's own
// anonymous-namespace PassNameAtSurvivingIndex() - resolves a resource's
// first/last-use POSITION (an index into snapshot.passesInExecutionOrder's
// own surviving prefix - see RenderGraphResourceSnapshot's own doc comment)
// back into that pass's real NAME - a raw integer index is meaningless to a
// human reader; a pass name is what actually answers "when is this resource
// alive".
const char* ResolvePassNameAtSurvivingIndex(const RenderGraphSnapshot& snapshot, std::int32_t index)
{
    if (index < 0 || static_cast<std::size_t>(index) >= snapshot.passesInExecutionOrder.size()) {
        return "?";
    }
    const std::string& name = snapshot.passesInExecutionOrder[static_cast<std::size_t>(index)].name;
    return name.empty() ? "(unnamed)" : name.c_str();
}

// NEW (this phase) - mirrors RenderGraphTypes.cpp's own ToString()
// precedent exactly: a plain switch, deliberately NO `default:` case, so a
// future new ResourceKind enumerator fails to compile here until updated -
// the same "no silent fallback" discipline DispatchByKind() (RenderGraphTypes.h)
// already documents for ResourceKind.
const char* ToString(ResourceKind kind) noexcept
{
    switch (kind) {
    case ResourceKind::Texture:
        return "Texture";
    case ResourceKind::Buffer:
        return "Buffer";
    case ResourceKind::VolumeTexture:
        return "VolumeTexture";
    }
    return "Unknown";
}

// NEW (this phase) - same "no default: case" discipline, for ViewScope.
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
