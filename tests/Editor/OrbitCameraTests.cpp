#include "Editor/OrbitCamera.h"

#include <gtest/gtest.h>

#include <cmath>
#include <limits>

namespace gte {
namespace {

bool IsFinite(const OrbitCamera& c)
{
    return std::isfinite(c.NearZ()) && std::isfinite(c.FarZ()) && std::isfinite(c.distance) && std::isfinite(c.Eye().x);
}

TEST(OrbitCameraTest, EyeAndViewMatchHandValues)
{
    OrbitCamera cam;
    cam.yawDeg = 0.0f;
    cam.pitchDeg = 0.0f;
    cam.distance = 10.0f;
    cam.target = Vec3::Zero();
    EXPECT_TRUE(ApproximatelyEqual(cam.Eye(), Vec3(0, 0, -10), 1e-5f));
    EXPECT_NEAR(cam.View().TransformPoint(cam.target).z, 10.0f, 1e-4f);

    cam.yawDeg = 90.0f;
    EXPECT_TRUE(ApproximatelyEqual(cam.Eye(), Vec3(10, 0, 0), 1e-4f));
}

TEST(OrbitCameraTest, PitchIsClamped)
{
    OrbitCamera cam;
    cam.pitchDeg = 0.0f;
    cam.Rotate(0.0f, 1.0e6f);
    EXPECT_FLOAT_EQ(cam.pitchDeg, 85.0f);
    cam.Rotate(0.0f, -1.0e7f);
    EXPECT_FLOAT_EQ(cam.pitchDeg, -85.0f);

    const float before = cam.yawDeg;
    cam.Rotate(std::numeric_limits<float>::quiet_NaN(), 1.0f);
    EXPECT_EQ(cam.yawDeg, before);
}

TEST(OrbitCameraTest, DollyStaysInsideDistanceLimits)
{
    OrbitCamera cam;
    cam.Frame(Vec3::Zero(), 100.0f);
    for (int i = 0; i < 200; ++i) {
        cam.Dolly(5.0f);
    }
    EXPECT_FLOAT_EQ(cam.distance, cam.MinDistance());
    EXPECT_FLOAT_EQ(cam.MinDistance(), 0.1f);

    for (int i = 0; i < 200; ++i) {
        cam.Dolly(-5.0f);
    }
    EXPECT_FLOAT_EQ(cam.distance, cam.MaxDistance());
}

TEST(OrbitCameraTest, DollyStepIsFifteenPercent)
{
    OrbitCamera cam;
    cam.Frame(Vec3::Zero(), 1.0f);
    cam.distance = 10.0f;
    cam.Dolly(1.0f);
    EXPECT_NEAR(cam.distance, 8.5f, 1e-5f);
}

TEST(OrbitCameraTest, PanMovesTargetAlongScreenAxes)
{
    OrbitCamera cam;
    cam.yawDeg = 0.0f;
    cam.pitchDeg = 0.0f;
    cam.distance = 100.0f;
    cam.Pan(10.0f, 0.0f); // dragging right moves the scene right, so the target goes left
    EXPECT_NEAR(cam.target.x, -1.5f, 1e-4f);
    EXPECT_NEAR(cam.target.y, 0.0f, 1e-4f);
    cam.Pan(0.0f, 10.0f);
    EXPECT_NEAR(cam.target.y, 1.5f, 1e-4f);
}

TEST(OrbitCameraTest, FrameFitsSphereAndRejectsBadInput)
{
    OrbitCamera cam;
    cam.Frame(Vec3(1, 2, 3), 10.0f);
    EXPECT_TRUE(ApproximatelyEqual(cam.target, Vec3(1, 2, 3)));
    EXPECT_GT(cam.distance, 10.0f);

    cam.Frame(Vec3(0, 0, 0), std::numeric_limits<float>::quiet_NaN());
    EXPECT_FLOAT_EQ(cam.radius, 1.0f);
    cam.Frame(Vec3(0, 0, 0), -5.0f);
    EXPECT_FLOAT_EQ(cam.radius, 1.0f);
}

TEST(OrbitCameraTest, ClipPlanesAreFiniteAndOrderedForTinyAndHugeMeshes)
{
    for (const float radius : { 0.001f, 1.0f, 5000.0f, 1.0e6f }) {
        OrbitCamera cam;
        cam.Frame(Vec3::Zero(), radius);
        EXPECT_TRUE(IsFinite(cam)) << radius;
        EXPECT_GT(cam.NearZ(), 0.0f) << radius;
        EXPECT_LT(cam.NearZ(), cam.FarZ()) << radius;

        cam.Dolly(100.0f); // closest possible
        EXPECT_LT(cam.NearZ(), cam.FarZ()) << radius;
    }
}

TEST(OrbitCameraTest, GizmoProjectionDiffersOnlyByYFlip)
{
    OrbitCamera cam;
    cam.Frame(Vec3::Zero(), 5.0f);
    const Mat4 flipped = cam.Projection(1.5f);
    const Mat4 plain = cam.GizmoProjection(1.5f);
    EXPECT_FLOAT_EQ(flipped(1, 1), -plain(1, 1));
    EXPECT_FLOAT_EQ(flipped(0, 0), plain(0, 0));
}

} // namespace
} // namespace gte
