#pragma once

// The single, JSON-able, engine-free "everything the Render Graph panel
// shows, in one object" reshape. Sits one level above RenderGraphSnapshot.h
// (never inside it): RenderGraphSnapshot keeps its own narrow "one regime's
// worth of already-executed passes/resources" contract untouched; this file
// folds both regimes of that, plus GpuDrivenBatchDebugInfo and
// RenderFeatureDebugEntry, into one object, with every enum already
// resolved to a human string and every tag bitmask resolved to at most one
// human label.
//
// BuildRenderGraphMetadata() is a pure function of already-computed plain
// data - no live RenderGraph/VkDevice/Renderer/Core - directly testable
// with hand-fabricated inputs.
//
// This is the one object that backs the Render Graph panel's ImGui tables,
// GET /render_graph's JSON response (via to_json() below), and the
// "Export DOT" Graphviz output - never three independently-maintained
// presentation paths.

#include "RenderGraphSnapshot.h"
#include "RenderGraphTypes.h"
#include "../Culling/GpuDrivenBatchDebugInfo.h"
#include "../../Core/Plugins/RenderFeatureDebugEntry.h"

#include <nlohmann/json.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace gte::rg {

// One resource reference from a pass's own reads/writes list - name plus
// already-resolved ResourceKind string. A single array of small structs
// (not parallel arrays) since a JSON consumer prefers {"name":...,
// "kind":...} pairs over zipping two same-length arrays itself.
struct RenderGraphResourceRefMetadata {
    std::string name;
    std::string kind; // rg::ToString(ResourceKind) - "Texture" | "Buffer" | "VolumeTexture" | "TextureArray".
    std::string bindingStageLabel; // rg::BindingStageLabel(ResourceAccess) at declaration time.
    // "<old> -> <new>" barrier transition label - only set for a write that
    // actually required a barrier this frame. nullopt for every read, and
    // for a write with no barrier applied this frame.
    std::optional<std::string> barrierTransitionLabel;
};

// One pass, fully presentation-ready - every enum already resolved to its
// ToString() text, GPU timing already formatted.
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
    std::string gpuTimingText;        // Formatted GPU timing text - "N/A" for a culled pass too.
    std::optional<double> gpuTimingMilliseconds; // Same value as a number, nullopt unless actually present.
};

// One resource, fully presentation-ready. Raw indices are kept (a caller
// cross-referencing this same response's own `passes` array positionally
// still wants them) but the referenced pass's name is also resolved here,
// so this response is meaningful without the caller re-indexing anything.
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
    bool timingSlotBudgetExhausted = false;
};

// The single, top-level, JSON-able object. Offscreen/present are two named
// fields (never an array) since there are always exactly two regimes;
// gpuDrivenBatches/renderFeatures are folded in here too so one response
// carries everything the panel needs.
struct RenderGraphMetadata {
    std::uint32_t schemaVersion = 1; // Bump on any future breaking JSON shape change.
    RenderGraphRegimeMetadata offscreenRegime; // ExecuteTimingMode::SynchronousImmediateReadback
    RenderGraphRegimeMetadata presentRegime;   // ExecuteTimingMode::PipelinedDeferredReadback
    std::vector<GpuDrivenBatchDebugInfo> gpuDrivenBatches;   // Reused directly, never re-wrapped.
    std::vector<RenderFeatureDebugEntry> renderFeatures;     // Reused directly, never re-wrapped.
};

// Pure, testable - takes already-resolved RenderGraphSnapshot/
// GpuDrivenBatchDebugInfo/RenderFeatureDebugEntry values, never a live
// RenderGraph&/Core&. `offscreen`/`present` map 1:1 onto
// RenderGraphMetadata::offscreenRegime/presentRegime.
RenderGraphMetadata BuildRenderGraphMetadata(const RenderGraphSnapshot& offscreen, const RenderGraphSnapshot& present,
    const std::vector<GpuDrivenBatchDebugInfo>& gpuDrivenBatches,
    const std::vector<RenderFeatureDebugEntry>& renderFeatures);

// One row's worth of pass metadata grouped by name: unifies every
// RenderGraphPassMetadata instance sharing the same `name` (e.g. a
// per-active-view pass declared once per view) into exactly one row.
// `instances` keeps every ungrouped, raw RenderGraphPassMetadata this group
// was built from, in original relative order, for a click-to-expand
// raw-breakdown UX - never re-derived/re-fetched.
struct RenderGraphGroupedPassMetadata {
    std::string name;
    // Human-readable label for which view(s) contributed a real, non-culled
    // instance this frame (e.g. "Game+Scene", "Game only", "Shared"). If
    // every instance sharing this name was culled, falls back to listing
    // every instance's own viewScope instead.
    std::string viewLabel;
    // True only if every instance sharing this name was culled this frame -
    // a pass surviving in one view but culled in another must not read as
    // fully culled.
    bool isCulled = false;
    std::uint32_t drawCallCount = 0; // Summed across every instance sharing this name.
    std::uint32_t triangleCount = 0; // Same rule.
    // Per-view GPU timing breakdown, e.g. "Game: 0.12 ms, Scene: 0.08 ms" -
    // never summed/maxed, since GPU timing is not meaningfully additive
    // across logically-separate view passes. A single-instance group shows
    // that instance's own gpuTimingText verbatim.
    std::string gpuTimingText;
    std::vector<std::string> reads;  // Combined, de-duplicated resource names across every instance (order-preserving, first-seen).
    std::vector<std::string> writes; // Same rule.
    std::vector<RenderGraphPassMetadata> instances; // Raw, ungrouped, original per-instance breakdown, original relative order.
};

// Pure, testable - groups `ungrouped` (typically a RenderGraphRegimeMetadata::passes
// vector) by `name`, in sorted, deterministic order so the panel's row order
// never jitters frame-to-frame and never depends on input order.
std::vector<RenderGraphGroupedPassMetadata> GroupPassMetadataByName(
    const std::vector<RenderGraphPassMetadata>& ungrouped);

// ADL free function - nlohmann::json's own standard pattern. Every nested
// to_json() this depends on is defined, TU-locally, inside
// RenderGraphMetadata.cpp.
void to_json(nlohmann::json& j, const RenderGraphMetadata& metadata);

} // namespace gte::rg

namespace gte {

// Small, otherwise-homeless structs get their own to_json() here, in
// namespace gte (matching each struct's own namespace, for correct ADL),
// physically defined inside RenderGraphMetadata.cpp rather than adding a
// nlohmann::json include to GpuDrivenBatchDebugInfo.h/RenderFeatureDebugEntry.h
// themselves.
void to_json(nlohmann::json& j, const GpuDrivenBatchDebugInfo& info);
void to_json(nlohmann::json& j, const RenderFeatureDebugEntry& entry);

} // namespace gte
