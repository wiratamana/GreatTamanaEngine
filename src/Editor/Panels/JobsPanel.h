#pragma once

#include "../../Profiling/WorkerTimelineData.h"

#include <vector>

namespace gte {

struct EditorContext;

// Job System Phase 7 (Editor "Jobs" Panel -
// task_manager/job_system/JOBSYSTEM_PHASE7_EDITOR_JOBS_PANEL.md): a Unity-
// Profiler-Timeline-style panel, docked alongside "Memory"/"Profiler"/
// "Render Graph" (see DockLayout.cpp) - a live, per-worker horizontal
// timeline (Idle vs. named, colored job spans) for the last-completed
// frame's Job System activity, reading exclusively from Job System Phase 5's
// Profiling::BuildWorkerTimelinePoints()/ComputeDistinctWorkerCount()
// reshape (src/Profiling/WorkerTimelineData.h) - no new engine-level
// tracking is added by this panel at all.
//
// A small STATEFUL CLASS, not a stateless free function like most panels
// under src/Editor/Panels/ - mirrors Panels/ProfilerPanel.h/
// Panels/RenderGraphPanel.h's own precedent exactly (AGENTS.md, "Editor
// Module Structure" pre-approves this exception for a panel that needs a
// Pause-frozen snapshot surviving across frames, while the underlying data
// capture keeps running underneath it unaffected). Still called explicitly
// BY NAME from ImGuiEditorLayer::BuildUI() - no IEditorPanel interface
// introduced.
//
// Deliberately has NO OWN "Capture" toggle - shares
// Profiling::FrameProfiler's existing capture flag with "Profiler" (see
// Panels/ProfilerPanel.cpp's own Capture checkbox) rather than introducing a
// second, independently-stateful toggle over the exact same underlying data
// source (see JOBSYSTEM_PHASE7_EDITOR_JOBS_PANEL.md, Step 3.4, point 5).
class JobsPanel {
public:
    void Build(EditorContext& ctx);

private:
    // See ProfilerPanel::m_paused's own doc comment for the full Pause
    // state machine (false->true captures a frozen snapshot once; staying
    // true, or un-pausing back to false, both need no extra code at all,
    // since every section in JobsPanel.cpp just reads m_paused's current
    // value each call).
    bool m_paused = false;

    // The frozen snapshot captured at the moment m_paused most recently
    // became true.
    std::vector<Profiling::WorkerTimelinePoint> m_frozenPoints;
    Profiling::FrameSample m_frozenLatestFrame{};
};

} // namespace gte
