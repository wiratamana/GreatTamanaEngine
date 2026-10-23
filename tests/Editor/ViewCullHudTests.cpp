// Text lines of the Scene cull view HUD.

#include "Editor/ViewCullHud.h"

#include <gtest/gtest.h>

#include <string>

namespace gte {
namespace {

fx::ViewCullReport LiveReport()
{
    fx::ViewCullReport r;
    r.valid = true;
    r.rendered = true;
    r.featureEnabled = true;
    r.cullCameraValid = true;
    r.claimedInstances = 12;
    r.frame = 500;
    return r;
}

bool HasLine(const std::vector<ViewCullHudLine>& lines, const std::string& text)
{
    for (const ViewCullHudLine& line : lines) {
        if (line.text == text) {
            return true;
        }
    }
    return false;
}

TEST(ViewCullHudTest, ShowsCountsOfOneFrameWithPercent)
{
    fx::ViewCullReport r = LiveReport();
    r.visibleValid = true;
    r.testedAtVisibleFrame = 10905;
    r.visible = 6489;
    const std::vector<ViewCullHudLine> lines = FormatViewCullHud(r);
    EXPECT_EQ(lines.front().text, "Clusters: tested 10905, visible 6489 (59.5%), hidden 4416");
    EXPECT_TRUE(HasLine(lines, "Cull camera: Scene camera (live)"));
    EXPECT_TRUE(HasLine(lines, "Instances on the GPU path: 12"));
    EXPECT_LE(lines.size(), 12u);
}

TEST(ViewCullHudTest, NoDataYetIsNeverZero)
{
    const fx::ViewCullReport r = LiveReport();
    EXPECT_EQ(FormatViewCullHud(r).front().text, "Clusters: no data yet");
}

TEST(ViewCullHudTest, VisibleAboveTestedIsSettlingNotWrapped)
{
    fx::ViewCullReport r = LiveReport();
    r.visibleValid = true;
    r.testedAtVisibleFrame = 100;
    r.visible = 101;
    EXPECT_EQ(FormatViewCullHud(r).front().text, "Counts are settling");
}

TEST(ViewCullHudTest, ZeroTestedHasNoPercent)
{
    fx::ViewCullReport r = LiveReport();
    r.visibleValid = true;
    r.testedAtVisibleFrame = 0;
    r.visible = 0;
    EXPECT_EQ(FormatViewCullHud(r).front().text, "Clusters: tested 0, visible 0, hidden 0");
}

TEST(ViewCullHudTest, NamesWhyTheGpuPathIsInactive)
{
    fx::ViewCullReport r = LiveReport();
    r.featureEnabled = false;
    EXPECT_EQ(FormatViewCullHud(r).front().text, "GPU path inactive: feature disabled");

    r = LiveReport();
    r.inertReason = fx::ClusterInertReason::ToggledOff;
    EXPECT_EQ(FormatViewCullHud(r).front().text, "GPU path inactive: toggled off");

    r = LiveReport();
    r.refused = true;
    EXPECT_EQ(FormatViewCullHud(r).front().text, "GPU path inactive: refused, see the log");

    r = LiveReport();
    r.cullCameraValid = false;
    EXPECT_EQ(FormatViewCullHud(r).front().text, "GPU path inactive: camera is not valid");

    r = LiveReport();
    r.claimedInstances = 0;
    EXPECT_EQ(FormatViewCullHud(r).front().text, "No cluster meshes on the GPU path");
}

TEST(ViewCullHudTest, HizLineOnlyWhenHizIsChosen)
{
    fx::ViewCullReport r = LiveReport();
    r.mode = fx::ViewCullMode::Frustum;
    r.occlusion = fx::OcclusionState::Applied;
    EXPECT_FALSE(HasLine(FormatViewCullHud(r), "Hi-Z: active"));

    r.mode = fx::ViewCullMode::FrustumHiz;
    EXPECT_TRUE(HasLine(FormatViewCullHud(r), "Hi-Z: active"));

    r.occlusion = fx::OcclusionState::Unusable;
    r.hizReason = HizReason::CameraCut;
    EXPECT_TRUE(HasLine(FormatViewCullHud(r), "Hi-Z: not usable now (camera cut)"));

    r.occlusion = fx::OcclusionState::CameraNotLive;
    EXPECT_TRUE(HasLine(FormatViewCullHud(r), "Hi-Z: off, the cull camera is not the Scene camera"));
}

TEST(ViewCullHudTest, FrozenLineShowsFrameAndAge)
{
    fx::ViewCullReport r = LiveReport();
    r.frozen = true;
    r.frozenFrame = 190;
    const std::vector<ViewCullHudLine> lines = FormatViewCullHud(r);
    EXPECT_TRUE(HasLine(lines, "Frozen at frame 190 (age 310)"));
    EXPECT_FALSE(HasLine(lines, "Cull camera: Scene camera (live)"));

    r.frozenFrame = 900; // A capture newer than the report never gives a negative age.
    EXPECT_TRUE(HasLine(FormatViewCullHud(r), "Frozen at frame 900 (age 0)"));
}

TEST(ViewCullHudTest, AlwaysSaysCpuMeshesAreNotCounted)
{
    EXPECT_TRUE(HasLine(FormatViewCullHud(LiveReport()),
        "Only cluster meshes are counted. CPU-drawn meshes are never culled here."));
}

} // namespace
} // namespace gte
