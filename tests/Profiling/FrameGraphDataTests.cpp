#include "Profiling/FrameGraphData.h"
#include "Profiling/FrameProfiler.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <vector>

namespace gte::Profiling {
namespace {

// FrameProfiler::Instance() is a process-wide singleton (see its own class
// comment) - every test here resets it to a known baseline before AND after
// running, mirroring tests/Profiling/FrameProfilerTests.cpp's own
// convention exactly (see AGENTS.md, "Profiling").
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

// Records one BeginFrame()/EndFrame() pair and then overrides its
// cpuFrameMilliseconds to an exact, caller-chosen literal via
// FrameProfiler::OverrideLastFrameCpuMillisecondsForTesting() - this is what
// lets this test file seed fully deterministic, bit-exact "known" CPU
// values (as PHASE2_FRAME_GRAPH_DATA_STRATEGY_v3.md's Step 3.3 cases 2/6
// call for) instead of depending on real, inherently-jittery wall-clock
// timing from SDL_GetPerformanceCounter().
void RecordFrameWithCpuMilliseconds(FrameProfiler& profiler, double cpuMilliseconds)
{
    profiler.BeginFrame();
    profiler.EndFrame();
    profiler.OverrideLastFrameCpuMillisecondsForTesting(cpuMilliseconds);
}

// --- Case 1: empty history -> empty output --------------------------------

TEST_F(FrameGraphDataTest, EmptyHistoryProducesEmptyOutputAndNoRangeData)
{
    FrameProfiler& profiler = FrameProfiler::Instance();

    const std::vector<FrameGraphPoint> points = BuildFrameGraphPoints(profiler);
    EXPECT_TRUE(points.empty());

    EXPECT_FALSE(ComputeCpuMillisecondsRange(points).hasData);
    EXPECT_FALSE(ComputeGpuMillisecondsRange(points, GpuPass::GameView).hasData);
}

// --- Case 2: a single completed frame round-trips exactly -----------------

TEST_F(FrameGraphDataTest, SingleFrameRoundTripsExactly)
{
    FrameProfiler& profiler = FrameProfiler::Instance();
    RecordFrameWithCpuMilliseconds(profiler, 12.5);

    const std::vector<FrameGraphPoint> points = BuildFrameGraphPoints(profiler);

    ASSERT_EQ(points.size(), 1u);
    EXPECT_EQ(points[0].frameIndex, 0u);
    EXPECT_DOUBLE_EQ(points[0].cpuMilliseconds, 12.5);

    for (const GpuPassSample& sample : points[0].gpuPasses) {
        EXPECT_EQ(sample.status, GpuSampleStatus::Absent);
    }
}

// --- Case 3: multiple frames preserve oldest-to-newest order, exactly -----

TEST_F(FrameGraphDataTest, MultipleFramesPreserveExactFrameIndexOrder)
{
    FrameProfiler& profiler = FrameProfiler::Instance();
    for (int i = 0; i < 4; ++i) {
        profiler.BeginFrame();
        profiler.EndFrame();
    }

    const std::vector<FrameGraphPoint> points = BuildFrameGraphPoints(profiler);
    ASSERT_EQ(points.size(), 4u);
    for (std::size_t i = 0; i < points.size(); ++i) {
        EXPECT_EQ(points[i].frameIndex, static_cast<std::uint64_t>(i));
    }
}

// --- Case 4: all three named GPU passes, all three tri-state values -------

TEST_F(FrameGraphDataTest, AllThreeGpuPassesAndTriStatesRoundTripToExactlyTheRightIndex)
{
    FrameProfiler& profiler = FrameProfiler::Instance();
    profiler.BeginFrame();
    profiler.SetGpuPassSample(GpuPass::GameView, GpuSampleStatus::Present, 3.5, 12, 400);
    profiler.SetGpuPassSample(GpuPass::SceneView, GpuSampleStatus::Unsupported);
    // GpuPass::Present (the pass identifier) is deliberately left untouched,
    // i.e. still its default GpuSampleStatus::Absent (the status value) -
    // this is a small, deliberate proof that this codebase's two
    // same-named-but-unrelated "Present" identifiers are never confused
    // with each other anywhere in the implementation (see
    // PHASE2_FRAME_GRAPH_DATA_STRATEGY_v3.md, Step 3.3 case 4).
    profiler.EndFrame();

    const std::vector<FrameGraphPoint> points = BuildFrameGraphPoints(profiler);
    ASSERT_EQ(points.size(), 1u);
    const FrameGraphPoint& point = points[0];

    const GpuPassSample& gameView = point.gpuPasses[static_cast<std::size_t>(GpuPass::GameView)];
    EXPECT_EQ(gameView.status, GpuSampleStatus::Present);
    EXPECT_DOUBLE_EQ(gameView.milliseconds, 3.5);
    EXPECT_EQ(gameView.drawCallCount, 12u);
    EXPECT_EQ(gameView.triangleCount, 400u);

    const GpuPassSample& sceneView = point.gpuPasses[static_cast<std::size_t>(GpuPass::SceneView)];
    EXPECT_EQ(sceneView.status, GpuSampleStatus::Unsupported);

    const GpuPassSample& presentPass = point.gpuPasses[static_cast<std::size_t>(GpuPass::Present)];
    EXPECT_EQ(presentPass.status, GpuSampleStatus::Absent);
}

// --- Case 5: ring buffer wraparound is transparently handled --------------

TEST_F(FrameGraphDataTest, RingBufferWraparoundKeepsExactBoundaryFrameIndices)
{
    FrameProfiler& profiler = FrameProfiler::Instance();
    const std::uint64_t totalFrames = static_cast<std::uint64_t>(kMaxFrameHistory) + 5;

    for (std::uint64_t i = 0; i < totalFrames; ++i) {
        profiler.BeginFrame();
        profiler.EndFrame();
    }

    const std::vector<FrameGraphPoint> points = BuildFrameGraphPoints(profiler);
    ASSERT_EQ(points.size(), kMaxFrameHistory);
    EXPECT_EQ(points.front().frameIndex, totalFrames - kMaxFrameHistory);
    EXPECT_EQ(points.back().frameIndex, totalFrames - 1);
}

// --- Case 6: ComputeCpuMillisecondsRange ignores nothing (CPU is always
//     real data), with the actual min/max landing in the middle of the
//     sequence rather than at either boundary, using known literal values -

TEST_F(FrameGraphDataTest, ComputeCpuMillisecondsRangeMatchesKnownMinAndMaxInTheMiddle)
{
    FrameProfiler& profiler = FrameProfiler::Instance();

    RecordFrameWithCpuMilliseconds(profiler, 2.0);
    RecordFrameWithCpuMilliseconds(profiler, 25.0); // Middle - the known max.
    RecordFrameWithCpuMilliseconds(profiler, 8.0);
    RecordFrameWithCpuMilliseconds(profiler, 0.5); // Middle - the known min.
    RecordFrameWithCpuMilliseconds(profiler, 12.0);

    const std::vector<FrameGraphPoint> points = BuildFrameGraphPoints(profiler);
    ASSERT_EQ(points.size(), 5u);

    const FrameGraphRange range = ComputeCpuMillisecondsRange(points);
    ASSERT_TRUE(range.hasData);
    EXPECT_DOUBLE_EQ(range.minMilliseconds, 0.5);
    EXPECT_DOUBLE_EQ(range.maxMilliseconds, 25.0);
}

// --- Case 7: ComputeGpuMillisecondsRange correctly ignores Absent frames,
//     and a never-Present pass reports no data even when a SIBLING pass
//     does have valid data --------------------------------------------

TEST_F(FrameGraphDataTest, ComputeGpuMillisecondsRangeIgnoresAbsentFramesForThatPassOnly)
{
    FrameProfiler& profiler = FrameProfiler::Instance();

    profiler.BeginFrame(); // Frame 0: GameView absent (default).
    profiler.EndFrame();

    profiler.BeginFrame(); // Frame 1: GameView present.
    profiler.SetGpuPassSample(GpuPass::GameView, GpuSampleStatus::Present, 2.0, 1, 10);
    profiler.EndFrame();

    profiler.BeginFrame(); // Frame 2: GameView absent again.
    profiler.EndFrame();

    profiler.BeginFrame(); // Frame 3: GameView present, the max.
    profiler.SetGpuPassSample(GpuPass::GameView, GpuSampleStatus::Present, 9.0, 3, 30);
    profiler.EndFrame();

    profiler.BeginFrame(); // Frame 4: GameView present, the min.
    profiler.SetGpuPassSample(GpuPass::GameView, GpuSampleStatus::Present, 0.5, 2, 5);
    profiler.EndFrame();

    const std::vector<FrameGraphPoint> points = BuildFrameGraphPoints(profiler);
    ASSERT_EQ(points.size(), 5u);

    const FrameGraphRange gameViewRange = ComputeGpuMillisecondsRange(points, GpuPass::GameView);
    ASSERT_TRUE(gameViewRange.hasData);
    EXPECT_DOUBLE_EQ(gameViewRange.minMilliseconds, 0.5);
    EXPECT_DOUBLE_EQ(gameViewRange.maxMilliseconds, 9.0);

    // SceneView was never set Present anywhere in this history - must report
    // no data, even though GameView's own range is valid. This is the direct
    // regression test for this whole phase's most important correctness
    // property (see PHASE2_FRAME_GRAPH_DATA_STRATEGY_v3.md, Step 1.2).
    const FrameGraphRange sceneViewRange = ComputeGpuMillisecondsRange(points, GpuPass::SceneView);
    EXPECT_FALSE(sceneViewRange.hasData);
}

// --- Case 8: an all-Unsupported series also reports no data ---------------

TEST_F(FrameGraphDataTest, AllUnsupportedSeriesReportsNoData)
{
    FrameProfiler& profiler = FrameProfiler::Instance();
    profiler.BeginFrame();
    profiler.SetGpuPassSample(GpuPass::SceneView, GpuSampleStatus::Unsupported);
    profiler.EndFrame();

    const std::vector<FrameGraphPoint> points = BuildFrameGraphPoints(profiler);
    const FrameGraphRange range = ComputeGpuMillisecondsRange(points, GpuPass::SceneView);
    EXPECT_FALSE(range.hasData);
}

// --- Case 9 (new in v3): an Absent/Unsupported sample carrying a
//     non-zero, "stale-looking" value is STILL correctly excluded --------

TEST_F(FrameGraphDataTest, AbsentSampleWithStaleNonZeroValueIsStillExcluded)
{
    FrameProfiler& profiler = FrameProfiler::Instance();
    profiler.BeginFrame();
    // SetGpuPassSample()'s own signature permits a non-default milliseconds/
    // drawCallCount/triangleCount alongside GpuSampleStatus::Absent - fully
    // constructible today, not a hypothetical. This positively rules out an
    // implementation that accidentally branches on "is the value zero"
    // instead of on the actual status tag.
    profiler.SetGpuPassSample(GpuPass::GameView, GpuSampleStatus::Absent, 42.0, 5, 100);
    profiler.EndFrame();

    const std::vector<FrameGraphPoint> points = BuildFrameGraphPoints(profiler);
    const FrameGraphRange range = ComputeGpuMillisecondsRange(points, GpuPass::GameView);
    EXPECT_FALSE(range.hasData);
}

TEST_F(FrameGraphDataTest, UnsupportedSampleWithStaleNonZeroValueIsStillExcluded)
{
    FrameProfiler& profiler = FrameProfiler::Instance();
    profiler.BeginFrame();
    profiler.SetGpuPassSample(GpuPass::GameView, GpuSampleStatus::Unsupported, 42.0, 5, 100);
    profiler.EndFrame();

    const std::vector<FrameGraphPoint> points = BuildFrameGraphPoints(profiler);
    const FrameGraphRange range = ComputeGpuMillisecondsRange(points, GpuPass::GameView);
    EXPECT_FALSE(range.hasData);
}

// --- Bounds-check requirement (Step 3.2/checklist): an out-of-range
//     GpuPass value must report no data, unconditionally, never crash ----

TEST_F(FrameGraphDataTest, OutOfRangeGpuPassReportsNoDataInsteadOfReadingOutOfBounds)
{
    FrameProfiler& profiler = FrameProfiler::Instance();
    profiler.BeginFrame();
    profiler.SetGpuPassSample(GpuPass::GameView, GpuSampleStatus::Present, 5.0, 1, 10);
    profiler.EndFrame();

    const std::vector<FrameGraphPoint> points = BuildFrameGraphPoints(profiler);
    const FrameGraphRange range = ComputeGpuMillisecondsRange(points, static_cast<GpuPass>(99));
    EXPECT_FALSE(range.hasData);
}

// --- Case 10 (new in v3): BuildFrameGraphPoints() never observes an
//     in-progress (BeginFrame()'d but not yet EndFrame()'d) frame --------

TEST_F(FrameGraphDataTest, BuildFrameGraphPointsNeverObservesAnInProgressFrame)
{
    FrameProfiler& profiler = FrameProfiler::Instance();
    profiler.BeginFrame();
    profiler.EndFrame();
    profiler.BeginFrame();
    profiler.EndFrame();

    profiler.BeginFrame(); // Deliberately no matching EndFrame() before reading.

    const std::vector<FrameGraphPoint> points = BuildFrameGraphPoints(profiler);
    EXPECT_EQ(points.size(), 2u);
}

} // namespace
} // namespace gte::Profiling
