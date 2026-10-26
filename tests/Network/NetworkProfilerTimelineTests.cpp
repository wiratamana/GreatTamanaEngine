// Tier 1: timeline picture geometry and PNG, the export document and the timeline replies.
#include "Network/NetworkProfilerRoutes.h"
#include "Network/NetworkProfilerTimeline.h"

#include <gtest/gtest.h>

#include <string>
#include <vector>

namespace gte::Network {
namespace {

using Profiling::SpikeDetail;
using Profiling::SpikeScopeRow;

SpikeDetail SampleFrame()
{
    SpikeDetail frame;
    frame.frameIndex = 7;
    frame.frameMs = 16.0;
    SpikeScopeRow root;
    root.name = "Frame";
    root.durationMs = 16.0;
    SpikeScopeRow wait = root;
    wait.name = "Wait";
    wait.startMs = 4.0;
    wait.durationMs = 6.0;
    wait.depth = 1;
    wait.kind = Profiling::ScopeKind::Blocked;
    frame.scopes = { root, wait };
    frame.gpu.views[static_cast<std::size_t>(Profiling::ViewSlot::Primary)] = { 5.0f, Profiling::SampleStatus::Present, 0 };
    return frame;
}

TEST(TimelineWidthTest, EmptyMeansDefaultAndTheRestIsBounded)
{
    EXPECT_EQ(ParseTimelineWidth(""), kTimelineWidthDefault);
    EXPECT_EQ(ParseTimelineWidth("800"), 800);
    EXPECT_FALSE(ParseTimelineWidth("199").has_value());
    EXPECT_FALSE(ParseTimelineWidth("2401").has_value());
    EXPECT_FALSE(ParseTimelineWidth("12px").has_value());
    EXPECT_FALSE(ParseTimelineWidth("-5").has_value());
}

TEST(TimelinePixelPlanTest, EveryQuadStaysInsideTheImageAndWindsPositive)
{
    const Profiling::TimelineLayout layout = Profiling::BuildTimelineLayout(SampleFrame());
    const TimelinePixelPlan plan = PlanTimelinePixels(layout, 600);
    EXPECT_EQ(plan.width, 600);
    EXPECT_GT(plan.height, 0);
    ASSERT_GE(plan.quads.size(), Profiling::kTimelineLaneCount + layout.bars.size());
    for (const OverlayQuad2D& quad : plan.quads) {
        for (const Vec2& p : quad.p) {
            EXPECT_GE(p.x, 0.0f);
            EXPECT_LE(p.x, static_cast<float>(plan.width));
            EXPECT_GE(p.y, 0.0f);
            EXPECT_LE(p.y, static_cast<float>(plan.height));
        }
        EXPECT_GT((quad.p[1].x - quad.p[0].x) * (quad.p[2].y - quad.p[1].y), 0.0f);
    }
}

TEST(TimelinePixelPlanTest, ABarThinnerThanAPixelStillShows)
{
    SpikeDetail frame;
    frame.frameMs = 1000.0;
    SpikeScopeRow tiny;
    tiny.durationMs = 0.01;
    frame.scopes = { tiny };
    const TimelinePixelPlan plan = PlanTimelinePixels(Profiling::BuildTimelineLayout(frame), 400);
    ASSERT_FALSE(plan.quads.empty());
    const OverlayQuad2D& bar = plan.quads.back();
    EXPECT_GE(bar.p[1].x - bar.p[0].x, 1.0f);
}

TEST(TimelinePngTest, TheBytesAreAPngOfThePlannedSize)
{
    const TimelinePixelPlan plan = PlanTimelinePixels(Profiling::BuildTimelineLayout(SampleFrame()), 300);
    const std::vector<std::uint8_t> png = RenderTimelinePng(plan);
    ASSERT_GT(png.size(), 24u);
    const std::uint8_t signature[8] = { 0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n' };
    for (int i = 0; i < 8; ++i) {
        EXPECT_EQ(png[i], signature[i]);
    }
    // IHDR width and height are big endian at bytes 16 and 20.
    const auto read32 = [&](std::size_t at) {
        return (static_cast<int>(png[at]) << 24) | (static_cast<int>(png[at + 1]) << 16) | (static_cast<int>(png[at + 2]) << 8)
            | static_cast<int>(png[at + 3]);
    };
    EXPECT_EQ(read32(16), plan.width);
    EXPECT_EQ(read32(20), plan.height);
}

TEST(TimelineReplyTest, ANewestFrameThatIsMissingAnswers503AndANamedOneAnswers404)
{
    const ProfilerImageReply newest = BuildProfilerTimelineReply(std::nullopt, false, 0, 600);
    EXPECT_EQ(newest.status, 503);
    EXPECT_TRUE(newest.contentType.empty());

    const ProfilerImageReply named = BuildProfilerTimelineReply(std::nullopt, true, 12, 600);
    EXPECT_EQ(named.status, 404);
    EXPECT_NE(named.body.find("12"), std::string::npos);
}

TEST(TimelineReplyTest, AFrameAnswersAPng)
{
    const ProfilerImageReply reply = BuildProfilerTimelineReply(SampleFrame(), false, 0, 400);
    EXPECT_EQ(reply.status, 200);
    EXPECT_EQ(reply.contentType, "image/png");
    ASSERT_GE(reply.body.size(), 8u);
    EXPECT_EQ(reply.body.substr(1, 3), "PNG");
}

TEST(ProfilerExportTest, CarriesTheFramesTheOverviewAndThePinnedSpikes)
{
    Profiling::ProfilerSnapshot snapshot;
    snapshot.hasFrame = true;
    snapshot.frameCount = 2;
    snapshot.frames[0].frameIndex = 1;
    snapshot.frames[1].frameIndex = 2;
    const std::vector<SpikeDetail> pinned{ SampleFrame() };

    const nlohmann::json out = ProfilerExportToJson(snapshot, 5, pinned);
    EXPECT_EQ(out["frames"].size(), 2u);
    EXPECT_TRUE(out.contains("overview"));
    EXPECT_TRUE(out["overview"].contains("timeline"));
    ASSERT_EQ(out["pinned_spikes"].size(), 1u);
    EXPECT_EQ(out["pinned_spikes"][0]["frame_index"], 7u);

    const nlohmann::json none = ProfilerExportToJson(snapshot, 1, {});
    EXPECT_EQ(none["frames"].size(), 1u);
    EXPECT_TRUE(none["pinned_spikes"].empty());
}

} // namespace
} // namespace gte::Network
