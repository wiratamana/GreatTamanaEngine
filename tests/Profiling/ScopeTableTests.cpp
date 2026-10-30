// Merged call table: tree merge, pre-order, sort, cap, GPU grouping and flat completeness.
#include "Profiling/FrameGate.h"
#include "Profiling/ManualClock.h"
#include "Profiling/ProfilingTypes.h"
#include "Profiling/ScopeAggregate.h"
#include "Profiling/ScopeLog.h"
#include "Profiling/ScopeTable.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <memory>
#include <vector>

namespace gte::Profiling {
namespace {

constexpr LaneRowCaps kCap100{ 100, 100, 100 };

// Records scopes through a real log, then copies the result into a heap frame record.
class Recorder {
public:
    Recorder()
    {
        FrameGate::MarkMainThread();
        m_log->BeginFrame(true);
    }

    void Leaf(NameId name, std::uint64_t ticks)
    {
        const ScopeStart start = m_log->Begin(m_clock.NowTicks());
        m_clock.AdvanceTicks(ticks);
        m_log->End(start, name, ScopeKind::Work, ViewSlot::Shared, m_clock.NowTicks());
    }

    template <class Body>
    void Scope(NameId name, std::uint64_t ownTicks, Body body)
    {
        const ScopeStart start = m_log->Begin(m_clock.NowTicks());
        m_clock.AdvanceTicks(ownTicks);
        body();
        m_log->End(start, name, ScopeKind::Work, ViewSlot::Shared, m_clock.NowTicks());
    }

    void Marker(NameId name) { m_log->Marker(name, ViewSlot::Shared, m_clock.NowTicks()); }

    std::unique_ptr<FrameRecord> Finish()
    {
        m_log->EndFrame();
        auto frame = std::make_unique<FrameRecord>();
        frame->scopes = m_log->Buffer();
        frame->aggregates = m_log->Aggregates();
        return frame;
    }

private:
    std::unique_ptr<ScopeLog> m_log = std::make_unique<ScopeLog>();
    ManualClock m_clock;
};

const ScopeTableRow* FindRow(const ScopeTable& table, NameId name)
{
    for (const ScopeTableRow& row : table.rows) {
        if (row.name == name) {
            return &row;
        }
    }
    return nullptr;
}

std::vector<NameId> Names(const ScopeTable& table)
{
    std::vector<NameId> names;
    for (const ScopeTableRow& row : table.rows) {
        names.push_back(row.name);
    }
    return names;
}

GpuPassRow MakePass(NameId pass, NameId feature, float workMs, SampleStatus status)
{
    GpuPassRow row;
    row.pass = pass;
    row.feature = feature;
    row.workMs = workMs;
    row.status = status;
    return row;
}

TEST(ScopeTableTest, MergesRepeatedChildrenIntoOneRow)
{
    Recorder rec;
    rec.Scope(200, 5, [&] {
        for (int i = 0; i < 400; ++i) {
            rec.Leaf(201, 3);
        }
    });
    const auto frame = rec.Finish();

    const ScopeTable table = BuildScopeTree(*frame, ScopeSort::CallOrder, kCap100);

    ASSERT_EQ(table.rows.size(), 2u);
    EXPECT_EQ(table.rows[0].name, 200u);
    EXPECT_EQ(table.rows[0].depth, 0u);
    EXPECT_TRUE(table.rows[0].hasChildren);
    EXPECT_EQ(table.rows[0].calls, 1u);
    EXPECT_EQ(table.rows[1].name, 201u);
    EXPECT_EQ(table.rows[1].depth, 1u);
    EXPECT_FALSE(table.rows[1].hasChildren);
    EXPECT_EQ(table.rows[1].calls, 400u);
    EXPECT_EQ(table.rows[1].totalTicks, 1200u);
    EXPECT_EQ(table.rows[1].selfTicks, 1200u);
    EXPECT_EQ(table.rows[1].maxTicks, 3u);
    EXPECT_EQ(table.rows[0].totalTicks, 1205u);
    EXPECT_EQ(table.rows[0].selfTicks, 5u);
}

TEST(ScopeTableTest, SameNameUnderTwoParentsStaysSeparateWithStableKeys)
{
    Recorder rec;
    rec.Scope(210, 1, [&] { rec.Leaf(212, 2); });
    rec.Scope(211, 1, [&] { rec.Leaf(212, 2); });
    const auto frame = rec.Finish();

    const ScopeTable first = BuildScopeTree(*frame, ScopeSort::CallOrder, kCap100);
    const ScopeTable second = BuildScopeTree(*frame, ScopeSort::CallOrder, kCap100);

    ASSERT_EQ(first.rows.size(), 4u);
    EXPECT_EQ(Names(first), (std::vector<NameId>{ 210, 212, 211, 212 }));
    EXPECT_NE(first.rows[1].pathKey, first.rows[3].pathKey);
    ASSERT_EQ(second.rows.size(), first.rows.size());
    for (std::size_t i = 0; i < first.rows.size(); ++i) {
        EXPECT_EQ(first.rows[i].pathKey, second.rows[i].pathKey);
    }
}

TEST(ScopeTableTest, PreOrderDepthAndHasChildren)
{
    Recorder rec;
    rec.Scope(220, 1, [&] {
        rec.Scope(221, 1, [&] { rec.Leaf(222, 1); });
        rec.Leaf(223, 1);
    });
    const auto frame = rec.Finish();

    const ScopeTable table = BuildScopeTree(*frame, ScopeSort::CallOrder, kCap100);

    ASSERT_EQ(table.rows.size(), 4u);
    EXPECT_EQ(Names(table), (std::vector<NameId>{ 220, 221, 222, 223 }));
    const std::array<std::uint16_t, 4> depths{ 0, 1, 2, 1 };
    const std::array<bool, 4> children{ true, true, false, false };
    for (std::size_t i = 0; i < 4; ++i) {
        EXPECT_EQ(table.rows[i].depth, depths[i]) << i;
        EXPECT_EQ(table.rows[i].hasChildren, children[i]) << i;
    }
    EXPECT_FALSE(table.treeTruncated);
}

TEST(ScopeTableTest, SortsSiblingsByTotalSelfAndCallOrder)
{
    Recorder rec;
    rec.Scope(404, 1, [&] {
        rec.Leaf(400, 10);                              // total 10, self 10
        rec.Scope(401, 5, [&] { rec.Leaf(403, 25); });  // total 30, self 5
        rec.Leaf(402, 20);                              // total 20, self 20
    });
    const auto frame = rec.Finish();

    const auto children = [&](ScopeSort sort) {
        std::vector<NameId> names;
        for (const ScopeTableRow& row : BuildScopeTree(*frame, sort, kCap100).rows) {
            if (row.depth == 1) {
                names.push_back(row.name);
            }
        }
        return names;
    };

    EXPECT_EQ(children(ScopeSort::CallOrder), (std::vector<NameId>{ 400, 401, 402 }));
    EXPECT_EQ(children(ScopeSort::TotalDesc), (std::vector<NameId>{ 401, 402, 400 }));
    EXPECT_EQ(children(ScopeSort::SelfDesc), (std::vector<NameId>{ 402, 400, 401 }));
}

TEST(ScopeTableTest, CapsRowsAndCountsTheRest)
{
    Recorder rec;
    rec.Scope(410, 1, [&] {
        for (NameId id = 411; id < 421; ++id) {
            rec.Leaf(id, 1);
        }
    });
    const auto frame = rec.Finish();

    const ScopeTable table = BuildScopeTree(*frame, ScopeSort::CallOrder, LaneRowCaps{ 4, 100, 100 });

    EXPECT_EQ(table.rows.size(), 4u);
    EXPECT_EQ(table.TotalDropped(), 7u);
    EXPECT_EQ(table.rowsDroppedByLane[static_cast<std::size_t>(ScopeLane::Main)], 7u);
}

TEST(ScopeTableTest, ALongMainLaneNeverHidesTheGpuLane)
{
    Recorder rec;
    rec.Scope(410, 1, [&] {
        for (NameId id = 411; id < 421; ++id) {
            rec.Leaf(id, 1);
        }
    });
    auto frame = rec.Finish();
    const std::array<GpuPassRow, 2> passes{
        MakePass(500, 600, 2.0f, SampleStatus::Present),
        MakePass(501, 600, 1.0f, SampleStatus::Present),
    };
    AppendGpuAggregates(passes, frame->aggregates);

    const auto gpuRows = [](const ScopeTable& table) {
        return std::count_if(table.rows.begin(), table.rows.end(),
            [](const ScopeTableRow& row) { return row.lane == ScopeLane::Gpu; });
    };

    const ScopeTable tree = BuildScopeTree(*frame, ScopeSort::CallOrder, LaneRowCaps{ 4, 100, 100 });
    EXPECT_EQ(tree.rows.size(), 4u + 3u); // 4 Main rows, then the feature row and its 2 passes
    EXPECT_EQ(gpuRows(tree), 3);
    EXPECT_EQ(tree.TotalDropped(), 7u);
    EXPECT_EQ(tree.rowsDroppedByLane[static_cast<std::size_t>(ScopeLane::Gpu)], 0u);

    const ScopeTable flat = BuildScopeFlat(*frame, ScopeSort::CallOrder, LaneRowCaps{ 4, 100, 1 });
    EXPECT_EQ(gpuRows(flat), 1);
    EXPECT_EQ(flat.rows.size(), 4u + 1u);
    EXPECT_EQ(flat.TotalDropped(), 7u + 1u);
    EXPECT_EQ(flat.rowsDroppedByLane[static_cast<std::size_t>(ScopeLane::Main)], 7u);
    EXPECT_EQ(flat.rowsDroppedByLane[static_cast<std::size_t>(ScopeLane::Gpu)], 1u);
}

TEST(ScopeTableTest, OrphanAttachesToNearestOpenRowAndDroppedEventsMarkTruncation)
{
    auto frame = std::make_unique<FrameRecord>();
    frame->scopes.Append(ScopeEvent{ 10, 20, 301, 2, ScopeKind::Work, ViewSlot::Shared }); // depth-1 parent missing
    frame->scopes.Append(ScopeEvent{ 0, 100, 300, 0, ScopeKind::Work, ViewSlot::Shared });
    for (std::size_t i = 0; i < kMaxScopeEventsPerFrame; ++i) {
        frame->scopes.Append(ScopeEvent{ 200, 201, 302, 0, ScopeKind::Work, ViewSlot::Shared });
    }
    ASSERT_GT(frame->scopes.Dropped(), 0u);

    const ScopeTable table = BuildScopeTree(*frame, ScopeSort::CallOrder, kCap100);

    const ScopeTableRow* orphan = FindRow(table, 301);
    ASSERT_NE(orphan, nullptr);
    EXPECT_EQ(orphan->depth, 1u);
    EXPECT_TRUE(table.treeTruncated);
}

TEST(ScopeTableTest, FlatRowsStayCompleteWhenEventsAreDropped)
{
    Recorder rec;
    rec.Scope(430, 5, [&] {
        for (int i = 0; i < 2500; ++i) {
            rec.Leaf(431, 4);
        }
    });
    const auto frame = rec.Finish();
    ASSERT_GT(frame->scopes.Dropped(), 0u);

    const ScopeTable tree = BuildScopeTree(*frame, ScopeSort::CallOrder, kCap100);
    const ScopeTable flat = BuildScopeFlat(*frame, ScopeSort::CallOrder, kCap100);

    EXPECT_TRUE(tree.treeTruncated);
    EXPECT_FALSE(flat.treeTruncated);
    ASSERT_EQ(flat.rows.size(), 2u);
    const ScopeTableRow* child = FindRow(flat, 431);
    const ScopeTableRow* parent = FindRow(flat, 430);
    ASSERT_TRUE(child && parent);
    EXPECT_EQ(child->calls, 2500u);
    EXPECT_EQ(child->totalTicks, 10000u);
    EXPECT_EQ(parent->calls, 1u);
    EXPECT_EQ(parent->totalTicks, 10005u);
    EXPECT_EQ(parent->depth, 0u);
}

TEST(ScopeTableTest, MarkersNeverAppear)
{
    Recorder rec;
    rec.Scope(440, 2, [&] {
        rec.Marker(441);
        rec.Leaf(442, 2);
    });
    const auto frame = rec.Finish();

    EXPECT_EQ(FindRow(BuildScopeTree(*frame, ScopeSort::CallOrder, kCap100), 441), nullptr);
    EXPECT_EQ(FindRow(BuildScopeFlat(*frame, ScopeSort::CallOrder, kCap100), 441), nullptr);
}

TEST(ScopeTableTest, GpuTreeSumsPassesUnderTheirFeature)
{
    const std::array<GpuPassRow, 3> passes{
        MakePass(500, 600, 2.0f, SampleStatus::Present),
        MakePass(501, 600, 0.0f, SampleStatus::Absent),
        MakePass(502, kInvalidNameId, 1.0f, SampleStatus::Present),
    };
    auto frame = std::make_unique<FrameRecord>();
    AppendGpuAggregates(passes, frame->aggregates);

    const ScopeTable tree = BuildScopeTree(*frame, ScopeSort::CallOrder, kCap100);

    ASSERT_EQ(tree.rows.size(), 4u);
    EXPECT_EQ(Names(tree), (std::vector<NameId>{ 600, 500, 501, 502 }));
    EXPECT_EQ(tree.rows[0].lane, ScopeLane::Gpu);
    EXPECT_EQ(tree.rows[0].depth, 0u);
    EXPECT_TRUE(tree.rows[0].hasChildren);
    EXPECT_EQ(tree.rows[0].calls, 2u);
    EXPECT_EQ(tree.rows[0].totalTicks, 2'000'000u);
    EXPECT_EQ(tree.rows[0].status, SampleStatus::Partial);
    EXPECT_EQ(tree.rows[1].depth, 1u);
    EXPECT_EQ(tree.rows[2].status, SampleStatus::Absent);
    EXPECT_EQ(tree.rows[3].depth, 0u);
    EXPECT_FALSE(tree.rows[3].hasChildren);

    const ScopeTable flat = BuildScopeFlat(*frame, ScopeSort::CallOrder, kCap100);

    ASSERT_EQ(flat.rows.size(), 3u);
    EXPECT_EQ(Names(flat), (std::vector<NameId>{ 500, 501, 502 }));
    EXPECT_EQ(flat.rows[0].feature, 600u);
    EXPECT_EQ(flat.rows[1].feature, 600u);
    EXPECT_EQ(flat.rows[1].status, SampleStatus::Absent);
    EXPECT_EQ(flat.rows[2].feature, kInvalidNameId);
}

TEST(ScopeTableTest, WorkerRowsFollowTheMainLaneAtDepthZero)
{
    Recorder rec;
    rec.Leaf(450, 3);
    auto frame = rec.Finish();
    const std::array<WorkerEvent, 2> events{ { { 0, 10, 451, 0, 0, 0 }, { 20, 50, 451, 1, 0, 0 } } };
    AppendWorkerAggregates(events, frame->aggregates);

    for (const ScopeTable& table : { BuildScopeTree(*frame, ScopeSort::CallOrder, kCap100),
             BuildScopeFlat(*frame, ScopeSort::CallOrder, kCap100) }) {
        ASSERT_EQ(table.rows.size(), 2u);
        EXPECT_EQ(table.rows[0].lane, ScopeLane::Main);
        EXPECT_EQ(table.rows[1].lane, ScopeLane::Workers);
        EXPECT_EQ(table.rows[1].depth, 0u);
        EXPECT_EQ(table.rows[1].calls, 2u);
        EXPECT_EQ(table.rows[1].totalTicks, 40u);
    }
}

} // namespace
} // namespace gte::Profiling
