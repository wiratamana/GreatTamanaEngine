#include "ScopeTable.h"

#include "ScopeTree.h"

#include <algorithm>
#include <array>
#include <unordered_map>
#include <utility>

namespace gte::Profiling {
namespace {

constexpr std::uint64_t kFnvOffset = 14695981039346656037ull;
constexpr std::uint64_t kFnvPrime = 1099511628211ull;
constexpr std::uint32_t kNoNode = 0xFFFFFFFFu;

std::uint64_t HashBytes(std::uint64_t hash, std::uint64_t value, int byteCount) noexcept
{
    for (int i = 0; i < byteCount; ++i) {
        hash ^= (value >> (8 * i)) & 0xFFu;
        hash *= kFnvPrime;
    }
    return hash;
}

std::uint64_t MakePathKey(std::uint64_t parentKey, ScopeLane lane, NameId name) noexcept
{
    std::uint64_t hash = HashBytes(kFnvOffset, parentKey, 8);
    hash = HashBytes(hash, static_cast<std::uint64_t>(lane), 1);
    return HashBytes(hash, name, 4);
}

bool Precedes(ScopeSort sort, const ScopeTableRow& a, const ScopeTableRow& b) noexcept
{
    switch (sort) {
    case ScopeSort::TotalDesc:
        return a.totalTicks > b.totalTicks;
    case ScopeSort::SelfDesc:
        return a.selfTicks > b.selfTicks;
    case ScopeSort::CallOrder:
        break;
    }
    return false;
}

void SortRows(std::vector<ScopeTableRow>& rows, ScopeSort sort)
{
    if (sort == ScopeSort::CallOrder) {
        return;
    }
    std::stable_sort(rows.begin(), rows.end(),
        [sort](const ScopeTableRow& a, const ScopeTableRow& b) { return Precedes(sort, a, b); });
}

// Collects rows up to the cap of each lane and counts the rest.
class RowSink {
public:
    RowSink(ScopeTable& table, const LaneRowCaps& caps)
        : m_table(table)
        , m_caps(caps)
    {
        std::size_t total = 0;
        for (const std::size_t cap : caps) {
            total += cap;
        }
        table.rows.reserve(std::min<std::size_t>(total, 1024));
    }

    void Push(const ScopeTableRow& row)
    {
        const std::size_t lane = static_cast<std::size_t>(row.lane);
        if (m_used[lane] < m_caps[lane]) {
            ++m_used[lane];
            m_table.rows.push_back(row);
        } else {
            ++m_table.rowsDropped;
        }
    }

private:
    ScopeTable& m_table;
    LaneRowCaps m_caps;
    std::array<std::size_t, kScopeLaneCount> m_used{};
};

ScopeTableRow RowFromAggregate(const ScopeAggregate& agg)
{
    ScopeTableRow row;
    row.lane = agg.lane;
    row.name = agg.name;
    row.feature = agg.feature;
    row.pathKey = MakePathKey(0, agg.lane, agg.name);
    row.kind = agg.kind;
    row.status = agg.status;
    row.calls = agg.calls;
    row.totalTicks = agg.totalTicks;
    row.selfTicks = agg.selfTicks;
    row.maxTicks = agg.maxTicks;
    return row;
}

void AppendFlatLane(const FrameRecord& frame, ScopeLane lane, ScopeSort sort, RowSink& sink)
{
    std::vector<ScopeTableRow> rows;
    for (const ScopeAggregate& agg : frame.aggregates.Rows()) {
        if (agg.lane == lane && agg.kind != ScopeKind::Marker) {
            rows.push_back(RowFromAggregate(agg));
        }
    }
    SortRows(rows, sort);
    for (const ScopeTableRow& row : rows) {
        sink.Push(row);
    }
}

struct Node {
    ScopeTableRow row;
    std::vector<std::uint32_t> children;
};

void AppendMainTree(const FrameRecord& frame, ScopeSort sort, RowSink& sink)
{
    const std::span<const ScopeEvent> events = frame.scopes.Events();
    std::vector<ScopeTimes> times(events.size());
    ComputeScopeTimes(events, times);

    // Events arrive children first. A parent starts no later than its children, so start time then depth
    // restores tree order.
    std::vector<std::size_t> order;
    order.reserve(events.size());
    for (std::size_t i = 0; i < events.size(); ++i) {
        if (events[i].kind != ScopeKind::Marker) {
            order.push_back(i);
        }
    }
    std::stable_sort(order.begin(), order.end(), [&events](std::size_t a, std::size_t b) {
        if (events[a].startTicks != events[b].startTicks) {
            return events[a].startTicks < events[b].startTicks;
        }
        return events[a].depth < events[b].depth;
    });

    std::vector<Node> nodes;
    nodes.reserve(order.size());
    std::vector<std::uint32_t> roots;
    std::vector<std::uint32_t> stack; // open node per tree depth
    std::unordered_map<std::uint64_t, std::uint32_t> byKey;
    byKey.reserve(order.size());

    for (const std::size_t i : order) {
        const ScopeEvent& e = events[i];

        // A missing parent event attaches the row to the nearest open one.
        std::uint32_t parent = kNoNode;
        if (e.depth > 0 && !stack.empty()) {
            parent = stack.size() >= e.depth ? stack[e.depth - 1] : stack.back();
        }
        const std::uint64_t parentKey = parent == kNoNode ? 0 : nodes[parent].row.pathKey;
        const std::uint64_t key = MakePathKey(parentKey, ScopeLane::Main, e.name);

        const auto [it, inserted] = byKey.try_emplace(key, static_cast<std::uint32_t>(nodes.size()));
        const std::uint32_t index = it->second;
        if (inserted) {
            Node node;
            node.row.lane = ScopeLane::Main;
            node.row.name = e.name;
            node.row.pathKey = key;
            node.row.depth = parent == kNoNode ? 0 : static_cast<std::uint16_t>(nodes[parent].row.depth + 1);
            node.row.kind = e.kind;
            nodes.push_back(std::move(node));
            (parent == kNoNode ? roots : nodes[parent].children).push_back(index);
        }

        ScopeTableRow& row = nodes[index].row;
        ++row.calls;
        row.totalTicks += times[i].totalTicks;
        row.selfTicks += times[i].selfTicks;
        row.maxTicks = std::max(row.maxTicks, times[i].totalTicks);

        stack.resize(std::min<std::size_t>(stack.size(), row.depth));
        stack.push_back(index);
    }

    if (sort != ScopeSort::CallOrder) {
        const auto byRow = [&nodes, sort](std::uint32_t a, std::uint32_t b) {
            return Precedes(sort, nodes[a].row, nodes[b].row);
        };
        std::stable_sort(roots.begin(), roots.end(), byRow);
        for (Node& node : nodes) {
            std::stable_sort(node.children.begin(), node.children.end(), byRow);
        }
    }

    std::vector<std::uint32_t> pending(roots.rbegin(), roots.rend());
    while (!pending.empty()) {
        const Node& node = nodes[pending.back()];
        pending.pop_back();
        ScopeTableRow row = node.row;
        row.hasChildren = !node.children.empty();
        sink.Push(row);
        pending.insert(pending.end(), node.children.rbegin(), node.children.rend());
    }
}

// A feature row with the pass rows below it. A pass without a feature stands alone.
struct GpuGroup {
    ScopeTableRow head;
    std::vector<ScopeTableRow> passes;
    SampleStatus firstStatus = SampleStatus::Absent;
    bool allPresent = true;
    bool anyTime = false;
};

void AppendGpuTree(const FrameRecord& frame, ScopeSort sort, RowSink& sink)
{
    std::vector<GpuGroup> groups;
    std::unordered_map<NameId, std::uint32_t> byFeature;

    for (const ScopeAggregate& agg : frame.aggregates.Rows()) {
        if (agg.lane != ScopeLane::Gpu || agg.kind == ScopeKind::Marker) {
            continue;
        }
        if (agg.feature == kInvalidNameId) {
            GpuGroup alone;
            alone.head = RowFromAggregate(agg);
            groups.push_back(std::move(alone));
            continue;
        }

        const auto [it, inserted] = byFeature.try_emplace(agg.feature, static_cast<std::uint32_t>(groups.size()));
        if (inserted) {
            GpuGroup group;
            group.head.lane = ScopeLane::Gpu;
            group.head.name = agg.feature;
            group.head.feature = agg.feature;
            group.head.pathKey = MakePathKey(0, ScopeLane::Gpu, agg.feature);
            group.firstStatus = agg.status;
            groups.push_back(std::move(group));
        }
        GpuGroup& group = groups[it->second];

        ScopeTableRow pass = RowFromAggregate(agg);
        pass.pathKey = MakePathKey(group.head.pathKey, ScopeLane::Gpu, agg.name);
        pass.depth = 1;
        group.passes.push_back(pass);

        group.head.calls += agg.calls;
        group.head.totalTicks += agg.totalTicks;
        group.head.selfTicks += agg.totalTicks;
        group.head.maxTicks = std::max(group.head.maxTicks, agg.maxTicks);
        const bool timed = agg.status == SampleStatus::Present || agg.status == SampleStatus::Partial;
        group.anyTime = group.anyTime || timed;
        group.allPresent = group.allPresent && agg.status == SampleStatus::Present;
    }

    for (GpuGroup& group : groups) {
        if (group.passes.empty()) {
            continue;
        }
        group.head.hasChildren = true;
        group.head.status = group.allPresent ? SampleStatus::Present
            : group.anyTime                  ? SampleStatus::Partial
                                             : group.firstStatus;
        SortRows(group.passes, sort);
    }
    if (sort != ScopeSort::CallOrder) {
        std::stable_sort(groups.begin(), groups.end(),
            [sort](const GpuGroup& a, const GpuGroup& b) { return Precedes(sort, a.head, b.head); });
    }

    for (const GpuGroup& group : groups) {
        sink.Push(group.head);
        for (const ScopeTableRow& pass : group.passes) {
            sink.Push(pass);
        }
    }
}

} // namespace

ScopeTable BuildScopeTree(const FrameRecord& frame, ScopeSort sort, const LaneRowCaps& caps)
{
    ScopeTable table;
    RowSink sink(table, caps);
    AppendMainTree(frame, sort, sink);
    AppendFlatLane(frame, ScopeLane::Workers, sort, sink);
    AppendGpuTree(frame, sort, sink);
    table.treeTruncated = frame.scopes.Dropped() != 0;
    return table;
}

ScopeTable BuildScopeFlat(const FrameRecord& frame, ScopeSort sort, const LaneRowCaps& caps)
{
    ScopeTable table;
    RowSink sink(table, caps);
    AppendFlatLane(frame, ScopeLane::Main, sort, sink);
    AppendFlatLane(frame, ScopeLane::Workers, sort, sink);
    AppendFlatLane(frame, ScopeLane::Gpu, sort, sink);
    return table;
}

} // namespace gte::Profiling
