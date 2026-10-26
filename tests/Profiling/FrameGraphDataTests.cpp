// Tier 1: history to plottable points, and the range helpers.
#include "Profiling/FrameGraphData.h"
#include "Profiling/FrameProfiler.h"

#include <gtest/gtest.h>

#include <vector>

namespace gte::Profiling {
namespace {

// FrameProfiler is a process-wide singleton, so each test starts from a clean state.
class FrameGraphDataTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        FrameProfiler::Instance().ResetForTesting();
        FrameProfiler::Instance().SetCaptureEnabled(true);
    }

    void TearDown() override
    {
        FrameProfiler::Instance().ResetForTesting();
        FrameProfiler::Instance().SetCaptureEnabled(true);
    }
};

FrameGraphPoint MakeCpuPoint(std::uint64_t frameIndex, double cpuMilliseconds)
{
    FrameGraphPoint point;
    point.frameIndex = frameIndex;
    point.cpuMilliseconds = cpuMilliseconds;
    return point;
}

FrameGraphPoint MakeMemoryPoint(SampleStatus status, std::uint64_t totalBytes)
{
    FrameGraphPoint point;
    point.memory.status = status;
    point.memory.totalBytes = totalBytes;
    return point;
}

TEST_F(FrameGraphDataTest, EmptyHistoryProducesNoPointsAndNoRange)
{
    const std::vector<FrameGraphPoint> points = BuildFrameGraphPoints(FrameProfiler::Instance());
    EXPECT_TRUE(points.empty());
    EXPECT_FALSE(ComputeCpuMillisecondsRange(points).hasData);
    EXPECT_FALSE(ComputeMemoryBytesRange(points).hasData);
}

TEST_F(FrameGraphDataTest, PointsFollowTheHistoryOldestFirst)
{
    FrameProfiler& profiler = FrameProfiler::Instance();
    MemorySnapshot snapshot;
    snapshot.status = SampleStatus::Present;
    snapshot.totalBytes = 777;

    for (int i = 0; i < 3; ++i) {
        profiler.BeginFrame();
        profiler.SetMemorySnapshot(snapshot);
        profiler.EndFrame();
    }

    const std::vector<FrameGraphPoint> points = BuildFrameGraphPoints(profiler);
    ASSERT_EQ(points.size(), 3u);
    for (std::size_t i = 0; i < points.size(); ++i) {
        EXPECT_EQ(points[i].frameIndex, i);
        EXPECT_GE(points[i].cpuMilliseconds, 0.0);
        EXPECT_EQ(points[i].memory.status, SampleStatus::Present);
        EXPECT_EQ(points[i].memory.totalBytes, 777u);
    }
}

TEST_F(FrameGraphDataTest, CaptureOffLeavesNoGapInFrameIndices)
{
    FrameProfiler& profiler = FrameProfiler::Instance();
    profiler.BeginFrame();
    profiler.EndFrame();

    profiler.SetCaptureEnabled(false);
    profiler.BeginFrame();
    profiler.EndFrame();
    profiler.SetCaptureEnabled(true);

    profiler.BeginFrame();
    profiler.EndFrame();

    const std::vector<FrameGraphPoint> points = BuildFrameGraphPoints(profiler);
    ASSERT_EQ(points.size(), 2u);
    EXPECT_EQ(points[0].frameIndex, 0u);
    EXPECT_EQ(points[1].frameIndex, 1u);
}

TEST_F(FrameGraphDataTest, OpenFrameIsNeverInThePoints)
{
    FrameProfiler& profiler = FrameProfiler::Instance();
    profiler.BeginFrame();
    profiler.EndFrame();
    profiler.BeginFrame(); // still open

    EXPECT_EQ(BuildFrameGraphPoints(profiler).size(), 1u);
    profiler.EndFrame();
}

TEST(FrameGraphRangeTest, CpuRangeFindsMinAndMax)
{
    const std::vector<FrameGraphPoint> points = { MakeCpuPoint(0, 8.0), MakeCpuPoint(1, 2.5), MakeCpuPoint(2, 30.0),
        MakeCpuPoint(3, 11.0) };

    const FrameGraphRange range = ComputeCpuMillisecondsRange(points);
    ASSERT_TRUE(range.hasData);
    EXPECT_DOUBLE_EQ(range.minMilliseconds, 2.5);
    EXPECT_DOUBLE_EQ(range.maxMilliseconds, 30.0);
}

TEST(FrameGraphRangeTest, CpuRangeOfOnePointIsThatPoint)
{
    const std::vector<FrameGraphPoint> points = { MakeCpuPoint(0, 4.0) };

    const FrameGraphRange range = ComputeCpuMillisecondsRange(points);
    ASSERT_TRUE(range.hasData);
    EXPECT_DOUBLE_EQ(range.minMilliseconds, 4.0);
    EXPECT_DOUBLE_EQ(range.maxMilliseconds, 4.0);
}

TEST(FrameGraphRangeTest, CpuRangeAcceptsASubSpan)
{
    const std::vector<FrameGraphPoint> points = { MakeCpuPoint(0, 100.0), MakeCpuPoint(1, 3.0), MakeCpuPoint(2, 5.0) };

    const FrameGraphRange range = ComputeCpuMillisecondsRange(std::span<const FrameGraphPoint>(points).subspan(1));
    ASSERT_TRUE(range.hasData);
    EXPECT_DOUBLE_EQ(range.maxMilliseconds, 5.0);
}

TEST(FrameGraphRangeTest, MemoryRangeSkipsPointsThatAreNotPresent)
{
    const std::vector<FrameGraphPoint> points = { MakeMemoryPoint(SampleStatus::Present, 500),
        MakeMemoryPoint(SampleStatus::Absent, 999999), MakeMemoryPoint(SampleStatus::Present, 200),
        MakeMemoryPoint(SampleStatus::Error, 1) };

    const MemoryBytesRange range = ComputeMemoryBytesRange(points);
    ASSERT_TRUE(range.hasData);
    EXPECT_EQ(range.minBytes, 200u);
    EXPECT_EQ(range.maxBytes, 500u);
}

TEST(FrameGraphRangeTest, MemoryRangeWithoutPresentPointsHasNoData)
{
    const std::vector<FrameGraphPoint> points = { MakeMemoryPoint(SampleStatus::Absent, 42) };
    EXPECT_FALSE(ComputeMemoryBytesRange(points).hasData);
}

} // namespace
} // namespace gte::Profiling
