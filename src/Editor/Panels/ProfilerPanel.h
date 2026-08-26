#pragma once

#include "../../Profiling/FrameGraphData.h"
#include "../../Profiling/ProfilingTypes.h"

#include <vector>

namespace gte {

struct EditorContext;

// Phase 7 (PHASE7_EDITOR_PROFILER_PANEL_STRATEGY_v2.md): a Unity-Profiler-
// window-style panel, docked alongside "Memory" (see DockLayout.cpp).
// Deliberately a small STATEFUL CLASS, not a stateless free function like
// every other panel in src/Editor/Panels/ - AGENTS.md's "Editor Module
// Structure" explicitly pre-approves this exception for a panel with real
// persistent state of its own (see BoneViewerWindow for the other precedent
// already established), and this panel genuinely needs some: the "Pause"
// control's frozen snapshot, and small reusable scratch buffers for its
// ImGui::PlotLines() float conversions. Still called explicitly BY NAME from
// ImGuiEditorLayer::BuildUI(), no IEditorPanel interface introduced.
class ProfilerPanel {
public:
    void Build(EditorContext& ctx);

private:
    // Whether the panel's OWN display is currently frozen - completely
    // independent of Profiling::FrameProfiler::IsCaptureEnabled() (see
    // Build()'s own "Capture"/"Pause" section). While true, every section
    // below reads m_frozenPoints/m_frozenLatestFrame instead of calling
    // Profiling::BuildFrameGraphPoints()/FrameProfiler::LastCompletedFrame()
    // fresh - this is the entire mechanism behind "Pause only freezes the
    // panel, never the underlying data capture".
    bool m_paused = false;

    // The frozen snapshot captured at the moment m_paused most recently
    // became true (see Build()'s own comment for the full pause/un-pause
    // state machine, including why un-pausing needs no special handling at
    // all beyond simply reading m_paused's current value again).
    std::vector<Profiling::FrameGraphPoint> m_frozenPoints;
    Profiling::FrameSample m_frozenLatestFrame{};

    // Reusable scratch buffers for the two ImGui::PlotLines() float
    // conversions (CPU frame-time graph, GPU memory sparkline) - cleared
    // and refilled every call this panel is visible, but only ever
    // REALLOCATES when the visible window genuinely grows past its
    // previous largest size (e.g. its first few frames of life), never on
    // every single frame once warmed up. Purely a Tier-2, ImGui-facing
    // efficiency detail - never read/written by anything in
    // ProfilerPanelData.h.
    std::vector<float> m_cpuGraphScratch;
    std::vector<float> m_memoryGraphScratch;
};

} // namespace gte
