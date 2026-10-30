#pragma once

#include "ProfilingTypes.h"
#include "ScopeAggregate.h"
#include "ScopeEvent.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace gte::Profiling {

// One row of a merged call table. Ticks are ns.
struct ScopeTableRow {
    ScopeLane lane = ScopeLane::Main;
    NameId name = kInvalidNameId;
    NameId feature = kInvalidNameId; // GPU rows: owning feature
    std::uint64_t pathKey = 0;       // stable id of the node: FNV-1a of parent key, lane and name
    std::uint16_t depth = 0;         // depth inside its lane; flat rows are 0
    bool hasChildren = false;
    ScopeKind kind = ScopeKind::Work;
    SampleStatus status = SampleStatus::Present;
    std::uint32_t calls = 0;
    std::uint64_t totalTicks = 0;
    std::uint64_t selfTicks = 0;
    std::uint64_t maxTicks = 0;
};

struct ScopeTable {
    std::vector<ScopeTableRow> rows; // lanes in order Main, Workers, Gpu; pre-order
    std::uint32_t rowsDropped = 0;   // rows beyond maxRows
    bool treeTruncated = false;      // tree mode only: events were dropped, so the tree is partial
};

enum class ScopeSort : std::uint8_t { CallOrder, TotalDesc, SelfDesc };

// Merged call tree. Main lane comes from events, Workers lane is flat, Gpu lane is feature -> pass.
ScopeTable BuildScopeTree(const FrameRecord& frame, ScopeSort sort, std::size_t maxRows);

// Flat rows from the aggregates. Complete even when events were dropped.
ScopeTable BuildScopeFlat(const FrameRecord& frame, ScopeSort sort, std::size_t maxRows);

} // namespace gte::Profiling
