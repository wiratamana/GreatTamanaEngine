// Tier 1 tests for EditorCamera: pan, look, dolly and SetPose. No ImGui, SDL or Vulkan.

#include "Editor/EditorCamera.h"

#include <gtest/gtest.h>

#include <cmath>
#include <limits>

namespace gte {
namespace {

TEST(EditorCameraTest, DefaultsLookAlongPlusZFromMinusFive)
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

TEST(EditorCameraTest, MiddleMouseDragPansAlongLocalRightAndUp)
{
    EditorCamera camera;
    const Vec3 start = camera.GetTransform().position;

    // Dragging right moves the camera left; dragging down moves it up.
    camera.Update(Vec2{ 10.0f, 10.0f }, /*scrollDelta=*/0.0f, /*middleMouseDown=*/true, /*rightMouseDown=*/false);

    const Vec3 expectedDelta = Vec3::Right() * -0.1f + Vec3::Up() * 0.1f;
    EXPECT_TRUE(ApproximatelyEqual(camera.GetTransform().position, start + expectedDelta));
    EXPECT_TRUE(ApproximatelyEqual(camera.YawDegrees(), 0.0f));
    EXPECT_TRUE(ApproximatelyEqual(camera.PitchDegrees(), 0.0f));
}

TEST(EditorCameraTest, MouseWheelDollysAlongLocalForward)
{
    EditorCamera camera;
    const Vec3 start = camera.GetTransform().position;

    camera.Update(Vec2{ 0.0f, 0.0f }, /*scrollDelta=*/2.0f, /*middleMouseDown=*/false, /*rightMouseDown=*/false);

    EXPECT_TRUE(ApproximatelyEqual(camera.GetTransform().position, start + Vec3::Forward() * 1.0f));
}

TEST(EditorCameraTest, RightMouseDragYawsTowardRightAndPitchesDownward)
{
    EditorCamera camera;

    camera.Update(Vec2{ 10.0f, 10.0f }, /*scrollDelta=*/0.0f, /*middleMouseDown=*/false, /*rightMouseDown=*/true);

    EXPECT_TRUE(ApproximatelyEqual(camera.YawDegrees(), 2.0f));
    EXPECT_TRUE(ApproximatelyEqual(camera.PitchDegrees(), 2.0f));
    EXPECT_TRUE(RepresentSameRotation(camera.GetTransform().rotation, Quat::FromEulerDegrees(2.0f, 2.0f, 0.0f)));

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

TEST(EditorCameraTest, PitchIsClampedJustShortOfNinetyDegrees)
{
    EditorCamera down;
    for (int i = 0; i < 50; ++i) {
        down.Update(Vec2{ 0.0f, 50.0f }, 0.0f, false, true);
    }
    EXPECT_LE(down.PitchDegrees(), 89.0f);
    EXPECT_GT(down.PitchDegrees(), 88.0f);

    EditorCamera up;
    for (int i = 0; i < 50; ++i) {
        up.Update(Vec2{ 0.0f, -50.0f }, 0.0f, false, true);
    }
    EXPECT_GE(up.PitchDegrees(), -89.0f);
    EXPECT_LT(up.PitchDegrees(), -88.0f);
}

TEST(EditorCameraTest, ViewProjectionMatchesCameraHelpers)
{
    EditorCamera camera;
    camera.Update(Vec2{ 30.0f, -15.0f }, /*scrollDelta=*/1.0f, /*middleMouseDown=*/true, /*rightMouseDown=*/true);

    const float aspect = 16.0f / 9.0f;
    const Camera plainCamera{};
    const Mat4 expected = plainCamera.ProjectionMatrix(aspect) * Camera::ViewMatrix(camera.GetTransform());

    EXPECT_TRUE(ApproximatelyEqual(camera.ViewProjection(aspect), expected));
}

TEST(EditorCameraSetPoseTest, RoundTripsPositionYawAndPitch)
{
    EditorCamera camera;

    ASSERT_TRUE(camera.SetPose(Vec3{ 1.0f, 2.0f, 3.0f }, 30.0f, 10.0f));

    EXPECT_TRUE(ApproximatelyEqual(camera.GetTransform().position, Vec3{ 1.0f, 2.0f, 3.0f }));
    EXPECT_TRUE(ApproximatelyEqual(camera.YawDegrees(), 30.0f));
    EXPECT_TRUE(ApproximatelyEqual(camera.PitchDegrees(), 10.0f));
    EXPECT_TRUE(RepresentSameRotation(camera.GetTransform().rotation, Quat::FromEulerDegrees(10.0f, 30.0f, 0.0f)));
}

TEST(EditorCameraSetPoseTest, ClampsPitchAndWrapsYaw)
{
    EditorCamera camera;

    ASSERT_TRUE(camera.SetPose(Vec3{}, 0.0f, 100.0f));
    EXPECT_TRUE(ApproximatelyEqual(camera.PitchDegrees(), 89.0f));

    ASSERT_TRUE(camera.SetPose(Vec3{}, 0.0f, -100.0f));
    EXPECT_TRUE(ApproximatelyEqual(camera.PitchDegrees(), -89.0f));

    ASSERT_TRUE(camera.SetPose(Vec3{}, 370.0f, 0.0f));
    EXPECT_TRUE(ApproximatelyEqual(camera.YawDegrees(), 10.0f));

    ASSERT_TRUE(camera.SetPose(Vec3{}, -350.0f, 0.0f));
    EXPECT_TRUE(ApproximatelyEqual(camera.YawDegrees(), 10.0f));
}

TEST(EditorCameraSetPoseTest, RejectsBadInputAndKeepsThePose)
{
    EditorCamera camera;
    ASSERT_TRUE(camera.SetPose(Vec3{ 4.0f, 5.0f, 6.0f }, 20.0f, 15.0f));

    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float inf = std::numeric_limits<float>::infinity();

    EXPECT_FALSE(camera.SetPose(Vec3{ nan, 0.0f, 0.0f }, 0.0f, 0.0f));
    EXPECT_FALSE(camera.SetPose(Vec3{ 0.0f, inf, 0.0f }, 0.0f, 0.0f));
    EXPECT_FALSE(camera.SetPose(Vec3{}, nan, 0.0f));
    EXPECT_FALSE(camera.SetPose(Vec3{}, 0.0f, -inf));
    EXPECT_FALSE(camera.SetPose(Vec3{ 2.0e6f, 0.0f, 0.0f }, 0.0f, 0.0f));
    EXPECT_FALSE(camera.SetPose(Vec3{}, 2.0e5f, 0.0f));

    EXPECT_TRUE(ApproximatelyEqual(camera.GetTransform().position, Vec3{ 4.0f, 5.0f, 6.0f }));
    EXPECT_TRUE(ApproximatelyEqual(camera.YawDegrees(), 20.0f));
    EXPECT_TRUE(ApproximatelyEqual(camera.PitchDegrees(), 15.0f));
}

TEST(EditorCameraSetPoseTest, RightDragContinuesFromTheNewYaw)
{
    EditorCamera camera;
    ASSERT_TRUE(camera.SetPose(Vec3{}, 40.0f, 0.0f));

    camera.Update(Vec2{ 10.0f, 0.0f }, 0.0f, false, true);

    EXPECT_TRUE(ApproximatelyEqual(camera.YawDegrees(), 42.0f));
}

TEST(EditorCameraSetPoseTest, MatchesAPoseReachedByMouseLook)
{
    EditorCamera dragged;
    dragged.Update(Vec2{ 100.0f, 50.0f }, 0.0f, false, true); // yaw 20, pitch 10

    EditorCamera posed;
    ASSERT_TRUE(posed.SetPose(dragged.GetTransform().position, 20.0f, 10.0f));

    const float aspect = 16.0f / 9.0f;
    EXPECT_TRUE(ApproximatelyEqual(posed.ViewProjection(aspect), dragged.ViewProjection(aspect), 1.0e-4f));
}

} // namespace
} // namespace gte
