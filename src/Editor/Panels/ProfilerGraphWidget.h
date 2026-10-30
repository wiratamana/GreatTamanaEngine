#pragma once

#include "../ProfilerGraphModel.h"

#include <vector>

namespace gte {

// Draws the frame graph and its legend. Legend checkboxes edit `visibility`; the [x] buttons untick rows.
void DrawProfilerGraph(const GraphModel& model, GraphVisibility& visibility, std::vector<TrackedKey>& tracked, float height);

} // namespace gte
