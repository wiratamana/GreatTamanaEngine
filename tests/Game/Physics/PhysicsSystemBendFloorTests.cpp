// Bend joint floor end to end: non-root joints swing under a rotating anchor, root children stay welded.

#include "PhysicsTestChainFixture.h"

#include "Math/MathTypes.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cmath>

namespace gte {
namespace {

using physicstest::BuildChainModel;
using physicstest::ChainScene;
using physicstest::kAnchorBone;

constexpr int kJointCount = 4;
constexpr int kFrames = 180;
constexpr int kWarmupFrames = 10;
constexpr double kRenderDt = 1.0 / 60.0;
constexpr float kConeToleranceDegrees = 0.1f;

struct SwingResult {
    float maxRootChildDistance = 0.0f;
    // Largest segment deviation per joint (index 0 = root child) after the warm-up.
    std::array<float, kJointCount> maxDeviationDegrees{};
    // Largest excess of a segment deviation over its cumulative limit.
    float maxConeExcessDegrees = -180.0f;
};

float SegmentAngleDegrees(const Vec3& a, const Vec3& b)
{
    const float la = Length(a);
    const float lb = Length(b);
    if (la < 1e-6f || lb < 1e-6f) {
        return 0.0f;
    }
    return RadToDeg(std::atan2(Length(Cross(a, b)), Dot(a, b)));
}

// The anchor bone rotates about Z around its own origin, so the whole strand below it swings.
void SetAnchorRotation(ChainScene& scene, float radians)
{
    std::vector<BoneLocalOffset>& pose = scene.Pose().pose;
    pose.assign(scene.data.skeleton.bones.size(), BoneLocalOffset{});
    pose[kAnchorBone].rotation = Quat::FromAxisAngle(Vec3(0.0f, 0.0f, 1.0f), radians);
}

// Joint k sits on bone kAnchorBone + 1 + k; every own PMX limit is 0.
SwingResult RunSwing(float floorDegrees, bool overrideFloor)
{
    ChainScene scene(BuildChainModel(kJointCount, 0.0f));
    GlobalPhysicsSettings& settings = scene.physics.GetGlobalPhysicsSettings();
    settings.gravity = Vec3(0.0f, -98.0f, 0.0f);
    settings.stiffnessMultiplier = 0.0f;
    settings.interpolateOutput = false;
    settings.bendLimitsEnabled = true;
    if (overrideFloor) {
        settings.bendJointFloorDegrees = floorDegrees;
    }
    const float floor = settings.bendJointFloorDegrees;

    SwingResult result;
    for (int frame = 0; frame < kFrames; ++frame) {
        const float angle = DegToRad(30.0f) * std::sin(2.0f * kPi * static_cast<float>(frame * kRenderDt));
        SetAnchorRotation(scene, angle);

        std::array<Vec3, kJointCount + 1> animated{};
        for (int k = 0; k <= kJointCount; ++k) {
            animated[k] = scene.BoneWorld(kAnchorBone + k);
        }
        scene.physics.Update(scene.registry, kRenderDt);

        std::array<Vec3, kJointCount + 1> written{};
        for (int k = 0; k <= kJointCount; ++k) {
            written[k] = scene.BoneWorld(kAnchorBone + k);
        }

        result.maxRootChildDistance = std::max(result.maxRootChildDistance, Length(written[1] - animated[1]));
        if (frame < kWarmupFrames) {
            continue;
        }
        for (int k = 0; k < kJointCount; ++k) {
            const float deviation = SegmentAngleDegrees(written[k + 1] - written[k], animated[k + 1] - animated[k]);
            result.maxDeviationDegrees[k] = std::max(result.maxDeviationDegrees[k], deviation);
            if (k > 0) {
                const float cumulativeLimit = floor * static_cast<float>(k);
                result.maxConeExcessDegrees = std::max(result.maxConeExcessDegrees, deviation - cumulativeLimit);
            }
        }
    }
    return result;
}

} // namespace

TEST(PhysicsSystemBendFloorTests, DefaultFloorLetsNonRootJointsSwing)
{
    const SwingResult result = RunSwing(0.0f, /*overrideFloor=*/false);

    EXPECT_LE(result.maxRootChildDistance, 0.001f);
    float maxNonRootDeviation = 0.0f;
    for (int k = 1; k < kJointCount; ++k) {
        maxNonRootDeviation = std::max(maxNonRootDeviation, result.maxDeviationDegrees[k]);
    }
    EXPECT_GT(maxNonRootDeviation, 1.0f);
    EXPECT_LE(result.maxConeExcessDegrees, kConeToleranceDegrees);
}

TEST(PhysicsSystemBendFloorTests, ZeroFloorKeepsTheWeld)
{
    const SwingResult result = RunSwing(0.0f, /*overrideFloor=*/true);

    EXPECT_LE(result.maxRootChildDistance, 0.001f);
    for (int k = 1; k < kJointCount; ++k) {
        EXPECT_LT(result.maxDeviationDegrees[k], kConeToleranceDegrees) << "joint " << k;
    }
}

} // namespace gte
