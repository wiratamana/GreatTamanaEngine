#pragma once

// task_manager/editor-core-separation-21 campaign, PHASE5
// (PHASE5_IRON_RULE_PERMANENT_MISMATCH_DETECTOR.md) - the pure,
// Tier-1-testable core of the "Render Pass Honesty" detector that makes the
// campaign's own "iron rule" self-enforcing in code:
//
//   "A render pass's declared/enabled state and the Frame Debugger's own
//    displayed event tree must NEVER disagree. If a pass is disabled, it
//    must not run, and it must not appear as an executed leaf anywhere the
//    Frame Debugger or the Render Graph panel can show it."
//
// Mirrors ImGuiIdConflictTracker.h's own precedent exactly: this file is
// deliberately dependency-light (no <imgui.h>, no Logger, not even a live
// rg::RenderPassToggleRegistry object) - see RenderPassHonestyGuard.h for
// the real, Logger-aware wrapper built on top of this function that
// production call sites (FrameDebuggerPanel::TriggerCapture()) actually
// use. Reuses rg::RenderGraphPassSnapshot directly (per this phase's own
// Step 3.1 guidance: "reuse that type, do not invent a parallel one") -
// that struct is already plain, ImGui-free, Vulkan-free data (see
// RenderGraphSnapshot.h), so no live GPU/RenderGraph object is needed to
// construct a test fixture either.

#include "../Renderer/RenderGraph/RenderGraphSnapshot.h"

#include <functional>
#include <string>
#include <vector>

namespace gte {

// Given every pass in one captured rg::RenderGraphSnapshot::passesInExecutionOrder
// (BOTH surviving AND culled entries - see that field's own doc comment,
// RenderGraphSnapshot.h) and `isEnabledLookup` (in production,
// rg::RenderPassToggleRegistry::IsEnabled(), injected here as a plain
// callable so this function needs no live registry object to unit test),
// returns every pass name that is BOTH:
//   (a) present, NON-CULLED, in `passes` - i.e. it genuinely executed this
//       frame and would be shown as a real leaf in the Frame Debugger's own
//       event tree, AND
//   (b) reports enabled == false via `isEnabledLookup`.
//
// This is a contradiction by construction - Panels/RenderGraphPanel.cpp's
// own documented contract is that a disabled pass leaves ZERO trace in the
// render graph, since it is never declared into the graph at all. A name
// satisfying both (a) and (b) simultaneously means the render pass and the
// Frame Debugger disagree - the render graph lied about a pass it claims is
// disabled.
//
// A CULLED pass (isCulled == true - a real, legitimate, UNRELATED reason a
// pass might not run this frame, RenderGraphCompiler::Compile()'s own
// dead-code elimination) is NEVER reported, even if its own toggle entry
// also happens to read false - culling and toggle-disabling are two
// different, both-legitimate reasons a pass might not execute, and this
// function's whole job is distinguishing "executed contradiction" from "did
// not execute for an unrelated, honest reason".
//
// A pass name `isEnabledLookup` has never heard of at all
// (RenderPassToggleRegistry::IsEnabled()'s own documented "never seen ->
// true" default) is also never reported, since a correctly-implemented
// `isEnabledLookup` would then simply return true for it - no special-case
// branch is needed here for that condition.
//
// Returns an empty vector if `isEnabledLookup` itself is empty (a
// defensive, unreachable-in-production guard only - every real call site
// always supplies a real callable).
std::vector<std::string> DetectRenderPassHonestyMismatches(
    const std::vector<rg::RenderGraphPassSnapshot>& passes,
    const std::function<bool(const std::string&)>& isEnabledLookup);

} // namespace gte
