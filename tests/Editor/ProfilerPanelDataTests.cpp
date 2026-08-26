// Unit tests for the Editor's "Profiler" panel data-shaping logic
// (src/Editor/ProfilerPanelData.h) - deliberately pure (no ImGui/Renderer/
// live-Vulkan-device knowledge at all), so it's Tier-1-testable exactly like
// MemoryPanelData.h despite living under src/Editor/ - see AGENTS.md,
// "Testability & Regression Safety". Only built when GTE_ENABLE_EDITOR is
// ON, since ProfilerPanelData.h/.cpp are only compiled into gte_core then
// (see the root CMakeLists.txt's "Editor Module Structure") - the same
// "zero-touch when off" rule already applied to
// tests/Editor/MemoryPanelDataTests.cpp.

#include "Editor/ProfilerPanelData.h"

#include <gtest/gtest.h>

namespace gte {
namespace {

Profiling::CpuScopeSample MakeScope(const char* name, double totalMilliseconds, std::uint32_t callCount)
{
    Profiling::CpuScopeSample sample;
    sample.name = name;
    sample.totalMilliseconds = totalMilliseconds;
    sample.callCount = callCount;
    return sample;
}

TEST(ProfilerPanelDataTest, BuildSortedCpuScopeRowsSortsBiggestFirst)
{
    Profiling::FrameSample frame;
    frame.cpuScopes[0] = MakeScope("Small", 1.0, 1);
    frame.cpuScopes[1] = MakeScope("Big", 10.0, 1);
    frame.cpuScopes[2] = MakeScope("Medium", 5.0, 1);
    frame.cpuScopeCount = 3;

    const std::vector<Profiling::CpuScopeSample> rows = BuildSortedCpuScopeRows(frame);

    ASSERT_EQ(rows.size(), 3u);
    EXPECT_STREQ(rows[0].name, "Big");
    EXPECT_STREQ(rows[1].name, "Medium");
    EXPECT_STREQ(rows[2].name, "Small");
}

TEST(ProfilerPanelDataTest, BuildSortedCpuScopeRowsRespectsCpuScopeCountNotArrayCapacity)
{
    Profiling::FrameSample frame;
    frame.cpuScopes[0] = MakeScope("OnlyOne", 3.0, 2);
    // Leave every other array slot at its default-constructed value (as if
    // stale from a differently-shaped prior frame) - cpuScopeCount is what
    // must gate iteration, never the array's fixed capacity.
    frame.cpuScopeCount = 1;

    const std::vector<Profiling::CpuScopeSample> rows = BuildSortedCpuScopeRows(frame);

    ASSERT_EQ(rows.size(), 1u);
    EXPECT_STREQ(rows[0].name, "OnlyOne");
}

TEST(ProfilerPanelDataTest, BuildSortedCpuScopeRowsOnEmptyFrameReturnsEmpty)
{
    const Profiling::FrameSample frame; // cpuScopeCount == 0 by default.

    const std::vector<Profiling::CpuScopeSample> rows = BuildSortedCpuScopeRows(frame);
    EXPECT_TRUE(rows.empty());
}

TEST(ProfilerPanelDataTest, FormatDurationProducesExpectedText)
{
    EXPECT_EQ(FormatDuration(0.0), "0.00 ms");
    EXPECT_EQ(FormatDuration(16.666), "16.67 ms");
    EXPECT_EQ(FormatDuration(2.4), "2.40 ms");
}

TEST(ProfilerPanelDataTest, FormatFrameTimeSummaryComputesFpsCorrectly)
{
    // 1000.0 / 16.666... ~= 60.0
    EXPECT_EQ(FormatFrameTimeSummary(1000.0 / 60.0), "16.67 ms / 60 FPS");
    EXPECT_EQ(FormatFrameTimeSummary(1000.0 / 30.0), "33.33 ms / 30 FPS");
}

TEST(ProfilerPanelDataTest, FormatFrameTimeSummaryGuardsAgainstNonPositiveInput)
{
    EXPECT_EQ(FormatFrameTimeSummary(0.0), "N/A");
    EXPECT_EQ(FormatFrameTimeSummary(-5.0), "N/A");
}

TEST(ProfilerPanelDataTest, FormatCountGroupsThousandsCorrectly)
{
    EXPECT_EQ(FormatCount(0), "0");
    EXPECT_EQ(FormatCount(42), "42");
    EXPECT_EQ(FormatCount(999), "999");
    EXPECT_EQ(FormatCount(1000), "1,000");
    EXPECT_EQ(FormatCount(128400), "128,400");
    EXPECT_EQ(FormatCount(1234567), "1,234,567");
}

TEST(ProfilerPanelDataTest, ResolveGpuPassCountsReportsAvailableForPresentStatus)
{
    Profiling::FrameSample frame;
    frame.gpuPasses[static_cast<std::size_t>(Profiling::GpuPass::GameView)].countStatus
        = Profiling::GpuSampleStatus::Present;
    frame.gpuPasses[static_cast<std::size_t>(Profiling::GpuPass::GameView)].drawCallCount = 42;
    frame.gpuPasses[static_cast<std::size_t>(Profiling::GpuPass::GameView)].triangleCount = 128400;

    const GpuPassCountDisplay display = ResolveGpuPassCounts(frame, Profiling::GpuPass::GameView);
    EXPECT_TRUE(display.available);
    EXPECT_EQ(display.drawCallCount, 42u);
    EXPECT_EQ(display.triangleCount, 128400u);
}

TEST(ProfilerPanelDataTest, ResolveGpuPassCountsReportsUnavailableForAbsentStatusEvenWithStaleNonZeroValues)
{
    Profiling::FrameSample frame;
    Profiling::GpuPassSample& sample = frame.gpuPasses[static_cast<std::size_t>(Profiling::GpuPass::SceneView)];
    sample.countStatus = Profiling::GpuSampleStatus::Absent;
    sample.drawCallCount = 99; // Stale/non-zero despite being Absent.
    sample.triangleCount = 12345;

    const GpuPassCountDisplay display = ResolveGpuPassCounts(frame, Profiling::GpuPass::SceneView);
    EXPECT_FALSE(display.available);
}

TEST(ProfilerPanelDataTest, ResolveGpuPassCountsBoundsChecksOutOfRangePass)
{
    const Profiling::FrameSample frame;
    const GpuPassCountDisplay display = ResolveGpuPassCounts(frame, static_cast<Profiling::GpuPass>(99));
    EXPECT_FALSE(display.available);
}

TEST(ProfilerPanelDataTest, ToStringGpuPassCoversAllThreeValues)
{
    EXPECT_STREQ(ToString(Profiling::GpuPass::GameView), "Game View");
    EXPECT_STREQ(ToString(Profiling::GpuPass::SceneView), "Scene View");
    EXPECT_STREQ(ToString(Profiling::GpuPass::Present), "Present");
    EXPECT_STREQ(ToString(static_cast<Profiling::GpuPass>(99)), "Unknown Pass");
}

TEST(ProfilerPanelDataTest, FormatGpuTimingLineReportsPlaceholderForAbsentAndUnsupported)
{
    Profiling::GpuPassSample absentSample;
    absentSample.timingStatus = Profiling::GpuSampleStatus::Absent;
    absentSample.milliseconds = 42.0; // Stale/non-zero - must still say N/A.
    EXPECT_EQ(FormatGpuTimingLine(absentSample), "N/A");

    Profiling::GpuPassSample unsupportedSample;
    unsupportedSample.timingStatus = Profiling::GpuSampleStatus::Unsupported;
    EXPECT_EQ(FormatGpuTimingLine(unsupportedSample), "N/A");
}

TEST(ProfilerPanelDataTest, FormatGpuTimingLineReportsRealValueForPresent)
{
    Profiling::GpuPassSample sample;
    sample.timingStatus = Profiling::GpuSampleStatus::Present;
    sample.milliseconds = 3.5;
    EXPECT_EQ(FormatGpuTimingLine(sample), "3.50 ms");
}

TEST(ProfilerPanelDataTest, CpuScopeTableEmptyMessageMatchesCompileTimeFlag)
{
    // This test can't meaningfully flip GTE_ENABLE_PROFILER at runtime, but
    // it does prove CpuScopeTableEmptyMessage() and
    // kCpuScopeInstrumentationCompiledIn never disagree with each other
    // within one build - a real, if narrow, regression it can actually
    // catch (see PHASE7_EDITOR_PROFILER_PANEL_STRATEGY_v2.md, Changelog #1).
    const std::string message = CpuScopeTableEmptyMessage();
    if (kCpuScopeInstrumentationCompiledIn) {
        EXPECT_EQ(message, "No CPU scopes recorded yet.");
    } else {
        EXPECT_NE(message.find("compiled out"), std::string::npos);
    }
}

} // namespace
} // namespace gte
