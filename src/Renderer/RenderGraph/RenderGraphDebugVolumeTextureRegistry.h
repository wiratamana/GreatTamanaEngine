#pragma once

// Volume-texture counterpart of RenderGraphDebugTextureRegistry: a passive name -> snapshot table that
// RenderGraph fills on every ExecuteCompiledGraph(). Every imported volume texture can appear here.

#include "RenderGraphBarrierPlanner.h"
#include "RenderGraphDebugResourceRegistryT.h"
#include "../VolumeTarget.h"

#include <cstdint>
#include <string>

namespace gte::rg {

enum class ExecuteTimingMode : std::uint8_t; // Forward-declared to avoid a circular include with RenderGraph.h.

// One volume texture's current state. Unlike DebugTextureSnapshot it has no depth half, so one ResourceState.
struct DebugVolumeTextureSnapshot {
    std::string name;
    ExecuteTimingMode regime{};

    VolumeTarget target; // image/imageView/extent (VkExtent3D)/format.

    // The layout/stage/access the registry last believes the volume image is in (one state, no depth half).
    ResourceState state;

    // Registry frame counter at the last Upsert().
    std::uint64_t lastUpdatedFrameCounter = 0;

    // Label of the format for GET /list_textures.
    std::string ColorFormatName() const;
};

// The shared base provides Upsert/FindByName/ListAll.
using RenderGraphDebugVolumeTextureRegistry = DebugResourceRegistryT<DebugVolumeTextureSnapshot>;

} // namespace gte::rg
