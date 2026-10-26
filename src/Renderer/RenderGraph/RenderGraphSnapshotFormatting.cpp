#include "RenderGraphSnapshotFormatting.h"

#include <cstdio>
#include <cstddef>

namespace gte::rg {

const char* ToString(GpuReadState state) noexcept
{
    switch (state) {
    case GpuReadState::Present:
        return "Present";
    case GpuReadState::Partial:
        return "Partial";
    case GpuReadState::Absent:
        return "Absent";
    case GpuReadState::Error:
        return "Error";
    case GpuReadState::Unsupported:
        return "Unsupported";
    case GpuReadState::Inactive:
        return "Inactive";
    }
    return "Unknown";
}

std::string FormatGpuTiming(const GpuPassRecord& record)
{
    const double milliseconds = static_cast<double>(record.barrierMs) + static_cast<double>(record.workMs);
    char buffer[40];
    switch (record.state) {
    case GpuReadState::Present:
        std::snprintf(buffer, sizeof(buffer), "%.2f ms", milliseconds);
        return std::string(buffer);
    case GpuReadState::Partial:
        std::snprintf(buffer, sizeof(buffer), "~%.2f ms (partial)", milliseconds);
        return std::string(buffer);
    case GpuReadState::Absent:
    case GpuReadState::Error:
    case GpuReadState::Unsupported:
    case GpuReadState::Inactive:
        break;
    }
    return ToString(record.state);
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
