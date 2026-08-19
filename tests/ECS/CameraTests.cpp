// Unit tests for Camera's pure math helpers (src/ECS/Components/Camera.h) -
// ProjectionMatrix()/ViewMatrix() touch nothing but Math/Mat4.h + a plain
// Transform value, no Renderer/live GPU device at all, so this is Tier-1-
// testable exactly like Transform/MeshRenderer's own coverage (see AGENTS.md,
// "Testability & Regression Safety"). RenderSystem::ResolveActiveCameraViewProjection()
// (the Registry-level "find the active Camera" step built on top of these) has
// its own tests in tests/Game/RenderSystemTests.cpp.

#include "ECS/Components/Camera.h"

#include <gtest/gtest.h>

namespace gte {
namespace {

TEST(CameraTest, ProjectionMatrixMatchesPerspectiveFovLH_ZO)
{
    Camera camera;
    camera.fovYDegrees = 90.0f;
    camera.nearZ = 0.5f;
    camera.farZ = 250.0f;

    const Mat4 actual = camera.ProjectionMatrix(1.7777778f);
    const Mat4 expected = Mat4::PerspectiveFovLH_ZO(DegToRad(90.0f), 1.7777778f, 0.5f, 250.0f, /*flipY=*/true);

    EXPECT_TRUE(ApproximatelyEqual(actual, expected));
}

TEST(CameraTest, ViewMatrixWithIdentityTransformLooksDownPositiveZ)
{
    Transform transform; // Identity: origin, no rotation.

    const Mat4 actual = Camera::ViewMatrix(transform);
    const Mat4 expected = Mat4::LookAtLH(Vec3::Zero(), Vec3::Forward(), Vec3::Up());

    EXPECT_TRUE(ApproximatelyEqual(actual, expected));
}

TEST(CameraTest, ViewMatrixUsesTransformPositionAsEye)
{
    Transform transform;
    transform.position = Vec3{ 0.0f, 0.0f, -5.0f };

    const Mat4 actual = Camera::ViewMatrix(transform);
    const Mat4 expected =
        Mat4::LookAtLH(Vec3{ 0.0f, 0.0f, -5.0f }, Vec3{ 0.0f, 0.0f, -4.0f }, Vec3::Up());

    EXPECT_TRUE(ApproximatelyEqual(actual, expected));
}

TEST(CameraTest, ViewMatrixRotatesForwardAndUpByTransformRotation)
{
    Transform transform;
    transform.position = Vec3::Zero();
    // +90 degrees around Up() turns Forward() into Right() (hand-verified in
    // tests/Math/QuatTests.cpp) - so the camera should now be looking down
    // +X instead of +Z, with Up() unaffected (rotation is purely around it).
    transform.rotation = Quat::FromAxisAngle(Vec3::Up(), DegToRad(90.0f));

    const Mat4 actual = Camera::ViewMatrix(transform);
    const Mat4 expected = Mat4::LookAtLH(Vec3::Zero(), Vec3::Right(), Vec3::Up());

    EXPECT_TRUE(ApproximatelyEqual(actual, expected));
}

TEST(CameraTest, DefaultsAreActiveWithSensibleFieldOfViewAndClipPlanes)
{
    const Camera camera;

    EXPECT_TRUE(camera.active);
    EXPECT_GT(camera.fovYDegrees, 0.0f);
    EXPECT_LT(camera.fovYDegrees, 180.0f);
    EXPECT_GT(camera.nearZ, 0.0f);
    EXPECT_GT(camera.farZ, camera.nearZ);
}

} // namespace
} // namespace gte
