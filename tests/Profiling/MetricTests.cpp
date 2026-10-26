// Tier 1: a metric prints its status, never a bare number for a missing value.
#include "Profiling/Metric.h"

#include <gtest/gtest.h>

#include <string>

namespace gte::Profiling {
namespace {

TEST(MetricTest, PresentValuePrintsWithItsUnit)
{
    EXPECT_EQ(FormatMetric(PresentMetric(12.345, Unit::Milliseconds)), "12.35 ms");
    EXPECT_EQ(FormatMetric(PresentMetric(7.0, Unit::Count)), "7");
    EXPECT_EQ(FormatMetric(PresentMetric(2.0 * 1024.0 * 1024.0, Unit::Bytes)), "2.0 MiB");
    EXPECT_EQ(FormatMetric(PresentMetric(12.5, Unit::Percent)), "12.5 %");
}

TEST(MetricTest, MissingValuesPrintTheStatusWordAndNoNumber)
{
    for (const SampleStatus status :
        { SampleStatus::Absent, SampleStatus::Unsupported, SampleStatus::Error, SampleStatus::Inactive }) {
        Metric metric = MissingMetric(Unit::Milliseconds, status);
        metric.value = 3.0; // must never be printed
        const std::string text = FormatMetric(metric);
        EXPECT_EQ(text, ToString(status));
        EXPECT_EQ(text.find("N/A"), std::string::npos);
        EXPECT_EQ(text.find("0.00"), std::string::npos);
        EXPECT_EQ(text.find('3'), std::string::npos);
    }
}

TEST(MetricTest, PartialAndStaleKeepTheValueAndSayWhy)
{
    Metric partial = PresentMetric(4.0, Unit::Milliseconds);
    partial.status = SampleStatus::Partial;
    partial.reason = Reason::Folded;
    EXPECT_EQ(FormatMetric(partial), "4.00 ms partial (folded)");

    Metric stale = PresentMetric(1.0, Unit::Milliseconds);
    stale.status = SampleStatus::Stale;
    EXPECT_EQ(FormatMetric(stale), "1.00 ms stale");
}

TEST(MetricTest, ReasonIsAppendedToAMissingValue)
{
    EXPECT_EQ(FormatMetric(MissingMetric(Unit::Milliseconds, SampleStatus::Absent, Reason::CaptureOff)),
        "Absent (capture_off)");
}

TEST(MetricTest, GpuViewMetricMapsStatusAndReason)
{
    GpuViewMetric view;
    view.milliseconds = 2.5f;
    view.status = SampleStatus::Present;
    Metric metric = MetricFromGpuView(view);
    EXPECT_EQ(metric.status, SampleStatus::Present);
    EXPECT_DOUBLE_EQ(metric.value, 2.5);
    EXPECT_EQ(metric.reason, Reason::None);

    view.status = SampleStatus::Partial;
    view.foldedPasses = 3;
    EXPECT_EQ(MetricFromGpuView(view).reason, Reason::Folded);

    view = GpuViewMetric{};
    view.status = SampleStatus::Inactive;
    EXPECT_EQ(MetricFromGpuView(view).reason, Reason::NoTarget);

    view.status = SampleStatus::Unsupported;
    EXPECT_EQ(MetricFromGpuView(view).reason, Reason::DeviceUnsupported);
}

TEST(MetricTest, EveryEnumeratorHasAWord)
{
    EXPECT_STREQ(ToString(Unit::Percent), "percent");
    EXPECT_STREQ(ToString(MetricKind::Estimated), "estimated");
    EXPECT_STREQ(ToString(Reason::Dropped), "dropped");
    EXPECT_STREQ(ToString(SampleStatus::Inactive), "Inactive");
}

} // namespace
} // namespace gte::Profiling
