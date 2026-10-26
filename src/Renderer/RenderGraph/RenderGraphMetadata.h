#pragma once

// Everything the Render Graph panel shows, as one engine-free, JSON-able object: both regimes, features,
// device indirect-draw caps and tracked GPU memory. One object backs the panel tables, GET /render_graph
// and the DOT export. BuildRenderGraphMetadata() is a pure function of plain data.

#include "RenderGraphSnapshot.h"
#include "RenderGraphTypes.h"
#include "../Culling/ClusterCullMath.h"
#include "../../Core/Plugins/RenderFeatureDebugEntry.h"

#include <nlohmann/json.hpp>

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

// One pass, presentation-ready: enums as ToString() text, timing already formatted.
struct RenderGraphPassMetadata {
    std::string name;
    bool isCulled = false;
    std::string kind;              // rg::ToString(PassKind)
    std::string category;          // rg::ToString(RenderPassCategory)
    std::string drawKind;          // rg::ToString(RenderPassDrawKind)
    std::string viewScope;         // rg::ToString(ViewScope)
    std::string renderPassEvent;   // rg::ToString(RenderPassEvent)
    std::uint32_t renderPassEventOrder = 0; // static_cast<std::uint32_t>(RenderPassEvent) - sortable, unlike the text above.
    std::string owningFeatureName; // Which feature owns this pass - always real, never optional.
    std::vector<RenderGraphResourceRefMetadata> reads;
    std::vector<RenderGraphResourceRefMetadata> writes;
    std::uint32_t drawCallCount = 0;   // 0 for a culled pass.
    std::uint32_t triangleCount = 0;   // same rule.
    std::uint32_t indirectDrawCount = 0; // Indirect draws issued; their triangles are GPU-side and not counted above.
    std::string gpuTimingText;        // Formatted GPU timing text; the status word when not measured.
    std::string gpuTimingStatus = "Absent"; // rg::ToString(GpuReadState) of this pass's record.
    std::optional<double> gpuTimingMilliseconds; // Barrier + work as a number, nullopt unless the status is Present.
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
    std::string gpuFrameStatus = "Absent";          // rg::ToString(GpuReadState) of the whole frame.
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
    std::uint32_t schemaVersion = 1; // Bump on any future breaking JSON shape change.
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

// All RenderGraphPassMetadata sharing one `name` (e.g. a per-view pass declared once per view), as one row.
// `instances` keeps the raw entries in original order for click-to-expand.
struct RenderGraphGroupedPassMetadata {
    std::string name;
    // Which views contributed a non-culled instance ("Game+Scene", "Game only", "Shared"); lists every
    // instance's view scope when all were culled.
    std::string viewLabel;
    // True only if every instance was culled.
    bool isCulled = false;
    std::uint32_t drawCallCount = 0; // Summed across every instance sharing this name.
    std::uint32_t triangleCount = 0; // Same rule.
    // Per-view timing, e.g. "Game: 0.12 ms, Scene: 0.08 ms" (not summed); a single instance shows its own text.
    std::string gpuTimingText;
    std::vector<std::string> reads;  // Combined, de-duplicated resource names across every instance (order-preserving, first-seen).
    std::vector<std::string> writes; // Same rule.
    std::vector<RenderGraphPassMetadata> instances; // Raw, ungrouped, original per-instance breakdown, original relative order.
};

// Groups `ungrouped` by `name` in sorted order, so row order never depends on input order.
std::vector<RenderGraphGroupedPassMetadata> GroupPassMetadataByName(
    const std::vector<RenderGraphPassMetadata>& ungrouped);

// ADL to_json; the nested ones are file-local to RenderGraphMetadata.cpp.
void to_json(nlohmann::json& j, const RenderGraphMetadata& metadata);

} // namespace gte::rg

namespace gte {

// to_json() for this lives in RenderGraphMetadata.cpp so its header needs no json include.
void to_json(nlohmann::json& j, const RenderFeatureDebugEntry& entry);

} // namespace gte
