#pragma once

// Pure, ImGui-free "flatten a compiled graph into something displayable"
// reshape, mirroring MemoryPanelData.h/ProfilerPanelData.h's own small,
// dedicated, directly-testable reshaping module shape.
//
// Deliberately its own header/translation unit, not folded into
// RenderGraph.h/.cpp: RenderGraph.h includes this (to store/serve a
// snapshot per ExecuteTimingMode regime), but BuildRenderGraphSnapshot()
// itself needs nothing but already-computed plain data (a CompiledGraph, a
// CompiledGraphInput, and a caller-supplied stats-lookup function) - no
// live RenderGraph/VkDevice/Renderer - so it is directly testable with
// hand-fabricated inputs.
//
// PassGpuStats is the one small, genuinely shared type between
// RenderGraph.h and this file - giving it a single home here (rather than
// two copies, or a circular include) is the cleanest resolution.

#include "RenderGraphBuilder.h"
#include "RenderGraphDebugMetadataSink.h"
#include "RenderGraphCompiler.h"
#include "../DrawStats.h"
#include "../GpuTiming.h"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace gte::rg {

// One pass's last-known stats, as of whichever Execute() call most recently
// ran it. `timing` is a Renderer-local tri-state GpuTimingSample (see
// GpuTiming.h); `drawStats` is real, fused-per-draw-call data.
struct PassGpuStats {
    DrawStats drawStats;
    GpuTimingSample timing;
};

// Combines N passes' own last-known PassGpuStats into one aggregate, for a
// group of passes a caller treats as logically "one thing". Pure,
// allocation-light, testable - takes plain PassGpuStats values, no live
// RenderGraph/VkDevice needed.
//
// `drawStats`: plain sum of every entry's drawCallCount/triangleCount.
//
// `timing`: sums `milliseconds` across every entry whose status is Present
// (result status is Present too, in that case). If none are Present, falls
// back to Unsupported if any entry reports Unsupported, otherwise Absent.
// An empty `stats` input returns a default-constructed PassGpuStats{}.
PassGpuStats CombinePassGpuStats(const std::vector<PassGpuStats>& stats);

// One pass, ready to display. `name`/`readNames`/`writeNames` are already
// resolved into plain, owned strings (never a raw `const char*` into
// PassRecord/RenderGraphBuilder's own name tables, which only live as long
// as the CompiledGraphInput that produced this snapshot) - a snapshot must
// remain valid and displayable indefinitely once captured.
struct RenderGraphPassSnapshot {
    std::string name;
    bool isCulled = false;
    std::vector<std::string> readNames;
    std::vector<std::string> writeNames;

    // `kind == PassKind::Compute` for a real compute dispatch. Copied
    // straight through for both a surviving and a culled pass - a culled
    // compute pass must still truthfully report this.
    PassKind kind = PassKind::Graphics;

    // Which conceptual group this pass belongs to. Copied straight through
    // for both a surviving and a culled pass, same as `kind` above.
    RenderPassCategory category = RenderPassCategory::General;

    // What kind of draw this pass issues (or would have issued if culled).
    // Copied straight through for both a surviving and a culled pass.
    RenderPassDrawKind drawKind = RenderPassDrawKind::DrawMesh;

    // Which view this pass belongs to. Copied straight through for both a
    // surviving and a culled pass.
    ViewScope viewScope = ViewScope::Shared;

    // Where this pass sorts in the frame. Copied straight through for both
    // a surviving and a culled pass.
    RenderPassEvent renderPassEvent = RenderPassEvent::Opaques;

    // Parallel to readNames/writeNames above (same index, same length) -
    // which ResourceKind each entry actually is, so a consumer never has to
    // guess/probe multiple registries.
    std::vector<ResourceKind> readKinds;
    std::vector<ResourceKind> writeKinds;

    // Editor Inspector binding-stage display - parallel to readNames/
    // writeNames exactly like readKinds/writeKinds above. The declared
    // ResourceAccess for each entry, resolved into a human label via
    // rg::BindingStageLabel() at the RenderGraphMetadata layer.
    std::vector<ResourceAccess> readAccess;
    std::vector<ResourceAccess> writeAccess;

    // Parallel to writeNames - one barrier transition label per declared
    // write ("" when no barrier was applied this frame for that write, or
    // no sink is installed). Resolved once, at snapshot-build time, via the
    // optional barrierLabelLookup callback below - never recomputed on the
    // hot Vulkan recording path.
    std::vector<std::string> writeBarrierLabels;

    // Left at its default (an empty DrawStats, an Absent GpuTimingSample)
    // for a culled pass - see BuildRenderGraphSnapshot()'s own doc comment
    // below for why.
    PassGpuStats stats;

    // Which feature owns this pass. Copied straight through for both a
    // surviving and a culled pass, same as the fields above. "ENGINE_UNOWNED"
    // only for a pass declared with no active RenderFeatureScope at all.
    std::string owningFeatureName;
};

// One resource, ready to display.
struct RenderGraphResourceSnapshot {
    std::string name;
    bool isImported = false;
    // Indices into RenderGraphSnapshot::passesInExecutionOrder's own
    // surviving (non-culled) prefix - i.e. ResourceLifetime::firstUsePassIndex/
    // lastUsePassIndex, unchanged (-1 means "never used"). Never an index
    // into the culled passes appended after that prefix.
    std::int32_t firstUsePassIndex = -1;
    std::int32_t lastUsePassIndex = -1;
};

// The whole displayable snapshot of one RenderGraph::Execute() call.
struct RenderGraphSnapshot {
    // Every surviving (non-culled) pass first, in real execution order
    // (matching CompiledGraph::executionOrder exactly), followed by every
    // culled pass appended after them, in their original declaration order.
    // A culled pass must still be visible here, distinguishable purely via
    // `isCulled`, and never reachable via a ResourceSnapshot's
    // firstUse/lastUsePassIndex (both of which only ever index into the
    // surviving prefix).
    std::vector<RenderGraphPassSnapshot> passesInExecutionOrder;
    std::vector<RenderGraphResourceSnapshot> resources;

    // True if this regime's fixed GPU-timing slot budget
    // (RenderGraph::kSynchronousTimingSlotBudget / kPipelinedTimingSlotBudget)
    // was already fully assigned to other pass names at least once during
    // the Execute() call that produced this snapshot - i.e. at least one
    // surviving pass this call could not be assigned a timing slot at all,
    // and its own PassGpuStats::timing will permanently read Status::Absent
    // for as long as this remains true. The Editor's "Render Graph" panel
    // should surface this structurally rather than leaving a reader to
    // infer it from a timing that quietly never updates.
    bool timingSlotBudgetExhausted = false;
};

// Pure reshape: `compiled`/`input` are exactly RenderGraphCompiler::Compile()'s
// own inputs/outputs for one Execute() call - `input` is read after that
// call's whole pass loop has already run, so `statsLookup` sees this
// frame's real, freshly-recorded PassGpuStats for every surviving pass.
// `statsLookup` resolves a pass name into its last-known PassGpuStats (in
// production, RenderGraph::LastKnownStatsFor() - a test can supply any
// stand-in, e.g. a lambda returning a canned value, which is what keeps
// this function itself testable with no live RenderGraph at all).
//
// A culled pass's `stats` is always left at its default (an empty
// DrawStats, an Absent GpuTimingSample) - deliberately never calling
// `statsLookup()` for one. A culled pass did not run this call, so showing
// whatever an earlier, different call happened to leave behind under the
// same name would misleadingly suggest it ran again.
//
// `timingSlotBudgetExhausted` - trailing, defaulted parameter so every
// pre-existing 3-argument call site keeps compiling unmodified - passed
// straight through to RenderGraphSnapshot::timingSlotBudgetExhausted.
//
// `metadataLookup` - trailing, defaulted parameter. Resolves one declared
// pass's category/drawKind/tags by declarationIndex (the same index space
// CompiledGraphInput::passes already uses). Returns false (or is left
// empty/unset) to mean "no metadata available for this index" -
// BuildPassSnapshot() leaves RenderGraphPassSnapshot::category/::drawKind/
// ::tags at their own struct defaults in that case. In production,
// RenderGraph::ExecuteCompiledGraph() supplies a real lookup backed by its
// own installed IPassDebugMetadataProvider*; a test can supply any
// stand-in.
//
// `barrierLabelLookup` - a third, trailing, defaulted parameter. Resolves
// one declared write's barrier transition label by (declarationIndex,
// resourceName), mirroring metadataLookup's own "empty means nothing
// available" convention. In production, RenderGraph::ExecuteCompiledGraph()
// supplies a real lookup backed by IPassDebugMetadataProvider::
// QueryBarrierTransitionLabel().
RenderGraphSnapshot BuildRenderGraphSnapshot(const CompiledGraph& compiled, const CompiledGraphInput& input,
    const std::function<PassGpuStats(const char*)>& statsLookup, bool timingSlotBudgetExhausted = false,
    const std::function<bool(std::size_t, PassDebugMetadata&)>& metadataLookup = {},
    const std::function<bool(std::size_t, const std::string&, std::string&)>& barrierLabelLookup = {});

// Resolves a declared usage's resource name from whichever of
// CompiledGraphInput's texture/buffer/volume-texture/texture-array tables
// actually applies. Degrades to an empty string for a stale or
// out-of-range index - a display helper, never an assertion. Shared by
// RenderGraphSnapshot.cpp's own BuildPassSnapshot() and RenderGraph.cpp's
// barrier-applied sink call, so both resolve a usage's name identically.
std::string ResourceUsageName(const ResourceUsage& usage, const CompiledGraphInput& input);

} // namespace gte::rg
