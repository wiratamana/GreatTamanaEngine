#include "RenderGraphSnapshotFormatting.h"

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

} // namespace gte::rg
