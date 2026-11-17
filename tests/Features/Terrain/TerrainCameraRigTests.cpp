#include "Features/Terrain/TerrainCameraRig.h"

#include <gtest/gtest.h>

#include <cmath>
#include <limits>

namespace gte::terrain {

namespace {

GroundHeightFn Flat(double height)
{
    return [height](double, double) { return height; };
}

RigPose Start(double y = 300.0, double yaw = 0.0, double pitch = 0.0)
{
    RigPose pose;
    pose.y = y;
    pose.yaw = yaw;
    pose.pitchDown = pitch;
    return pose;
}

} // namespace

TEST(TerrainCameraRigTest, ModeNamesRoundTrip)
{
    for (const CameraMode mode : { CameraMode::Script, CameraMode::FreeFly, CameraMode::Orbit }) {
        CameraMode parsed = CameraMode::Script;
        ASSERT_TRUE(TryParseCameraMode(CameraModeName(mode), parsed));
        EXPECT_EQ(parsed, mode);
    }
    CameraMode ignored = CameraMode::Script;
    EXPECT_FALSE(TryParseCameraMode("fly", ignored));
}

TEST(TerrainCameraRigTest, PoseFromForwardInvertsTheBasis)
{
    const RigPose pose = Start(10.0, 0.7, 0.3);
    double forward[3];
    double right[3];
    PoseBasis(pose, forward, right);
    const RigPose back = PoseFromForward(0.0, 10.0, 0.0, forward[0], forward[1], forward[2]);
    EXPECT_NEAR(back.yaw, 0.7, 1e-9);
    EXPECT_NEAR(back.pitchDown, 0.3, 1e-9);
    EXPECT_NEAR(forward[0] * right[0] + forward[1] * right[1] + forward[2] * right[2], 0.0, 1e-12);
}

TEST(TerrainCameraRigTest, ScriptModeDoesNotMove)
{
    CameraRig rig;
    rig.Begin(CameraMode::Script, Start(), Flat(0.0));
    CameraInput input;
    input.moveForward = 1.0f;
    const RigPose pose = rig.Advance(input, 0.1, Flat(0.0));
    EXPECT_DOUBLE_EQ(pose.z, 0.0);
}

TEST(TerrainCameraRigTest, FreeFlyForwardFollowsTheHeading)
{
    CameraRig rig;
    rig.Begin(CameraMode::FreeFly, Start(300.0, 3.14159265358979323846 / 2.0, 0.0), Flat(0.0)); // Heading +X.
    CameraInput input;
    input.moveForward = 1.0f;
    RigPose pose;
    for (int i = 0; i < 120; ++i) {
        pose = rig.Advance(input, 1.0 / 60.0, Flat(0.0));
    }
    EXPECT_GT(pose.x, 50.0);
    EXPECT_NEAR(pose.z, 0.0, 1e-6);
    EXPECT_NEAR(pose.y, 300.0, 1e-6);
}

TEST(TerrainCameraRigTest, SpeedGrowsWithHeightAndBoost)
{
    CameraRig low;
    CameraRig high;
    low.Begin(CameraMode::FreeFly, Start(20.0), Flat(0.0));
    high.Begin(CameraMode::FreeFly, Start(2000.0), Flat(0.0));
    CameraInput input;
    input.moveForward = 1.0f;
    low.Advance(input, 0.016, Flat(0.0));
    high.Advance(input, 0.016, Flat(0.0));
    EXPECT_GT(high.Speed(), low.Speed() * 5.0);
    CameraRig boosted;
    boosted.Begin(CameraMode::FreeFly, Start(20.0), Flat(0.0));
    input.boost = true;
    boosted.Advance(input, 0.016, Flat(0.0));
    EXPECT_NEAR(boosted.Speed(), low.Speed() * boosted.Config().boostFactor, 1e-9);
}

TEST(TerrainCameraRigTest, WheelScalesTheSpeedAndIsBounded)
{
    CameraRig rig;
    rig.Begin(CameraMode::FreeFly, Start(), Flat(0.0));
    CameraInput input;
    input.wheel = 2.0f;
    rig.Advance(input, 0.016, Flat(0.0));
    EXPECT_NEAR(rig.SpeedMultiplier(), 1.5625, 1e-9);
    input.wheel = 1000.0f;
    rig.Advance(input, 0.016, Flat(0.0));
    EXPECT_LE(rig.SpeedMultiplier(), 50.0);
}

TEST(TerrainCameraRigTest, LookTurnsAndClampsPitch)
{
    CameraRig rig;
    rig.Begin(CameraMode::FreeFly, Start(), Flat(0.0));
    CameraInput input;
    input.look = true;
    input.lookDeltaX = 100.0f;
    input.lookDeltaY = 1.0e6f;
    const RigPose pose = rig.Advance(input, 0.016, Flat(0.0));
    EXPECT_NEAR(pose.yaw, 100.0 * rig.Config().lookRadiansPerPixel, 1e-9);
    EXPECT_DOUBLE_EQ(pose.pitchDown, rig.Config().maxPitch);
    input.look = false; // Mouse movement without the button does nothing.
    input.lookDeltaX = 500.0f;
    EXPECT_DOUBLE_EQ(rig.Advance(input, 0.016, Flat(0.0)).yaw, pose.yaw);
}

TEST(TerrainCameraRigTest, StaysAboveGroundAndSea)
{
    CameraRig rig;
    rig.Begin(CameraMode::FreeFly, Start(30.0, 0.0, 1.2), Flat(100.0));
    CameraInput input;
    input.moveForward = 1.0f;
    input.moveUp = -1.0f;
    RigPose pose;
    for (int i = 0; i < 300; ++i) {
        pose = rig.Advance(input, 1.0 / 60.0, Flat(100.0));
    }
    EXPECT_GE(pose.y, 100.0 + rig.Config().minClearance - 1e-9);

    CameraRig sea;
    sea.Begin(CameraMode::FreeFly, Start(5.0), Flat(-200.0));
    input.moveForward = 0.0f;
    for (int i = 0; i < 60; ++i) {
        pose = sea.Advance(input, 1.0 / 60.0, Flat(-200.0));
    }
    EXPECT_GE(pose.y, rig.Config().minClearance - 1e-9); // Never under the water plane.
}

TEST(TerrainCameraRigTest, FreeFlyDoesNotDependOnTheFrameRate)
{
    auto run = [](double step) {
        CameraRig rig;
        rig.Begin(CameraMode::FreeFly, Start(400.0, 0.4, 0.2), Flat(0.0));
        CameraInput input;
        input.moveForward = 1.0f;
        input.moveRight = 0.5f;
        RigPose pose;
        for (double t = 0.0; t < 2.0 - 1e-9; t += step) {
            pose = rig.Advance(input, step, Flat(0.0));
        }
        return pose;
    };
    const RigPose slow = run(1.0 / 30.0);
    const RigPose fast = run(1.0 / 144.0);
    const double travelled = std::hypot(fast.x, fast.z);
    ASSERT_GT(travelled, 100.0);
    EXPECT_NEAR(slow.x, fast.x, travelled * 0.03);
    EXPECT_NEAR(slow.z, fast.z, travelled * 0.03);
}

TEST(TerrainCameraRigTest, ALongFrameIsClamped)
{
    CameraRig rig;
    rig.Begin(CameraMode::FreeFly, Start(), Flat(0.0));
    CameraInput input;
    input.moveForward = 1.0f;
    const RigPose pose = rig.Advance(input, 30.0, Flat(0.0));
    EXPECT_LT(std::abs(pose.z), rig.Config().maxSpeed * rig.Config().maxStepSeconds);
}

TEST(TerrainCameraRigTest, OrbitKeepsItsDistanceToTheTarget)
{
    CameraRig rig;
    rig.Begin(CameraMode::Orbit, Start(500.0, 0.0, 0.6), Flat(0.0));
    rig.SetOrbitTarget(1000.0, 2000.0, 900.0, Flat(0.0));
    CameraInput input;
    input.look = true;
    input.lookDeltaX = 80.0f;
    input.lookDeltaY = 30.0f;
    RigPose pose = rig.Advance(input, 0.016, Flat(0.0));
    const double dx = pose.x - rig.TargetX();
    const double dy = pose.y - rig.TargetY();
    const double dz = pose.z - rig.TargetZ();
    EXPECT_NEAR(std::sqrt(dx * dx + dy * dy + dz * dz), 900.0, 1e-6);
    // The camera looks at the target.
    double forward[3];
    double right[3];
    PoseBasis(pose, forward, right);
    EXPECT_NEAR(pose.x + forward[0] * 900.0, rig.TargetX(), 1e-6);
    EXPECT_NEAR(pose.z + forward[2] * 900.0, rig.TargetZ(), 1e-6);
}

TEST(TerrainCameraRigTest, OrbitWheelZoomsAndIsBounded)
{
    CameraRig rig;
    rig.Begin(CameraMode::Orbit, Start(500.0, 0.0, 0.6), Flat(0.0));
    rig.SetOrbitTarget(0.0, 0.0, 1000.0, Flat(0.0));
    CameraInput input;
    input.wheel = 3.0f;
    rig.Advance(input, 0.016, Flat(0.0));
    EXPECT_NEAR(rig.OrbitDistance(), 1000.0 * std::pow(0.85, 3.0), 1e-6);
    input.wheel = 1000.0f;
    rig.Advance(input, 0.016, Flat(0.0));
    EXPECT_GE(rig.OrbitDistance(), 10.0);
    input.wheel = -1000.0f;
    rig.Advance(input, 0.016, Flat(0.0));
    EXPECT_LE(rig.OrbitDistance(), 100000.0);
}

TEST(TerrainCameraRigTest, OrbitKeysMoveTheCentreOverTheGround)
{
    CameraRig rig;
    rig.Begin(CameraMode::Orbit, Start(500.0, 0.0, 0.6), Flat(50.0));
    rig.SetOrbitTarget(0.0, 0.0, 800.0, Flat(50.0));
    CameraInput input;
    input.moveForward = 1.0f;
    for (int i = 0; i < 60; ++i) {
        rig.Advance(input, 1.0 / 60.0, Flat(50.0));
    }
    EXPECT_GT(rig.TargetZ(), 10.0); // Heading +Z.
    EXPECT_NEAR(rig.TargetY(), 50.0, 1e-9);
}

TEST(TerrainCameraRigTest, OrbitBeginPicksATargetOnTheViewRay)
{
    CameraRig rig;
    const RigPose pose = Start(600.0, 0.5, 0.5);
    rig.Begin(CameraMode::Orbit, pose, Flat(0.0));
    double forward[3];
    double right[3];
    PoseBasis(pose, forward, right);
    EXPECT_NEAR(rig.TargetX(), pose.x + forward[0] * rig.OrbitDistance(), 1e-6);
    // Switching mode does not move the camera.
    EXPECT_DOUBLE_EQ(rig.Pose().y, 600.0);
}

TEST(TerrainCameraRigTest, AccumulateSumsLookAndKeepsTheLatestAxes)
{
    CameraInput sum;
    CameraInput a;
    a.lookDeltaX = 3.0f;
    a.wheel = 1.0f;
    a.moveForward = 1.0f;
    CameraInput b;
    b.lookDeltaX = 4.0f;
    b.wheel = 2.0f;
    b.moveForward = 0.0f;
    AccumulateInput(sum, a);
    AccumulateInput(sum, b);
    EXPECT_FLOAT_EQ(sum.lookDeltaX, 7.0f);
    EXPECT_FLOAT_EQ(sum.wheel, 3.0f);
    EXPECT_FLOAT_EQ(sum.moveForward, 0.0f);
}

TEST(TerrainCameraRigTest, NonFiniteInputIsIgnored)
{
    CameraRig rig;
    rig.Begin(CameraMode::FreeFly, Start(), Flat(0.0));
    CameraInput input;
    input.moveForward = std::nanf("");
    input.look = true;
    input.lookDeltaX = std::numeric_limits<float>::infinity();
    input.wheel = std::nanf("");
    const RigPose pose = rig.Advance(input, std::nan(""), Flat(0.0));
    EXPECT_TRUE(std::isfinite(pose.x) && std::isfinite(pose.y) && std::isfinite(pose.z) && std::isfinite(pose.yaw));
}

} // namespace gte::terrain
