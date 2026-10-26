// Text lines and pure request logic of the Scene cull view.

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
    r.cameraValid = true;
    r.cullCameraValid = true;
    r.claimedInstances = 12;
    r.frame = 500;
    return r;
}

// Counts as the feature fills them: hidden only when the visible count fits the tested count.
fx::ViewCullReport WithCounts(fx::ViewCullReport r, std::uint64_t tested, std::uint32_t visible)
{
    r.visibleValid = true;
    r.testedAtVisibleFrame = tested;
    r.visible = visible;
    r.hiddenValid = visible <= tested;
    r.hidden = r.hiddenValid ? tested - visible : 0;
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
    const fx::ViewCullReport r = WithCounts(LiveReport(), 10905, 6489);
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
    const fx::ViewCullReport r = WithCounts(LiveReport(), 100, 101);
    EXPECT_EQ(FormatViewCullHud(r).front().text, "Counts are settling");
}

TEST(ViewCullHudTest, ValidVisibleWithoutHiddenIsSettling)
{
    fx::ViewCullReport r = LiveReport();
    r.visibleValid = true;
    r.hiddenValid = false;
    EXPECT_EQ(FormatViewCullHud(r).front().text, "Counts are settling");
}

TEST(ViewCullHudTest, ZeroTestedHasNoPercent)
{
    const fx::ViewCullReport r = WithCounts(LiveReport(), 0, 0);
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
    r.cameraValid = false;
    EXPECT_EQ(FormatViewCullHud(r).front().text, "GPU path inactive: camera is not valid");

    r = LiveReport();
    r.claimedInstances = 0;
    EXPECT_EQ(FormatViewCullHud(r).front().text, "No cluster meshes on the GPU path");
}

TEST(ViewCullHudTest, InvalidLiveCameraWinsOverAFrozenCullCamera)
{
    fx::ViewCullReport r = LiveReport();
    r.cameraValid = false;
    r.cullCameraValid = true; // A frozen camera exists, but nothing was culled.
    EXPECT_EQ(FormatViewCullHud(r).front().text, "GPU path inactive: camera is not valid");
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

TEST(ViewCullHudTest, FrozenSourceWithoutCaptureWaits)
{
    fx::ViewCullReport r = LiveReport();
    r.source = fx::ViewCullSource::Frozen;
    r.frozen = false;
    const std::vector<ViewCullHudLine> lines = FormatViewCullHud(r);
    EXPECT_TRUE(HasLine(lines, "Cull camera: Frozen, waiting for a valid camera"));
    EXPECT_FALSE(HasLine(lines, "Cull camera: Scene camera (live)"));
}

TEST(ViewCullHudTest, AlwaysSaysCpuMeshesAreNotCounted)
{
    EXPECT_TRUE(HasLine(FormatViewCullHud(LiveReport()),
        "Only cluster meshes are counted. CPU-drawn meshes are never culled here."));
}

TEST(ViewCullReportIsDefaultTest, EachFieldBreaksTheDefault)
{
    fx::ViewCullReport r;
    EXPECT_TRUE(ViewCullReportIsDefault(r));

    r.choice = fx::ViewCullChoice::Frustum;
    EXPECT_FALSE(ViewCullReportIsDefault(r));

    r = fx::ViewCullReport{};
    r.source = fx::ViewCullSource::Frozen;
    EXPECT_FALSE(ViewCullReportIsDefault(r));

    r = fx::ViewCullReport{};
    r.cone = true;
    EXPECT_FALSE(ViewCullReportIsDefault(r));
}

TEST(BeginViewCullChangeTest, CopiesTheReportWhenNothingIsPending)
{
    fx::ViewCullLink link;
    link.report.choice = fx::ViewCullChoice::Frustum;
    link.report.cone = true;
    link.report.source = fx::ViewCullSource::Frozen;

    fx::ViewCullRequest& request = BeginViewCullChange(link);
    EXPECT_TRUE(request.pending);
    EXPECT_EQ(request.choice, fx::ViewCullChoice::Frustum);
    EXPECT_TRUE(request.cone);
    EXPECT_EQ(request.source, fx::ViewCullSource::Frozen);
    EXPECT_FALSE(request.resetAll);
}

TEST(BeginViewCullChangeTest, ContinuesAPendingChangeAndKeepsFreeze)
{
    fx::ViewCullLink link;
    link.report.choice = fx::ViewCullChoice::Off;
    BeginViewCullChange(link).freezeHere = true;
    link.report.choice = fx::ViewCullChoice::Frustum; // A later report must not overwrite the pending fields.

    fx::ViewCullRequest& request = BeginViewCullChange(link);
    EXPECT_EQ(request.choice, fx::ViewCullChoice::Off);
    EXPECT_TRUE(request.freezeHere);
}

TEST(BeginViewCullChangeTest, ChangeAfterAPendingResetStartsFromDefaults)
{
    fx::ViewCullLink link;
    link.report.choice = fx::ViewCullChoice::FrustumHiz;
    link.report.cone = true;
    link.report.source = fx::ViewCullSource::Frozen;
    link.request.pending = true;
    link.request.resetAll = true;
    link.request.freezeHere = true;

    fx::ViewCullRequest& request = BeginViewCullChange(link);
    request.cone = true;
    EXPECT_TRUE(request.pending);
    EXPECT_FALSE(request.resetAll);
    EXPECT_FALSE(request.freezeHere);
    EXPECT_EQ(request.choice, fx::ViewCullChoice::FollowSettings);
    EXPECT_EQ(request.source, fx::ViewCullSource::Live);
    EXPECT_TRUE(request.cone);
}

TEST(ViewCullFreshnessTest, FirstCallIsNeverStale)
{
    ViewCullFreshness f;
    EXPECT_FALSE(f.Update(7, 100));
}

TEST(ViewCullFreshnessTest, MovingFrameIsNeverStale)
{
    ViewCullFreshness f;
    for (std::uint64_t ui = 10; ui < 60; ++ui) {
        EXPECT_FALSE(f.Update(ui * 2, ui));
    }
}

TEST(ViewCullFreshnessTest, StandingFrameIsStaleAfterFiveUiFrames)
{
    ViewCullFreshness f;
    EXPECT_FALSE(f.Update(5, 10));
    for (std::uint64_t ui = 11; ui <= 14; ++ui) {
        EXPECT_FALSE(f.Update(5, ui));
    }
    EXPECT_TRUE(f.Update(5, 15));
    EXPECT_TRUE(f.Update(5, 16));
    EXPECT_FALSE(f.Update(6, 17)); // The report moved again.
}

TEST(ViewCullFreshnessTest, GapInUiFramesStartsFresh)
{
    ViewCullFreshness f;
    EXPECT_FALSE(f.Update(5, 10));
    EXPECT_FALSE(f.Update(5, 11));
    EXPECT_FALSE(f.Update(5, 500)); // The panel was hidden in between.
    EXPECT_FALSE(f.Update(5, 501));
}

TEST(ViewCullChoiceOptionsTest, ListsFourEntriesAndAllowHizOnALiveSource)
{
    const auto options = ViewCullChoiceOptions(LiveReport());
    ASSERT_EQ(options.size(), 4u);
    EXPECT_EQ(options[0].choice, fx::ViewCullChoice::FollowSettings);
    EXPECT_EQ(options[1].choice, fx::ViewCullChoice::Off);
    EXPECT_EQ(options[2].choice, fx::ViewCullChoice::Frustum);
    EXPECT_EQ(options[3].choice, fx::ViewCullChoice::FrustumHiz);
    for (const ViewCullChoiceOption& option : options) {
        EXPECT_TRUE(option.enabled);
    }
}

TEST(ViewCullChoiceOptionsTest, HizIsDisabledWhileFrozen)
{
    fx::ViewCullReport r = LiveReport();
    r.source = fx::ViewCullSource::Frozen;
    const auto options = ViewCullChoiceOptions(r);
    EXPECT_TRUE(options[0].enabled);
    EXPECT_TRUE(options[1].enabled);
    EXPECT_TRUE(options[2].enabled);
    EXPECT_FALSE(options[3].enabled);
}

} // namespace
} // namespace gte
