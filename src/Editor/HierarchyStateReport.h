#pragma once

#include "../ECS/Entity.h"

#include <cstdint>
#include <string>
#include <vector>

namespace gte {

// One Hierarchy row as drawn on the last frame.
struct HierarchyStateRow {
    Entity entity;
    Entity parent; // kInvalidEntity for a root
    std::uint32_t depth = 0;
    std::uint32_t siblingPosition = 0;
    std::string label;
    bool hasChildren = false;
    bool expanded = true;
    bool selected = false;
};

// Snapshot of the Hierarchy panel for GET /hierarchy_state. The timings are valid only while `visible`.
struct HierarchyStateReport {
    bool editorAvailable = true;
    bool visible = false; // false while the panel is hidden; `rows` is then empty
    std::uint64_t entityCount = 0;
    std::uint64_t rowCount = 0;
    std::uint64_t collapsedCount = 0;
    std::uint64_t first = 0;
    std::vector<HierarchyStateRow> rows;
    double buildMs = 0.0;
    double flattenMs = 0.0;
    std::uint32_t drawnRows = 0;
};

// Outcome of POST /hierarchy_action. `rows` are the rows around the target after the action, starting at row `firstRow`.
struct HierarchyActionReport {
    bool editorAvailable = true;
    bool success = false;
    std::string errorMessage; // set when !success
    std::string kind;         // "reparent" | "reorder" | "toggle"
    bool changed = false;
    std::uint64_t firstRow = 0;
    std::vector<HierarchyStateRow> rows;
};

} // namespace gte
