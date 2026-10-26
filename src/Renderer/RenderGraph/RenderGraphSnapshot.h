#pragma once

// Pure, ImGui-free reshape of a compiled graph into something displayable, kept out of RenderGraph.h so
// tests can feed it hand-made inputs. PassGpuStats lives here as the one type shared with RenderGraph.h.

#include "RenderGraphBuilder.h"
#include "RenderGraphDebugMetadataSink.h"
#include "RenderGraphCompiler.h"
#include "../DrawStats.h"
#include "../GpuStampPlan.h"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace gte::rg {

// One executed pass's draw stats and measured GPU record (Absent until a read-back resolved it).
struct PassGpuStats {
    DrawStats drawStats;
    GpuPassRecord gpu;
};

// One pass, ready to display. Names are owned strings, not pointers into PassRecord tables, so a
// snapshot stays valid indefinitely.
struct RenderGraphPassSnapshot {
    std::string name;
    bool isCulled = false;
    std::vector<std::string> readNames;
    std::vector<std::string> writeNames;

    // The next five fields are copied through for surviving and culled passes alike.
    PassKind kind = PassKind::Graphics;
    RenderPassCategory category = RenderPassCategory::General;
    RenderPassDrawKind drawKind = RenderPassDrawKind::DrawMesh;
    ViewScope viewScope = ViewScope::Shared;
    RenderPassEvent renderPassEvent = RenderPassEvent::Opaques;

    // Parallel to readNames/writeNames: the ResourceKind of each entry.
    std::vector<ResourceKind> readKinds;
    std::vector<ResourceKind> writeKinds;

    // Parallel to readNames/writeNames: the declared access, resolved to a label by BindingStageLabel().
    std::vector<ResourceAccess> readAccess;
    std::vector<ResourceAccess> writeAccess;

    // Parallel to writeNames: the barrier transition label, "" when no barrier was applied this frame
    // or no sink is installed. Resolved once at snapshot-build time.
    std::vector<std::string> writeBarrierLabels;

    // Default (empty draw stats, Absent GPU record) for a culled pass.
    PassGpuStats stats;

    // The owning feature; "ENGINE_UNOWNED" when declared outside any RenderFeatureScope.
    std::string owningFeatureName;
};

// One resource, ready to display.
struct RenderGraphResourceSnapshot {
    std::string name;
    bool isImported = false;
    // Indices into the surviving prefix of RenderGraphSnapshot::passesInExecutionOrder (-1 = never used).
    std::int32_t firstUsePassIndex = -1;
    std::int32_t lastUsePassIndex = -1;
};

// The displayable snapshot of one RenderGraph::Execute() call.
struct RenderGraphSnapshot {
    // Surviving passes first in execution order, then culled passes in declaration order. Culled passes
    // are told apart by `isCulled` and are never referenced by a resource's use indices.
    std::vector<RenderGraphPassSnapshot> passesInExecutionOrder;
    std::vector<RenderGraphResourceSnapshot> resources;

    // Frame-level GPU timing of the frame whose records the passes carry; Absent until one was read back.
    GpuFrameReport gpuReport;
    std::uint64_t gpuFrameIndex = 0; // Presenter frame number of that frame (0 in the synchronous regime).
    std::uint32_t droppedGpuFrames = 0; // Frames whose GPU data was lost or never read, since the graph was made.
};

// Reshapes one Execute() call into a snapshot. The lookups resolve per-pass stats, metadata and barrier
// labels by declaration index; an empty function (or false) keeps the defaults.
RenderGraphSnapshot BuildRenderGraphSnapshot(const CompiledGraph& compiled, const CompiledGraphInput& input,
    const std::function<PassGpuStats(std::size_t)>& statsLookup,
    const std::function<bool(std::size_t, PassDebugMetadata&)>& metadataLookup = {},
    const std::function<bool(std::size_t, const std::string&, std::string&)>& barrierLabelLookup = {});

// Name of a declared usage's resource from whichever table applies; empty for a stale or out-of-range
// index. A display helper, never asserts.
std::string ResourceUsageName(const ResourceUsage& usage, const CompiledGraphInput& input);

} // namespace gte::rg
