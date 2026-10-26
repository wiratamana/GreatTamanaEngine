#include "Profiling/EventBuffer.h"
#include "Profiling/FrameGate.h"
#include "Profiling/ManualClock.h"
#include "Profiling/NameTable.h"
#include "Profiling/ProfilingBudget.h"
#include "Profiling/ScopeEvent.h"
#include "Profiling/ScopeLog.h"
#include "Profiling/ScopeTree.h"

#include <gtest/gtest.h>

#include <array>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace gte::Profiling {
namespace {

constexpr NameId kA = 2;
constexpr NameId kB = 3;
constexpr NameId kC = 4;

class ScopeLogTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        FrameGate::MarkMainThread();
        log = std::make_unique<ScopeLog>();
    }

    void End(const ScopeStart& start, NameId name, std::uint64_t now)
    {
        log->End(start, name, ScopeKind::Work, ViewSlot::Shared, now);
    }

    std::unique_ptr<ScopeLog> log;
};

// ---- NameTable (interning) ----

TEST(NameTableTest, SameTextGivesTheSameStableId)
{
    auto table = std::make_unique<NameTable>();
    const NameId a = table->Intern("Alpha");
    const NameId b = table->Intern("Beta");
    EXPECT_NE(a, kInvalidNameId);
    EXPECT_NE(a, b);
    EXPECT_NE(a, kOverflowNameId);

    const std::uint32_t countBefore = table->Count();
    EXPECT_EQ(table->Intern("Alpha"), a);
    EXPECT_EQ(table->Intern(std::string("Beta")), b);
    EXPECT_EQ(table->Count(), countBefore);

    EXPECT_STREQ(table->Resolve(a), "Alpha");
    EXPECT_STREQ(table->Resolve(b), "Beta");
    EXPECT_EQ(table->OverflowCount(), 0u);
}

TEST(NameTableTest, InvalidInputAndUnknownIdsAreSafe)
{
    auto table = std::make_unique<NameTable>();
    EXPECT_EQ(table->Intern(static_cast<const char*>(nullptr)), kInvalidNameId);
    EXPECT_EQ(table->Intern(""), kInvalidNameId);
    EXPECT_STREQ(table->Resolve(kInvalidNameId), "");
    EXPECT_STREQ(table->Resolve(123456), "");
    EXPECT_STREQ(table->Resolve(kOverflowNameId), "<overflow>");
}

TEST(NameTableTest, FullTableReturnsTheOverflowIdAndCountsIt)
{
    auto table = std::make_unique<NameTable>();
    const NameId first = table->Intern("scope_0");

    constexpr std::size_t kTries = 4000;
    std::size_t overflowed = 0;
    for (std::size_t i = 0; i < kTries; ++i) {
        if (table->Intern("scope_" + std::to_string(i)) == kOverflowNameId) {
            ++overflowed;
        }
    }

    EXPECT_EQ(table->Count(), kMaxNames);
    EXPECT_EQ(overflowed, kTries - (kMaxNames - 1));
    EXPECT_EQ(table->OverflowCount(), overflowed);

    // Names interned before the table filled up still resolve and still match.
    EXPECT_EQ(table->Intern("scope_0"), first);
    EXPECT_STREQ(table->Resolve(first), "scope_0");
    EXPECT_EQ(table->OverflowCount(), overflowed);
}

TEST(NameTableTest, FullArenaAndOverLongNamesOverflow)
{
    auto table = std::make_unique<NameTable>();
    EXPECT_EQ(table->Intern(std::string(kMaxNameLength + 1, 'x')), kOverflowNameId);
    EXPECT_EQ(table->OverflowCount(), 1u);

    NameId firstLong = kInvalidNameId;
    bool overflowed = false;
    for (std::size_t i = 0; i < 1000 && !overflowed; ++i) {
        const std::string name = std::to_string(i) + std::string(200, 'y');
        const NameId id = table->Intern(name);
        if (i == 0) {
            firstLong = id;
        }
        overflowed = id == kOverflowNameId;
    }
    EXPECT_TRUE(overflowed);
    EXPECT_LT(table->Count(), kMaxNames);
    EXPECT_STREQ(table->Resolve(firstLong), (std::string("0") + std::string(200, 'y')).c_str());
}

TEST(NameTableTest, ThreadsInterningTheSameNamesAgree)
{
    auto table = std::make_unique<NameTable>();
    constexpr int kThreads = 4;
    constexpr int kNames = 200;
    std::array<std::vector<NameId>, kThreads> ids;

    std::vector<std::thread> threads;
    for (int t = 0; t < kThreads; ++t) {
        threads.emplace_back([&, t] {
            for (int i = 0; i < kNames; ++i) {
                ids[t].push_back(table->Intern("shared_" + std::to_string(i)));
            }
        });
    }
    for (std::thread& th : threads) {
        th.join();
    }

    for (int t = 1; t < kThreads; ++t) {
        EXPECT_EQ(ids[t], ids[0]);
    }
    EXPECT_EQ(table->Count(), static_cast<std::uint32_t>(kNames + 1)); // names plus the overflow id
}

TEST(NameTableTest, GlobalTableInternsAndResolves)
{
    const NameId id = InternName("ProfilingEventModelTests.GlobalName");
    EXPECT_NE(id, kInvalidNameId);
    EXPECT_EQ(InternName("ProfilingEventModelTests.GlobalName"), id);
    EXPECT_STREQ(ResolveName(id), "ProfilingEventModelTests.GlobalName");
}

// ---- Budgets ----

TEST(ProfilingBudgetTest, EventSizesAndTotalStayInBudget)
{
    EXPECT_EQ(sizeof(ScopeEvent), 24u);
    EXPECT_LE(kTotalBudgetBytes, 16u * 1024u * 1024u);
    EXPECT_EQ(ScopeEventBuffer::kCapacity, kMaxScopeEventsPerFrame);
}

// ---- Scope tree: total and self ----

TEST_F(ScopeLogTest, ParentTotalCoversChildrenAndSelfIsTheRemainder)
{
    log->BeginFrame(true);
    const ScopeStart outer = log->Begin(0);
    const ScopeStart a = log->Begin(10);
    End(a, kA, 40);
    const ScopeStart b = log->Begin(50);
    End(b, kB, 70);
    End(outer, kC, 100);
    log->EndFrame();

    const auto events = log->Buffer().Events();
    ASSERT_EQ(events.size(), 3u);
    EXPECT_EQ(events[0].depth, 1u);
    EXPECT_EQ(events[1].depth, 1u);
    EXPECT_EQ(events[2].depth, 0u);

    std::array<ScopeTimes, 3> times{};
    const ScopeTreeReport report = ComputeScopeTimes(events, times);
    EXPECT_EQ(report.childrenExceedParent, 0u);
    EXPECT_EQ(times[0].totalTicks, 30u);
    EXPECT_EQ(times[1].totalTicks, 20u);
    EXPECT_EQ(times[2].totalTicks, 100u);
    EXPECT_EQ(times[0].selfTicks, 30u);
    EXPECT_EQ(times[2].selfTicks, 50u);
    EXPECT_GE(times[2].totalTicks, times[0].totalTicks + times[1].totalTicks);
}

TEST_F(ScopeLogTest, RecursionIsNotDoubleCounted)
{
    log->BeginFrame(true);
    const ScopeStart outer = log->Begin(0);
    const ScopeStart mid = log->Begin(10);
    const ScopeStart inner = log->Begin(20);
    End(inner, kA, 30);
    End(mid, kA, 60);
    End(outer, kA, 100);
    log->EndFrame();

    const auto events = log->Buffer().Events();
    ASSERT_EQ(events.size(), 3u);
    std::array<ScopeTimes, 3> times{};
    ComputeScopeTimes(events, times);

    EXPECT_EQ(times[0].selfTicks, 10u);
    EXPECT_EQ(times[1].selfTicks, 40u);
    EXPECT_EQ(times[2].selfTicks, 50u);
    EXPECT_EQ(times[0].selfTicks + times[1].selfTicks + times[2].selfTicks, times[2].totalTicks);
}

TEST_F(ScopeLogTest, SelfSumEqualsTopLevelTotalsAcrossSiblingTrees)
{
    ManualClock clock;
    log->BeginFrame(true);
    for (int tree = 0; tree < 3; ++tree) {
        const ScopeStart top = log->Begin(clock.NowTicks());
        clock.AdvanceTicks(5);
        const ScopeStart child = log->Begin(clock.NowTicks());
        clock.AdvanceTicks(20);
        End(child, kA, clock.NowTicks());
        clock.AdvanceTicks(7);
        End(top, kB, clock.NowTicks());
        clock.AdvanceTicks(3); // gap between top-level scopes
    }
    const ScopeStart leaf = log->Begin(clock.NowTicks());
    clock.AdvanceTicks(11);
    End(leaf, kC, clock.NowTicks());
    log->EndFrame();

    const auto events = log->Buffer().Events();
    std::vector<ScopeTimes> times(events.size());
    const ScopeTreeReport report = ComputeScopeTimes(events, times);
    EXPECT_EQ(report.childrenExceedParent, 0u);

    std::uint64_t selfSum = 0;
    std::uint64_t topSum = 0;
    for (std::size_t i = 0; i < events.size(); ++i) {
        selfSum += times[i].selfTicks;
        if (events[i].depth == 0) {
            topSum += times[i].totalTicks;
        }
    }
    EXPECT_EQ(selfSum, topSum);
    EXPECT_EQ(topSum, 3u * 32u + 11u);
}

TEST(ScopeTreeTest, ChildrenLongerThanParentGiveZeroSelfAndAreReported)
{
    const std::array<ScopeEvent, 2> events{ {
        { 0, 50, kA, 1, ScopeKind::Work, ViewSlot::Shared },
        { 0, 40, kB, 0, ScopeKind::Work, ViewSlot::Shared },
    } };
    std::array<ScopeTimes, 2> times{};
    const ScopeTreeReport report = ComputeScopeTimes(events, times);
    EXPECT_EQ(report.childrenExceedParent, 1u);
    EXPECT_EQ(times[1].selfTicks, 0u);
    EXPECT_EQ(times[1].totalTicks, 40u);
}

TEST(ScopeTreeTest, BackwardsClockReadsAsZeroAndDepthIsClamped)
{
    const std::array<ScopeEvent, 2> events{ {
        { 100, 50, kA, 0, ScopeKind::Work, ViewSlot::Shared },
        { 0, 10, kB, 500, ScopeKind::Work, ViewSlot::Shared },
    } };
    std::array<ScopeTimes, 2> times{};
    const ScopeTreeReport report = ComputeScopeTimes(events, times);
    EXPECT_EQ(times[0].totalTicks, 0u);
    EXPECT_EQ(report.depthClamped, 1u);
    EXPECT_EQ(times[1].totalTicks, 10u);
}

TEST(ScopeTreeTest, ShortOutputSpanIsRespected)
{
    const std::array<ScopeEvent, 3> events{ {
        { 0, 10, kA, 0, ScopeKind::Work, ViewSlot::Shared },
        { 10, 20, kA, 0, ScopeKind::Work, ViewSlot::Shared },
        { 20, 30, kA, 0, ScopeKind::Work, ViewSlot::Shared },
    } };
    std::array<ScopeTimes, 2> times{};
    ComputeScopeTimes(events, times);
    EXPECT_EQ(times[1].totalTicks, 10u);
}

// ---- Budgets of the per-frame log ----

TEST_F(ScopeLogTest, ScopesBeyondTheCapacityAreCountedAndFlagPartial)
{
    log->BeginFrame(true);
    for (std::size_t i = 0; i <= kMaxScopeEventsPerFrame; ++i) { // one more than fits
        const ScopeStart s = log->Begin(i * 10);
        End(s, kA, i * 10 + 5);
    }
    log->EndFrame();

    EXPECT_EQ(log->Buffer().Size(), kMaxScopeEventsPerFrame);
    EXPECT_EQ(log->Buffer().Dropped(), 1u);
    EXPECT_TRUE(log->Buffer().Partial());

    log->BeginFrame(true);
    EXPECT_EQ(log->Buffer().Size(), 0u);
    EXPECT_FALSE(log->Buffer().Partial());
}

TEST_F(ScopeLogTest, KnownDurationReadsBackExactly)
{
    ManualClock clock;
    log->BeginFrame(true);
    const ScopeStart s = log->Begin(clock.NowTicks());
    clock.AdvanceMilliseconds(10);
    End(s, kA, clock.NowTicks());
    log->EndFrame();

    ASSERT_EQ(log->Buffer().Size(), 1u);
    EXPECT_EQ(DurationTicks(log->Buffer().Events()[0]), 10u * ManualClock::kTicksPerMillisecond);
}

// ---- Frame tokens ----

TEST_F(ScopeLogTest, ScopeEndingAfterTheFrameEndedIsDroppedAndCounted)
{
    log->BeginFrame(true);
    const ScopeStart s = log->Begin(0);
    log->EndFrame();
    End(s, kA, 10);

    EXPECT_EQ(log->Buffer().Size(), 0u);
    const GateCounters counters = log->Gate().TakeCounters();
    EXPECT_EQ(counters.lateScopes, 1u);
    EXPECT_EQ(counters.foreignScopes, 0u);
    EXPECT_EQ(log->Gate().TakeCounters().lateScopes, 0u); // counters clear on read
}

TEST_F(ScopeLogTest, ScopeCrossingIntoTheNextFrameIsLateNotMisfiled)
{
    log->BeginFrame(true);
    const ScopeStart s = log->Begin(0);
    log->EndFrame();
    log->BeginFrame(true);
    End(s, kA, 10);

    EXPECT_EQ(log->Buffer().Size(), 0u);
    EXPECT_EQ(log->Gate().TakeCounters().lateScopes, 1u);
}

TEST_F(ScopeLogTest, FrameBegunWithCaptureOffStoresNothingAndCountsNothing)
{
    log->BeginFrame(false);
    const ScopeStart s = log->Begin(0);
    End(s, kA, 10);
    log->Marker(kB, ViewSlot::Shared, 12);
    log->EndFrame();

    EXPECT_EQ(log->Buffer().Size(), 0u);
    const GateCounters counters = log->Gate().TakeCounters();
    EXPECT_EQ(counters.lateScopes, 0u);
    EXPECT_EQ(counters.foreignScopes, 0u);
}

TEST_F(ScopeLogTest, ScopeStartedWithCaptureOnIsKeptInItsOwnFrame)
{
    log->BeginFrame(true);
    const ScopeStart s = log->Begin(0);
    EXPECT_NE(s.token, 0u);
    End(s, kA, 10); // capture may be switched off now: the token still matches
    log->EndFrame();

    ASSERT_EQ(log->Buffer().Size(), 1u);
    EXPECT_EQ(log->Buffer().Events()[0].name, kA);
}

TEST_F(ScopeLogTest, ScopeOnAForeignThreadIsDroppedAndCounted)
{
    log->BeginFrame(true);
    std::thread worker([this] {
        const ScopeStart s = log->Begin(1);
        End(s, kA, 2);
    });
    worker.join();
    log->EndFrame();

    EXPECT_EQ(log->Buffer().Size(), 0u);
    EXPECT_EQ(log->Gate().TakeCounters().foreignScopes, 1u);
}

TEST_F(ScopeLogTest, MarkersAreZeroDurationAndFrameTokensNeverRepeatAsZero)
{
    log->BeginFrame(true);
    log->Marker(kB, ViewSlot::Primary, 77);
    log->EndFrame();

    ASSERT_EQ(log->Buffer().Size(), 1u);
    const ScopeEvent& e = log->Buffer().Events()[0];
    EXPECT_EQ(e.kind, ScopeKind::Marker);
    EXPECT_EQ(e.view, ViewSlot::Primary);
    EXPECT_EQ(DurationTicks(e), 0u);

    FrameGate gate;
    gate.BeginFrame(true);
    const std::uint64_t t1 = gate.ScopeToken();
    gate.EndFrame();
    EXPECT_EQ(gate.ScopeToken(), 0u);
    gate.BeginFrame(true);
    EXPECT_NE(gate.ScopeToken(), 0u);
    EXPECT_NE(gate.ScopeToken(), t1);
}

} // namespace
} // namespace gte::Profiling
