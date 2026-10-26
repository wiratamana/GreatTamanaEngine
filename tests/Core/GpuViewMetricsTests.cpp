// Tier 1: per-view GPU numbers from pass records.
#include "Core/GpuViewMetrics.h"

#include <gtest/gtest.h>

#include <vector>

namespace gte {
namespace {

using Profiling::SampleStatus;
using Profiling::ViewSlot;

constexpr std::size_t kPrimary = static_cast<std::size_t>(ViewSlot::Primary);
constexpr std::size_t kSecondary = static_cast<std::size_t>(ViewSlot::Secondary);

GpuPassRecord Record(rg::ViewScope view, float barrierMs, float workMs, GpuReadState state = GpuReadState::Present)
{
    GpuPassRecord record;
    record.view = static_cast<std::uint8_t>(view);
    record.barrierMs = barrierMs;
    record.workMs = workMs;
    record.state = state;
    return record;
}

GpuFrameReport PresentReport(double spanMs)
{
    GpuFrameReport report;
    report.state = GpuReadState::Present;
    report.spanMs = spanMs;
    report.spanValid = true;
    return report;
}

TEST(GpuViewMetricsTest, StatesMapOneToOne)
{
    EXPECT_EQ(ToSampleStatus(GpuReadState::Present), SampleStatus::Present);
    EXPECT_EQ(ToSampleStatus(GpuReadState::Partial), SampleStatus::Partial);
    EXPECT_EQ(ToSampleStatus(GpuReadState::Absent), SampleStatus::Absent);
    EXPECT_EQ(ToSampleStatus(GpuReadState::Error), SampleStatus::Error);
    EXPECT_EQ(ToSampleStatus(GpuReadState::Unsupported), SampleStatus::Unsupported);
    EXPECT_EQ(ToSampleStatus(GpuReadState::Inactive), SampleStatus::Inactive);
}

TEST(GpuViewMetricsTest, ScopesMapToSlots)
{
    EXPECT_EQ(ToViewSlot(rg::ViewScope::Shared), ViewSlot::Shared);
    EXPECT_EQ(ToViewSlot(rg::ViewScope::Primary), ViewSlot::Primary);
    EXPECT_EQ(ToViewSlot(rg::ViewScope::Secondary), ViewSlot::Secondary);
}

TEST(GpuViewMetricsTest, TwoViewsReadTwoDifferentNumbers)
{
    const std::vector<GpuPassRecord> records = {
        Record(rg::ViewScope::Primary, 0.1f, 1.4f),
        Record(rg::ViewScope::Primary, 0.0f, 0.5f),
        Record(rg::ViewScope::Secondary, 0.2f, 3.0f),
    };
    const Profiling::GpuFrameMetrics metrics = BuildGpuFrameMetrics(records, PresentReport(6.0), GpuMetricsInput{});

    EXPECT_EQ(metrics.views[kPrimary].status, SampleStatus::Present);
    EXPECT_NEAR(metrics.views[kPrimary].milliseconds, 2.0f, 1e-4f);
    EXPECT_EQ(metrics.views[kSecondary].status, SampleStatus::Present);
    EXPECT_NEAR(metrics.views[kSecondary].milliseconds, 3.2f, 1e-4f);
    EXPECT_EQ(metrics.span.status, SampleStatus::Present);
    EXPECT_NEAR(metrics.span.milliseconds, 6.0f, 1e-4f);
    EXPECT_EQ(metrics.recordedPasses, 3u);
}

TEST(GpuViewMetricsTest, ClosedViewIsInactiveNotZero)
{
    const std::vector<GpuPassRecord> records = { Record(rg::ViewScope::Primary, 0.0f, 1.0f) };
    GpuMetricsInput input;
    input.viewActive[kSecondary] = false;
    const Profiling::GpuFrameMetrics metrics = BuildGpuFrameMetrics(records, PresentReport(1.0), input);

    EXPECT_EQ(metrics.views[kPrimary].status, SampleStatus::Present);
    EXPECT_EQ(metrics.views[kSecondary].status, SampleStatus::Inactive);
    EXPECT_EQ(metrics.views[kSecondary].milliseconds, 0.0f);
}

TEST(GpuViewMetricsTest, NoActiveViewMakesTheSpanInactive)
{
    GpuMetricsInput input;
    input.viewActive[kPrimary] = false;
    input.viewActive[kSecondary] = false;
    const Profiling::GpuFrameMetrics metrics = BuildGpuFrameMetrics({}, GpuFrameReport{}, input);

    EXPECT_EQ(metrics.span.status, SampleStatus::Inactive);
}

TEST(GpuViewMetricsTest, CaptureOffReadsAbsentEverywhere)
{
    const std::vector<GpuPassRecord> records = { Record(rg::ViewScope::Primary, 0.0f, 1.0f) };
    GpuMetricsInput input;
    input.captureEnabled = false;
    const Profiling::GpuFrameMetrics metrics = BuildGpuFrameMetrics(records, PresentReport(1.0), input);

    EXPECT_EQ(metrics.views[kPrimary].status, SampleStatus::Absent);
    EXPECT_EQ(metrics.views[kSecondary].status, SampleStatus::Absent);
    EXPECT_EQ(metrics.span.status, SampleStatus::Absent);
}

TEST(GpuViewMetricsTest, UnmeasuredRecordMakesTheViewPartial)
{
    const std::vector<GpuPassRecord> records = {
        Record(rg::ViewScope::Primary, 0.0f, 1.0f),
        Record(rg::ViewScope::Primary, 0.0f, 0.0f, GpuReadState::Absent),
    };
    const Profiling::GpuFrameMetrics metrics = BuildGpuFrameMetrics(records, PresentReport(1.0), GpuMetricsInput{});

    EXPECT_EQ(metrics.views[kPrimary].status, SampleStatus::Partial);
    EXPECT_NEAR(metrics.views[kPrimary].milliseconds, 1.0f, 1e-4f);
}

TEST(GpuViewMetricsTest, FoldedPassesMakeTheViewPartial)
{
    GpuPassRecord bucket = Record(rg::ViewScope::Secondary, 0.0f, 2.0f);
    bucket.foldedPasses = 9;
    const std::vector<GpuPassRecord> records = { bucket };
    const Profiling::GpuFrameMetrics metrics = BuildGpuFrameMetrics(records, PresentReport(2.0), GpuMetricsInput{});

    EXPECT_EQ(metrics.views[kSecondary].status, SampleStatus::Partial);
    EXPECT_EQ(metrics.views[kSecondary].foldedPasses, 9u);
}

TEST(GpuViewMetricsTest, ErrorRecordsOnlyReadError)
{
    const std::vector<GpuPassRecord> records = { Record(rg::ViewScope::Primary, 0.0f, 0.0f, GpuReadState::Error) };
    const Profiling::GpuFrameMetrics metrics = BuildGpuFrameMetrics(records, GpuFrameReport{}, GpuMetricsInput{});

    EXPECT_EQ(metrics.views[kPrimary].status, SampleStatus::Error);
    EXPECT_EQ(metrics.views[kPrimary].milliseconds, 0.0f);
}

TEST(GpuViewMetricsTest, UnsupportedDeviceWithoutRecordsReadsUnsupported)
{
    GpuFrameReport report;
    report.state = GpuReadState::Unsupported;
    const Profiling::GpuFrameMetrics metrics = BuildGpuFrameMetrics({}, report, GpuMetricsInput{});

    EXPECT_EQ(metrics.views[kPrimary].status, SampleStatus::Unsupported);
    EXPECT_EQ(metrics.span.status, SampleStatus::Unsupported);
}

TEST(GpuViewMetricsTest, IndirectDrawsAreCountedSeparatelyPerView)
{
    GpuPassRecord a = Record(rg::ViewScope::Primary, 0.0f, 1.0f);
    a.indirectDraws = 4;
    GpuPassRecord b = Record(rg::ViewScope::Secondary, 0.0f, 1.0f);
    b.indirectDraws = 7;
    const std::vector<GpuPassRecord> records = { a, b };
    const Profiling::GpuFrameMetrics metrics = BuildGpuFrameMetrics(records, PresentReport(2.0), GpuMetricsInput{});

    EXPECT_EQ(metrics.indirectDraws[kPrimary], 4u);
    EXPECT_EQ(metrics.indirectDraws[kSecondary], 7u);
}

TEST(GpuViewMetricsTest, CoverageCountersAreCopied)
{
    GpuMetricsInput input;
    input.declaredPasses = 12;
    input.executedPasses = 10;
    input.disabledByToggle = 3;
    input.droppedFrames = 2;
    const Profiling::GpuFrameMetrics metrics = BuildGpuFrameMetrics({}, GpuFrameReport{}, input);

    EXPECT_EQ(metrics.declaredPasses, 12u);
    EXPECT_EQ(metrics.executedPasses, 10u);
    EXPECT_EQ(metrics.disabledByToggle, 3u);
    EXPECT_EQ(metrics.droppedFrames, 2u);
}

} // namespace
} // namespace gte
