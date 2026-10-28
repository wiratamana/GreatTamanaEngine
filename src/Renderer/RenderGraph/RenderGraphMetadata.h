#pragma once

// Plain data behind the Render Graph panel, GET /render_graph and the DOT export.
// View scope and GPU state are typed enums; Editor and Network render the text.

#include "RenderGraphSnapshot.h"
#include "RenderGraphTypes.h"
#include "../Culling/ClusterCullMath.h"
#include "../GpuStampPlan.h"
#include "../../Core/Plugins/RenderFeatureDebugEntry.h"

#include <cstdint>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace gte::rg {

// One resource a pass reads or writes (a struct, not parallel arrays, so JSON gets name/kind pairs).
struct RenderGraphResourceRefMetadata {
    std::string name;
    std::string kind; // rg::ToString(ResourceKind) - "Texture" | "Buffer" | "VolumeTexture" | "TextureArray".
    std::string bindingStageLabel; // rg::BindingStageLabel(ResourceAccess) at declaration time.
    // "<old> -> <new>" label; set only for a write that needed a barrier this frame.
    std::optional<std::string> barrierTransitionLabel;
};

// One pass as plain data: text enums as ToString() text, view scope and GPU state as enums, numbers as numbers.
struct RenderGraphPassMetadata {
    std::string name;
    bool isCulled = false;
    std::string kind;              // rg::ToString(PassKind)
    std::string category;          // rg::ToString(RenderPassCategory)
    std::string drawKind;          // rg::ToString(RenderPassDrawKind)
    ViewScope viewScope = ViewScope::Shared;
    std::string instance;          // Hex id of the pass's binding instance; empty when it never executed.
    std::string renderPassEvent;   // rg::ToString(RenderPassEvent)
    std::uint32_t renderPassEventOrder = 0; // static_cast<std::uint32_t>(RenderPassEvent) - sortable, unlike the text above.
    std::string owningFeatureName; // Which feature owns this pass - always real, never optional.
    std::vector<RenderGraphResourceRefMetadata> reads;
    std::vector<RenderGraphResourceRefMetadata> writes;
    std::uint32_t drawCallCount = 0;   // 0 for a culled pass.
    std::uint32_t triangleCount = 0;   // same rule.
    std::uint32_t indirectDrawCount = 0; // Indirect draws issued; their triangles are GPU-side and not counted above.
    GpuReadState gpuTimingState = GpuReadState::Absent; // State of this pass's GPU record.
    std::optional<double> gpuTimingMilliseconds; // Barrier + work as a number, nullopt unless the status is Present.
    std::optional<double> gpuBarrierMilliseconds; // Barrier part; nullopt unless Present and the barrier was stamped.
    std::optional<double> gpuWorkMilliseconds;    // Work part; nullopt unless the status is Present.
    double gpuRecordedMilliseconds = 0.0; // Barrier + work of the record whatever its status; meaningful for Present/Partial only.
    std::uint32_t gpuFoldedPasses = 0; // Passes this record stands for when it is the overflow bucket.
};

// One resource, presentation-ready. Raw pass indices are kept and the pass names are resolved too.
struct RenderGraphResourceMetadata {
    std::string name;
    bool isImported = false;
    std::int32_t firstUsePassIndex = -1;
    std::int32_t lastUsePassIndex = -1;
    std::optional<std::string> firstUsePassName; // nullopt iff firstUsePassIndex < 0 ("never used").
    std::optional<std::string> lastUsePassName;  // nullopt iff lastUsePassIndex < 0.
};

// One ExecuteTimingMode regime's worth of already-formatted data.
struct RenderGraphRegimeMetadata {
    std::string regimeName; // Exact gte::rg::ExecuteTimingMode enumerator name, so callers can round-trip it.
    std::vector<RenderGraphPassMetadata> passes; // Survivors first, then culled - matches RenderGraphSnapshot order exactly.
    std::vector<RenderGraphResourceMetadata> resources;
    // GPU timing of the frame the passes' records come from.
    GpuReadState gpuFrameStatus = GpuReadState::Absent; // State of the whole frame's GPU timing.
    std::optional<double> gpuSpanMilliseconds;      // Frame span; nullopt unless it was measured.
    std::uint32_t gpuFoldedPasses = 0;              // Passes folded into the overflow bucket.
    std::uint32_t droppedGpuFrames = 0;             // Frames whose GPU data was lost or never read.
};

// One live GPU allocation: its debug name (empty when unnamed) and size.
struct GpuAllocationSample {
    std::string name;
    std::uint64_t bytes = 0;
};

// Tracked GPU memory: grand total, bytes per debug name and the live Mesh object count.
struct RenderGraphGpuMemoryMetadata {
    std::uint64_t totalBytes = 0;
    std::uint64_t meshCount = 0;
    std::map<std::string, std::uint64_t> byName; // Unnamed allocations are summed under kUnnamedGpuAllocation.
};

inline constexpr const char* kUnnamedGpuAllocation = "(unnamed)";

// Pure: sums `samples` per name and in total.
RenderGraphGpuMemoryMetadata BuildGpuMemoryMetadata(std::span<const GpuAllocationSample> samples, std::uint64_t meshCount);

// State of the bounded GPU waits: a lost device is final.
struct RenderGraphGpuWaitMetadata {
    bool deviceLost = false;
    std::uint32_t consecutiveFrameTimeouts = 0;
    std::uint32_t abandonedObjects = 0; // GPU objects parked after a failed wait
};

// The top-level JSON-able object: two named regimes plus features, caps, waits and memory in one response.
struct RenderGraphMetadata {
    std::uint32_t schemaVersion = 2; // Bump on any future breaking JSON shape change.
    RenderGraphRegimeMetadata offscreenRegime; // ExecuteTimingMode::SynchronousImmediateReadback
    RenderGraphRegimeMetadata presentRegime;   // ExecuteTimingMode::PipelinedDeferredReadback
    std::vector<RenderFeatureDebugEntry> renderFeatures;     // Reused directly, never re-wrapped.
    IndirectDrawCapsView indirectDraw;                       // Device indirect draw capabilities.
    RenderGraphGpuWaitMetadata gpuWait;                      // Read on the main thread when published.
    RenderGraphGpuMemoryMetadata gpuMemory;                  // Read on the main thread when published.
};

// Pure: builds from already-resolved snapshots, never a live graph or Core.
RenderGraphMetadata BuildRenderGraphMetadata(const RenderGraphSnapshot& offscreen, const RenderGraphSnapshot& present,
    const std::vector<RenderFeatureDebugEntry>& renderFeatures);

} // namespace gte::rg
