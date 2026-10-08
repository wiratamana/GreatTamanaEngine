#pragma once

#include "../../Profiling/WorkerTimelineData.h"

#include <vector>

namespace gte {

struct EditorContext;
class Game;

// Editor "Jobs" panel: a Unity-Profiler-Timeline-style panel, docked
// alongside "Memory"/"Profiler" (see DockLayout.cpp) - a live, per-worker
// horizontal timeline (Idle vs. named, colored job spans) for the
// last-completed frame's Job System activity, reading exclusively from
// Profiling::BuildWorkerTimelinePoints()/ComputeDistinctWorkerCount()
// (src/Profiling/WorkerTimelineData.h).
//
// A small stateful class, not a stateless free function like most panels
// under src/Editor/Panels/ - mirrors Panels/ProfilerPanel.h's own precedent:
// needs a Pause-frozen snapshot surviving across frames while the
// underlying data capture keeps running underneath it. Still called
// explicitly by name from ImGuiEditorLayer::BuildUI() - no IEditorPanel
// interface introduced.
//
// Deliberately has NO OWN "Capture" toggle - shares
// Profiling::FrameProfiler's existing capture flag with "Profiler" (see
// Panels/ProfilerPanel.cpp's own Capture checkbox) rather than introducing
// a second, independently-stateful toggle over the same underlying data.
//
// Also hosts the engine-wide CPU (Job System) / GPU (Compute) vertex
// skinning mode toggle, since a user watching this panel's worker timeline
// benefits from seeing "SkinVertices" entries appear/disappear right next
// to the control that causes it - this is why Build() takes a Game&, the
// one dependency beyond Profiling::FrameProfiler/Jobs::JobSystem.
class JobsPanel {
public:
    void Build(EditorContext& ctx, Game& game);

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
