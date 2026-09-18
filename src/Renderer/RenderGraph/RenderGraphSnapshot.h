#pragma once

// Phase 8 (RENDERGRAPH_PHASE8_EDITOR_DEBUG_TOOLING_STRATEGY_v1.md, part 8 of
// the wider RENDERGRAPH_PHASE0_MASTER_STRATEGY_v2.md campaign) - "Add a
// Debug Window in the Editor": the pure, ImGui-free "flatten a compiled
// graph into something displayable" reshape, mirroring
// src/Editor/MemoryPanelData.h/ProfilerPanelData.h's own "small, dedicated,
// directly-testable reshaping module" precedent (see AGENTS.md,
// "Testability & Regression Safety") - applied here to Renderer/RenderGraph
// data instead of Editor data, so it lives alongside the rest of the Render
// Graph campaign under src/Renderer/RenderGraph/ rather than under
// src/Editor/.
//
// Deliberately its OWN header/translation unit, not folded into
// RenderGraph.h/.cpp: RenderGraph.h includes this (to store/serve a
// snapshot per ExecuteTimingMode regime - see RenderGraph::LastSnapshot()
// in RenderGraph.h), but BuildRenderGraphSnapshot() itself needs nothing
// but already-computed plain data (a CompiledGraph, a CompiledGraphInput,
// and a caller-supplied stats-lookup function) - no live RenderGraph/
// VkDevice/Renderer - so it is directly Tier-1-testable with hand-
// fabricated inputs, exactly like every other pure module in this
// campaign (see tests/Renderer/RenderGraph/RenderGraphSnapshotTests.cpp).
//
// PassGpuStats (moved here from where Phase 6 originally defined it inline
// in RenderGraph.h) is the one small, genuinely shared type between
// RenderGraph.h and this file - giving it a single home here (rather than
// leaving two copies, or introducing a circular #include between
// RenderGraph.h and this header) is the cleanest resolution; RenderGraph.h
// now just includes this file and uses PassGpuStats from here.

#include "RenderGraphBuilder.h"
#include "RenderGraphCompiler.h"
#include "../DrawStats.h"
#include "../GpuTiming.h"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace gte::rg {

// One pass's last-known stats, as of whichever Execute() call most recently
// ran it. `timing` is a genuine gte::GpuTimingSample (Renderer-local
// tri-state, see GpuTiming.h) - see RenderGraph.h's own "GPU TIMING NOTE"
// for the full "why GpuTimingSample is always Absent today" reasoning; this
// struct itself carries no opinion about that, it just bundles the two
// pieces of data together. `drawStats` is real, fused-per-draw-call data
// (see RenderGraph.h's PassContext::recordDraw).
struct PassGpuStats {
    DrawStats drawStats;
    GpuTimingSample timing;
};

// Render Pass campaign (task_manager/render-pass-1), PHASE2
// (PHASE2_RENDER_OPAQUE_SKY_SPLIT_AND_TRANSPARENT_STUB.md, Step 3.4) -
// combines N passes' own last-known PassGpuStats into one aggregate, for a
// group of passes a caller treats as logically "one thing" (e.g.
// Application.cpp's own Profiling::GpuPass::GameView, which used to read a
// single "GameView" pass's stats before this campaign split it into
// "RenderOpaque" + "DrawSkyBackground" + "RenderTransparent"). Pure,
// allocation-light, Tier-1-testable (see
// tests/Renderer/RenderGraph/RenderGraphSnapshotTests.cpp) - takes plain
// PassGpuStats values, no live RenderGraph/VkDevice needed.
//
// `drawStats`: a plain sum of every entry's drawCallCount/triangleCount -
// each pass's own stats already independently reflect its own real draws
// this frame, so summing them is exactly "how many draws/triangles did this
// whole logical group issue".
//
// `timing`: sums `milliseconds` across every entry whose status is
// GpuTimingSample::Status::Present (the combined result's own status is
// Present too, in that case). If NONE are Present, falls back to
// Unsupported if any entry reports Unsupported (a permanent, device-level
// condition - see GpuTiming.h's own ResolveGpuTimingStatus() priority
// order), otherwise Absent (the common case today - every pass's own timing
// is Absent, see RenderGraph.h's "GPU TIMING NOTE"). An empty `stats` input
// returns a default-constructed PassGpuStats{} (zero draws, Absent timing).
PassGpuStats CombinePassGpuStats(const std::vector<PassGpuStats>& stats);

// One pass, ready to display. `name`/`readNames`/`writeNames` are already
// resolved into plain, OWNED strings (never a raw `const char*` into
// PassRecord/RenderGraphBuilder's own name tables, which only live as long
// as the CompiledGraphInput that produced this snapshot) - a snapshot must
// remain valid and displayable indefinitely once captured (the Editor's
// "Render Graph" panel Pause control freezes one exactly like
// PHASE7_EDITOR_PROFILER_PANEL_STRATEGY_v2.md's own ProfilerPanel already
// does for its own frozen data), so every string is copied here, not
// referenced.
struct RenderGraphPassSnapshot {
    std::string name;
    bool isCulled = false;
    std::vector<std::string> readNames;
    std::vector<std::string> writeNames;

    // frame-debugger-5 campaign, PHASE1
    // (PHASE1_RENDERGRAPH_COMPUTE_DISPATCH_CHOKEPOINT_INFRASTRUCTURE.md) -
    // RENAMED from the original plain `bool isComputePass` to `PassKind kind`
    // by the Render Pass campaign's own PHASE1 (task_manager/render-pass-1) -
    // `kind == PassKind::Compute` for a real compute dispatch (see
    // PassRecord::kind's own doc comment, RenderGraphTypes.h) - copied
    // straight through for BOTH a surviving AND a culled pass (a culled
    // compute pass must still truthfully report this - only its `stats`
    // below stays at its own default for a culled pass, per this struct's
    // own pre-existing convention).
    PassKind kind = PassKind::Graphics;

    // Render Pass campaign (task_manager/render-pass-1), PHASE1 - which
    // conceptual GROUP this pass belongs to (see RenderPassCategory's own
    // doc comment, RenderGraphTypes.h) - copied straight through for BOTH a
    // surviving AND a culled pass, exactly like `kind` above.
    RenderPassCategory category = RenderPassCategory::General;

    // Frame Debugger Pass-Ownership campaign (task_manager/render-pass-2),
    // PHASE1 - copied straight through for BOTH a surviving AND a culled
    // pass, exactly like `category` above (a culled pass must still
    // truthfully report what kind of draw it WOULD have issued).
    RenderPassDrawKind drawKind = RenderPassDrawKind::DrawMesh;

    // frame-debugger-6 campaign, PHASE1
    // (PHASE1_RENDERGRAPH_VIEWSCOPE_CHOKEPOINT_INFRASTRUCTURE.md) - copied
    // straight through for BOTH a surviving AND a culled pass, exactly like
    // `kind` above (a culled pass must still truthfully report which view it
    // belonged to).
    ViewScope viewScope = ViewScope::Shared;

    // frame-debugger-5 campaign, PHASE1 - PARALLEL to readNames/writeNames
    // above (same index, same length) - which ResourceKind (RenderGraphTypes.h)
    // each entry actually is, so a consumer never has to guess/probe
    // multiple registries to tell "this name is a real 2D texture" from
    // "...a buffer" from "...a volume texture". A pass with, e.g., 2 reads
    // and 1 write has readKinds.size() == 2 and writeKinds.size() == 1,
    // always exactly matching readNames.size()/writeNames.size().
    std::vector<ResourceKind> readKinds;
    std::vector<ResourceKind> writeKinds;

    // Deliberately left at its default (an empty DrawStats, an Absent
    // GpuTimingSample) for a CULLED pass - see BuildRenderGraphSnapshot()'s
    // own doc comment below for why.
    PassGpuStats stats;
};

// One resource, ready to display.
struct RenderGraphResourceSnapshot {
    std::string name;
    bool isImported = false;
    // Indices into RenderGraphSnapshot::passesInExecutionOrder's own
    // SURVIVING (non-culled) PREFIX - i.e. exactly
    // RenderGraphCompiler.h's own ResourceLifetime::firstUsePassIndex/
    // lastUsePassIndex, unchanged (-1 means "never used"). Never an index
    // into the culled passes appended after that prefix - see
    // RenderGraphSnapshot::passesInExecutionOrder's own comment below.
    std::int32_t firstUsePassIndex = -1;
    std::int32_t lastUsePassIndex = -1;
};

// The whole displayable snapshot of one RenderGraph::Execute() call.
struct RenderGraphSnapshot {
    // Every SURVIVING (non-culled) pass FIRST, in real execution order
    // (matching CompiledGraph::executionOrder exactly), followed by every
    // CULLED pass appended after them, in their original declaration
    // order. A culled pass must still be VISIBLE here (see this phase's
    // own strategy document's Step 3.1: "a culled pass must still be
    // VISIBLE in the panel, tagged as culled, or 'why didn't my pass run'
    // becomes just as hard to answer as before") - distinguishable purely
    // via `isCulled`, and never reachable via a ResourceSnapshot's
    // firstUse/lastUsePassIndex (both of which only ever index into the
    // surviving prefix, per RenderGraphCompiler.h's own contract).
    std::vector<RenderGraphPassSnapshot> passesInExecutionOrder;
    std::vector<RenderGraphResourceSnapshot> resources;
};

// Pure reshape: `compiled`/`input` are exactly RenderGraphCompiler::Compile()'s
// own inputs/outputs for one Execute() call - `input` is read AFTER that
// call's whole pass loop has already run (see RenderGraph.cpp), so
// `statsLookup` sees this frame's real, freshly-recorded PassGpuStats for
// every SURVIVING pass. `statsLookup` resolves a pass name into its
// last-known PassGpuStats (in production, RenderGraph::LastKnownStatsFor() -
// a test can supply any stand-in, e.g. a lambda returning a canned value,
// which is exactly what keeps this function itself Tier-1-testable with no
// live RenderGraph at all).
//
// A CULLED pass's `stats` is always left at its default (an empty
// DrawStats, an Absent GpuTimingSample) - deliberately NEVER calling
// `statsLookup()` for one. A culled pass did not run THIS call, so showing
// whatever an EARLIER, different call happened to leave behind under the
// same name (e.g. if this exact pass name survived culling last frame but
// was culled this frame) would misleadingly suggest it ran again.
RenderGraphSnapshot BuildRenderGraphSnapshot(const CompiledGraph& compiled, const CompiledGraphInput& input,
    const std::function<PassGpuStats(const char*)>& statsLookup);

} // namespace gte::rg
