// Online per-name aggregates: they must match the event tree and stay exact when events are dropped.
#include "Profiling/FrameGate.h"
#include "Profiling/FrameProfiler.h"
#include "Profiling/ManualClock.h"
#include "Profiling/NameTable.h"
#include "Profiling/ProfilingClock.h"
#include "Profiling/ScopeAggregate.h"
#include "Profiling/ScopeLog.h"
#include "Profiling/ScopeTree.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <map>
#include <memory>
#include <random>
#include <span>
#include <thread>
#include <vector>

namespace gte::Profiling {
namespace {

const ScopeAggregate* FindRow(std::span<const ScopeAggregate> rows, ScopeLane lane, NameId name)
{
    for (const ScopeAggregate& row : rows) {
        if (row.lane == lane && row.name == name) {
            return &row;
        }
    }
    return nullptr;
}

// Every aggregate row equals the sums computed from the stored events.
void ExpectMatchesEvents(const ScopeLog& log)
{
    const std::span<const ScopeEvent> events = log.Buffer().Events();
    ASSERT_EQ(log.Buffer().Dropped(), 0u);
    std::vector<ScopeTimes> times(events.size());
    ComputeScopeTimes(events, times);

    struct Sum {
        std::uint64_t total = 0;
        std::uint64_t self = 0;
        std::uint32_t calls = 0;
    };
    std::map<NameId, Sum> expected;
    for (std::size_t i = 0; i < events.size(); ++i) {
        if (events[i].kind == ScopeKind::Marker) {
            continue;
        }
        Sum& sum = expected[events[i].name];
        sum.total += times[i].totalTicks;
        sum.self += times[i].selfTicks;
        ++sum.calls;
    }

    const std::span<const ScopeAggregate> rows = log.Aggregates().Rows();
    EXPECT_EQ(rows.size(), expected.size());
    for (const auto& [name, sum] : expected) {
        const ScopeAggregate* row = FindRow(rows, ScopeLane::Main, name);
        ASSERT_NE(row, nullptr) << "name " << name;
        EXPECT_EQ(row->totalTicks, sum.total) << "name " << name;
        EXPECT_EQ(row->selfTicks, sum.self) << "name " << name;
        EXPECT_EQ(row->calls, sum.calls) << "name " << name;
    }
}

void RunNested(ScopeLog& log, ManualClock& clock, std::mt19937& rng, int depth)
{
    const NameId name = 10 + rng() % 3;
    const ScopeStart start = log.Begin(clock.NowTicks());
    clock.AdvanceTicks(1 + rng() % 20);
    const int children = depth < 6 ? static_cast<int>(rng() % 3) : static_cast<int>(rng() % 2);
    for (int i = 0; i < children && depth < 10; ++i) {
        RunNested(log, clock, rng, depth + 1);
        if (rng() % 4 == 0) {
            log.Marker(11, ViewSlot::Shared, clock.NowTicks());
        }
    }
    clock.AdvanceTicks(rng() % 20);
    log.End(start, name, rng() % 5 == 0 ? ScopeKind::Blocked : ScopeKind::Work, ViewSlot::Primary, clock.NowTicks());
}

TEST(ScopeAggregateTest, MatchesTheEventTreeForRandomNesting)
{
    FrameGate::MarkMainThread();
    auto log = std::make_unique<ScopeLog>();
    ManualClock clock;
    std::mt19937 rng(12345);

    log->BeginFrame(true);
    for (int root = 0; root < 4; ++root) {
        RunNested(*log, clock, rng, 0);
    }
    log->EndFrame();

    ASSERT_GT(log->Buffer().Size(), 0u);
    ASSERT_LT(log->Buffer().Size(), kMaxScopeEventsPerFrame);
    ExpectMatchesEvents(*log);
}

TEST(ScopeAggregateTest, MatchesTheEventTreeWhenDepthIsClamped)
{
    FrameGate::MarkMainThread();
    auto log = std::make_unique<ScopeLog>();
    ManualClock clock;

    log->BeginFrame(true);
    std::vector<ScopeStart> open;
    for (int i = 0; i < 70; ++i) {
        open.push_back(log->Begin(clock.NowTicks()));
        clock.AdvanceTicks(3);
    }
    for (int i = 69; i >= 0; --i) {
        clock.AdvanceTicks(2);
        log->End(open[static_cast<std::size_t>(i)], 20 + static_cast<NameId>(i % 3), ScopeKind::Work, ViewSlot::Shared,
            clock.NowTicks());
    }
    log->EndFrame();

    ExpectMatchesEvents(*log);
}

TEST(ScopeAggregateTest, StaysExactWhenTheEventBufferOverflows)
{
    FrameGate::MarkMainThread();
    auto log = std::make_unique<ScopeLog>();
    ManualClock clock;
    constexpr NameId kParent = 30;
    constexpr NameId kChild = 31;

    log->BeginFrame(true);
    const ScopeStart parent = log->Begin(clock.NowTicks());
    clock.AdvanceTicks(5);
    for (int i = 0; i < 2500; ++i) {
        const ScopeStart child = log->Begin(clock.NowTicks());
        clock.AdvanceTicks(4);
        log->End(child, kChild, ScopeKind::Work, ViewSlot::Shared, clock.NowTicks());
    }
    log->End(parent, kParent, ScopeKind::Work, ViewSlot::Shared, clock.NowTicks());
    log->EndFrame();

    EXPECT_GT(log->Buffer().Dropped(), 0u);
    const std::span<const ScopeAggregate> rows = log->Aggregates().Rows();
    const ScopeAggregate* child = FindRow(rows, ScopeLane::Main, kChild);
    const ScopeAggregate* top = FindRow(rows, ScopeLane::Main, kParent);
    ASSERT_NE(child, nullptr);
    ASSERT_NE(top, nullptr);
    EXPECT_EQ(child->calls, 2500u);
    EXPECT_EQ(child->totalTicks, 10000u);
    EXPECT_EQ(child->maxTicks, 4u);
    EXPECT_EQ(top->calls, 1u);
    EXPECT_EQ(top->totalTicks, 10005u);
    EXPECT_EQ(top->selfTicks, 5u);
}

TEST(ScopeAggregateTest, CountsRowsThatDoNotFitAndKeepsExistingRowsExact)
{
    FrameGate::MarkMainThread();
    auto log = std::make_unique<ScopeLog>();
    ManualClock clock;
    constexpr std::size_t kNames = kMaxScopeAggregatesPerFrame + 32;

    log->BeginFrame(true);
    for (std::size_t i = 0; i < kNames; ++i) {
        const ScopeStart start = log->Begin(clock.NowTicks());
        clock.AdvanceTicks(2);
        log->End(start, static_cast<NameId>(100 + i), ScopeKind::Work, ViewSlot::Shared, clock.NowTicks());
    }
    const ScopeStart again = log->Begin(clock.NowTicks());
    clock.AdvanceTicks(2);
    log->End(again, 100, ScopeKind::Work, ViewSlot::Shared, clock.NowTicks());
    log->EndFrame();

    EXPECT_EQ(log->Aggregates().Size(), kMaxScopeAggregatesPerFrame);
    EXPECT_EQ(log->Aggregates().Dropped(), 32u);
    const ScopeAggregate* first = FindRow(log->Aggregates().Rows(), ScopeLane::Main, 100);
    ASSERT_NE(first, nullptr);
    EXPECT_EQ(first->calls, 2u);
    EXPECT_EQ(first->totalTicks, 4u);
}

TEST(ScopeAggregateTest, IgnoredLateAndForeignScopesCreateNoRows)
{
    FrameGate::MarkMainThread();
    auto log = std::make_unique<ScopeLog>();
    ManualClock clock;

    log->BeginFrame(false); // capture off: token 0
    const ScopeStart ignored = log->Begin(clock.NowTicks());
    log->End(ignored, 40, ScopeKind::Work, ViewSlot::Shared, clock.NowTicks());
    log->EndFrame();
    EXPECT_EQ(log->Aggregates().Size(), 0u);

    log->BeginFrame(true);
    const ScopeStart late = log->Begin(clock.NowTicks());
    log->EndFrame();
    log->BeginFrame(true); // the scope's frame is over
    log->End(late, 41, ScopeKind::Work, ViewSlot::Shared, clock.NowTicks());
    EXPECT_EQ(log->Aggregates().Size(), 0u);

    const ScopeStart foreign = log->Begin(clock.NowTicks());
    std::thread worker([&] { log->End(foreign, 42, ScopeKind::Work, ViewSlot::Shared, clock.NowTicks()); });
    worker.join();
    EXPECT_EQ(log->Aggregates().Size(), 0u);
}

TEST(ScopeAggregateTest, BeginFrameStartsClean)
{
    FrameGate::MarkMainThread();
    auto log = std::make_unique<ScopeLog>();
    ManualClock clock;

    for (int frame = 0; frame < 2; ++frame) {
        log->BeginFrame(true);
        ASSERT_EQ(log->Aggregates().Size(), 0u);
        const ScopeStart start = log->Begin(clock.NowTicks());
        clock.AdvanceTicks(7);
        log->End(start, 50, ScopeKind::Work, ViewSlot::Shared, clock.NowTicks());
        log->EndFrame();

        ASSERT_EQ(log->Aggregates().Size(), 1u);
        EXPECT_EQ(log->Aggregates().Rows()[0].calls, 1u);
        EXPECT_EQ(log->Aggregates().Rows()[0].totalTicks, 7u);
    }
}

TEST(ScopeAggregateTest, MarkersMakeNoRowAndKeepNeighbourSelfTime)
{
    FrameGate::MarkMainThread();
    auto log = std::make_unique<ScopeLog>();
    ManualClock clock;

    log->BeginFrame(true);
    const ScopeStart parent = log->Begin(clock.NowTicks());
    clock.AdvanceTicks(10);
    log->Marker(60, ViewSlot::Shared, clock.NowTicks());
    clock.AdvanceTicks(10);
    log->End(parent, 61, ScopeKind::Work, ViewSlot::Shared, clock.NowTicks());
    log->EndFrame();

    EXPECT_EQ(FindRow(log->Aggregates().Rows(), ScopeLane::Main, 60), nullptr);
    const ScopeAggregate* top = FindRow(log->Aggregates().Rows(), ScopeLane::Main, 61);
    ASSERT_NE(top, nullptr);
    EXPECT_EQ(top->totalTicks, 20u);
    EXPECT_EQ(top->selfTicks, 20u);
}

TEST(ScopeAggregateTest, WorkerEventsMergeByName)
{
    const std::array<WorkerEvent, 3> events{ {
        { 0, 10, 70, 0, 0, 0 },
        { 20, 50, 70, 1, 0, 0 },
        { 5, 6, 71, 0, 0, 0 },
    } };
    auto out = std::make_unique<ScopeAggregateBuffer>();

    AppendWorkerAggregates(events, *out);

    ASSERT_EQ(out->Size(), 2u);
    const ScopeAggregate* merged = FindRow(out->Rows(), ScopeLane::Workers, 70);
    ASSERT_NE(merged, nullptr);
    EXPECT_EQ(merged->calls, 2u);
    EXPECT_EQ(merged->totalTicks, 40u);
    EXPECT_EQ(merged->selfTicks, 40u);
    EXPECT_EQ(merged->maxTicks, 30u);
}

GpuPassRow MakePass(NameId pass, float barrierMs, float workMs, SampleStatus status, std::uint32_t folded = 0)
{
    GpuPassRow row;
    row.pass = pass;
    row.feature = 9;
    row.barrierMs = barrierMs;
    row.workMs = workMs;
    row.status = status;
    row.foldedPasses = folded;
    return row;
}

TEST(ScopeAggregateTest, GpuRowsMergeAndKeepTheirFeature)
{
    const std::array<GpuPassRow, 2> rows{ MakePass(80, 1.0f, 2.0f, SampleStatus::Present), MakePass(80, 0.5f, 0.5f, SampleStatus::Present) };
    auto out = std::make_unique<ScopeAggregateBuffer>();

    AppendGpuAggregates(rows, *out);

    ASSERT_EQ(out->Size(), 1u);
    const ScopeAggregate& row = out->Rows()[0];
    EXPECT_EQ(row.lane, ScopeLane::Gpu);
    EXPECT_EQ(row.calls, 2u);
    EXPECT_EQ(row.totalTicks, 4'000'000u);
    EXPECT_EQ(row.selfTicks, row.totalTicks);
    EXPECT_EQ(row.maxTicks, 3'000'000u);
    EXPECT_EQ(row.feature, 9u);
    EXPECT_EQ(row.status, SampleStatus::Present);
}

TEST(ScopeAggregateTest, GpuStatusCombinesPerTheRules)
{
    auto out = std::make_unique<ScopeAggregateBuffer>();
    const std::array<GpuPassRow, 7> rows{
        MakePass(81, 1.0f, 1.0f, SampleStatus::Present), // present then absent: partial
        MakePass(81, 5.0f, 5.0f, SampleStatus::Absent),
        MakePass(82, 1.0f, 1.0f, SampleStatus::Absent), // all absent: absent, no time
        MakePass(82, 1.0f, 1.0f, SampleStatus::Absent),
        MakePass(83, 3.0f, 3.0f, SampleStatus::Error), // not measured adds nothing
        MakePass(84, 1.0f, 0.0f, SampleStatus::Absent), // absent then present: partial
        MakePass(84, 1.0f, 1.0f, SampleStatus::Present),
    };

    AppendGpuAggregates(rows, *out);

    const ScopeAggregate* partial = FindRow(out->Rows(), ScopeLane::Gpu, 81);
    const ScopeAggregate* absent = FindRow(out->Rows(), ScopeLane::Gpu, 82);
    const ScopeAggregate* error = FindRow(out->Rows(), ScopeLane::Gpu, 83);
    const ScopeAggregate* late = FindRow(out->Rows(), ScopeLane::Gpu, 84);
    ASSERT_TRUE(partial && absent && error && late);
    EXPECT_EQ(partial->status, SampleStatus::Partial);
    EXPECT_EQ(partial->totalTicks, 2'000'000u);
    EXPECT_EQ(absent->status, SampleStatus::Absent);
    EXPECT_EQ(absent->totalTicks, 0u);
    EXPECT_EQ(error->status, SampleStatus::Error);
    EXPECT_EQ(error->totalTicks, 0u);
    EXPECT_EQ(late->status, SampleStatus::Partial);
    EXPECT_EQ(late->totalTicks, 2'000'000u);
}

TEST(ScopeAggregateTest, FoldedGpuPassesBecomeOneNamedPartialRow)
{
    const std::array<GpuPassRow, 1> rows{ MakePass(85, 1.0f, 1.0f, SampleStatus::Present, 3) };
    auto out = std::make_unique<ScopeAggregateBuffer>();

    AppendGpuAggregates(rows, *out);

    ASSERT_EQ(out->Size(), 1u);
    const ScopeAggregate& row = out->Rows()[0];
    EXPECT_STREQ(ResolveName(row.name), "(folded passes)");
    EXPECT_EQ(row.status, SampleStatus::Partial);
    EXPECT_EQ(row.calls, 3u);
    EXPECT_EQ(row.totalTicks, 2'000'000u);
}

class FrameProfilerAggregateTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        FrameProfiler::Instance().ResetForTesting();
        FrameProfiler::Instance().SetCaptureEnabled(true);
    }
    void TearDown() override { FrameProfiler::Instance().ResetForTesting(); }
};

TEST_F(FrameProfilerAggregateTest, EndFrameCarriesMainWorkerAndGpuRows)
{
    FrameProfiler& profiler = FrameProfiler::Instance();
    const NameId mainName = InternName("ScopeAggregateTest.Main");
    const NameId workerName = InternName("ScopeAggregateTest.Worker");
    const NameId passName = InternName("ScopeAggregateTest.Pass");

    profiler.BeginFrame();
    const ScopeStart start = profiler.BeginScope();
    const std::uint64_t workerStart = GetProfilingPerformanceCounter();
    profiler.RecordWorkerScope(0, workerName, workerStart, workerStart + 1);
    profiler.EndScope(start, mainName, ScopeKind::Work, ViewSlot::Shared);
    const std::array<GpuPassRow, 1> gpu{ MakePass(passName, 1.0f, 1.0f, SampleStatus::Present) };
    profiler.SetGpuPassRows(gpu);
    profiler.EndFrame();

    const FrameRecord& frame = profiler.LastCompletedFrame();
    const std::span<const ScopeAggregate> rows = frame.aggregates.Rows();
    EXPECT_NE(FindRow(rows, ScopeLane::Main, mainName), nullptr);
    EXPECT_NE(FindRow(rows, ScopeLane::Workers, workerName), nullptr);
    const ScopeAggregate* gpuRow = FindRow(rows, ScopeLane::Gpu, passName);
    ASSERT_NE(gpuRow, nullptr);
    EXPECT_EQ(gpuRow->totalTicks, 2'000'000u);

    std::array<float, 4> series{};
    EXPECT_TRUE(profiler.Scopes().CopySeries(ScopeLane::Gpu, passName, ScopeSeries::TotalMs, series));
    EXPECT_FLOAT_EQ(series[3], 2.0f);
    EXPECT_EQ(profiler.Scopes().NewestFrameIndex(), frame.summary.frameIndex);
}

TEST_F(FrameProfilerAggregateTest, GpuRowsOutsideAFrameAreIgnored)
{
    FrameProfiler& profiler = FrameProfiler::Instance();
    const std::array<GpuPassRow, 1> gpu{ MakePass(86, 1.0f, 1.0f, SampleStatus::Present) };

    profiler.SetGpuPassRows(gpu); // before the frame
    profiler.BeginFrame();
    profiler.EndFrame();
    profiler.SetGpuPassRows(gpu); // after the frame

    EXPECT_EQ(profiler.LastCompletedFrame().aggregates.Size(), 0u);
}

} // namespace
} // namespace gte::Profiling
