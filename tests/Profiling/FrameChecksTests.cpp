// Tier 1: frame totals, wait classification and the self-check list. No clock, no GPU.
#include "Profiling/FrameChecks.h"
#include "Profiling/NameTable.h"

#include <gtest/gtest.h>

#include <memory>

namespace gte::Profiling {
namespace {

constexpr std::uint64_t Ms(std::uint64_t value)
{
    return value * 1'000'000ull;
}

std::unique_ptr<FrameRecord> MakeRecord(std::uint64_t frameMs)
{
    auto record = std::make_unique<FrameRecord>();
    record->summary.startTicks = 0;
    record->summary.endTicks = Ms(frameMs);
    return record;
}

void Add(FrameRecord& record, const char* name, std::uint64_t startMs, std::uint64_t endMs, std::uint16_t depth,
    ScopeKind kind = ScopeKind::Work)
{
    record.scopes.Append(ScopeEvent{ Ms(startMs), Ms(endMs), InternName(name), depth, kind, ViewSlot::Shared });
}

const FrameCheck& CheckOf(const ProfilerChecks& checks, CheckId id)
{
    return checks.items[static_cast<std::size_t>(id)];
}

TEST(FrameChecksTotalsTest, SplitsTopLevelBlockedAndUntrackedTime)
{
    auto record = MakeRecord(20);
    Add(*record, "GpuWait(Offscreen:test)", 1, 4, 1, ScopeKind::Blocked);
    Add(*record, "Wait.Workers", 5, 6, 1, ScopeKind::Blocked);
    Add(*record, "ChecksOtherWait", 7, 7, 1, ScopeKind::Blocked);
    Add(*record, "ChecksParent", 0, 10, 0);

    const FrameTotals totals = ComputeFrameTotals(*record);
    EXPECT_EQ(totals.frameTicks, Ms(20));
    EXPECT_EQ(totals.topLevelTicks, Ms(10));
    EXPECT_EQ(totals.untrackedTicks, Ms(10));
    EXPECT_EQ(totals.blockedGpuTicks, Ms(3));
    EXPECT_EQ(totals.blockedWorkerTicks, Ms(1));
    EXPECT_EQ(totals.blockedOtherTicks, 0u);
    EXPECT_EQ(totals.scopeCount, 4u);
}

TEST(FrameChecksTotalsTest, GpuWaitNamesAreClassifiedByPrefix)
{
    auto record = MakeRecord(10);
    Add(*record, "GpuWait(Offscreen:fence)", 1, 3, 1, ScopeKind::Blocked);
    Add(*record, "GpuWait(AcquireImage)", 4, 5, 1, ScopeKind::Blocked);
    Add(*record, "ChecksNotAGpuWait", 6, 8, 1, ScopeKind::Blocked);
    Add(*record, "ChecksFrameBody", 0, 9, 0);

    const FrameTotals totals = ComputeFrameTotals(*record);
    EXPECT_EQ(totals.blockedTicks, Ms(5));
    EXPECT_EQ(totals.blockedGpuTicks, Ms(3));
    EXPECT_EQ(totals.blockedOtherTicks, Ms(2));
}

TEST(FrameChecksTotalsTest, MarkersAddNoTime)
{
    auto record = MakeRecord(10);
    record->scopes.Append(ScopeEvent{ Ms(2), Ms(2), InternName("ChecksMarker"), 0, ScopeKind::Marker, ViewSlot::Shared });

    const FrameTotals totals = ComputeFrameTotals(*record);
    EXPECT_EQ(totals.topLevelTicks, 0u);
    EXPECT_EQ(totals.untrackedTicks, Ms(10));
}

TEST(FrameChecksTest, ACleanFrameHasNoIssue)
{
    auto record = MakeRecord(20);
    Add(*record, "ChecksCleanBody", 0, 20, 0);

    const ProfilerChecks checks = EvaluateChecks(*record, ComputeFrameTotals(*record), true);
    EXPECT_EQ(checks.IssueCount(), 0u);
}

TEST(FrameChecksTest, CaptureOffWarns)
{
    auto record = MakeRecord(0);
    const ProfilerChecks checks = EvaluateChecks(*record, ComputeFrameTotals(*record), false);
    EXPECT_EQ(CheckOf(checks, CheckId::CaptureOn).status, CheckStatus::Warn);
}

TEST(FrameChecksTest, DroppedScopesFailAndClear)
{
    auto record = MakeRecord(10);
    for (std::size_t i = 0; i < ScopeEventBuffer::kCapacity + 3; ++i) {
        Add(*record, "ChecksFlood", 0, 0, 0);
    }
    ProfilerChecks checks = EvaluateChecks(*record, ComputeFrameTotals(*record), true);
    EXPECT_EQ(CheckOf(checks, CheckId::ScopesDropped).status, CheckStatus::Fail);
    EXPECT_EQ(CheckOf(checks, CheckId::ScopesDropped).value, 3u);

    record->scopes.Clear();
    checks = EvaluateChecks(*record, ComputeFrameTotals(*record), true);
    EXPECT_EQ(CheckOf(checks, CheckId::ScopesDropped).status, CheckStatus::Ok);
}

TEST(FrameChecksTest, SummaryCountersRaiseTheirOwnChecks)
{
    auto record = MakeRecord(10);
    record->summary.lateScopes = 2;
    record->summary.foreignScopes = 3;
    record->summary.lostWorkerEvents = 4;

    const ProfilerChecks checks = EvaluateChecks(*record, ComputeFrameTotals(*record), true);
    EXPECT_EQ(CheckOf(checks, CheckId::LateScopes).status, CheckStatus::Warn);
    EXPECT_EQ(CheckOf(checks, CheckId::LateScopes).value, 2u);
    EXPECT_EQ(CheckOf(checks, CheckId::ForeignScopes).value, 3u);
    EXPECT_EQ(CheckOf(checks, CheckId::WorkerEvents).value, 4u);
}

TEST(FrameChecksTest, ReconciliationWarnsOnUntrackedTimeAndFailsOnOverrun)
{
    auto warn = MakeRecord(20);
    Add(*warn, "ChecksHalf", 0, 10, 0);
    ProfilerChecks checks = EvaluateChecks(*warn, ComputeFrameTotals(*warn), true);
    EXPECT_EQ(CheckOf(checks, CheckId::Reconciliation).status, CheckStatus::Warn);
    EXPECT_EQ(CheckOf(checks, CheckId::Reconciliation).value, 500u);

    auto fail = MakeRecord(20);
    Add(*fail, "ChecksTooLong", 0, 25, 0);
    checks = EvaluateChecks(*fail, ComputeFrameTotals(*fail), true);
    EXPECT_EQ(CheckOf(checks, CheckId::Reconciliation).status, CheckStatus::Fail);

    auto ok = MakeRecord(100);
    Add(*ok, "ChecksNearlyAll", 0, 96, 0);
    checks = EvaluateChecks(*ok, ComputeFrameTotals(*ok), true);
    EXPECT_EQ(CheckOf(checks, CheckId::Reconciliation).status, CheckStatus::Ok);
}

TEST(FrameChecksTest, ChildrenLongerThanTheirParentWarn)
{
    auto record = MakeRecord(10);
    Add(*record, "ChecksChild", 0, 5, 1);
    Add(*record, "ChecksShortParent", 0, 2, 0);

    const ProfilerChecks checks = EvaluateChecks(*record, ComputeFrameTotals(*record), true);
    EXPECT_EQ(CheckOf(checks, CheckId::ScopeNesting).status, CheckStatus::Warn);
}

TEST(FrameChecksTest, ItemsAreInCheckIdOrder)
{
    auto record = MakeRecord(0);
    const ProfilerChecks checks = EvaluateChecks(*record, ComputeFrameTotals(*record), true);
    for (std::size_t i = 0; i < kCheckCount; ++i) {
        EXPECT_EQ(static_cast<std::size_t>(checks.items[i].id), i);
        EXPECT_NE(checks.items[i].text[0], '\0');
    }
}

} // namespace
} // namespace gte::Profiling
