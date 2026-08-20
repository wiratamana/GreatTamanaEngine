// Unit tests for EditorCamera (src/Editor/EditorCamera.h) - deliberately
// pure logic with no ImGui/SDL/Vulkan dependency at all (unlike most of
// src/Editor/), so it is Tier-1-testable exactly like the rest of the
// engine's math/ECS coverage (see AGENTS.md, "Testability & Regression
// Safety"). Only actually compiled/linked when GTE_ENABLE_EDITOR is ON,
// since EditorCamera itself is only ever compiled into gte_core then (see
// the root CMakeLists.txt's "Editor Module Structure" - the whole
// src/Editor/ folder, not just ImGui-touching files, is gated on it) -
// see tests/CMakeLists.txt.

#include "Editor/EditorCamera.h"

#include <gtest/gtest.h>

namespace gte {
namespace {

TEST(EditorCameraTest, DefaultsMatchGameDemoSceneCameraSoSceneIsNeverBlank)
{
    const EditorCamera camera;

    EXPECT_TRUE(ApproximatelyEqual(camera.GetTransform().position, Vec3{ 0.0f, 0.0f, -5.0f }));
    EXPECT_TRUE(RepresentSameRotation(camera.GetTransform().rotation, Quat::Identity()));
    EXPECT_TRUE(ApproximatelyEqual(camera.YawDegrees(), 0.0f));
    EXPECT_TRUE(ApproximatelyEqual(camera.PitchDegrees(), 0.0f));
}

TEST(EditorCameraTest, UpdateWithNoButtonsAndNoScrollDoesNotMoveTheCamera)
{
    EditorCamera camera;
    const Vec3 before = camera.GetTransform().position;

    camera.Update(Vec2{ 25.0f, 25.0f }, /*scrollDelta=*/0.0f, /*middleMouseDown=*/false, /*rightMouseDown=*/false);

    EXPECT_TRUE(ApproximatelyEqual(camera.GetTransform().position, before));
    EXPECT_TRUE(ApproximatelyEqual(camera.YawDegrees(), 0.0f));
    EXPECT_TRUE(ApproximatelyEqual(camera.PitchDegrees(), 0.0f));
}

TEST(EditorCameraTest, MiddleMouseDragPansAlongLocalRightAndUpWithIdentityRotation)
{
    EditorCamera camera; // Identity rotation - local axes match world axes.
    const Vec3 start = camera.GetTransform().position;

    // Dragging right (dx>0) should make the camera move LEFT (see
    // EditorCamera.cpp's "grab and drag" rationale); dragging down (dy>0,
    // screen-space) should make the camera move UP.
    camera.Update(Vec2{ 10.0f, 10.0f }, /*scrollDelta=*/0.0f, /*middleMouseDown=*/true, /*rightMouseDown=*/false);

    const Vec3 expectedDelta = Vec3::Right() * -0.1f + Vec3::Up() * 0.1f; // 10px * 0.01 units/px
    EXPECT_TRUE(ApproximatelyEqual(camera.GetTransform().position, start + expectedDelta));
    // Panning must never affect orientation.
    EXPECT_TRUE(ApproximatelyEqual(camera.YawDegrees(), 0.0f));
    EXPECT_TRUE(ApproximatelyEqual(camera.PitchDegrees(), 0.0f));
}

TEST(EditorCameraTest, MouseWheelDollysAlongLocalForwardWithIdentityRotation)
{
    EditorCamera camera;
    const Vec3 start = camera.GetTransform().position;

    camera.Update(Vec2{ 0.0f, 0.0f }, /*scrollDelta=*/2.0f, /*middleMouseDown=*/false, /*rightMouseDown=*/false);

    // Forward() is +Z with identity rotation; 2 notches * 0.5 units/notch.
    EXPECT_TRUE(ApproximatelyEqual(camera.GetTransform().position, start + Vec3::Forward() * 1.0f));
}

TEST(EditorCameraTest, RightMouseDragYawsTowardRightAndPitchesDownward)
{
    EditorCamera camera;

    // 10px * 0.2 degrees/px = 2 degrees on each axis.
    camera.Update(Vec2{ 10.0f, 10.0f }, /*scrollDelta=*/0.0f, /*middleMouseDown=*/false, /*rightMouseDown=*/true);

    EXPECT_TRUE(ApproximatelyEqual(camera.YawDegrees(), 2.0f));
    EXPECT_TRUE(ApproximatelyEqual(camera.PitchDegrees(), 2.0f));
    EXPECT_TRUE(RepresentSameRotation(camera.GetTransform().rotation, Quat::FromEulerDegrees(2.0f, 2.0f, 0.0f)));

    // Positive yaw turns Forward() toward Right() (this engine's documented
    // convention - see Quat.h/tests/Math/QuatTests.cpp); positive pitch
    // tilts it downward (negative Y) - dragging right+down should do both.
    const Vec3 forward = camera.GetTransform().rotation.RotateVector(Vec3::Forward());
    EXPECT_GT(forward.x, 0.0f);
    EXPECT_LT(forward.y, 0.0f);
}

TEST(EditorCameraTest, RightMouseDragDoesNotPanOrDolly)
{
    EditorCamera camera;
    const Vec3 start = camera.GetTransform().position;

    camera.Update(Vec2{ 50.0f, 50.0f }, /*scrollDelta=*/0.0f, /*middleMouseDown=*/false, /*rightMouseDown=*/true);

    EXPECT_TRUE(ApproximatelyEqual(camera.GetTransform().position, start));
}

TEST(EditorCameraTest, PitchIsClampedJustShortOfNinetyDegreesAndNeverFlipsUpsideDown)
{
    EditorCamera camera;

    // Drag downward repeatedly, far past what would be +90 degrees of pitch
    // if unclamped (50 updates * 50px * 0.2 deg/px = 500 degrees).
    for (int i = 0; i < 50; ++i) {
        camera.Update(Vec2{ 0.0f, 50.0f }, /*scrollDelta=*/0.0f, /*middleMouseDown=*/false, /*rightMouseDown=*/true);
    }

    EXPECT_LE(camera.PitchDegrees(), 89.0f);
    EXPECT_GT(camera.PitchDegrees(), 88.0f); // Clamped at the limit, not stuck at some smaller value.

    // Same clamp in the other direction.
    EditorCamera pitchedUp;
    for (int i = 0; i < 50; ++i) {
        pitchedUp.Update(Vec2{ 0.0f, -50.0f }, /*scrollDelta=*/0.0f, /*middleMouseDown=*/false, /*rightMouseDown=*/true);
    }
    EXPECT_GE(pitchedUp.PitchDegrees(), -89.0f);
    EXPECT_LT(pitchedUp.PitchDegrees(), -88.0f);
}

TEST(EditorCameraTest, ViewProjectionMatchesCamerasOwnProjectionAndViewMatrixHelpers)
{
    EditorCamera camera;
    camera.Update(Vec2{ 30.0f, -15.0f }, /*scrollDelta=*/1.0f, /*middleMouseDown=*/true, /*rightMouseDown=*/true);

    const float aspect = 16.0f / 9.0f;
    const Mat4 actual = camera.ViewProjection(aspect);

    const Camera plainCamera{}; // Same defaults EditorCamera itself uses internally.
    const Mat4 expected = plainCamera.ProjectionMatrix(aspect) * Camera::ViewMatrix(camera.GetTransform());

    EXPECT_TRUE(ApproximatelyEqual(actual, expected));
}

} // namespace
} // namespace gte
