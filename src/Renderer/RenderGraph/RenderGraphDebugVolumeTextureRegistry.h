#pragma once

// The volume-texture counterpart of RenderGraphDebugTextureRegistry.h - a
// pure, Vulkan-header-only name -> physical-volume-texture-snapshot table.
// This class has no opinion about HOW it gets populated - it is a dumb,
// passive store; RenderGraph is what actually calls Upsert() every
// ExecuteCompiledGraph() call.
//
// Every volume texture RenderGraphBuilder::ImportVolumeTexture() ever
// declares, in either ExecuteTimingMode regime, is automatically eligible
// to appear here.

#include "RenderGraphBarrierPlanner.h"
#include "RenderGraphDebugResourceRegistryT.h"
#include "../VolumeTarget.h"

#include <cstdint>
#include <string>

namespace gte::rg {

enum class ExecuteTimingMode : std::uint8_t; // see RenderGraph.h - forward-declared to avoid a circular include.

// One volume texture's full current knowledge - the VolumeTarget
// counterpart of DebugTextureSnapshot, but with no depth half at all: a
// volume texture has no depth-companion concept (see VolumeTarget.h), so
// there is only ONE ResourceState here, never a color/depth pair.
struct DebugVolumeTextureSnapshot {
    std::string name;
    ExecuteTimingMode regime{};

    VolumeTarget target; // image/imageView/extent (VkExtent3D)/format.

    // The layout/stage/access this registry LAST believes the volume image
    // is actually in. Not split into a color/depth pair - see this struct's
    // own header comment above.
    ResourceState state;

    // This registry's own monotonically increasing frame counter value at
    // the moment this entry was last written by Upsert() - NOT touched by
    // ApplyStateOverride(), which corrects state only, never freshness.
    std::uint64_t lastUpdatedFrameCounter = 0;
};

// Upsert/FindByName/ListAll mechanics live in the shared
// DebugResourceRegistryT<DebugVolumeTextureSnapshot> base - this class only
// adds the single-state override a volume texture needs (no color/depth
// split, unlike RenderGraphDebugTextureRegistry).
class RenderGraphDebugVolumeTextureRegistry : public DebugResourceRegistryT<DebugVolumeTextureSnapshot> {
public:
    RenderGraphDebugVolumeTextureRegistry() = default;

    // Overwrites JUST the ResourceState of the entry named `name` (a safe
    // no-op if no such entry exists yet). Deliberately does NOT touch
    // `lastUpdatedFrameCounter` - a state correction is not a fresh capture
    // of contents.
    void ApplyStateOverride(const std::string& name, const ResourceState& newState);
};

} // namespace gte::rg
