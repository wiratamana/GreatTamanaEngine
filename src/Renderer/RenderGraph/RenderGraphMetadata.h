#pragma once

// editor-core-separation-7 campaign, PHASE2
// (PHASE2_RENDERGRAPH_METADATA_MODEL_AND_JSON.md) - the single, JSON-able,
// engine-free "everything the Render Graph panel shows, in one object"
// reshape. Sits ONE LEVEL ABOVE RenderGraphSnapshot.h (never inside it - see
// PHASE0_MASTER_STRATEGY.md's Locked Design Decision #11): RenderGraphSnapshot
// keeps its own narrow, already-tested "one regime's worth of already-
// executed passes/resources" contract untouched; this file folds TWO
// regimes of that, PLUS GpuDrivenBatchDebugInfo, PLUS RenderFeatureDebugEntry,
// into one object, with every enum already resolved to a human string and
// every tag bitmask already resolved to at most one human label.
//
// BuildRenderGraphMetadata() is a PURE function of already-computed plain
// data - no live RenderGraph/VkDevice/Renderer/Core - directly Tier-1-
// testable with hand-fabricated inputs, exactly like BuildRenderGraphSnapshot()
// itself already is (see RenderGraphSnapshotTests.cpp's own precedent).
//
// This is the ONE object that simultaneously backs:
//   - RenderGraphPanel::Build()'s ImGui tables (PHASE3) - never a second,
//     independently-hand-maintained presentation path.
//   - GET /render_graph's JSON response body (PHASE4), via to_json() below.
//   - "Export DOT"'s Graphviz output (PHASE3), via
//     src/Editor/RenderGraphDotExport.h/.cpp (gte_editor-tier, NOT a
//     same-folder sibling of this file - see PHASE0_MASTER_STRATEGY.md's
//     Locked Design Decision #14) consuming this exact same struct.
//
// Nothing calls BuildRenderGraphMetadata() from production code yet - this
// phase is purely additive (see PHASE2_RENDERGRAPH_METADATA_MODEL_AND_JSON.md's
// own "Verification" step 3: confirmed zero hits for BuildRenderGraphMetadata(
// outside this file pair and its own test file).

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

// One resource REFERENCE from a pass's own reads/writes list - name +
// already-resolved ResourceKind string, parallel-array-free (unlike
// RenderGraphPassSnapshot's own readNames/readKinds parallel-vector shape) -
// this is deliberately a single array of small structs here, since a JSON
// consumer benefits from {"name":...,"kind":...} pairs far more than two
// same-length parallel arrays it has to zip itself.
struct RenderGraphResourceRefMetadata {
    std::string name;
    std::string kind; // rg::ToString(ResourceKind) - "Texture" | "Buffer" | "VolumeTexture".
};

// One pass, fully presentation-ready - every enum already resolved to its
// ToString() text, every GpuTimingSample already resolved via
// RenderGraphSnapshotFormatting.h's FormatGpuTiming() (PHASE1), tagGroupLabel
// resolved via RenderPassGroupRegistry (at most ONE label - see
// PHASE0_MASTER_STRATEGY.md's Locked Design Decision #5).
struct RenderGraphPassMetadata {
    std::string name;
    bool isCulled = false;
    std::string kind;              // rg::ToString(PassKind)
    std::string category;          // rg::ToString(RenderPassCategory)
    std::string drawKind;          // rg::ToString(RenderPassDrawKind)
    std::string viewScope;         // rg::ToString(ViewScope) (PHASE1's new function)
    std::string renderPassEvent;   // rg::ToString(RenderPassEvent)
    std::optional<std::string> tagGroupLabel; // RenderPassGroupRegistry::FindPassGroupIndexForTags() result, or nullopt.
    std::vector<RenderGraphResourceRefMetadata> reads;
    std::vector<RenderGraphResourceRefMetadata> writes;
    std::uint32_t drawCallCount = 0;   // 0 for a culled pass (mirrors RenderGraphSnapshot's own "culled pass keeps stats at default" rule).
    std::uint32_t triangleCount = 0;   // same rule.
    std::string gpuTimingText;        // FormatGpuTiming() text - "N/A" for a culled pass too (its stats.timing is always default/Absent).
    std::optional<double> gpuTimingMilliseconds; // the SAME value, still numeric (nullopt unless GpuTimingSample::Status::Present), for a machine caller that wants a number, not a string to reparse.
};

// One resource, fully presentation-ready - the raw indices are KEPT (never
// removed - a caller cross-referencing this SAME response's own `passes`
// array positionally still wants them) but the referenced pass's NAME is
// ALSO resolved here, always, so this response is meaningful without the
// caller having to re-index anything (PHASE0_MASTER_STRATEGY.md's Locked
// Design Decision #10).
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
    std::string regimeName; // "SynchronousImmediateReadback" | "PipelinedDeferredReadback" - the EXACT gte::rg::ExecuteTimingMode enumerator name, never a friendlier paraphrase, so a caller can round-trip this string back to the enum if it ever needs to.
    std::vector<RenderGraphPassMetadata> passes; // survivors first, then culled - matches RenderGraphSnapshot::passesInExecutionOrder's own order exactly.
    std::vector<RenderGraphResourceMetadata> resources;
    bool timingSlotBudgetExhausted = false;
};

// THE single, top-level, JSON-able object. See PHASE0_MASTER_STRATEGY.md's
// Locked Design Decision #1 for why offscreen/present are two NAMED fields,
// not an array, and Locked Design Decision #2 for why gpuDrivenBatches AND
// renderFeatures are both folded in here too (not just the two regimes).
struct RenderGraphMetadata {
    std::uint32_t schemaVersion = 1; // bump on any FUTURE breaking JSON shape change - see PHASE4.
    RenderGraphRegimeMetadata offscreenRegime; // ExecuteTimingMode::SynchronousImmediateReadback
    RenderGraphRegimeMetadata presentRegime;   // ExecuteTimingMode::PipelinedDeferredReadback
    std::vector<GpuDrivenBatchDebugInfo> gpuDrivenBatches;   // reused directly, never re-wrapped (Locked Design Decision #6).
    std::vector<RenderFeatureDebugEntry> renderFeatures;     // reused directly, never re-wrapped (Locked Design Decision #6).
};

// Pure, Tier-1-testable - takes already-resolved RenderGraphSnapshot/
// GpuDrivenBatchDebugInfo/RenderFeatureDebugEntry values, never a live
// RenderGraph&/Core&. `offscreen`/`present` map 1:1 onto
// RenderGraphMetadata::offscreenRegime/presentRegime.
RenderGraphMetadata BuildRenderGraphMetadata(const RenderGraphSnapshot& offscreen, const RenderGraphSnapshot& present,
    const std::vector<GpuDrivenBatchDebugInfo>& gpuDrivenBatches,
    const std::vector<RenderFeatureDebugEntry>& renderFeatures);

// ADL free function - nlohmann::json's own standard pattern (mirrors
// src/ECS/Reflection/MathJsonAdapters.h's to_json(nlohmann::json&, const Vec3&)
// precedent exactly). Every nested to_json() this depends on
// (RenderGraphResourceRefMetadata/RenderGraphPassMetadata/
// RenderGraphResourceMetadata/RenderGraphRegimeMetadata) is defined,
// TU-locally, inside RenderGraphMetadata.cpp - none of them are declared
// here since nothing outside that one .cpp file constructs a JSON value from
// one of those sub-structs directly today.
void to_json(nlohmann::json& j, const RenderGraphMetadata& metadata);

} // namespace gte::rg

namespace gte {

// PHASE0_MASTER_STRATEGY.md's Locked Design Decision #6 - these two
// otherwise-homeless small structs get their OWN to_json() here, in
// namespace gte (matching each struct's own namespace, for correct ADL),
// physically defined inside RenderGraphMetadata.cpp, rather than adding a
// nlohmann::json include to GpuDrivenBatchDebugInfo.h/RenderFeatureDebugEntry.h
// themselves (keeping both exactly as dependency-free as their own doc
// comments already promise).
void to_json(nlohmann::json& j, const GpuDrivenBatchDebugInfo& info);
void to_json(nlohmann::json& j, const RenderFeatureDebugEntry& entry);

} // namespace gte
