// Panel text of the culling feature.

#include "Features/GpuDrivenScene/Editor/GpuDrivenScenePanelText.h"

#include <gtest/gtest.h>

#include <optional>
#include <string>
#include <vector>

namespace gte {
namespace {

GpuDrivenSceneSettings Hiz()
{
    GpuDrivenSceneSettings s;
    s.mode = GpuCullMode::HiZ;
    return s;
}

TEST(GpuDrivenScenePanelTextTest, EveryModeHasALine)
{
    for (const auto& entry : kCullModeTable) {
        EXPECT_FALSE(CullModeLine(entry.value).empty());
    }
}

TEST(GpuDrivenScenePanelTextTest, NoGpuPathLineOnlyForHizWithCpuMeshesAndNoClaims)
{
    GpuDrivenSceneViewSnapshot v;
    v.cpuTested = 5;
    v.gpu.claimedEntities = 0;
    EXPECT_FALSE(NoGpuPathLine(Hiz(), v).empty());

    GpuDrivenSceneSettings frustum;
    EXPECT_TRUE(NoGpuPathLine(frustum, v).empty());

    v.gpu.claimedEntities = 1;
    EXPECT_TRUE(NoGpuPathLine(Hiz(), v).empty());

    v.gpu.claimedEntities = 0;
    v.cpuTested = 0;
    EXPECT_TRUE(NoGpuPathLine(Hiz(), v).empty());
}

TEST(GpuDrivenScenePanelTextTest, CpuCullStateLines)
{
    GpuDrivenSceneViewSnapshot v;
    v.cpuCullState = CpuCullState::Active;
    EXPECT_TRUE(CpuCullStateLine(v).empty());
    v.cpuCullState = CpuCullState::SuppressedCapture;
    EXPECT_EQ(CpuCullStateLine(v), "CPU cull suppressed: frame capture");
    v.cpuCullState = CpuCullState::NoCamera;
    EXPECT_EQ(CpuCullStateLine(v), "CPU cull: no camera");
    v.cpuCullState = CpuCullState::Off;
    EXPECT_EQ(CpuCullStateLine(v), "CPU cull: off");
}

TEST(GpuDrivenScenePanelTextTest, HizLineSaysWhyHiZIsNotRunning)
{
    GpuDrivenSceneViewSnapshot v;
    v.gpu.occlusion = OcclusionState::Unusable;
    v.gpu.hizReason = HizReason::Unavailable;
    EXPECT_EQ(HizLine(v, true), "Hi-Z: primary view only (acts as Frustum)");
    EXPECT_EQ(HizLine(v, false), "Hi-Z: off (not available)");
    v.gpu.hizReason = HizReason::TooLarge;
    EXPECT_EQ(HizLine(v, false), "Hi-Z: off (view is too large)");

    v.gpu.occlusion = OcclusionState::Applied;
    EXPECT_EQ(HizLine(v, false), "Hi-Z: active");
    EXPECT_EQ(HizLine(v, true), "Hi-Z: primary view's survivors");
    v.gpu.occlusion = OcclusionState::NoCullCamera;
    EXPECT_EQ(HizLine(v, false), "Hi-Z: no cull camera");
    v.gpu.occlusion = OcclusionState::NotRequested;
    EXPECT_EQ(HizLine(v, false), "Hi-Z: off");
}

TEST(GpuDrivenScenePanelTextTest, CullCameraLine)
{
    GpuDrivenSceneViewSnapshot v;
    v.cullCamera = CullCameraSource::Own;
    EXPECT_EQ(CullCameraLine({}, v), "Cull camera: own");
    v.cullCamera = CullCameraSource::Primary;
    EXPECT_EQ(CullCameraLine({}, v), "Cull camera: game camera");
    v.cullCamera = CullCameraSource::None;
    EXPECT_EQ(CullCameraLine({}, v), "Cull camera: none (no active camera)");
    GpuDrivenSceneSettings off;
    off.mode = GpuCullMode::NoCull;
    EXPECT_EQ(CullCameraLine(off, v), "Cull camera: none (culling is off)");
}

TEST(GpuDrivenScenePanelTextTest, CountLines)
{
    GpuDrivenSceneViewSnapshot v;
    v.gpu.claimedEntities = 3;
    v.gpu.testedClusters = 40;
    EXPECT_EQ(GpuPathLine(v), "GPU path: 3 entities, 40 clusters tested, stats are off");
    v.gpu.statsCollected = true;
    EXPECT_EQ(GpuPathLine(v), "GPU path: 3 entities, 40 clusters tested, visible count not ready");
    v.gpu.visibleValid = true;
    v.gpu.visibleClusters = 12;
    EXPECT_EQ(GpuPathLine(v), "GPU path: 3 entities, 40 clusters tested, 12 visible");
    v.gpu.sharedSurvivors = true;
    EXPECT_EQ(GpuPathLine(v), "GPU path: 3 entities, 40 clusters (primary view's survivors), 12 visible");

    v.cpuTested = 7;
    v.cpuCulled = 2;
    v.cpuUnbounded = 1;
    EXPECT_EQ(CpuPathLine(v), "CPU path: 7 tested, 2 culled, 1 kept without bounds (last frame)");
}

TEST(GpuDrivenScenePanelTextTest, CpuReasonsLineNamesOnlyTheNonZeroReasons)
{
    GpuDrivenSceneViewSnapshot v;
    EXPECT_TRUE(CpuReasonsLine(v).empty());
    v.gpu.cpuPath.skinned = 2;
    v.gpu.cpuPath.noSlot = 5;
    const std::string line = CpuReasonsLine(v);
    EXPECT_EQ(line, "On the CPU path (no Hi-Z): 5 without a free part slot, 2 skinned");
}

TEST(GpuDrivenScenePanelTextTest, HelpLineMentionsMillisecondsAndNoVsync)
{
    const std::string_view help = CullHelpLine();
    EXPECT_NE(help.find("milliseconds"), std::string_view::npos);
    EXPECT_NE(help.find("--no-vsync"), std::string_view::npos);
}

TEST(GpuDrivenScenePanelTextTest, ReasonsLineNeedsStatsAndShowsTheCounters)
{
    GpuDrivenSceneViewSnapshot v;
    EXPECT_NE(ReasonsLine(v).find("enable stats"), std::string::npos);
    v.gpu.statsCollected = true;
    EXPECT_EQ(ReasonsLine(v), "Reasons: not ready");
    v.gpu.reasons.valid = true;
    v.gpu.reasons.frustum = 7;
    v.gpu.reasons.cone = 1;
    v.gpu.reasons.pyramid = 5;
    v.gpu.reasons.kept = 12;
    v.gpu.reasons.history = 9;
    EXPECT_EQ(ReasonsLine(v), "Rejected by frustum 7, cone 1, pyramid 5; kept 12 (first phase drew 9 from history)");
}

TEST(GpuDrivenScenePanelTextTest, InertLineNamesTheReasonOrStaysEmpty)
{
    GpuDrivenSceneViewSnapshot v;
    GpuDrivenSceneSettings s;
    EXPECT_TRUE(InertLine(s, v).empty());

    v.gpu.inertReason = fx::ClusterInertReason::CaptureArmed;
    EXPECT_NE(InertLine(s, v).find("capture"), std::string::npos);
    v.gpu.inertReason = fx::ClusterInertReason::ToggledOff;
    EXPECT_NE(InertLine(s, v).find("toggled off"), std::string::npos);
    v.gpu.inertReason = fx::ClusterInertReason::DeviceCaps;
    EXPECT_NE(InertLine(s, v).find("multi draw indirect"), std::string::npos);

    // Idle: CPU meshes exist, nothing was claimed, no reason was reported.
    v.gpu.inertReason = fx::ClusterInertReason::None;
    v.cpuTested = 3;
    EXPECT_NE(InertLine(s, v).find("no mesh qualified"), std::string::npos);

    // A refusal text or a switched-off path already explains itself.
    v.gpu.refusal = "disabled";
    EXPECT_TRUE(InertLine(s, v).empty());
    v.gpu.refusal.clear();
    s.enabled = false;
    EXPECT_TRUE(InertLine(s, v).empty());
}

TEST(GpuDrivenScenePanelTextTest, SkippedPartsLineShowsOnlyWhenPartsWereSkipped)
{
    GpuDrivenSceneViewSnapshot v;
    EXPECT_TRUE(SkippedPartsLine(v).empty());
    v.gpu.skippedParts = 2;
    EXPECT_EQ(SkippedPartsLine(v), "Parts skipped (outside view): 2");
}

TEST(GpuDrivenScenePanelTextTest, SkippedInstancesCountAsOnTheGpuPath)
{
    GpuDrivenSceneViewSnapshot v;
    v.cpuTested = 5;
    v.gpu.claimedEntities = 100; // All of them belong to skipped parts.
    v.gpu.skippedParts = 1;
    v.gpu.skippedInstances = 100;
    EXPECT_TRUE(NoGpuPathLine(Hiz(), v).empty());
    EXPECT_TRUE(InertLine(GpuDrivenSceneSettings{}, v).empty());
}

TEST(GpuDrivenScenePanelTextTest, HizLineSaysNothingInView)
{
    GpuDrivenSceneViewSnapshot v;
    v.gpu.occlusion = OcclusionState::Unusable;
    v.gpu.hizReason = HizReason::NothingInView;
    EXPECT_EQ(HizLine(v, false), "Hi-Z: nothing in view");
}

TEST(GpuDrivenScenePanelTextTest, CullTimeSplitGroupsByViewAndOwnerAndNeverInventsZero)
{
    const std::string_view cull = "GpuDrivenScene";
    const std::vector<CullTimeRow> rows = {
        { CullTimeView::Primary, "GpuDrivenScene", false, 0.5 },
        { CullTimeView::Primary, "Shadow", false, 4.0 },
        { CullTimeView::Primary, "Shadow", true, std::nullopt }, // Culled: ignored.
        { CullTimeView::Secondary, "GpuDrivenScene", false, 0.25 },
        { CullTimeView::Secondary, "Ssgi", false, std::nullopt }, // Not measured.
        { CullTimeView::Other, "Atmosphere", false, 9.0 },        // Shared: not a view.
    };
    const CullTimeSplits splits = SplitCullTime(rows, cull);

    ASSERT_TRUE(splits.primary.total.has_value());
    EXPECT_DOUBLE_EQ(*splits.primary.total, 4.5);
    EXPECT_DOUBLE_EQ(*splits.primary.cull, 0.5);
    EXPECT_DOUBLE_EQ(*splits.primary.other, 4.0);
    EXPECT_EQ(splits.primary.passes, 2u);

    // One pass of the secondary view was not measured: no total and no remainder, but the cull part is known.
    EXPECT_FALSE(splits.secondary.total.has_value());
    EXPECT_FALSE(splits.secondary.other.has_value());
    ASSERT_TRUE(splits.secondary.cull.has_value());
    EXPECT_DOUBLE_EQ(*splits.secondary.cull, 0.25);
    EXPECT_EQ(splits.secondary.passes, 2u);
    EXPECT_EQ(splits.secondary.measured, 1u);
}

TEST(GpuDrivenScenePanelTextTest, CullTimeSplitIsEmptyWithoutPasses)
{
    const CullTimeSplits splits = SplitCullTime({}, "GpuDrivenScene");
    EXPECT_FALSE(splits.primary.total.has_value());
    EXPECT_FALSE(splits.primary.cull.has_value());
    EXPECT_FALSE(splits.secondary.other.has_value());
}

TEST(GpuDrivenScenePanelTextTest, CullTimeLineShowsADashForWhatWasNotMeasured)
{
    CullTimeSplit split;
    EXPECT_EQ(CullTimeLine(split), "GPU time: total - ms, culling feature - ms, everything else - ms");
    split.total = 12.3;
    split.cull = 0.4;
    split.other = 11.9;
    EXPECT_EQ(CullTimeLine(split), "GPU time: total 12.30 ms, culling feature 0.40 ms, everything else 11.90 ms");
}

} // namespace
} // namespace gte
