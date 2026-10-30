#pragma once

#include "ProfilingTypes.h"
#include "ScopeAggregate.h"
#include "ScopeEvent.h"

#include <array>
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
    std::array<std::uint32_t, kScopeLaneCount> rowsDroppedByLane{}; // rows beyond the lane cap
    bool treeTruncated = false;      // tree mode only: events were dropped, so the tree is partial

    std::uint32_t TotalDropped() const noexcept
    {
        std::uint32_t total = 0;
        for (const std::uint32_t dropped : rowsDroppedByLane) {
            total += dropped;
        }
        return total;
    }
};

enum class ScopeSort : std::uint8_t { CallOrder, TotalDesc, SelfDesc };

// Row cap per lane, indexed by ScopeLane (Main, Workers, Gpu). A busy lane never pushes out another.
using LaneRowCaps = std::array<std::size_t, kScopeLaneCount>;

// Merged call tree. Main lane comes from events, Workers lane is flat, Gpu lane is feature -> pass.
ScopeTable BuildScopeTree(const FrameRecord& frame, ScopeSort sort, const LaneRowCaps& caps);

// Flat rows from the aggregates. Complete even when events were dropped.
ScopeTable BuildScopeFlat(const FrameRecord& frame, ScopeSort sort, const LaneRowCaps& caps);

} // namespace gte::Profiling
