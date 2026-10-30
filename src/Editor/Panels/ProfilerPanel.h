#pragma once

#include "../ProfilerGraphModel.h"
#include "../ProfilerPanelData.h"
#include "../ProfilerTableModel.h"
#include "../../Profiling/FrameGraphData.h"
#include "../../Profiling/ProfilingTypes.h"

#include <array>
#include <memory>
#include <string>
#include <vector>

namespace gte {

struct EditorContext;

// Profiler panel: plain summary, CPU/GPU graph, scope table, checks, spikes and GPU status.
// Pause keeps a frozen copy of the newest frame; Capture is a separate switch.
class ProfilerPanel {
public:
    void Build(EditorContext& ctx);

private:
    bool m_wasPaused = false;
    bool m_showSelf = false; // graph and sort use self time instead of inclusive time
    bool m_hadIssues = false;
    double m_limitMessageUntil = 0.0;

    std::array<char, 64> m_filterBuffer{};
    TableOptions m_options;
    GraphVisibility m_visibility;
    std::vector<TrackedKey> m_tracked;

    // Frame the panel shows while paused.
    std::unique_ptr<Profiling::FrameRecord> m_frozenRecord;

    std::vector<Profiling::FrameGraphPoint> m_points;
    GraphModel m_graph;
    TableModel m_table;
    std::vector<float> m_memoryScratch;

    // Why the last pin request was refused; empty when it was not.
    std::string m_pinMessage;
};

} // namespace gte
