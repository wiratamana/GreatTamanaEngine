#include "PhysicsTestChainFixture.h"

#include <gtest/gtest.h>

#include <algorithm>

namespace gte {
namespace {

using physicstest::BuildChainModel;
using physicstest::ChainScene;
using physicstest::kAnchorBone;

constexpr double kRenderDt = 1.0 / 240.0;

void ConfigureFixedSixtyHz(PhysicsSystem& physics, bool interpolate)
{
    GlobalPhysicsSettings& settings = physics.GetGlobalPhysicsSettings();
    settings.gravity = Vec3::Zero();
    settings.fixedTimestepSeconds = 1.0f / 60.0f;
    settings.maxStepsPerFrame = 10;
    settings.bendLimitsEnabled = true;
    settings.interpolateOutput = interpolate;
}

// Largest distance of the two joints from their animated spots over a 1 unit/s anchor slide.
// Only frames with zero steps count when `zeroStepFramesOnly` is set.
float WeldedChainMaxDeviation(bool interpolate, bool zeroStepFramesOnly, int* zeroStepFrames)
{
    ChainScene scene(BuildChainModel(2, 0.0f));
    ConfigureFixedSixtyHz(scene.physics, interpolate);
    scene.physics.GetDebugRecorder().Configure(true, 600);

    float maxDeviation = 0.0f;
    for (int frame = 0; frame < 240; ++frame) {
        const float anchorX = static_cast<float>(frame * kRenderDt);
        scene.SetAnimatedAnchorX(anchorX);
        scene.physics.Update(scene.registry, kRenderDt);

        const auto frames = scene.physics.GetDebugRecorder().Frames(scene.entity, 0);
        const bool zeroStep = !frames.empty() && frames.back()->stepsThisFrame == 0;
        if (zeroStep && zeroStepFrames != nullptr) {
            ++*zeroStepFrames;
        }
        if (zeroStepFramesOnly && !zeroStep) {
            continue;
        }
        for (std::int32_t bone = kAnchorBone + 1; bone <= kAnchorBone + 2; ++bone) {
            maxDeviation = std::max(maxDeviation, Length(scene.BoneWorld(bone) - scene.AnimatedWorld(bone, anchorX)));
        }
    }
    return maxDeviation;
}

TEST(PhysicsSystemInterpolationTests, WeldedChainStaysOnTheAnimatedPoseOnEveryRenderFrameWhenInterpolated)
{
    int zeroStepFrames = 0;
    EXPECT_LT(WeldedChainMaxDeviation(true, false, &zeroStepFrames), 1e-3f);
    EXPECT_GT(zeroStepFrames, 100) << "the scene must exercise frames without a fixed step";
}

TEST(PhysicsSystemInterpolationTests, WithoutInterpolationAZeroStepFrameLagsBehindTheAnimatedPose)
{
    EXPECT_GT(WeldedChainMaxDeviation(false, true, nullptr), 0.005f);
}

TEST(PhysicsSystemInterpolationTests, FrozenChainRidesWithTheAnchor)
{
    ChainScene scene(BuildChainModel(2, 1.0f));
    ConfigureFixedSixtyHz(scene.physics, true);
    GlobalPhysicsSettings& settings = scene.physics.GetGlobalPhysicsSettings();
    settings.gravity = Vec3(0.0f, -9.8f, 0.0f);
    settings.bendLimitsEnabled = false;

    for (int frame = 0; frame < 40; ++frame) {
        scene.SetAnimatedAnchorX(0.0f);
        scene.physics.Update(scene.registry, 1.0 / 60.0);
    }

    scene.Rig().frozen = true;
    scene.SetAnimatedAnchorX(0.0f);
    scene.physics.Update(scene.registry, 1.0 / 60.0);
    const Vec3 frozenOffset = scene.BoneWorld(kAnchorBone + 1) - scene.BoneWorld(kAnchorBone);

    scene.SetAnimatedAnchorX(0.5f);
    scene.physics.Update(scene.registry, 1.0 / 60.0);
    const Vec3 movedOffset = scene.BoneWorld(kAnchorBone + 1) - scene.BoneWorld(kAnchorBone);

    EXPECT_NEAR(movedOffset.x, frozenOffset.x, 1e-3f);
    EXPECT_NEAR(movedOffset.y, frozenOffset.y, 1e-3f);
    EXPECT_NEAR(movedOffset.z, frozenOffset.z, 1e-3f);
    EXPECT_NEAR(scene.BoneWorld(kAnchorBone).x, 1.5f, 1e-4f);
}

TEST(PhysicsSystemInterpolationTests, TeleportReseedsAndTheDisplayEqualsTheAnimatedPose)
{
    ChainScene scene(BuildChainModel(2, 1.0f));
    ConfigureFixedSixtyHz(scene.physics, true);
    scene.physics.GetGlobalPhysicsSettings().bendLimitsEnabled = false;
    scene.physics.GetDebugRecorder().Configure(true, 16);

    for (int frame = 0; frame < 10; ++frame) {
        scene.SetAnimatedAnchorX(0.0f);
        scene.physics.Update(scene.registry, 1.0 / 60.0);
    }

    constexpr float kTeleportX = 1000.0f;
    scene.SetAnimatedAnchorX(kTeleportX);
    scene.physics.Update(scene.registry, 1.0 / 60.0);

    const auto frames = scene.physics.GetDebugRecorder().Frames(scene.entity, 0);
    ASSERT_FALSE(frames.empty());
    EXPECT_GE(frames.back()->stepsThisFrame, 1u);
    EXPECT_EQ(scene.Rig().chainStates[0].reseedCount, 1u);
    for (std::int32_t bone = kAnchorBone + 1; bone <= kAnchorBone + 2; ++bone) {
        EXPECT_LT(Length(scene.BoneWorld(bone) - scene.AnimatedWorld(bone, kTeleportX)), 1e-3f);
    }
}

} // namespace
} // namespace gte
