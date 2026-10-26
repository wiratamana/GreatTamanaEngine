// Tier 1: pure data shaping and formatting of the Profiler panel (src/Editor/ProfilerPanelData.h).

#include "Editor/ProfilerPanelData.h"

#include "Profiling/NameTable.h"

#include <gtest/gtest.h>

namespace gte {
namespace {

// Appends one finished scope with explicit tick times (1 tick = 1 ns).
void AddScope(Profiling::FrameRecord& frame, const char* name, std::uint64_t start, std::uint64_t end,
    std::uint16_t depth, Profiling::ScopeKind kind = Profiling::ScopeKind::Work)
{
    frame.scopes.Append(Profiling::ScopeEvent{
        start, end, Profiling::InternName(name), depth, kind, Profiling::ViewSlot::Shared });
}

TEST(ProfilerPanelDataTest, RowsAreSortedBiggestFirst)
{
    Profiling::FrameRecord frame;
    AddScope(frame, "PanelTestSmall", 0, 1'000'000, 0);
    AddScope(frame, "PanelTestBig", 0, 10'000'000, 0);
    AddScope(frame, "PanelTestMedium", 0, 5'000'000, 0);

    const std::vector<CpuScopeRow> rows = BuildSortedCpuScopeRows(frame);

    ASSERT_EQ(rows.size(), 3u);
    EXPECT_STREQ(rows[0].name, "PanelTestBig");
    EXPECT_STREQ(rows[1].name, "PanelTestMedium");
    EXPECT_STREQ(rows[2].name, "PanelTestSmall");
}

TEST(ProfilerPanelDataTest, SelfTimeExcludesDirectChildren)
{
    // Post-order: the child ends before its parent.
    Profiling::FrameRecord frame;
    AddScope(frame, "PanelTestChild", 1'000'000, 4'000'000, 1);
    AddScope(frame, "PanelTestParent", 0, 10'000'000, 0);

    const std::vector<CpuScopeRow> rows = BuildSortedCpuScopeRows(frame);

    ASSERT_EQ(rows.size(), 2u);
    EXPECT_STREQ(rows[0].name, "PanelTestParent");
    EXPECT_DOUBLE_EQ(rows[0].totalMilliseconds, 10.0);
    EXPECT_DOUBLE_EQ(rows[0].selfMilliseconds, 7.0);
    EXPECT_DOUBLE_EQ(rows[1].selfMilliseconds, 3.0);
}

TEST(ProfilerPanelDataTest, SameNameIsSummedAndCounted)
{
    Profiling::FrameRecord frame;
    AddScope(frame, "PanelTestRepeat", 0, 2'000'000, 0);
    AddScope(frame, "PanelTestRepeat", 3'000'000, 4'000'000, 0);

    const std::vector<CpuScopeRow> rows = BuildSortedCpuScopeRows(frame);

    ASSERT_EQ(rows.size(), 1u);
    EXPECT_EQ(rows[0].callCount, 2u);
    EXPECT_DOUBLE_EQ(rows[0].totalMilliseconds, 3.0);
}

TEST(ProfilerPanelDataTest, MarkersAreNotRows)
{
    Profiling::FrameRecord frame;
    AddScope(frame, "PanelTestMarker", 5, 5, 0, Profiling::ScopeKind::Marker);
    EXPECT_TRUE(BuildSortedCpuScopeRows(frame).empty());
}

TEST(ProfilerPanelDataTest, EmptyFrameHasNoRows)
{
    const Profiling::FrameRecord frame{};
    EXPECT_TRUE(BuildSortedCpuScopeRows(frame).empty());
}

TEST(ProfilerPanelDataTest, FormatDurationProducesExpectedText)
{
    EXPECT_EQ(FormatDuration(0.0), "0.00 ms");
    EXPECT_EQ(FormatDuration(16.666), "16.67 ms");
    EXPECT_EQ(FormatDuration(2.4), "2.40 ms");
}

TEST(ProfilerPanelDataTest, FormatFrameTimeSummaryComputesFpsCorrectly)
{
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

TEST(ProfilerPanelDataTest, GpuTimingLineShowsANumberOnlyForPresent)
{
    using Profiling::SampleStatus;
    EXPECT_EQ(FormatGpuTimingLine(SampleStatus::Present, 3.5), "3.50 ms");
    EXPECT_EQ(FormatGpuTimingLine(SampleStatus::Absent, 42.0), "not measured");
    EXPECT_EQ(FormatGpuTimingLine(SampleStatus::Unsupported, 42.0), "unsupported");
    EXPECT_EQ(FormatGpuTimingLine(SampleStatus::Partial, 42.0), "partial");
    EXPECT_EQ(FormatGpuTimingLine(SampleStatus::Error, 42.0), "error");
    EXPECT_EQ(FormatGpuTimingLine(SampleStatus::Inactive, 42.0), "inactive");
    EXPECT_EQ(FormatGpuTimingLine(SampleStatus::Stale, 42.0), "stale");
}

TEST(ProfilerPanelDataTest, CpuScopeTableEmptyMessageMatchesCompileTimeFlag)
{
    const std::string message = CpuScopeTableEmptyMessage();
    if (kCpuScopeInstrumentationCompiledIn) {
        EXPECT_EQ(message, "No CPU scopes recorded yet.");
    } else {
        EXPECT_NE(message.find("compiled out"), std::string::npos);
    }
}

} // namespace
} // namespace gte
