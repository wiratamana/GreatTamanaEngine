// Tier 1: GPU stamp planning and the reverse resolve (statuses, identity, overflow, ordering).
#include "Renderer/GpuStampPlan.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <random>
#include <vector>

namespace gte {
namespace {

// One tick is one microsecond: ticks = ms * 1000.
constexpr float kPeriodNs = 1000.0f;
constexpr double kTicksPerMs = 1000.0;

GpuTimestampCapability FakeCapability(std::uint32_t validBits = 64)
{
    return InterpretTimestampCapability(true, kPeriodNs, validBits);
}

struct PassTimes {
    double barrierMs = 0.0;
    double workMs = 0.0;
    bool skipped = false;
};

// Raw queries of one frame, built from the durations the chained stamps would measure.
struct Frame {
    GpuStampPlan plan;
    std::vector<GpuStampAssignment> assignments;
    std::vector<std::uint64_t> values;
    std::vector<std::uint8_t> available;
    std::vector<GpuPassRecord> records;
    double totalMs = 0.0;
};

Frame BuildFrame(const std::vector<PassTimes>& times, GpuStampMode mode, std::uint32_t budget, double tailMs,
    std::uint64_t startTicks = 1'000'000)
{
    Frame frame;
    std::vector<GpuStampPassInput> inputs;
    for (const PassTimes& t : times) {
        inputs.push_back({ t.skipped });
    }
    frame.assignments.resize(inputs.size());
    frame.plan = PlanGpuStamps(inputs, frame.assignments, mode, budget);
    frame.values.assign(frame.plan.queryCount, 0);
    frame.available.assign(frame.plan.queryCount, 1);
    frame.records.resize(frame.plan.recordCount);

    std::uint64_t now = startTicks;
    const std::uint64_t first = now;
    const auto advance = [&](double ms) { now += static_cast<std::uint64_t>(std::llround(ms * kTicksPerMs)); };
    for (std::size_t i = 0; i < times.size(); ++i) {
        const GpuStampAssignment& slot = frame.assignments[i];
        const bool stamped = slot.leader && slot.startQuery >= 0;
        if (stamped) {
            frame.values[static_cast<std::size_t>(slot.startQuery)] = now;
        }
        advance(times[i].barrierMs);
        if (stamped && slot.workQuery >= 0) {
            frame.values[static_cast<std::size_t>(slot.workQuery)] = now;
        }
        advance(times[i].workMs);
    }
    frame.values[static_cast<std::size_t>(frame.plan.endQuery)] = now;
    advance(tailMs);
    frame.values[static_cast<std::size_t>(frame.plan.finalQuery)] = now;
    frame.totalMs = static_cast<double>(now - first) / kTicksPerMs;
    return frame;
}

GpuFrameReport Resolve(Frame& frame, bool capture = true, GpuReadCall call = GpuReadCall::Ok,
    bool supported = true)
{
    GpuReadInput input;
    input.values = frame.values;
    input.available = frame.available;
    input.capability = supported ? FakeCapability() : GpuTimestampCapability{};
    input.captureEnabled = capture;
    input.call = call;
    return ResolveGpuStamps(frame.plan, frame.assignments, input, frame.records);
}

std::vector<PassTimes> RandomPasses(std::mt19937& rng, std::size_t count, bool withSkips)
{
    std::uniform_real_distribution<double> ms(0.0, 3.0);
    std::bernoulli_distribution skip(0.2);
    std::vector<PassTimes> passes(count);
    for (PassTimes& p : passes) {
        p.skipped = withSkips && skip(rng);
        if (!p.skipped) {
            p.barrierMs = ms(rng);
            p.workMs = ms(rng);
        }
    }
    return passes;
}

TEST(GpuStampPlanTest, LayoutPutsPairsFirstThenEndAndFinalStamps)
{
    const std::vector<GpuStampPassInput> inputs(3);
    std::vector<GpuStampAssignment> slots(3);
    const GpuStampPlan plan = PlanGpuStamps(inputs, slots, GpuStampMode::Fine, 4);

    EXPECT_EQ(plan.queryCount, 12u);
    EXPECT_EQ(plan.endQuery, 10);
    EXPECT_EQ(plan.finalQuery, 11);
    EXPECT_EQ(plan.recordCount, 3u);
    EXPECT_EQ(plan.foldedPasses, 0u);
    EXPECT_FALSE(plan.hasBucket);
    EXPECT_EQ(slots[0].startQuery, 0);
    EXPECT_EQ(slots[0].workQuery, 1);
    EXPECT_EQ(slots[2].startQuery, 4);
    EXPECT_EQ(slots[2].workQuery, 5);
}

TEST(GpuStampPlanTest, SkippedPassesKeepARecordAndUseNoQueries)
{
    std::vector<GpuStampPassInput> inputs(3);
    inputs[1].skipped = true;
    std::vector<GpuStampAssignment> slots(3);
    const GpuStampPlan plan = PlanGpuStamps(inputs, slots, GpuStampMode::Fine, 8);

    EXPECT_EQ(plan.recordCount, 3u);
    EXPECT_EQ(plan.stampedRecords, 2u);
    EXPECT_EQ(slots[1].startQuery, -1);
    EXPECT_EQ(slots[1].flags, kGpuRecordSkipped);
    EXPECT_EQ(slots[2].startQuery, 2);
}

TEST(GpuStampPlanTest, MoreInputThanSlotsIsFlagged)
{
    const std::vector<GpuStampPassInput> inputs(5);
    std::vector<GpuStampAssignment> slots(3);
    const GpuStampPlan plan = PlanGpuStamps(inputs, slots, GpuStampMode::Fine, 8);

    EXPECT_TRUE(plan.inputTruncated);
    EXPECT_EQ(plan.passCount, 3u);
}

TEST(GpuStampPlanTest, RecordBudgetCoversTheGraphConstants)
{
    EXPECT_EQ(GpuQueryCount(kGpuRecordBudgetSynchronous), 2 * 513u + 2);
    EXPECT_EQ(sizeof(GpuPassRecord), 48u);
}

// The chained identity holds for any order, with skipped passes.
TEST(GpuStampPlanTest, ChainIdentityHoldsForShuffledAndSkippedPasses)
{
    std::mt19937 rng(1234);
    for (int round = 0; round < 40; ++round) {
        const std::size_t count = static_cast<std::size_t>(rng() % 60);
        Frame frame = BuildFrame(RandomPasses(rng, count, true), GpuStampMode::Fine, 64, 0.7);
        const GpuFrameReport report = Resolve(frame);

        EXPECT_EQ(report.state, GpuReadState::Present) << "round " << round;
        EXPECT_TRUE(report.spanValid);
        EXPECT_NEAR(report.summedMs, report.spanMs, 0.05) << "round " << round;
        EXPECT_NEAR(report.spanMs, frame.totalMs, 0.05);
        EXPECT_EQ(report.errorRecords, 0u);
    }
}

TEST(GpuStampPlanTest, SkippedRecordHasNoTimeButIsPresent)
{
    Frame frame = BuildFrame({ { 0.1, 1.0, false }, { 0.0, 0.0, true }, { 0.2, 2.0, false } }, GpuStampMode::Fine, 8, 0.5);
    const GpuFrameReport report = Resolve(frame);

    ASSERT_EQ(frame.records.size(), 3u);
    EXPECT_EQ(frame.records[1].flags & kGpuRecordSkipped, kGpuRecordSkipped);
    EXPECT_EQ(frame.records[1].state, GpuReadState::Present);
    EXPECT_FLOAT_EQ(frame.records[1].workMs, 0.0f);
    EXPECT_NEAR(frame.records[0].workMs, 1.0, 0.01);
    EXPECT_NEAR(frame.records[2].barrierMs, 0.2, 0.01);
    EXPECT_NEAR(report.summedMs, report.spanMs, 0.05);
}

TEST(GpuStampPlanTest, EmptyGraphIsJustTheTail)
{
    Frame frame = BuildFrame({}, GpuStampMode::Fine, 8, 0.4);
    const GpuFrameReport report = Resolve(frame);

    EXPECT_EQ(report.state, GpuReadState::Present);
    EXPECT_EQ(report.recordCount, 0u);
    EXPECT_NEAR(report.tailMs, 0.4, 0.01);
    EXPECT_NEAR(report.spanMs, 0.4, 0.01);
}

// 513 passes give 512 records plus one bucket.
TEST(GpuStampPlanTest, OverBudgetPassesFoldIntoOneBucket)
{
    std::vector<GpuStampPassInput> inputs(513);
    std::vector<GpuStampAssignment> slots(inputs.size());
    const GpuStampPlan plan = PlanGpuStamps(inputs, slots, GpuStampMode::Fine, kGpuRecordBudgetSynchronous);

    EXPECT_EQ(plan.recordCount, 513u);
    EXPECT_EQ(plan.stampedRecords, 513u);
    EXPECT_TRUE(plan.hasBucket);
    EXPECT_EQ(plan.foldedPasses, 1u);
    EXPECT_EQ(plan.queryCount, 1028u);
    EXPECT_EQ(slots[512].startQuery, 1024);
    EXPECT_EQ(slots[512].flags, kGpuRecordOverflowBucket);

    std::vector<GpuStampPassInput> more(600);
    std::vector<GpuStampAssignment> moreSlots(more.size());
    const GpuStampPlan big = PlanGpuStamps(more, moreSlots, GpuStampMode::Fine, kGpuRecordBudgetSynchronous);
    EXPECT_EQ(big.recordCount, 513u);
    EXPECT_EQ(big.foldedPasses, 88u);
}

TEST(GpuStampPlanTest, BucketKeepsTheIdentityAndMakesTheFrameLookPartial)
{
    std::mt19937 rng(99);
    Frame frame = BuildFrame(RandomPasses(rng, 10, false), GpuStampMode::Fine, 4, 0.3);
    const GpuFrameReport report = Resolve(frame);

    EXPECT_EQ(frame.plan.recordCount, 5u);
    EXPECT_EQ(report.foldedPasses, 6u);
    EXPECT_EQ(report.state, GpuReadState::Partial);
    EXPECT_NEAR(report.summedMs, report.spanMs, 0.05);
    EXPECT_EQ(frame.records[4].flags & kGpuRecordOverflowBucket, kGpuRecordOverflowBucket);
    EXPECT_EQ(frame.records[4].foldedPasses, 6u);
    EXPECT_EQ(frame.records[0].foldedPasses, 0u);
}

TEST(GpuStampPlanTest, OnlyCoarseRecordsMarkTheBarrierAsUnmeasured)
{
    Frame coarse = BuildFrame({ { 0.0, 1.0, false }, { 0.0, 2.0, false } }, GpuStampMode::Coarse, 8, 0.5);
    Resolve(coarse);
    EXPECT_EQ(coarse.records[0].flags & kGpuRecordBarrierUnmeasured, kGpuRecordBarrierUnmeasured);
    EXPECT_EQ(coarse.records[1].flags & kGpuRecordBarrierUnmeasured, kGpuRecordBarrierUnmeasured);

    Frame fine = BuildFrame({ { 0.5, 1.0, false } }, GpuStampMode::Fine, 8, 0.5);
    Resolve(fine);
    EXPECT_EQ(fine.records[0].flags & kGpuRecordBarrierUnmeasured, 0);
}

TEST(GpuStampPlanTest, OutOfOrderStampsGiveErrorAndAreNotSummed)
{
    Frame frame = BuildFrame({ { 0.5, 1.0, false }, { 0.5, 2.0, false }, { 0.5, 3.0, false } }, GpuStampMode::Fine,
        8, 0.5);
    // The work stamp of pass 1 lands before its start stamp.
    frame.values[static_cast<std::size_t>(frame.assignments[1].workQuery)] =
        frame.values[static_cast<std::size_t>(frame.assignments[1].startQuery)] - 100;
    const GpuFrameReport report = Resolve(frame);

    EXPECT_EQ(frame.records[1].state, GpuReadState::Error);
    EXPECT_EQ(frame.records[1].flags & kGpuRecordStampOrder, kGpuRecordStampOrder);
    EXPECT_FLOAT_EQ(frame.records[1].barrierMs, 0.0f);
    EXPECT_FLOAT_EQ(frame.records[1].workMs, 0.0f);
    EXPECT_EQ(report.errorRecords, 1u);
    EXPECT_EQ(report.state, GpuReadState::Partial);
    // Everything except pass 1 (0.5 + 2.0) is summed.
    EXPECT_NEAR(report.summedMs, report.spanMs - 2.5, 0.05);
}

TEST(GpuStampPlanTest, ImplausiblyLargeDeltaIsAnError)
{
    Frame frame = BuildFrame({ { 0.5, 1.0, false } }, GpuStampMode::Fine, 8, 0.5);
    frame.values[static_cast<std::size_t>(frame.plan.endQuery)] += 6'000ULL * 1000ULL; // 6000 ms.
    frame.values[static_cast<std::size_t>(frame.plan.finalQuery)] += 6'000ULL * 1000ULL;
    const GpuFrameReport report = Resolve(frame);

    EXPECT_EQ(frame.records[0].state, GpuReadState::Error);
    EXPECT_EQ(report.errorRecords, 1u);
    EXPECT_TRUE(report.tailValid);
}

// Statuses: a missing stamp reads Absent or Partial, never zero.
TEST(GpuStampPlanTest, MissingStampsReadAbsentNeverZero)
{
    Frame frame = BuildFrame({ { 0.2, 1.0, false }, { 0.3, 2.0, false }, { 0.4, 3.0, false } }, GpuStampMode::Fine,
        8, 0.5);
    frame.available[static_cast<std::size_t>(frame.assignments[1].workQuery)] = 0;
    const GpuFrameReport report = Resolve(frame);

    EXPECT_EQ(frame.records[0].state, GpuReadState::Present);
    // Pass 1 needs its own work stamp for both numbers; pass 0 still ends at pass 1's start stamp.
    EXPECT_EQ(frame.records[1].state, GpuReadState::Absent);
    EXPECT_EQ(frame.records[2].state, GpuReadState::Present);
    EXPECT_EQ(report.absentRecords, 1u);
    EXPECT_EQ(report.state, GpuReadState::Partial);
    EXPECT_TRUE(report.tailValid);
}

TEST(GpuStampPlanTest, OneMissingStampMakesARecordPartial)
{
    Frame frame = BuildFrame({ { 0.2, 1.0, false }, { 0.3, 2.0, false } }, GpuStampMode::Fine, 8, 0.5);
    frame.available[static_cast<std::size_t>(frame.assignments[1].startQuery)] = 0;
    const GpuFrameReport report = Resolve(frame);

    // Pass 0 lost its work end, pass 1 lost its barrier start; each keeps the half that is known.
    EXPECT_EQ(frame.records[0].state, GpuReadState::Partial);
    EXPECT_EQ(frame.records[1].state, GpuReadState::Partial);
    EXPECT_EQ(report.partialRecords, 2u);
    EXPECT_EQ(report.state, GpuReadState::Partial);
}

TEST(GpuStampPlanTest, NothingAvailableIsAbsent)
{
    Frame frame = BuildFrame({ { 0.2, 1.0, false }, { 0.3, 2.0, false } }, GpuStampMode::Fine, 8, 0.5);
    std::fill(frame.available.begin(), frame.available.end(), std::uint8_t{ 0 });
    const GpuFrameReport report = Resolve(frame);

    EXPECT_EQ(report.state, GpuReadState::Absent);
    EXPECT_EQ(frame.records[0].state, GpuReadState::Absent);
    EXPECT_FALSE(report.spanValid);
}

TEST(GpuStampPlanTest, FailedCallCaptureOffAndUnsupportedDeviceAreNamed)
{
    Frame frame = BuildFrame({ { 0.2, 1.0, false } }, GpuStampMode::Fine, 8, 0.5);

    EXPECT_EQ(Resolve(frame, true, GpuReadCall::Failed).state, GpuReadState::Error);
    EXPECT_EQ(frame.records[0].state, GpuReadState::Error);

    EXPECT_EQ(Resolve(frame, false).state, GpuReadState::Absent);
    EXPECT_EQ(frame.records[0].state, GpuReadState::Absent);
    EXPECT_FLOAT_EQ(frame.records[0].workMs, 0.0f);

    EXPECT_EQ(Resolve(frame, true, GpuReadCall::Ok, false).state, GpuReadState::Unsupported);
    EXPECT_EQ(frame.records[0].state, GpuReadState::Unsupported);

    EXPECT_EQ(Resolve(frame, true, GpuReadCall::NotReady).state, GpuReadState::Present);
}

TEST(GpuStampPlanTest, ShortRecordListIsAnError)
{
    Frame frame = BuildFrame({ { 0.2, 1.0, false }, { 0.2, 1.0, false } }, GpuStampMode::Fine, 8, 0.5);
    frame.records.resize(1);

    EXPECT_EQ(Resolve(frame).state, GpuReadState::Error);
}

TEST(GpuStampPlanTest, CoarseModeUsesOneStampPerPassAndKeepsTheIdentity)
{
    std::mt19937 rng(7);
    std::vector<PassTimes> passes = RandomPasses(rng, 12, true);
    for (PassTimes& p : passes) {
        p.workMs += p.barrierMs;
        p.barrierMs = 0.0;
    }
    Frame frame = BuildFrame(passes, GpuStampMode::Coarse, 16, 0.6);
    const GpuFrameReport report = Resolve(frame);

    EXPECT_FALSE(report.barrierMeasured);
    EXPECT_EQ(frame.plan.queryCount, GpuQueryCount(16));
    for (const GpuStampAssignment& slot : frame.assignments) {
        EXPECT_EQ(slot.workQuery, -1);
    }
    EXPECT_EQ(report.state, GpuReadState::Present);
    EXPECT_NEAR(report.summedMs, report.spanMs, 0.05);
}

TEST(GpuStampPlanTest, CounterWrapAcrossTheValidBitsStaysCorrect)
{
    // 36 valid bits: the counter wraps between the first stamp and the rest.
    const std::uint64_t mask = (std::uint64_t{ 1 } << 36) - 1;
    Frame frame = BuildFrame({ { 0.5, 1.0, false }, { 0.5, 2.0, false } }, GpuStampMode::Fine, 4, 0.5, mask - 1500);
    for (std::uint64_t& v : frame.values) {
        v &= mask;
    }
    GpuReadInput input;
    input.values = frame.values;
    input.available = frame.available;
    input.capability = FakeCapability(36);
    const GpuFrameReport report = ResolveGpuStamps(frame.plan, frame.assignments, input, frame.records);

    EXPECT_EQ(report.state, GpuReadState::Present);
    EXPECT_NEAR(report.spanMs, 4.5, 0.05);
    EXPECT_NEAR(report.summedMs, report.spanMs, 0.05);
}

} // namespace
} // namespace gte
