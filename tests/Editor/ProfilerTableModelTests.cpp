// Table view model: percent bases, filter, category mask, collapse, waiting flag, top things.
#include "Editor/ProfilerTableModel.h"
#include "Profiling/NameTable.h"
#include "Profiling/ProfilingTypes.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <memory>

namespace gte {
namespace {

using Profiling::FrameRecord;
using Profiling::ScopeAggregate;
using Profiling::ScopeEvent;
using Profiling::ScopeKind;
using Profiling::ScopeLane;
using Profiling::SampleStatus;
using Profiling::ViewSlot;

constexpr std::uint64_t kMs = 1'000'000;

Profiling::NameId Id(const char* name)
{
    return Profiling::InternName(name);
}

std::unique_ptr<FrameRecord> MakeFrame(double frameMs)
{
    auto frame = std::make_unique<FrameRecord>();
    frame->summary.cpuMilliseconds = frameMs;
    return frame;
}

void AddEvent(FrameRecord& frame, const char* name, std::uint64_t startMs, std::uint64_t endMs, std::uint16_t depth,
    ScopeKind kind = ScopeKind::Work)
{
    frame.scopes.Append(ScopeEvent{ startMs * kMs, endMs * kMs, Id(name), depth, kind, ViewSlot::Shared });
}

void AddAggregate(FrameRecord& frame, ScopeLane lane, const char* name, std::uint64_t totalMs, std::uint64_t selfMs,
    SampleStatus status = SampleStatus::Present, const char* feature = nullptr)
{
    ScopeAggregate agg;
    agg.lane = lane;
    agg.name = Id(name);
    agg.feature = feature != nullptr ? Id(feature) : Profiling::kInvalidNameId;
    agg.totalTicks = totalMs * kMs;
    agg.selfTicks = selfMs * kMs;
    agg.maxTicks = agg.totalTicks;
    agg.calls = 1;
    agg.status = status;
    frame.aggregates.Add(agg);
}

void SetGpuSpan(FrameRecord& frame, float ms, SampleStatus status)
{
    frame.summary.gpu.span.milliseconds = ms;
    frame.summary.gpu.span.status = status;
}

// Offscreen (0-100) with child Offscreen.Begin (10-20), then PhysicsSystem::Update (100-150).
std::unique_ptr<FrameRecord> MakeTreeFrame()
{
    auto frame = MakeFrame(200.0);
    AddEvent(*frame, "Offscreen.Begin", 10, 20, 1);
    AddEvent(*frame, "Offscreen", 0, 100, 0);
    AddEvent(*frame, "PhysicsSystem::Update", 100, 150, 0);
    return frame;
}

const TableRowView* Find(const TableModel& model, const char* rawName)
{
    for (const TableRowView& row : model.rows) {
        if (row.rawName == rawName) {
            return &row;
        }
    }
    return nullptr;
}

TEST(ProfilerTableModelTest, PercentUsesFrameForCpuAndSpanForGpu)
{
    auto frame = MakeFrame(200.0);
    AddAggregate(*frame, ScopeLane::Main, "Offscreen", 100, 100);
    AddAggregate(*frame, ScopeLane::Gpu, "PassA", 10, 10, SampleStatus::Present, "FeatA");
    SetGpuSpan(*frame, 20.0f, SampleStatus::Present);

    TableOptions options;
    options.tree = false;
    const TableModel model = BuildTableModel(*frame, options, 100);

    const TableRowView* cpu = Find(model, "Offscreen");
    const TableRowView* gpu = Find(model, "PassA");
    ASSERT_NE(cpu, nullptr);
    ASSERT_NE(gpu, nullptr);
    EXPECT_TRUE(cpu->percentKnown);
    EXPECT_DOUBLE_EQ(cpu->percentOfBase, 50.0);
    EXPECT_TRUE(gpu->percentKnown);
    EXPECT_DOUBLE_EQ(gpu->percentOfBase, 50.0);
    EXPECT_FALSE(gpu->featureTitle.empty());
    EXPECT_TRUE(model.gpuBaseKnown);
}

TEST(ProfilerTableModelTest, UnknownGpuBaseClearsGpuPercentOnly)
{
    auto frame = MakeFrame(200.0);
    AddAggregate(*frame, ScopeLane::Main, "Offscreen", 100, 100);
    AddAggregate(*frame, ScopeLane::Gpu, "PassA", 10, 10, SampleStatus::Present, "FeatA");
    SetGpuSpan(*frame, 0.0f, SampleStatus::Absent);

    TableOptions options;
    options.tree = false;
    const TableModel model = BuildTableModel(*frame, options, 100);

    EXPECT_FALSE(model.gpuBaseKnown);
    ASSERT_NE(Find(model, "PassA"), nullptr);
    EXPECT_FALSE(Find(model, "PassA")->percentKnown);
    EXPECT_TRUE(Find(model, "Offscreen")->percentKnown);
}

TEST(ProfilerTableModelTest, TreeKeepsAncestorsOfFilterHitsCaseInsensitive)
{
    auto frame = MakeTreeFrame();
    TableOptions options;
    options.filter = "OFFSCREEN.BEGIN";
    TableModel model = BuildTableModel(*frame, options, 100);

    ASSERT_EQ(model.rows.size(), 2u);
    EXPECT_EQ(model.rows[0].rawName, "Offscreen");
    EXPECT_EQ(model.rows[1].rawName, "Offscreen.Begin");
    EXPECT_EQ(model.rows[1].row.depth, 1u);

    options.filter = "START OF";
    model = BuildTableModel(*frame, options, 100);
    EXPECT_NE(Find(model, "Offscreen.Begin"), nullptr);
    EXPECT_EQ(Find(model, "PhysicsSystem::Update"), nullptr);
}

TEST(ProfilerTableModelTest, CategoryMaskHidesACategory)
{
    auto frame = MakeTreeFrame();
    const TableRowView physics = BuildTableModel(*frame, TableOptions{}, 100).rows.back();
    ASSERT_EQ(physics.rawName, "PhysicsSystem::Update");

    TableOptions options;
    options.categoryMask = kAllCategoriesMask & ~CategoryBit(physics.category);
    const TableModel model = BuildTableModel(*frame, options, 100);
    EXPECT_EQ(Find(model, "PhysicsSystem::Update"), nullptr);
    EXPECT_NE(Find(model, "Offscreen"), nullptr);
}

TEST(ProfilerTableModelTest, CollapsedRowHidesDescendantsButKeepsItsTotals)
{
    auto frame = MakeTreeFrame();
    const TableModel full = BuildTableModel(*frame, TableOptions{}, 100);
    ASSERT_EQ(full.rows.size(), 3u);
    const TableRowView* parent = Find(full, "Offscreen");
    ASSERT_NE(parent, nullptr);

    TableOptions options;
    options.collapsed.push_back(parent->row.pathKey);
    const TableModel model = BuildTableModel(*frame, options, 100);
    ASSERT_EQ(model.rows.size(), 2u);
    EXPECT_EQ(Find(model, "Offscreen.Begin"), nullptr);
    EXPECT_DOUBLE_EQ(Find(model, "Offscreen")->totalMs, 100.0);
}

TEST(ProfilerTableModelTest, BlockedRowsAndCatalogWaitsAreWaiting)
{
    auto frame = MakeFrame(100.0);
    AddEvent(*frame, "SampleBlockedName", 0, 10, 0, ScopeKind::Blocked);
    AddEvent(*frame, "Wait.Workers", 10, 20, 0);
    AddEvent(*frame, "PhysicsSystem::Update", 20, 30, 0);

    const TableModel model = BuildTableModel(*frame, TableOptions{}, 100);
    ASSERT_NE(Find(model, "SampleBlockedName"), nullptr);
    EXPECT_TRUE(Find(model, "SampleBlockedName")->waiting);
    EXPECT_TRUE(Find(model, "Wait.Workers")->waiting);
    EXPECT_FALSE(Find(model, "PhysicsSystem::Update")->waiting);
}

TEST(ProfilerTableModelTest, MaxRowsCapsAfterFilteringAndCountsTheRest)
{
    auto frame = MakeTreeFrame();
    const TableModel model = BuildTableModel(*frame, TableOptions{}, 2);
    EXPECT_EQ(model.rows.size(), 2u);
    EXPECT_EQ(model.rowsDropped, 1u);
}

TEST(ProfilerTableModelTest, TopThingsTakeBigNonWaitingCpuRowsAndBigGpuRows)
{
    auto frame = MakeFrame(100.0);
    AddAggregate(*frame, ScopeLane::Main, "PhysicsSystem::Update", 60, 60);
    AddAggregate(*frame, ScopeLane::Main, "Offscreen", 70, 2);
    AddAggregate(*frame, ScopeLane::Main, "Wait.Workers", 30, 30);
    AddAggregate(*frame, ScopeLane::Gpu, "PassA", 10, 10, SampleStatus::Present, "FeatA");
    AddAggregate(*frame, ScopeLane::Gpu, "PassB", 1, 1, SampleStatus::Present, "FeatA");
    SetGpuSpan(*frame, 40.0f, SampleStatus::Present);

    const std::vector<TopThing> things = BuildTopThings(*frame);
    ASSERT_EQ(things.size(), 2u);
    EXPECT_FALSE(things[0].gpu);
    EXPECT_DOUBLE_EQ(things[0].ms, 60.0);
    EXPECT_DOUBLE_EQ(things[0].percent, 60.0);
    EXPECT_TRUE(things[1].gpu);
    EXPECT_DOUBLE_EQ(things[1].percent, 25.0);
}

TEST(ProfilerTableModelTest, PlainSummaryInputSplitsBusyAndWait)
{
    Profiling::FrameSummary summary;
    summary.cpuMilliseconds = 20.0;
    summary.blockedTicks = 5 * kMs;
    summary.gpu.span.milliseconds = 8.0f;
    summary.gpu.span.status = SampleStatus::Present;

    const PlainSummaryInput in = MakePlainSummaryInput(summary);
    EXPECT_DOUBLE_EQ(in.frameMs, 20.0);
    EXPECT_DOUBLE_EQ(in.cpuBusyMs, 15.0);
    EXPECT_DOUBLE_EQ(in.cpuWaitMs, 5.0);
    EXPECT_EQ(in.gpuStatus, SampleStatus::Present);
}

} // namespace
} // namespace gte
