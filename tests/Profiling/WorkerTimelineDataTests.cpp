// Tier 1: Job System Phase 5's pure "one frame's raw WorkerJobSample log ->
// a per-worker timeline" reshape (gte::Profiling::BuildWorkerTimelinePoints()/
// ComputeDistinctWorkerCount() - src/Profiling/WorkerTimelineData.h). No
// live clock/JobSystem/FrameProfiler singleton involved - fed entirely with
// a hand-built FrameSample, mirroring tests/Profiling/FrameGraphDataTests.cpp's
// own "pure reshape, hand-built fixture" style.
#include "Profiling/WorkerTimelineData.h"

#include <gtest/gtest.h>

namespace gte::Profiling {
namespace {

TEST(WorkerTimelineDataTests, EmptyFrameProducesEmptyResult)
{
    FrameSample frame;
    ASSERT_EQ(frame.workerJobCount, 0u); // Default-constructed - nothing recorded.

    const std::vector<WorkerTimelinePoint> points = BuildWorkerTimelinePoints(frame);
    EXPECT_TRUE(points.empty());
    EXPECT_EQ(ComputeDistinctWorkerCount(points), 0u);
}

TEST(WorkerTimelineDataTests, EachSamplePreservesWorkerIndexNameAndDuration)
{
    FrameSample frame;
    frame.frameStartTicks = 1000;

    frame.workerJobs[0] = WorkerJobSample{ /*workerIndex=*/0, "SkinVertices", /*milliseconds=*/2.5,
        /*startTicks=*/1000 };
    frame.workerJobs[1] = WorkerJobSample{ /*workerIndex=*/1, "SkinVertices", /*milliseconds=*/1.25,
        /*startTicks=*/1500 };
    frame.workerJobCount = 2;

    const std::vector<WorkerTimelinePoint> points = BuildWorkerTimelinePoints(frame);
    ASSERT_EQ(points.size(), 2u);

    EXPECT_EQ(points[0].workerIndex, 0u);
    EXPECT_STREQ(points[0].name, "SkinVertices");
    EXPECT_DOUBLE_EQ(points[0].durationMilliseconds, 2.5);
    EXPECT_GE(points[0].startMilliseconds, 0.0);

    EXPECT_EQ(points[1].workerIndex, 1u);
    EXPECT_STREQ(points[1].name, "SkinVertices");
    EXPECT_DOUBLE_EQ(points[1].durationMilliseconds, 1.25);
    // The second sample started strictly LATER than the first (500 more raw
    // ticks) - its own startMilliseconds must reflect that ordering.
    EXPECT_GT(points[1].startMilliseconds, points[0].startMilliseconds);
}

TEST(WorkerTimelineDataTests, StartMillisecondsIsRelativeToFrameStart)
{
    // A sample whose own startTicks EXACTLY equals the frame's own start
    // must report startMilliseconds == 0.0 - this is what "relative to the
    // frame's own start" actually means.
    FrameSample frame;
    frame.frameStartTicks = 5000;
    frame.workerJobs[0] = WorkerJobSample{ 0, "AtFrameStart", 1.0, 5000 };
    frame.workerJobCount = 1;

    const std::vector<WorkerTimelinePoint> points = BuildWorkerTimelinePoints(frame);
    ASSERT_EQ(points.size(), 1u);
    EXPECT_DOUBLE_EQ(points[0].startMilliseconds, 0.0);
}

TEST(WorkerTimelineDataTests, DistinctWorkerCountIgnoresRepeats)
{
    FrameSample frame;
    frame.workerJobs[0] = WorkerJobSample{ 0, "A", 1.0, 100 };
    frame.workerJobs[1] = WorkerJobSample{ 1, "B", 1.0, 200 };
    frame.workerJobs[2] = WorkerJobSample{ 0, "C", 1.0, 300 }; // Same worker as index 0 above.
    frame.workerJobCount = 3;

    const std::vector<WorkerTimelinePoint> points = BuildWorkerTimelinePoints(frame);
    ASSERT_EQ(points.size(), 3u);
    EXPECT_EQ(ComputeDistinctWorkerCount(points), 2u);
}

TEST(WorkerTimelineDataTests, OrderingIsPreservedExactlyAsRecordedNeverResorted)
{
    FrameSample frame;
    frame.workerJobs[0] = WorkerJobSample{ 2, "RecordedFirstInSlot0", 1.0, 400 };
    frame.workerJobs[1] = WorkerJobSample{ 0, "RecordedSecondInSlot1", 1.0, 100 };
    frame.workerJobCount = 2;

    const std::vector<WorkerTimelinePoint> points = BuildWorkerTimelinePoints(frame);
    ASSERT_EQ(points.size(), 2u);
    // BuildWorkerTimelinePoints() must never re-sort by startTicks/worker -
    // it preserves FrameSample::workerJobs' own recorded order exactly,
    // even though slot 0's own startTicks (400) is LATER than slot 1's
    // (100).
    EXPECT_STREQ(points[0].name, "RecordedFirstInSlot0");
    EXPECT_STREQ(points[1].name, "RecordedSecondInSlot1");
}

TEST(WorkerTimelineDataTests, OnlyReadsExactlyWorkerJobCountEntries)
{
    // A frame whose workerJobs array has stale/leftover data beyond
    // workerJobCount (e.g. from a previous frame's larger recording, if
    // this FrameSample were ever reused rather than freshly constructed)
    // must never be read past workerJobCount.
    FrameSample frame;
    frame.workerJobs[0] = WorkerJobSample{ 0, "Real", 1.0, 100 };
    frame.workerJobs[1] = WorkerJobSample{ 5, "StaleShouldNeverBeRead", 99.0, 999999 };
    frame.workerJobCount = 1;

    const std::vector<WorkerTimelinePoint> points = BuildWorkerTimelinePoints(frame);
    ASSERT_EQ(points.size(), 1u);
    EXPECT_STREQ(points[0].name, "Real");
}

} // namespace
} // namespace gte::Profiling
