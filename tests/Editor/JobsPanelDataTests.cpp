// Unit tests for the Editor's "Jobs" panel data-shaping logic
// (src/Editor/JobsPanelData.h) - deliberately pure (no ImGui/Renderer/live-
// Vulkan-device/real JobSystem/FrameProfiler singleton involved at all), so
// it's Tier-1-testable exactly like ProfilerPanelData.h/MemoryPanelData.h
// despite living under src/Editor/ - see AGENTS.md, "Testability &
// Regression Safety". Only built when GTE_ENABLE_EDITOR is ON, since
// JobsPanelData.h/.cpp are only compiled into gte_core then (see the root
// CMakeLists.txt's "Editor Module Structure") - the same "zero-touch when
// off" rule already applied to tests/Editor/ProfilerPanelDataTests.cpp.

#include "Editor/JobsPanelData.h"

#include <gtest/gtest.h>

namespace gte {
namespace {

Profiling::WorkerTimelinePoint MakePoint(
    std::size_t workerIndex, const char* name, double startMilliseconds, double durationMilliseconds)
{
    Profiling::WorkerTimelinePoint point;
    point.workerIndex = workerIndex;
    point.name = name;
    point.startMilliseconds = startMilliseconds;
    point.durationMilliseconds = durationMilliseconds;
    return point;
}

TEST(JobsPanelDataTest, ColorForJobNameIsDeterministicForTheSameContent)
{
    // The SAME name text, even via two DIFFERENT pointers (mirroring the
    // real-world case of the same string literal appearing in two different
    // translation units at two different addresses), must map to the exact
    // same color every time.
    const char nameA[] = "SkinVertices";
    const char nameB[] = "SkinVertices";
    ASSERT_NE(static_cast<const void*>(nameA), static_cast<const void*>(nameB));

    const JobColor colorA = ColorForJobName(nameA);
    const JobColor colorB = ColorForJobName(nameB);
    EXPECT_FLOAT_EQ(colorA.r, colorB.r);
    EXPECT_FLOAT_EQ(colorA.g, colorB.g);
    EXPECT_FLOAT_EQ(colorA.b, colorB.b);
}

TEST(JobsPanelDataTest, ColorForJobNameDistinguishesDifferentNames)
{
    // Not every possible pair of names is GUARANTEED to differ (a fixed
    // palette can only hold so many entries), but these two particular,
    // deliberately different-looking names must not collide, so a
    // regression that made every job the same color would be caught.
    const JobColor colorA = ColorForJobName("SkinVertices");
    const JobColor colorB = ColorForJobName("SomeOtherJobKind");
    const bool identical = colorA.r == colorB.r && colorA.g == colorB.g && colorA.b == colorB.b;
    EXPECT_FALSE(identical);
}

TEST(JobsPanelDataTest, ColorForJobNameHandlesNullAndEmptyWithoutCrashing)
{
    const JobColor nullColor = ColorForJobName(nullptr);
    const JobColor emptyColor = ColorForJobName("");
    // Both are the same fixed neutral gray - not a crash, not undefined
    // behavior, and distinguishable at a glance from any real palette entry.
    EXPECT_FLOAT_EQ(nullColor.r, emptyColor.r);
    EXPECT_FLOAT_EQ(nullColor.g, emptyColor.g);
    EXPECT_FLOAT_EQ(nullColor.b, emptyColor.b);
}

TEST(JobsPanelDataTest, ComputeWorkerUtilizationSummaryCountsDistinctWorkersOnly)
{
    std::vector<Profiling::WorkerTimelinePoint> points;
    points.push_back(MakePoint(0, "A", 0.0, 1.0));
    points.push_back(MakePoint(0, "B", 1.0, 1.0)); // Same worker as above - must not double-count.
    points.push_back(MakePoint(2, "C", 0.0, 1.0));

    const WorkerUtilizationSummary summary = ComputeWorkerUtilizationSummary(points, 4);
    EXPECT_EQ(summary.workersWithAtLeastOneJob, 2u); // Workers 0 and 2.
    EXPECT_EQ(summary.totalWorkerCount, 4u);
}

TEST(JobsPanelDataTest, ComputeWorkerUtilizationSummaryOnEmptyPointsReportsZeroBusyWorkers)
{
    const std::vector<Profiling::WorkerTimelinePoint> points;
    const WorkerUtilizationSummary summary = ComputeWorkerUtilizationSummary(points, 8);
    EXPECT_EQ(summary.workersWithAtLeastOneJob, 0u);
    EXPECT_EQ(summary.totalWorkerCount, 8u);
}

TEST(JobsPanelDataTest, ComputeWorkerUtilizationSummaryClampsAgainstStaleTotalWorkerCount)
{
    // A defensive case: more distinct worker indices in `points` than the
    // caller-supplied totalWorkerCount claims exist - never report more
    // "busy" workers than the total.
    std::vector<Profiling::WorkerTimelinePoint> points;
    points.push_back(MakePoint(0, "A", 0.0, 1.0));
    points.push_back(MakePoint(1, "B", 0.0, 1.0));
    points.push_back(MakePoint(2, "C", 0.0, 1.0));

    const WorkerUtilizationSummary summary = ComputeWorkerUtilizationSummary(points, 1);
    EXPECT_EQ(summary.workersWithAtLeastOneJob, 1u);
    EXPECT_EQ(summary.totalWorkerCount, 1u);
}

TEST(JobsPanelDataTest, FormatWorkerUtilizationSummaryProducesExpectedText)
{
    WorkerUtilizationSummary summary;
    summary.workersWithAtLeastOneJob = 6;
    summary.totalWorkerCount = 8;
    EXPECT_EQ(FormatWorkerUtilizationSummary(summary),
        "6 / 8 workers had at least one job this frame - 2 idle the whole frame");
}

TEST(JobsPanelDataTest, FormatWorkerUtilizationSummaryHandlesZeroWorkers)
{
    const WorkerUtilizationSummary summary; // totalWorkerCount == 0 by default.
    EXPECT_EQ(FormatWorkerUtilizationSummary(summary), "No workers.");
}

TEST(JobsPanelDataTest, PointsForWorkerFiltersAndPreservesOrder)
{
    std::vector<Profiling::WorkerTimelinePoint> points;
    points.push_back(MakePoint(0, "First", 0.0, 1.0));
    points.push_back(MakePoint(1, "Other", 0.0, 1.0));
    points.push_back(MakePoint(0, "Second", 2.0, 1.0));
    points.push_back(MakePoint(1, "OtherAgain", 1.0, 1.0));
    points.push_back(MakePoint(0, "Third", 4.0, 1.0));

    const std::vector<Profiling::WorkerTimelinePoint> worker0 = PointsForWorker(points, 0);
    ASSERT_EQ(worker0.size(), 3u);
    EXPECT_STREQ(worker0[0].name, "First");
    EXPECT_STREQ(worker0[1].name, "Second");
    EXPECT_STREQ(worker0[2].name, "Third");

    const std::vector<Profiling::WorkerTimelinePoint> worker1 = PointsForWorker(points, 1);
    ASSERT_EQ(worker1.size(), 2u);
    EXPECT_STREQ(worker1[0].name, "Other");
    EXPECT_STREQ(worker1[1].name, "OtherAgain");
}

TEST(JobsPanelDataTest, PointsForWorkerOnUnknownIndexReturnsEmpty)
{
    std::vector<Profiling::WorkerTimelinePoint> points;
    points.push_back(MakePoint(0, "First", 0.0, 1.0));

    const std::vector<Profiling::WorkerTimelinePoint> worker5 = PointsForWorker(points, 5);
    EXPECT_TRUE(worker5.empty());
}

TEST(JobsPanelDataTest, JobsTimelineEmptyMessageMatchesCompileTimeFlag)
{
    // This test can't meaningfully flip GTE_ENABLE_PROFILER at runtime, but
    // it does prove JobsTimelineEmptyMessage() and
    // kJobTimingInstrumentationCompiledIn never disagree with each other
    // within one build - mirrors
    // ProfilerPanelDataTest.CpuScopeTableEmptyMessageMatchesCompileTimeFlag
    // exactly.
    const std::string message = JobsTimelineEmptyMessage();
    if (kJobTimingInstrumentationCompiledIn) {
        EXPECT_EQ(message, "No job samples recorded yet this frame.");
    } else {
        EXPECT_NE(message.find("compiled out"), std::string::npos);
    }
}

// GPU Vertex Skinning campaign, Phase 7 (Editor Toggle & Profiling UX) - the
// "Jobs" panel's own CPU/GPU skinning-mode toggle's pure display text.

TEST(JobsPanelDataTest, SkinningModeDisplayNameReflectsMode)
{
    EXPECT_STREQ(SkinningModeDisplayName(false), "CPU (Job System)");
    EXPECT_STREQ(SkinningModeDisplayName(true), "GPU (Compute)");
}

TEST(JobsPanelDataTest, SkinningModeCrossReferenceHintDiffersByMode)
{
    const std::string cpuHint = SkinningModeCrossReferenceHint(false);
    const std::string gpuHint = SkinningModeCrossReferenceHint(true);

    // Distinct text for each mode - never the same message regardless of
    // which mode is currently selected.
    EXPECT_NE(cpuHint, gpuHint);

    // Each points the user at the OTHER panel that shows the corresponding
    // data for the mode NOT currently selected (see this phase's own
    // strategy document, Step 3.3) - CPU mode's hint should mention where
    // GPU timing would show up, and vice versa.
    EXPECT_NE(cpuHint.find("Render Graph"), std::string::npos);
    EXPECT_NE(gpuHint.find("Render Graph"), std::string::npos);
    EXPECT_NE(cpuHint.find("SkinVertices"), std::string::npos);
    EXPECT_NE(gpuHint.find("SkinVertices"), std::string::npos);
}

} // namespace
} // namespace gte
