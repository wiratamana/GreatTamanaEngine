// Scene overlay lines shared by the Scene panel and the HTTP image.

#include "Editor/SceneOverlay.h"

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

CameraPickCandidate CameraAtOrigin()
{
    CameraPickCandidate candidate;
    candidate.id = 7;
    candidate.frustum.position = Vec3(0.0f, 0.0f, -3.0f);
    candidate.frustum.farZ = 4.0f;
    return candidate;
}

TEST(SceneOverlayTest, NoCandidatesDrawNothing)
{
    SceneOverlayInput input;
    input.viewProjection = SceneViewProjection();
    std::vector<OverlaySegment2D> segments;
    std::vector<OverlayQuad2D> quads;
    AppendSceneOverlay(input, Vec2(0.0f, 0.0f), Vec2(640.0f, 480.0f), segments, quads);
    EXPECT_TRUE(segments.empty());
    EXPECT_TRUE(quads.empty());
}

TEST(SceneOverlayTest, PanelAndImagePathsDrawTheSameLines)
{
    const std::vector<CameraPickCandidate> candidates{ CameraAtOrigin() };
    SceneOverlayInput input;
    input.candidates = candidates;
    input.selectedId = candidates.front().id;
    input.viewProjection = SceneViewProjection();

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

} // namespace
} // namespace gte
