#pragma once

#include "../EditorWindow.h"
#include "../../Profiling/WorkerTimelineData.h"

#include <vector>

namespace gte {

// One timeline row per job worker for the last finished frame, plus the skinning mode toggle.
// Pause keeps a frozen copy of the timeline; Capture and Pause are shared with the Profiler window.
class JobsPanel final : public EditorWindow {
public:
    JobsPanel() : EditorWindow("Jobs", WindowDock::Bottom) {}

protected:
    void OnFrame(EditorFrame& frame) override;
    void OnGui(EditorFrame& frame) override;

private:
    // Copies the timeline on the frame Pause turns on.
    void SyncPauseEdge();

    bool m_wasPaused = false;
    std::vector<Profiling::WorkerTimelinePoint> m_frozenPoints;
    double m_frozenFrameMilliseconds = 0.0;
};

} // namespace gte
