// Graph view model: base lines, gaps, axis, tracked rows and history alignment.
#include "Editor/ProfilerGraphModel.h"
#include "Profiling/NameTable.h"

#include <gtest/gtest.h>

#include <cmath>
#include <memory>
#include <vector>

namespace gte {
namespace {

using Profiling::FrameGraphPoint;
using Profiling::SampleStatus;
using Profiling::ScopeLane;

constexpr std::uint64_t kMs = 1'000'000;

FrameGraphPoint MakePoint(std::uint64_t index, double frameMs, double blockedMs, float gpuMs, SampleStatus gpuStatus)
{
    Profiling::FrameSummary summary;
    summary.frameIndex = index;
    summary.cpuMilliseconds = frameMs;
    summary.blockedTicks = static_cast<std::uint64_t>(blockedMs * 1.0e6);
    summary.gpu.span.milliseconds = gpuMs;
    summary.gpu.span.status = gpuStatus;

    FrameGraphPoint point;
    point.frameIndex = index;
    point.cpuMilliseconds = summary.cpuMilliseconds;
    point.cpuBusyMilliseconds = Profiling::CpuBusyMilliseconds(summary);
    point.gpuSpan = summary.gpu.span;
    return point;
}

std::vector<FrameGraphPoint> MakePoints(std::size_t count, double frameMs = 10.0)
{
    std::vector<FrameGraphPoint> points;
    for (std::size_t i = 0; i < count; ++i) {
        points.push_back(MakePoint(i, frameMs, 2.0, 4.0f, SampleStatus::Present));
    }
    return points;
}

std::unique_ptr<Profiling::ScopeHistory> MakeHistory(const char* name, std::size_t frames)
{
    auto history = std::make_unique<Profiling::ScopeHistory>();
    Profiling::ScopeAggregate agg;
    agg.name = Profiling::InternName(name);
    agg.totalTicks = 5 * kMs;
    agg.selfTicks = 5 * kMs;
    agg.calls = 1;
    for (std::size_t i = 0; i < frames; ++i) {
        history->Record(i, std::span<const Profiling::ScopeAggregate>(&agg, 1));
    }
    return history;
}

TEST(ProfilerGraphModelTest, BaseSeriesHaveFixedOrderColorsAndValues)
{
    const auto history = std::make_unique<Profiling::ScopeHistory>();
    const std::vector<FrameGraphPoint> points = MakePoints(5);
    const GraphModel model = BuildGraphModel(points, *history, {}, Profiling::ScopeSeries::TotalMs, GraphVisibility{});

    ASSERT_EQ(model.series.size(), 3u);
    EXPECT_EQ(model.series[0].color.r, kGraphFrameColor.r);
    EXPECT_EQ(model.series[1].color.b, kGraphCpuBusyColor.b);
    EXPECT_EQ(model.series[2].color.r, kGraphGpuColor.r);
    EXPECT_FLOAT_EQ(model.series[0].values[0], 10.0f);
    EXPECT_FLOAT_EQ(model.series[1].values[0], 8.0f);
    EXPECT_FLOAT_EQ(model.series[2].values[0], 4.0f);
    EXPECT_EQ(model.frameIndices.size(), 5u);
    EXPECT_FALSE(model.historyOutOfSync);
}

TEST(ProfilerGraphModelTest, GpuIsAGapWhenNotPresentAndBusyNeverNegative)
{
    const auto history = std::make_unique<Profiling::ScopeHistory>();
    std::vector<FrameGraphPoint> points;
    points.push_back(MakePoint(0, 10.0, 50.0, 3.0f, SampleStatus::Absent));
    const GraphModel model = BuildGraphModel(points, *history, {}, Profiling::ScopeSeries::TotalMs, GraphVisibility{});

    EXPECT_TRUE(std::isnan(model.series[2].values[0]));
    EXPECT_FLOAT_EQ(model.series[1].values[0], 0.0f);
}

TEST(ProfilerGraphModelTest, AxisIsAtLeastOneFrameAtSixtyFpsAndIgnoresHiddenLines)
{
    const auto history = std::make_unique<Profiling::ScopeHistory>();
    std::vector<FrameGraphPoint> points;
    points.push_back(MakePoint(0, 5.0, 0.0, 500.0f, SampleStatus::Present));

    GraphVisibility visibility;
    visibility.gpu = false;
    GraphModel model = BuildGraphModel(points, *history, {}, Profiling::ScopeSeries::TotalMs, visibility);
    EXPECT_GE(model.axisMaxMs, 16.7f);
    EXPECT_FALSE(model.guideLow);
    EXPECT_FALSE(model.guideHigh);
    EXPECT_FALSE(model.series[2].visible);
    EXPECT_FLOAT_EQ(model.series[2].values[0], 500.0f);

    model = BuildGraphModel(points, *history, {}, Profiling::ScopeSeries::TotalMs, GraphVisibility{});
    EXPECT_FLOAT_EQ(model.axisMaxMs, 550.0f);
    EXPECT_TRUE(model.guideLow);
    EXPECT_TRUE(model.guideHigh);
}

TEST(ProfilerGraphModelTest, WindowKeepsTheNewestFrames)
{
    const auto history = std::make_unique<Profiling::ScopeHistory>();
    const std::vector<FrameGraphPoint> points = MakePoints(kGraphWindowFrames + 10);
    const GraphModel model = BuildGraphModel(points, *history, {}, Profiling::ScopeSeries::TotalMs, GraphVisibility{});

    ASSERT_EQ(model.frameIndices.size(), kGraphWindowFrames);
    EXPECT_EQ(model.frameIndices.front(), 10u);
    EXPECT_EQ(model.frameIndices.back(), kGraphWindowFrames + 9);
}

TEST(ProfilerGraphModelTest, TrackedRowComesFromHistoryWithItsPaletteColor)
{
    const auto history = MakeHistory("TrackedSample", 5);
    const std::vector<FrameGraphPoint> points = MakePoints(5);
    const TrackedKey key{ ScopeLane::Main, Profiling::InternName("TrackedSample") };
    const std::vector<TrackedKey> tracked{ key };
    const GraphModel model = BuildGraphModel(points, *history, tracked, Profiling::ScopeSeries::TotalMs, GraphVisibility{});

    ASSERT_EQ(model.series.size(), 4u);
    const GraphSeries& s = model.series[3];
    EXPECT_FALSE(s.isBase);
    EXPECT_TRUE(s.key == key);
    EXPECT_EQ(s.label, "TrackedSample");
    EXPECT_EQ(s.color.g, kGraphTrackedColors[0].g);
    ASSERT_EQ(s.values.size(), 5u);
    EXPECT_FLOAT_EQ(s.values[4], 5.0f);
    EXPECT_FALSE(model.historyOutOfSync);
}

TEST(ProfilerGraphModelTest, UntrackedNameIsOmittedAndMisalignedHistoryIsFlagged)
{
    const auto history = MakeHistory("TrackedSample", 5);
    const TrackedKey known{ ScopeLane::Main, Profiling::InternName("TrackedSample") };
    const TrackedKey unknown{ ScopeLane::Main, Profiling::InternName("NeverRecordedSample") };
    const std::vector<TrackedKey> tracked{ known, unknown };

    const std::vector<FrameGraphPoint> shifted = MakePoints(8);
    const GraphModel model = BuildGraphModel(shifted, *history, tracked, Profiling::ScopeSeries::TotalMs, GraphVisibility{});
    EXPECT_EQ(model.series.size(), 4u);
    EXPECT_TRUE(model.historyOutOfSync);
}

TEST(ProfilerGraphModelTest, NinthTrackedRowIsRefused)
{
    std::vector<TrackedKey> list;
    for (std::uint32_t i = 0; i < kMaxTrackedRows; ++i) {
        EXPECT_TRUE(AddTracked(list, TrackedKey{ ScopeLane::Main, 100 + i }));
    }
    EXPECT_FALSE(AddTracked(list, TrackedKey{ ScopeLane::Main, 999 }));
    EXPECT_FALSE(AddTracked(list, TrackedKey{ ScopeLane::Main, 100 }));
    EXPECT_EQ(list.size(), kMaxTrackedRows);
    EXPECT_TRUE(IsTracked(list, TrackedKey{ ScopeLane::Main, 103 }));
    EXPECT_FALSE(IsTracked(list, TrackedKey{ ScopeLane::Gpu, 103 }));
    EXPECT_TRUE(RemoveTracked(list, TrackedKey{ ScopeLane::Main, 103 }));
    EXPECT_FALSE(RemoveTracked(list, TrackedKey{ ScopeLane::Main, 103 }));
}

} // namespace
} // namespace gte
