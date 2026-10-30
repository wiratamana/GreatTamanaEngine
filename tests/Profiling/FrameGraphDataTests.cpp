// Tier 1: history to plottable points, and the memory range helper.
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

std::vector<FrameGraphPoint> PointsOf(const FrameProfiler& profiler)
{
    std::vector<FrameGraphPoint> points;
    BuildFrameGraphPoints(profiler, points);
    return points;
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
    const std::vector<FrameGraphPoint> points = PointsOf(FrameProfiler::Instance());
    EXPECT_TRUE(points.empty());
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

    const std::vector<FrameGraphPoint> points = PointsOf(profiler);
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

    const std::vector<FrameGraphPoint> points = PointsOf(profiler);
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

    EXPECT_EQ(PointsOf(profiler).size(), 1u);
    profiler.EndFrame();
}

TEST_F(FrameGraphDataTest, RefillingAReusedVectorGivesTheSameAsAFreshOne)
{
    FrameProfiler& profiler = FrameProfiler::Instance();
    for (int i = 0; i < 3; ++i) {
        profiler.BeginFrame();
        profiler.EndFrame();
    }

    std::vector<FrameGraphPoint> reused;
    BuildFrameGraphPoints(profiler, reused);
    profiler.BeginFrame();
    profiler.EndFrame();
    BuildFrameGraphPoints(profiler, reused);

    std::vector<FrameGraphPoint> fresh;
    BuildFrameGraphPoints(profiler, fresh);
    ASSERT_EQ(reused.size(), fresh.size());
    ASSERT_EQ(fresh.size(), 4u);
    for (std::size_t i = 0; i < fresh.size(); ++i) {
        EXPECT_EQ(reused[i].frameIndex, fresh[i].frameIndex);
    }
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
