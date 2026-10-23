// Scene overlay lines shared by the Scene panel and the HTTP image.

#include "Editor/ViewCullOverlay.h"

#include "Math/MathTypes.h"

#include <gtest/gtest.h>

#include <vector>

namespace gte {
namespace {

Mat4 SceneViewProjection()
{
    const Mat4 view = Mat4::LookAtLH(Vec3(0.0f, 0.0f, -10.0f), Vec3(0.0f, 0.0f, 0.0f), Vec3::Up());
    return Mat4::PerspectiveFovLH_ZO(DegToRad(60.0f), 1.0f, 0.1f, 200.0f, true) * view;
}

// A frozen cull camera a few units in front of the Scene camera.
fx::ViewCullReport FrozenReport()
{
    const Mat4 view = Mat4::LookAtLH(Vec3(0.0f, 0.0f, -4.0f), Vec3(0.0f, 0.0f, 0.0f), Vec3::Up());
    fx::ViewCullReport r;
    r.valid = true;
    r.rendered = true;
    r.frozen = true;
    r.cullCameraValid = true;
    r.cullViewProjection = Mat4::PerspectiveFovLH_ZO(DegToRad(40.0f), 1.0f, 0.5f, 6.0f, true) * view;
    return r;
}

std::vector<OverlaySegment2D> Outline(const fx::ViewCullReport& report, Vec2 rectMin, Vec2 rectMax)
{
    std::vector<OverlaySegment2D> segments;
    AppendCullOutlineOverlay(report, SceneViewProjection(), rectMin, rectMax, segments);
    return segments;
}

TEST(ViewCullOverlayTest, FrozenCameraDrawsAtMostTwelveCyanSegments)
{
    const std::vector<OverlaySegment2D> segments = Outline(FrozenReport(), Vec2(0.0f, 0.0f), Vec2(800.0f, 600.0f));
    EXPECT_GT(segments.size(), 0u);
    EXPECT_LE(segments.size(), 12u);
    for (const OverlaySegment2D& s : segments) {
        EXPECT_EQ(s.rgba, kSceneCameraViewerColor);
    }
}

TEST(ViewCullOverlayTest, NothingWithoutAFrozenValidReport)
{
    const Vec2 min(0.0f, 0.0f);
    const Vec2 max(800.0f, 600.0f);

    fx::ViewCullReport r = FrozenReport();
    r.frozen = false;
    EXPECT_TRUE(Outline(r, min, max).empty());

    r = FrozenReport();
    r.rendered = false;
    EXPECT_TRUE(Outline(r, min, max).empty());

    r = FrozenReport();
    r.cullCameraValid = false;
    EXPECT_TRUE(Outline(r, min, max).empty());

    r = FrozenReport();
    r.cullViewProjection = Mat4::Zero();
    EXPECT_TRUE(Outline(r, min, max).empty());
}

TEST(ViewCullOverlayTest, PanelAndImagePathsDrawTheSameLines)
{
    const fx::ViewCullReport report = FrozenReport();
    SceneOverlayInput input;
    input.viewProjection = SceneViewProjection();
    input.cull = &report;

    std::vector<OverlaySegment2D> image;
    std::vector<OverlayQuad2D> imageQuads;
    AppendSceneOverlay(input, Vec2(0.0f, 0.0f), Vec2(640.0f, 480.0f), image, imageQuads);

    const Vec2 offset(300.0f, 40.0f); // The panel draws the same image somewhere on screen.
    std::vector<OverlaySegment2D> panel;
    std::vector<OverlayQuad2D> panelQuads;
    AppendSceneOverlay(input, offset, offset + Vec2(640.0f, 480.0f), panel, panelQuads);

    ASSERT_EQ(image.size(), panel.size());
    ASSERT_GT(image.size(), 0u);
    for (std::size_t i = 0; i < image.size(); ++i) {
        EXPECT_NEAR(image[i].a.x + offset.x, panel[i].a.x, 1e-3f);
        EXPECT_NEAR(image[i].b.y + offset.y, panel[i].b.y, 1e-3f);
    }
}

TEST(ViewCullOverlayTest, NoReportLeavesOnlyCameraBodies)
{
    SceneOverlayInput input;
    input.viewProjection = SceneViewProjection();
    std::vector<OverlaySegment2D> segments;
    std::vector<OverlayQuad2D> quads;
    AppendSceneOverlay(input, Vec2(0.0f, 0.0f), Vec2(640.0f, 480.0f), segments, quads);
    EXPECT_TRUE(segments.empty());
    EXPECT_TRUE(quads.empty());
}

} // namespace
} // namespace gte
