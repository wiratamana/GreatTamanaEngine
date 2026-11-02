#include "PhysicsTestChainFixture.h"

#include "Math/MathTypes.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>

namespace gte {
namespace {

using physicstest::BuildChainModel;
using physicstest::ChainScene;
using physicstest::kAnchorBone;

constexpr int kJointCount = 4;

// Largest excess (degrees) of a written pose segment over its cumulative cone limit, after a warm-up.
float MaxConeExcessDegrees(bool bendLimitsEnabled)
{
    // Own limit of every joint = hypot(a, a) with a = 5 degrees.
    const float axisLimit = DegToRad(5.0f);
    const float ownLimitDegrees = RadToDeg(axisLimit * std::sqrt(2.0f));

    ChainScene scene(BuildChainModel(kJointCount, axisLimit));
    GlobalPhysicsSettings& settings = scene.physics.GetGlobalPhysicsSettings();
    settings.gravity = Vec3(0.0f, -98.0f, 0.0f);
    settings.stiffnessMultiplier = 0.0f;
    settings.interpolateOutput = false;
    settings.bendLimitsEnabled = bendLimitsEnabled;

    float maxExcess = -180.0f;
    for (int frame = 0; frame < 120; ++frame) {
        scene.SetAnimatedAnchorX(0.0f);
        scene.physics.Update(scene.registry, 1.0 / 60.0);
        if (frame < 10) {
            continue;
        }
        for (int k = 1; k <= kJointCount; ++k) {
            const Vec3 parent = scene.BoneWorld(kAnchorBone + k - 1);
            const Vec3 child = scene.BoneWorld(kAnchorBone + k);
            const Vec3 segment = child - parent;
            const float length = Length(segment);
            if (length < 1e-6f) {
                continue;
            }
            // The animated segments all point along +X.
            const float angle = RadToDeg(std::acos(std::clamp(segment.x / length, -1.0f, 1.0f)));
            maxExcess = std::max(maxExcess, angle - ownLimitDegrees * static_cast<float>(k));
        }
    }
    return maxExcess;
}

TEST(PhysicsSystemBendLimitTests, WrittenPoseSegmentsStayInsideTheCumulativeCone)
{
    EXPECT_LE(MaxConeExcessDegrees(true), 1.5f);
}

TEST(PhysicsSystemBendLimitTests, WithoutBendLimitsTheSegmentsLeaveTheCone)
{
    EXPECT_GT(MaxConeExcessDegrees(false), 20.0f);
}

} // namespace
} // namespace gte
