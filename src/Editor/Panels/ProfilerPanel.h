#pragma once

#include "../ProfilerPanelData.h"
#include "../../Profiling/FrameGraphData.h"
#include "../../Profiling/ProfilingTypes.h"

#include <vector>

namespace gte {

struct EditorContext;

// Profiler panel: CPU frame time, scope table, GPU memory and GPU timing status.
// Stateful because Pause keeps a frozen copy of what the panel shows. Capture is a separate switch.
class ProfilerPanel {
public:
    void Build(EditorContext& ctx);

private:
    // Display pause state as of the last Build() call.
    bool m_wasPaused = false;

    // Copy taken when Pause turned on.
    std::vector<Profiling::FrameGraphPoint> m_frozenPoints;
    std::vector<CpuScopeRow> m_frozenRows;
    Profiling::FrameSummary m_frozenSummary{};

    // Scratch for the two plots.
    std::vector<float> m_cpuGraphScratch;
    std::vector<float> m_memoryGraphScratch;
};

} // namespace gte
