#pragma once

#include "../../Profiling/WorkerTimelineData.h"

#include <vector>

namespace gte {

struct EditorContext;
class Game;

// Jobs panel: one timeline row per job worker for the last finished frame, plus the skinning mode toggle.
// Stateful because Pause keeps a frozen copy of the timeline. Capture and Pause are shared with the Profiler panel.
class JobsPanel {
public:
    void Build(EditorContext& ctx, Game& game);

private:
    // Display pause state as of the last Build() call.
    bool m_wasPaused = false;

    // Copy taken when Pause turned on.
    std::vector<Profiling::WorkerTimelinePoint> m_frozenPoints;
    double m_frozenFrameMilliseconds = 0.0;
};

} // namespace gte
