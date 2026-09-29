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

// editor-core-separation-22 campaign, PHASE5
// (PHASE5_UNIFY_DUPLICATE_PASS_ROWS_RENDER_GRAPH_PANEL.md) - one row's worth
// of GROUPED-BY-NAME pass metadata: unifies every RenderGraphPassMetadata
// instance sharing the same `name` (e.g. a ProviderScope::PerActiveView
// pass like "RenderOpaque"/"DrawSkyBackground", declared once per active
// view - see RenderPipeline::DeclareOnePhase()) into exactly ONE row,
// closing the "two rows in the live pass table vs one row in the Disabled
// Built-In Passes section" cardinality mismatch (PHASE0_MASTER_STRATEGY.md's
// Root Cause #1, Step 2.1). `instances` keeps every ungrouped, raw
// RenderGraphPassMetadata this group was built from, in their ORIGINAL
// relative order, so the panel's own click-to-expand raw-breakdown UX
// (Step 3.4) has something real to show - never re-derived/re-fetched.
struct RenderGraphGroupedPassMetadata {
    std::string name;
    // Human-readable label describing which view(s) actually contributed a
    // real, NON-CULLED instance this frame (e.g. "Game+Scene", "Game only",
    // "Scene only", "Shared"). If EVERY instance sharing this name was
    // culled this frame (isCulled below is true), there is no "real"
    // contribution to prefer, so this falls back to listing every
    // instance's own viewScope instead (still deterministic, still useful
    // context) - see GroupPassMetadataByName()'s own BuildViewLabel() helper
    // in RenderGraphMetadata.cpp for the exact rule.
    std::string viewLabel;
    // True only if EVERY instance sharing this name was culled this frame -
    // a pass surviving in one view but culled in the other must NOT read as
    // fully culled (PHASE0_MASTER_STRATEGY.md's own explicit warning, Step
    // 3.2 of this phase's own file).
    bool isCulled = false;
    std::uint32_t drawCallCount = 0; // summed across every instance sharing this name (a culled instance always contributes 0, by construction - see RenderGraphPassMetadata::drawCallCount's own doc comment).
    std::uint32_t triangleCount = 0; // same rule.
    // Per-view GPU timing breakdown, e.g. "Game: 0.12 ms, Scene: 0.08 ms" -
    // deliberately NEVER summed/maxed (locked decision, via ask_questions,
    // PHASE5_COMPLETION_REPORT.md) since GPU timing is not meaningfully
    // additive/comparable across two logically-separate view passes the way
    // draw/triangle counts are. A single-instance group (this name was only
    // ever declared by one view/regime this frame) shows that one
    // instance's own gpuTimingText verbatim, with no redundant view-name
    // prefix (the row's own viewLabel badge already states the view).
    std::string gpuTimingText;
    std::vector<std::string> reads;  // combined, de-duplicated resource NAMES across every instance (order-preserving, first-seen) - the full per-instance kind-resolved breakdown is still available via `instances` below.
    std::vector<std::string> writes; // same rule.
    std::vector<RenderGraphPassMetadata> instances; // raw, ungrouped, original per-instance breakdown, in their original relative order - see struct doc comment above.
};

// Pure, Tier-1-testable (mirrors BuildRenderGraphMetadata()'s own "no live
// RenderGraph&/VkDevice&/Renderer&" precedent exactly) - groups `ungrouped`
// (typically a RenderGraphRegimeMetadata::passes vector) by `name`, in
// SORTED, deterministic order (mirrors RenderPassToggleRegistry::ListAll()'s
// own "sorted, deterministic iteration order" discipline - the panel's own
// row order must not visibly jitter frame-to-frame for no reason, and must
// not depend on `ungrouped`'s own input order either - see this phase's own
// Step 3.5 item (e)).
std::vector<RenderGraphGroupedPassMetadata> GroupPassMetadataByName(
    const std::vector<RenderGraphPassMetadata>& ungrouped);

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
