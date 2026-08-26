#pragma once

#include "../../Renderer/RenderGraph/RenderGraphSnapshot.h"

namespace gte {

struct EditorContext;

namespace rg {
class RenderGraph;
} // namespace rg

// Phase 8 (RENDERGRAPH_PHASE8_EDITOR_DEBUG_TOOLING_STRATEGY_v1.md, part 8 of
// the wider RENDERGRAPH_PHASE0_MASTER_STRATEGY_v2.md campaign) - a Unity-
// Memory-Profiler/"Profiler"-window-style **"Render Graph"** panel, docked
// alongside "Memory"/"Profiler" along the bottom (see DockLayout.cpp): a
// live, honest view of exactly what gte::rg::RenderGraph::Execute() decided
// the last time each of this engine's two real submission regimes ran (see
// RenderGraph.h's own ExecuteTimingMode) - which passes ran (in real
// execution order), which were culled (and why), each surviving pass's
// declared reads/writes plus its draw-call/triangle/GPU-timing stats, and
// every declared resource's computed lifetime (see
// src/Renderer/RenderGraph/RenderGraphSnapshot.h for the underlying data
// model this panel is a thin ImGui-table wrapper around).
//
// A small STATEFUL CLASS, not a stateless free function like every other
// panel under src/Editor/Panels/ - mirrors Panels/ProfilerPanel.h's own
// precedent exactly (AGENTS.md, "Editor Module Structure" pre-approves this
// exception for a panel with real persistent state of its own): this
// panel's "Pause" control needs to freeze a snapshot across frames, the
// same real, already-established reason ProfilerPanel/BoneViewerWindow are
// also small stateful classes. Still called explicitly BY NAME from
// ImGuiEditorLayer::BuildUI() - no IEditorPanel interface introduced.
//
// Deliberately has NO "Capture" control (unlike "Profiler", which has both
// Capture and Pause) - building a RenderGraphSnapshot costs nothing beyond
// copying already-computed small strings/vectors once per Execute() call
// (no new Vulkan call, no new GPU cost at all - it's pure CPU-side
// reflection of work the graph was doing anyway), so there is no
// meaningful "disable snapshot capture" runtime toggle to offer.
class RenderGraphPanel {
public:
    // `renderGraph` is the SAME gte::rg::RenderGraph Application owns and
    // drives every frame (see Application::m_renderGraph) - this panel only
    // ever reads its two LastSnapshot() results, never mutates it.
    void Build(EditorContext& ctx, const rg::RenderGraph& renderGraph);

private:
    // See ProfilerPanel::m_paused's own doc comment for the full Pause
    // state machine (false->true captures a frozen snapshot once; staying
    // true or un-pausing back to false both need no extra code at all,
    // since every section below just reads m_paused's current value).
    bool m_paused = false;

    // The frozen snapshots captured at the moment m_paused most recently
    // became true - one per ExecuteTimingMode regime, mirroring
    // RenderGraph's own two independent LastSnapshot() results exactly.
    rg::RenderGraphSnapshot m_frozenOffscreenSnapshot;
    rg::RenderGraphSnapshot m_frozenPresentSnapshot;
};

} // namespace gte
