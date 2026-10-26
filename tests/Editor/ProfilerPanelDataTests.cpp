// Tier 1: pure data shaping and formatting of the Profiler panel (src/Editor/ProfilerPanelData.h).

#include "Editor/ProfilerPanelData.h"

#include "Profiling/NameTable.h"
#include "Renderer/RenderGraph/RenderGraphSnapshotFormatting.h"

#include <algorithm>
#include <optional>
#include <string>
#include <vector>

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
    EXPECT_EQ(FormatGpuTimingLine(SampleStatus::Absent, 42.0), "absent");
    EXPECT_EQ(FormatGpuTimingLine(SampleStatus::Unsupported, 42.0), "unsupported");
    EXPECT_EQ(FormatGpuTimingLine(SampleStatus::Partial, 42.0), "partial");
    EXPECT_EQ(FormatGpuTimingLine(SampleStatus::Error, 42.0), "error");
    EXPECT_EQ(FormatGpuTimingLine(SampleStatus::Inactive, 42.0), "inactive");
    EXPECT_EQ(FormatGpuTimingLine(SampleStatus::Stale, 42.0), "stale");
}

TEST(ProfilerPanelDataTest, GpuMetricNeverPrintsNotAvailableOrAFakeNumber)
{
    using Profiling::GpuViewMetric;
    using Profiling::SampleStatus;
    const SampleStatus all[] = { SampleStatus::Present, SampleStatus::Stale, SampleStatus::Absent,
        SampleStatus::Unsupported, SampleStatus::Partial, SampleStatus::Error, SampleStatus::Inactive };
    for (const SampleStatus status : all) {
        GpuViewMetric metric;
        metric.status = status;
        metric.milliseconds = 0.0f;
        const std::string text = FormatGpuMetric(metric);
        EXPECT_EQ(text.find("N/A"), std::string::npos) << text;
        if (status != SampleStatus::Present) {
            EXPECT_EQ(text.find("0.00"), std::string::npos) << text;
        }
    }
}

TEST(ProfilerPanelDataTest, GpuMetricInactiveAndAbsentIgnoreTheirValue)
{
    Profiling::GpuViewMetric metric;
    metric.milliseconds = 7.0f;
    metric.status = Profiling::SampleStatus::Inactive;
    EXPECT_EQ(FormatGpuMetric(metric), "inactive");
    metric.status = Profiling::SampleStatus::Absent;
    EXPECT_EQ(FormatGpuMetric(metric), "absent");
}

TEST(ProfilerPanelDataTest, GpuMetricPartialSaysWhyItIsPartial)
{
    Profiling::GpuViewMetric metric;
    metric.status = Profiling::SampleStatus::Partial;
    metric.milliseconds = 1.5f;
    metric.foldedPasses = 9;
    EXPECT_EQ(FormatGpuMetric(metric), "1.50 ms partial (9 passes folded)");
    metric.foldedPasses = 0;
    EXPECT_EQ(FormatGpuMetric(metric), "1.50 ms partial (some passes not measured)");
}

TEST(ProfilerPanelDataTest, GpuCoverageTextNamesEveryCount)
{
    Profiling::GpuFrameMetrics gpu;
    gpu.executedPasses = 20;
    gpu.declaredPasses = 24;
    gpu.recordedPasses = 18;
    gpu.disabledByToggle = 3;
    EXPECT_EQ(FormatGpuCoverage(gpu), "executed 20 of declared 24, recorded 18, disabled by toggle 3");
}

rg::RenderGraphPassMetadata MakeTimedPass(rg::ViewScope view, std::optional<double> milliseconds, const char* status)
{
    rg::RenderGraphPassMetadata pass;
    pass.viewScope = rg::ToString(view);
    pass.gpuTimingMilliseconds = milliseconds;
    pass.gpuTimingStatus = status;
    return pass;
}

TEST(ProfilerPanelDataTest, RegimeGpuIsSummedPerViewAndNeverAcrossViews)
{
    std::vector<rg::RenderGraphPassMetadata> passes;
    passes.push_back(MakeTimedPass(rg::ViewScope::Primary, 1.0, "Present"));
    passes.push_back(MakeTimedPass(rg::ViewScope::Primary, 0.5, "Present"));
    passes.push_back(MakeTimedPass(rg::ViewScope::Secondary, 4.0, "Present"));

    const RegimeGpuByView byView = SumRegimeGpuByView(passes);
    EXPECT_EQ(byView.game.status, Profiling::SampleStatus::Present);
    EXPECT_NEAR(byView.game.milliseconds, 1.5f, 1e-4f);
    EXPECT_EQ(byView.scene.status, Profiling::SampleStatus::Present);
    EXPECT_NEAR(byView.scene.milliseconds, 4.0f, 1e-4f);
    EXPECT_EQ(byView.shared.status, Profiling::SampleStatus::Inactive);
}

TEST(ProfilerPanelDataTest, RegimeGpuViewWithoutPassesIsInactiveAndUnmeasuredIsNotZero)
{
    std::vector<rg::RenderGraphPassMetadata> passes;
    passes.push_back(MakeTimedPass(rg::ViewScope::Primary, std::nullopt, "Absent"));
    rg::RenderGraphPassMetadata culled = MakeTimedPass(rg::ViewScope::Secondary, 9.0, "Present");
    culled.isCulled = true;
    passes.push_back(culled);

    const RegimeGpuByView byView = SumRegimeGpuByView(passes);
    EXPECT_EQ(byView.game.status, Profiling::SampleStatus::Absent);
    EXPECT_EQ(byView.scene.status, Profiling::SampleStatus::Inactive);
    EXPECT_EQ(FormatGpuMetric(byView.scene).find("N/A"), std::string::npos);
}

TEST(ProfilerPanelDataTest, RegimeGpuFoldedPassesMakeAViewPartial)
{
    rg::RenderGraphPassMetadata bucket = MakeTimedPass(rg::ViewScope::Primary, 2.0, "Present");
    bucket.gpuFoldedPasses = 5;
    const RegimeGpuByView byView = SumRegimeGpuByView({ bucket });
    EXPECT_EQ(byView.game.status, Profiling::SampleStatus::Partial);
    EXPECT_EQ(byView.game.foldedPasses, 5u);
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

TEST(ProfilerPanelDataTest, TreeRowsListParentsBeforeChildrenAndEndWithUntracked)
{
    // Post-order input: children first. The frame is 20 ms; top-level scopes cover 10 ms.
    Profiling::FrameRecord frame;
    frame.summary.startTicks = 0;
    frame.summary.endTicks = 20'000'000;
    AddScope(frame, "PanelTreeChildA", 1'000'000, 4'000'000, 1);
    AddScope(frame, "PanelTreeChildB", 5'000'000, 7'000'000, 1);
    AddScope(frame, "PanelTreeParent", 0, 10'000'000, 0);

    const Profiling::FrameTotals totals = Profiling::ComputeFrameTotals(frame);
    const std::vector<CpuTreeRow> rows = BuildCpuTreeRows(frame, totals);

    ASSERT_EQ(rows.size(), 4u);
    EXPECT_STREQ(rows[0].name, "PanelTreeParent");
    EXPECT_EQ(rows[0].depth, 0u);
    EXPECT_DOUBLE_EQ(rows[0].selfMilliseconds, 5.0);
    EXPECT_DOUBLE_EQ(rows[0].percentOfFrame, 50.0);
    EXPECT_STREQ(rows[1].name, "PanelTreeChildA");
    EXPECT_EQ(rows[1].depth, 1u);
    EXPECT_STREQ(rows[2].name, "PanelTreeChildB");
    EXPECT_TRUE(rows[3].untracked);
    EXPECT_DOUBLE_EQ(rows[3].totalMilliseconds, 10.0);
}

TEST(ProfilerPanelDataTest, TreeRowsOfAnEmptyFrameHaveNoUntrackedRow)
{
    const Profiling::FrameRecord frame{};
    const std::vector<CpuTreeRow> rows = BuildCpuTreeRows(frame, Profiling::ComputeFrameTotals(frame));
    EXPECT_TRUE(rows.empty());
}

TEST(ProfilerPanelDataTest, FrameViewCarriesChecksAndRows)
{
    Profiling::FrameRecord frame;
    frame.summary.endTicks = 10'000'000;
    AddScope(frame, "PanelViewBody", 0, 10'000'000, 0);

    const ProfilerFrameView view = BuildProfilerFrameView(frame, false);
    EXPECT_EQ(view.rows.size(), 1u);
    EXPECT_EQ(view.tree.size(), 2u); // the body and the untracked row
    EXPECT_EQ(view.checks.items[static_cast<std::size_t>(Profiling::CheckId::CaptureOn)].status,
        Profiling::CheckStatus::Warn);
}

TEST(ProfilerPanelDataTest, CheckAndBlockedLinesNameTheirStatusAndNumbers)
{
    Profiling::FrameCheck warn;
    warn.status = Profiling::CheckStatus::Warn;
    warn.value = 12;
    warn.text = "something";
    EXPECT_EQ(FormatCheckLine(warn), "[WARN] something (12)");

    Profiling::FrameCheck ok;
    ok.text = "fine";
    EXPECT_EQ(FormatCheckLine(ok), "[OK] fine");

    Profiling::FrameTotals totals;
    totals.blockedTicks = 3'200'000;
    totals.blockedGpuTicks = 2'900'000;
    totals.blockedWorkerTicks = 100'000;
    totals.blockedOtherTicks = 200'000;
    EXPECT_EQ(FormatBlockedLine(totals), "Blocked 3.20 ms (GPU 2.90, workers 0.10, other 0.20)");
}

// Text built for the panel never says N/A, never prints a number for a missing value and never prints NaN.
bool IsCleanText(const std::string& text)
{
    return text.find("N/A") == std::string::npos && text.find("nan") == std::string::npos
        && text.find("inf") == std::string::npos && text.find("-0.00") == std::string::npos;
}

bool HasDigit(const std::string& text)
{
    return std::any_of(text.begin(), text.end(), [](char c) { return c >= '0' && c <= '9'; });
}

Profiling::SpikeStore::Row SpikeRow(std::uint64_t frame, Profiling::SampleStatus gpuStatus)
{
    Profiling::SpikeStore::Row row;
    row.frameIndex = frame;
    row.frameMs = 50.0;
    row.medianMs = 16.0;
    row.pinned = frame % 2 == 0;
    row.classification.kind = frame % 3 == 0 ? Profiling::SpikeClass::Unexplained : Profiling::SpikeClass::Explained;
    row.classification.excessMs = 34.0;
    row.classification.dominantSelfMs = 30.0;
    row.classification.dominant = frame % 5 == 0 ? Profiling::kInvalidNameId : Profiling::InternName("PanelSpikeScope");
    row.classification.markerPresent = frame % 7 == 0;
    row.gpu.span = { 9.0f, gpuStatus, 0 };
    return row;
}

TEST(ProfilerPanelSpikeTest, EveryStatusFormatsWithoutNotApplicableOrFakeZeros)
{
    std::vector<Profiling::SpikeStore::Row> spikes;
    std::uint64_t frame = 1;
    for (const Profiling::SampleStatus status : { Profiling::SampleStatus::Present, Profiling::SampleStatus::Stale,
             Profiling::SampleStatus::Absent, Profiling::SampleStatus::Unsupported, Profiling::SampleStatus::Partial,
             Profiling::SampleStatus::Error, Profiling::SampleStatus::Inactive }) {
        spikes.push_back(SpikeRow(frame++, status));
    }

    const std::vector<SpikeListRow> rows = BuildSpikeListRows(spikes);
    ASSERT_EQ(rows.size(), spikes.size());
    for (std::size_t i = 0; i < rows.size(); ++i) {
        const SpikeListRow& row = rows[i];
        for (const std::string* text : { &row.frameTime, &row.overMedian, &row.classification, &row.cause, &row.gpu }) {
            EXPECT_TRUE(IsCleanText(*text)) << *text;
        }
        EXPECT_EQ(row.frameTime, "50.00 ms");
        EXPECT_EQ(row.overMedian, "34.00 ms over the 16.00 ms median");

        const Profiling::SampleStatus status = spikes[i].gpu.span.status;
        const bool hasValue = status == Profiling::SampleStatus::Present || status == Profiling::SampleStatus::Partial;
        EXPECT_EQ(HasDigit(row.gpu), hasValue) << row.gpu; // only a value-carrying status prints a number
    }
}

TEST(ProfilerPanelSpikeTest, TheCauseNamesTheDominantScopeOrAMarker)
{
    Profiling::SpikeStore::Row named = SpikeRow(1, Profiling::SampleStatus::Present);
    Profiling::SpikeStore::Row marked = SpikeRow(5, Profiling::SampleStatus::Present);
    marked.classification.markerPresent = true;
    Profiling::SpikeStore::Row nothing = SpikeRow(5, Profiling::SampleStatus::Present);
    nothing.classification.markerPresent = false;

    const std::vector<Profiling::SpikeStore::Row> spikes{ named, marked, nothing };
    const std::vector<SpikeListRow> rows = BuildSpikeListRows(spikes);
    EXPECT_EQ(rows[0].cause, "PanelSpikeScope");
    EXPECT_EQ(rows[1].cause, "marker");
    EXPECT_EQ(rows[2].cause, "-");
    EXPECT_TRUE(rows[0].explained);
}

TEST(ProfilerPanelSpikeTest, ThePinStateAndOrderAreKept)
{
    const std::vector<Profiling::SpikeStore::Row> spikes{
        SpikeRow(4, Profiling::SampleStatus::Present), SpikeRow(3, Profiling::SampleStatus::Present) };
    const std::vector<SpikeListRow> rows = BuildSpikeListRows(spikes);
    ASSERT_EQ(rows.size(), 2u);
    EXPECT_EQ(rows[0].frameIndex, 4u);
    EXPECT_TRUE(rows[0].pinned);
    EXPECT_FALSE(rows[1].pinned);
    EXPECT_TRUE(BuildSpikeListRows({}).empty());
}

TEST(ProfilerPanelSpikeTest, TheSummaryUsesTheMetricFormat)
{
    EXPECT_EQ(FormatSpikeSummary(3, 1), "Spikes: 3 frozen, 1 of 8 pinned");
}

TEST(ProfilerPanelOverheadTest, AnUnmeasuredCostSaysSo)
{
    EXPECT_EQ(FormatOverheadLine(Profiling::EstimateOverhead(0.0, 100, 16.0)), "Profiler overhead: not measured");
}

TEST(ProfilerPanelOverheadTest, TheEstimateIsLabelledAndWarnsAboveTheLimit)
{
    const std::string quiet = FormatOverheadLine(Profiling::EstimateOverhead(100.0, 2000, 20.0));
    EXPECT_EQ(quiet.find("[WARN]"), std::string::npos);
    EXPECT_NE(quiet.find("estimated 0.20 ms"), std::string::npos) << quiet;
    EXPECT_NE(quiet.find("1.0 %"), std::string::npos) << quiet;

    const std::string loud = FormatOverheadLine(Profiling::EstimateOverhead(1000.0, 2000, 20.0));
    EXPECT_EQ(loud.rfind("[WARN]", 0), 0u) << loud;
    EXPECT_NE(loud.find("3.0 %"), std::string::npos) << loud;
}

TEST(ProfilerPanelVerdictTest, ACaptureOffProfilerHasNoVerdictAndSaysHowManyFrames)
{
    Profiling::FrameProfiler& profiler = Profiling::FrameProfiler::Instance();
    profiler.ResetForTesting();
    profiler.SetCaptureEnabled(false);

    const ProfilerVerdictLine line = BuildVerdictLine(profiler, Profiling::ProfilerChecks{});
    EXPECT_EQ(line.kind, Profiling::VerdictKind::Unknown);
    EXPECT_EQ(line.text, "No verdict yet: 0 frames measured");
    EXPECT_TRUE(IsCleanText(line.text));
    profiler.SetCaptureEnabled(true);
}

} // namespace
} // namespace gte
