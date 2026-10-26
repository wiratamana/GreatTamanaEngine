// Tier 1: worker events of one frame to timeline points.
#include "Profiling/NameTable.h"
#include "Profiling/WorkerTimelineData.h"

#include <gtest/gtest.h>

namespace gte::Profiling {
namespace {

// Appends a worker event. Ticks are nanoseconds, so 1'000'000 ticks are 1 ms.
void AddEvent(FrameRecord& frame, std::uint16_t worker, const char* name, std::uint64_t start, std::uint64_t end,
    std::uint8_t flags = 0)
{
    frame.workers.Append(WorkerEvent{ start, end, InternName(name), worker, flags, 0 });
}

TEST(WorkerTimelineDataTests, EmptyFrameProducesEmptyResult)
{
    const FrameRecord frame{};

    const std::vector<WorkerTimelinePoint> points = BuildWorkerTimelinePoints(frame);
    EXPECT_TRUE(points.empty());
    EXPECT_EQ(ComputeDistinctWorkerCount(points), 0u);
}

TEST(WorkerTimelineDataTests, EachEventKeepsWorkerNameAndDuration)
{
    FrameRecord frame;
    frame.summary.startTicks = 1'000'000;
    AddEvent(frame, 0, "TimelineSkin", 1'000'000, 3'500'000);
    AddEvent(frame, 1, "TimelineSkin", 2'000'000, 3'250'000);

    const std::vector<WorkerTimelinePoint> points = BuildWorkerTimelinePoints(frame);
    ASSERT_EQ(points.size(), 2u);

    EXPECT_EQ(points[0].workerIndex, 0u);
    EXPECT_STREQ(points[0].name, "TimelineSkin");
    EXPECT_DOUBLE_EQ(points[0].durationMilliseconds, 2.5);
    EXPECT_DOUBLE_EQ(points[0].startMilliseconds, 0.0);

    EXPECT_EQ(points[1].workerIndex, 1u);
    EXPECT_DOUBLE_EQ(points[1].durationMilliseconds, 1.25);
    EXPECT_DOUBLE_EQ(points[1].startMilliseconds, 1.0);
}

TEST(WorkerTimelineDataTests, StartIsRelativeToTheFrameStart)
{
    FrameRecord frame;
    frame.summary.startTicks = 5'000'000;
    AddEvent(frame, 0, "TimelineAtStart", 5'000'000, 6'000'000);

    const std::vector<WorkerTimelinePoint> points = BuildWorkerTimelinePoints(frame);
    ASSERT_EQ(points.size(), 1u);
    EXPECT_DOUBLE_EQ(points[0].startMilliseconds, 0.0);
}

TEST(WorkerTimelineDataTests, JobThatStartedBeforeTheFrameStartsAtZero)
{
    FrameRecord frame;
    frame.summary.startTicks = 5'000'000;
    AddEvent(frame, 2, "TimelineCarried", 1'000'000, 7'000'000, kWorkerEventCarried);

    const std::vector<WorkerTimelinePoint> points = BuildWorkerTimelinePoints(frame);
    ASSERT_EQ(points.size(), 1u);
    EXPECT_DOUBLE_EQ(points[0].startMilliseconds, 0.0);
    EXPECT_DOUBLE_EQ(points[0].durationMilliseconds, 6.0);
}

TEST(WorkerTimelineDataTests, DistinctWorkerCountIgnoresRepeats)
{
    FrameRecord frame;
    AddEvent(frame, 0, "TimelineA", 100, 200);
    AddEvent(frame, 1, "TimelineB", 200, 300);
    AddEvent(frame, 0, "TimelineC", 300, 400);

    const std::vector<WorkerTimelinePoint> points = BuildWorkerTimelinePoints(frame);
    ASSERT_EQ(points.size(), 3u);
    EXPECT_EQ(ComputeDistinctWorkerCount(points), 2u);
}

TEST(WorkerTimelineDataTests, OrderIsTheRecordedOrder)
{
    FrameRecord frame;
    AddEvent(frame, 2, "TimelineFirst", 400, 500);
    AddEvent(frame, 0, "TimelineSecond", 100, 200);

    const std::vector<WorkerTimelinePoint> points = BuildWorkerTimelinePoints(frame);
    ASSERT_EQ(points.size(), 2u);
    EXPECT_STREQ(points[0].name, "TimelineFirst");
    EXPECT_STREQ(points[1].name, "TimelineSecond");
}

} // namespace
} // namespace gte::Profiling
