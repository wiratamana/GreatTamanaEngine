// Tier 1: bars of one frame on a shared time axis.
#include "Profiling/TimelineLayout.h"

#include <gtest/gtest.h>

namespace gte {
namespace {

using namespace Profiling;

SpikeScopeRow Scope(double start, double duration, std::uint16_t depth, ScopeKind kind = ScopeKind::Work)
{
    SpikeScopeRow row;
    row.name = "scope";
    row.startMs = start;
    row.durationMs = duration;
    row.depth = depth;
    row.kind = kind;
    return row;
}

std::size_t CountOn(const TimelineLayout& layout, TimelineLane lane)
{
    std::size_t count = 0;
    for (const TimelineBar& bar : layout.bars) {
        count += bar.lane == lane ? 1 : 0;
    }
    return count;
}

TEST(TimelineLayoutTest, AnEmptyFrameHasNoBarsAndOneRowPerLane)
{
    const TimelineLayout layout = BuildTimelineLayout(SpikeDetail{});
    EXPECT_TRUE(layout.bars.empty());
    for (const std::uint16_t rows : layout.laneRows) {
        EXPECT_EQ(rows, 1);
    }
}

TEST(TimelineLayoutTest, ScopesLandInTheirLaneAtTheirDepthAsAxisFractions)
{
    SpikeDetail frame;
    frame.frameMs = 20.0;
    frame.scopes = { Scope(0.0, 20.0, 0), Scope(5.0, 10.0, 1), Scope(6.0, 4.0, 1, ScopeKind::Blocked) };

    const TimelineLayout layout = BuildTimelineLayout(frame);
    EXPECT_DOUBLE_EQ(layout.axisMs, 20.0);
    ASSERT_EQ(layout.bars.size(), 3u);
    EXPECT_EQ(layout.bars[1].lane, TimelineLane::CpuMain);
    EXPECT_EQ(layout.bars[1].row, 1);
    EXPECT_FLOAT_EQ(layout.bars[1].x0, 0.25f);
    EXPECT_FLOAT_EQ(layout.bars[1].x1, 0.75f);
    EXPECT_EQ(layout.bars[2].lane, TimelineLane::Blocked);
    EXPECT_EQ(layout.laneRows[static_cast<std::size_t>(TimelineLane::CpuMain)], 2);
}

TEST(TimelineLayoutTest, TheAxisGrowsToTheLongestThing)
{
    SpikeDetail frame;
    frame.frameMs = 10.0;
    frame.scopes = { Scope(8.0, 6.0, 0) };
    SpikeWorkerRow job;
    job.worker = 3;
    job.startMs = 0.0;
    job.durationMs = 40.0;
    frame.workers = { job };

    const TimelineLayout layout = BuildTimelineLayout(frame);
    EXPECT_DOUBLE_EQ(layout.axisMs, 40.0);
    for (const TimelineBar& bar : layout.bars) {
        EXPECT_GE(bar.x0, 0.0f);
        EXPECT_LE(bar.x1, 1.0f);
        EXPECT_GE(bar.x1, bar.x0);
    }
}

TEST(TimelineLayoutTest, WorkersGetOneRowEachInOrderOfFirstAppearance)
{
    SpikeDetail frame;
    frame.frameMs = 10.0;
    for (const std::uint16_t worker : { 7, 2, 7 }) {
        SpikeWorkerRow job;
        job.worker = worker;
        job.durationMs = 1.0;
        frame.workers.push_back(job);
    }
    const TimelineLayout layout = BuildTimelineLayout(frame);
    ASSERT_EQ(layout.bars.size(), 3u);
    EXPECT_EQ(layout.bars[0].row, 0);
    EXPECT_EQ(layout.bars[1].row, 1);
    EXPECT_EQ(layout.bars[2].row, 0);
    EXPECT_EQ(layout.laneRows[static_cast<std::size_t>(TimelineLane::Workers)], 2);
}

TEST(TimelineLayoutTest, OnlyMeasuredGpuViewsGetABar)
{
    SpikeDetail frame;
    frame.frameMs = 10.0;
    frame.gpu.views[static_cast<std::size_t>(ViewSlot::Primary)] = { 4.0f, SampleStatus::Present, 0 };
    frame.gpu.views[static_cast<std::size_t>(ViewSlot::Secondary)] = { 0.0f, SampleStatus::Inactive, 0 };
    frame.gpu.views[static_cast<std::size_t>(ViewSlot::Shared)] = { 9.0f, SampleStatus::Absent, 0 };

    const TimelineLayout layout = BuildTimelineLayout(frame);
    ASSERT_EQ(CountOn(layout, TimelineLane::Gpu), 1u);
    const TimelineBar& bar = layout.bars.front();
    EXPECT_EQ(bar.kind, TimelineBarKind::GpuPrimary);
    EXPECT_FLOAT_EQ(bar.x0, 0.0f);
    EXPECT_FLOAT_EQ(bar.x1, 0.4f);
}

TEST(TimelineLayoutTest, DeepNestingBeyondTheRowLimitIsCountedNotDrawn)
{
    SpikeDetail frame;
    frame.frameMs = 10.0;
    frame.scopes = { Scope(0.0, 10.0, 0), Scope(0.0, 5.0, kTimelineMaxRows) };
    const TimelineLayout layout = BuildTimelineLayout(frame);
    EXPECT_EQ(layout.bars.size(), 1u);
    EXPECT_EQ(layout.hiddenBars, 1u);
}

TEST(TimelineLayoutTest, MarkersAreZeroWidthBars)
{
    SpikeDetail frame;
    frame.frameMs = 10.0;
    frame.scopes = { Scope(5.0, 0.0, 0, ScopeKind::Marker) };
    const TimelineLayout layout = BuildTimelineLayout(frame);
    ASSERT_EQ(layout.bars.size(), 1u);
    EXPECT_EQ(layout.bars[0].kind, TimelineBarKind::Marker);
    EXPECT_FLOAT_EQ(layout.bars[0].x0, layout.bars[0].x1);
}

} // namespace
} // namespace gte
