#pragma once

#include "../ProfilerGraphModel.h"
#include "../ProfilerTableModel.h"

#include <cstdint>
#include <vector>

namespace gte {

// What the table edits: ticked graph rows and collapsed tree nodes.
struct TableDrawState {
    std::vector<TrackedKey>& tracked;
    std::vector<std::uint64_t>& collapsed;
    bool tree = true;
    bool technicalNames = false;
    bool limitRefused = false; // set when a tick was refused because the graph holds the maximum rows
};

// Draws the scope table with lane headers. `height` is the scrolling area in pixels.
void DrawProfilerTable(const TableModel& model, TableDrawState& state, float height);

} // namespace gte
