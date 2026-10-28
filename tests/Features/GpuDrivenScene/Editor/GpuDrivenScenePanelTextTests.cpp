// Panel text of the culling feature.

#include "Features/GpuDrivenScene/Editor/GpuDrivenScenePanelText.h"

#include <gtest/gtest.h>

namespace gte {
namespace {

GpuDrivenSceneSettings Hiz()
{
    GpuDrivenSceneSettings s;
    s.mode = CullMode::HiZ;
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
    off.mode = CullMode::NoCull;
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

} // namespace
} // namespace gte
