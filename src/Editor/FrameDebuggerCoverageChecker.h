#pragma once

// task_manager/editor-core-separation-22 campaign, PHASE6
// (PHASE6_IRON_RULE_V2_BIDIRECTIONAL_DETECTOR.md, Step 3.1) - Clause B of
// this campaign's own extended "iron rule" (see PHASE0_MASTER_STRATEGY.md's
// Step 1.2):
//
//   "A render pass's declared/enabled state and the Frame Debugger's own
//    displayed event tree must NEVER disagree. Clause B: if a pass runs
//    (survives culling, is non-culled in the real RenderGraphSnapshot), the
//    Frame Debugger's own tree MUST show it as a real leaf somewhere."
//
// Mirrors RenderPassHonestyChecker.h's own shape/precedent EXACTLY (same
// file-organization pattern: a dependency-light, ImGui-free, ordinary-
// data-only pure function here, plus a thin Logger-aware guard on top - see
// FrameDebuggerCoverageGuard.h for that real, production wrapper). This
// file's own detector answers a DIFFERENT question than
// RenderPassHonestyChecker.h's Clause A: Clause A never looks at the built
// FrameDebuggerSnapshot tree at all (only the raw RenderGraphSnapshot vs.
// the toggle registry); this file compares the raw RenderGraphSnapshot
// against the ACTUAL, ALREADY-BUILT FrameDebuggerSnapshot tree that same
// capture produced.

#include "FrameDebuggerData.h"
#include "../Renderer/RenderGraph/RenderGraphSnapshot.h"

#include <string>
#include <unordered_set>
#include <vector>

namespace gte {

// Recursively walks `tree` (its `rootNodes`, and every descendant at any
// depth) and collects every real, name-carrying leaf's own underlying pass
// identity - i.e. every FrameDebuggerEventNode with `isDrawCall == true` AND
// a populated `details` (BuildRealFrameDebuggerSnapshot()'s own real
// pass-level nodes always set `details->passName` to the pass's own real,
// raw RenderGraphPassSnapshot::name - see BuildComputeDispatchLeaf()/
// BuildGraphicsPassLeaf()/BuildRenderOpaqueLeaf() in FrameDebuggerData.cpp).
// A GROUP node (isDrawCall == false, e.g. "Compute Dispatches
// (Pre-GameView)", the root "Game View" node itself) contributes nothing
// here - it carries no single real pass identity of its own. A per-entity
// "RenderOpaque (Entity Draw)" child leaf's own `passName` is collected too
// (harmless - it never collides with a real RenderGraphPassSnapshot::name,
// since no real pass is ever named that), but every REAL pass-level node
// (whether it also happens to own a child event row via
// WrapPassWithOwnedChildEvent() or not - see that function's own doc
// comment, FrameDebuggerData.cpp: the PARENT keeps its own `isDrawCall`/
// `details` unchanged even once it gains a child) is what this function
// actually needs to find, and always can.
//
// Exposed as its own separate, directly-testable function (not just
// inlined into DetectPassesMissingFromFrameDebuggerTree() below) so a
// future consumer/test can inspect exactly what this detector considers
// "the tree's own real pass-name universe" directly.
std::unordered_set<std::string> CollectPassNamesPresentInFrameDebuggerTree(const FrameDebuggerSnapshot& tree);

// Clause B pure detector. Given every pass in one captured
// rg::RenderGraphSnapshot::passesInExecutionOrder and the SAME capture's
// own already-built FrameDebuggerSnapshot tree, returns every pass name
// that is:
//   (a) non-culled (it genuinely executed this frame),
//   (b) NOT rg::RenderPassCategory::FrameDebuggerInternal (the one,
//       permanently-documented, genuinely Frame-Debugger-OWNED ephemeral
//       scaffolding exception - RenderGraphTypes.h's own updated doc
//       comment, editor-core-separation-22 campaign PHASE4 - never a real
//       tree citizen, by design, not a contradiction),
//   (c) NOT rg::ViewScope::SceneView (mirrors
//       BuildRealFrameDebuggerSnapshot()'s own documented, honest SceneView
//       exclusion throughout that function - the Frame Debugger tree is
//       Game-View-only, by design, not a contradiction),
// yet has NO corresponding leaf anywhere in `tree` (per
// CollectPassNamesPresentInFrameDebuggerTree() above). A pass satisfying
// all of (a)/(b)/(c) with no leaf is a genuine Clause B contradiction: it
// genuinely ran this frame, yet the Frame Debugger shows nothing for it at
// all - the exact permanent regression guard for
// PHASE0_MASTER_STRATEGY.md's Root Cause #2 (the `DemoRenderFeaturePlugin_Clear`
// bug editor-core-separation-22's own PHASE4 fixed).
std::vector<std::string> DetectPassesMissingFromFrameDebuggerTree(
    const std::vector<rg::RenderGraphPassSnapshot>& passes, const FrameDebuggerSnapshot& tree);

} // namespace gte
