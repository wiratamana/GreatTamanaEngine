// Bend limits: effective limits, the cone in StepDynamicChain() and limit detection on a skirt-like rig.

#include "Physics/DynamicChainDetection.h"
#include "Physics/DynamicChainSolver.h"
#include "Physics/PhysicsDiagnostics.h"
#include "Math/MathTypes.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace gte {
namespace {

constexpr float kDt = 1.0f / 120.0f;

DynamicChainDefinition BuildHangingChain(std::size_t jointCount, float restLength, float ownLimitDegrees)
{
    DynamicChainDefinition definition;
    definition.rootBoneIndex = -1;
    definition.jointBoneIndices.resize(jointCount);
    definition.parentJointIndex = DynamicChainDefinition::MakeLinearParentIndices(jointCount);
    definition.jointSettings.assign(jointCount, DynamicJointSettings{});
    definition.restLengths.assign(jointCount, restLength);
    definition.collisionEnabled = false;
    for (std::size_t i = 0; i < jointCount; ++i) {
        definition.jointBoneIndices[i] = static_cast<std::int32_t>(i);
        definition.jointSettings[i].bendLimitRadians = DegToRad(ownLimitDegrees);
    }
    return definition;
}

StepOptions EnabledOptions()
{
    StepOptions options;
    options.bendLimitsEnabled = true;
    return options;
}

// Animated pose: the chain hangs straight down from the anchor.
std::vector<Vec3> HangingTargets(const Vec3& root, std::size_t jointCount, float restLength)
{
    std::vector<Vec3> targets(jointCount);
    for (std::size_t i = 0; i < jointCount; ++i) {
        targets[i] = root + Vec3(0.0f, -restLength * static_cast<float>(i + 1), 0.0f);
    }
    return targets;
}

// Largest segment-direction error (degrees) over every joint, per joint, after each step from `firstCheckedStep`.
std::vector<float> RunSwayingAnchor(const DynamicChainDefinition& definition, const StepOptions& options, float restLength)
{
    const std::size_t jointCount = definition.jointBoneIndices.size();
    std::vector<float> maxErrorPerJoint(jointCount, 0.0f);
    DynamicChainRuntimeState state;
    const Vec3 gravity(0.0f, -98.0f, 0.0f);
    constexpr int kSteps = 240;
    constexpr int kFirstCheckedStep = 10;
    // Anchor acceleration (amplitude * omega^2 = 59 units/s^2) is large against gravity, so an unlimited chain bends far.
    constexpr float kAmplitude = 1.5f;
    constexpr float kOmega = 2.0f * kPi;

    for (int step = 0; step < kSteps; ++step) {
        const Vec3 root(kAmplitude * std::sin(kOmega * static_cast<float>(step) * kDt), 0.0f, 0.0f);
        const std::vector<Vec3> targets = HangingTargets(root, jointCount, restLength);
        StepDynamicChain(definition, root, targets, state, kDt, gravity, WindSettings{}, {}, options);
        if (step < kFirstCheckedStep) {
            continue;
        }
        for (std::size_t i = 0; i < jointCount; ++i) {
            const Vec3 parent = i == 0 ? root : state.particles[i - 1].position;
            const float error = VectorAngleDegrees(state.particles[i].position - parent, Vec3(0.0f, -restLength, 0.0f));
            maxErrorPerJoint[i] = std::max(maxErrorPerJoint[i], error);
        }
    }
    return maxErrorPerJoint;
}

Bone MakeBone(Vec3 position, std::int32_t parentBoneIndex)
{
    Bone bone;
    bone.position = position;
    bone.parentBoneIndex = parentBoneIndex;
    return bone;
}

RigidBody MakeBody(std::int32_t boneIndex, RigidBodyMotionType motionType)
{
    RigidBody body;
    body.boneIndex = boneIndex;
    body.motionType = motionType;
    return body;
}

Joint MakePmxJoint(std::int32_t bodyA, std::int32_t bodyB, Vec3 lowerDegrees, Vec3 upperDegrees)
{
    Joint joint;
    joint.rigidBodyAIndex = bodyA;
    joint.rigidBodyBIndex = bodyB;
    joint.rotateLowerLimit = Vec3(DegToRad(lowerDegrees.x), DegToRad(lowerDegrees.y), DegToRad(lowerDegrees.z));
    joint.rotateUpperLimit = Vec3(DegToRad(upperDegrees.x), DegToRad(upperDegrees.y), DegToRad(upperDegrees.z));
    return joint;
}

// Bone 0 = waist (static anchor), bones 1..3 = row 0, bones 4..6 = row 1 (one column each).
// PMX joints: 0..2 weld waist -> row 0, 3..5 column joints +-3 deg, 6..9 ring joints with wide limits.
void BuildSkirtFixture(SkeletonData& skeleton, PhysicsData& physics, bool dropMiddleColumnJoint)
{
    skeleton.bones.push_back(MakeBone(Vec3(0.0f, 0.0f, 0.0f), -1));
    for (int column = 0; column < 3; ++column) {
        skeleton.bones.push_back(MakeBone(Vec3(static_cast<float>(column), -1.0f, 0.0f), 0));
    }
    for (int column = 0; column < 3; ++column) {
        skeleton.bones.push_back(MakeBone(Vec3(static_cast<float>(column), -2.0f, 0.0f), 1 + column));
    }

    physics.rigidBodies.push_back(MakeBody(0, RigidBodyMotionType::Static));
    for (std::int32_t bone = 1; bone <= 6; ++bone) {
        physics.rigidBodies.push_back(MakeBody(bone, RigidBodyMotionType::Dynamic));
    }

    const Vec3 zero(0.0f, 0.0f, 0.0f);
    for (std::int32_t column = 0; column < 3; ++column) {
        physics.joints.push_back(MakePmxJoint(0, 1 + column, zero, zero));
    }
    const Vec3 lower(-3.0f, 0.0f, -3.0f);
    const Vec3 upper(3.0f, 0.0f, 3.0f);
    for (std::int32_t column = 0; column < 3; ++column) {
        if (dropMiddleColumnJoint && column == 1) {
            continue;
        }
        physics.joints.push_back(MakePmxJoint(1 + column, 4 + column, lower, upper));
    }
    const Vec3 ringLower(-10.0f, -20.0f, -10.0f);
    const Vec3 ringUpper(10.0f, 20.0f, 10.0f);
    physics.joints.push_back(MakePmxJoint(1, 2, ringLower, ringUpper));
    physics.joints.push_back(MakePmxJoint(2, 3, ringLower, ringUpper));
    physics.joints.push_back(MakePmxJoint(4, 5, ringLower, ringUpper));
    physics.joints.push_back(MakePmxJoint(5, 6, ringLower, ringUpper));
}

} // namespace

TEST(DynamicChainSolverBendLimitTests, EffectiveLimitsAccumulateAlongTheChain)
{
    const DynamicChainDefinition definition = BuildHangingChain(3, 1.0f, 10.0f);
    const std::vector<float> limits = ComputeEffectiveBendLimits(definition, EnabledOptions());
    ASSERT_EQ(limits.size(), 3u);
    EXPECT_NEAR(RadToDeg(limits[0]), 10.0f, 1e-3f);
    EXPECT_NEAR(RadToDeg(limits[1]), 20.0f, 1e-3f);
    EXPECT_NEAR(RadToDeg(limits[2]), 30.0f, 1e-3f);
}

TEST(DynamicChainSolverBendLimitTests, ScaleAndMinimumAreApplied)
{
    const DynamicChainDefinition definition = BuildHangingChain(3, 1.0f, 10.0f);

    StepOptions scaled = EnabledOptions();
    scaled.bendLimitScale = 0.5f;
    const std::vector<float> half = ComputeEffectiveBendLimits(definition, scaled);
    EXPECT_NEAR(RadToDeg(half[0]), 5.0f, 1e-3f);
    EXPECT_NEAR(RadToDeg(half[1]), 10.0f, 1e-3f);
    EXPECT_NEAR(RadToDeg(half[2]), 15.0f, 1e-3f);

    StepOptions floored = EnabledOptions();
    floored.bendLimitMinRadians = DegToRad(2.0f);
    const std::vector<float> plusTwo = ComputeEffectiveBendLimits(definition, floored);
    EXPECT_NEAR(RadToDeg(plusTwo[0]), 12.0f, 1e-3f);
    EXPECT_NEAR(RadToDeg(plusTwo[1]), 22.0f, 1e-3f);
    EXPECT_NEAR(RadToDeg(plusTwo[2]), 32.0f, 1e-3f);
}

TEST(DynamicChainSolverBendLimitTests, EffectiveLimitIsCappedAtPiAndDisabledMeansFree)
{
    const DynamicChainDefinition definition = BuildHangingChain(3, 1.0f, 100.0f);
    const std::vector<float> capped = ComputeEffectiveBendLimits(definition, EnabledOptions());
    EXPECT_NEAR(RadToDeg(capped[0]), 100.0f, 1e-3f);
    EXPECT_FLOAT_EQ(capped[1], kPi);
    EXPECT_FLOAT_EQ(capped[2], kPi);

    const std::vector<float> disabled = ComputeEffectiveBendLimits(definition, StepOptions{});
    ASSERT_EQ(disabled.size(), 3u);
    for (const float limit : disabled) {
        EXPECT_FLOAT_EQ(limit, kPi);
    }
}

TEST(DynamicChainSolverBendLimitTests, BranchChildrenBothInheritTheParentCumulativeLimit)
{
    DynamicChainDefinition definition = BuildHangingChain(3, 1.0f, 0.0f);
    definition.parentJointIndex = { -1, 0, 0 }; // joints 1 and 2 are both children of joint 0.
    definition.jointSettings[0].bendLimitRadians = DegToRad(10.0f);
    definition.jointSettings[1].bendLimitRadians = DegToRad(20.0f);
    definition.jointSettings[2].bendLimitRadians = DegToRad(30.0f);

    const std::vector<float> limits = ComputeEffectiveBendLimits(definition, EnabledOptions());
    EXPECT_NEAR(RadToDeg(limits[0]), 10.0f, 1e-3f);
    EXPECT_NEAR(RadToDeg(limits[1]), 30.0f, 1e-3f);
    EXPECT_NEAR(RadToDeg(limits[2]), 40.0f, 1e-3f);
}

TEST(DynamicChainSolverBendLimitTests, SwayingAnchorNeverBendsAJointPastItsCumulativeLimit)
{
    constexpr std::size_t kJointCount = 6;
    constexpr float kRestLength = 0.4f;
    const DynamicChainDefinition definition = BuildHangingChain(kJointCount, kRestLength, 3.0f);

    const std::vector<float> maxError = RunSwayingAnchor(definition, EnabledOptions(), kRestLength);
    for (std::size_t i = 0; i < kJointCount; ++i) {
        EXPECT_LE(maxError[i], 3.0f * static_cast<float>(i + 1) + 0.5f) << "joint " << i;
    }
}

TEST(DynamicChainSolverBendLimitTests, WithoutBendLimitsTheSameSceneBendsFarPastTheLimit)
{
    constexpr std::size_t kJointCount = 6;
    constexpr float kRestLength = 0.4f;
    const DynamicChainDefinition definition = BuildHangingChain(kJointCount, kRestLength, 3.0f);

    const std::vector<float> maxError = RunSwayingAnchor(definition, StepOptions{}, kRestLength);
    EXPECT_GT(*std::max_element(maxError.begin(), maxError.end()), 20.0f);
}

TEST(DynamicChainSolverBendLimitTests, ZeroLimitRootChildIsWeldedToTheAnimatedPose)
{
    constexpr float kRestLength = 0.4f;
    DynamicChainDefinition definition = BuildHangingChain(2, kRestLength, 0.0f);
    definition.jointSettings[1].bendLimitRadians = kPi;

    DynamicChainRuntimeState state;
    const Vec3 gravity(0.0f, -98.0f, 0.0f);
    for (int step = 0; step < 240; ++step) {
        const float angle = 3.0f * static_cast<float>(step) * kDt;
        const Vec3 root(0.5f * std::cos(angle), 0.0f, 0.5f * std::sin(angle));
        const std::vector<Vec3> targets = HangingTargets(root, 2, kRestLength);
        StepDynamicChain(definition, root, targets, state, kDt, gravity, WindSettings{}, {}, EnabledOptions());
        EXPECT_LE(Length(state.particles[0].position - targets[0]), 0.01f) << "step " << step;
    }
}

TEST(DynamicChainSolverBendLimitTests, BendClampedJointsAreCountedInTheDiagnostics)
{
    constexpr float kRestLength = 0.4f;
    const DynamicChainDefinition definition = BuildHangingChain(2, kRestLength, 0.0f);
    DynamicChainRuntimeState state;
    StepDiagnostics diagnostics;
    StepOptions options = EnabledOptions();
    options.diagnostics = &diagnostics;

    // Seed on the animated pose, then jump the anchor sideways so the rods drag the joints out of the cone.
    StepDynamicChain(definition, Vec3::Zero(), HangingTargets(Vec3::Zero(), 2, kRestLength), state, kDt, Vec3::Zero(),
        WindSettings{}, {}, options);
    diagnostics = StepDiagnostics{};

    const Vec3 moved(0.3f, 0.0f, 0.0f);
    StepDynamicChain(definition, moved, HangingTargets(moved, 2, kRestLength), state, kDt, Vec3::Zero(), WindSettings{},
        {}, options);
    EXPECT_GE(diagnostics.bendClampedJoints, 1u);
    EXPECT_LE(diagnostics.bendClampedJoints, 2u); // at most once per joint.
}

TEST(DynamicChainSolverBendLimitTests, DetectionAssignsColumnLimitsAndIgnoresRingJoints)
{
    SkeletonData skeleton;
    PhysicsData physics;
    BuildSkirtFixture(skeleton, physics, /*dropMiddleColumnJoint=*/false);

    const DynamicChainDetectionResult detection = DetectDynamicChains(skeleton, &physics, DynamicChainDetectionDefaults{});
    ASSERT_EQ(detection.chains.size(), 1u);
    const DynamicChainDefinition& chain = detection.chains[0];
    ASSERT_EQ(chain.jointBoneIndices, (std::vector<std::int32_t>{ 1, 2, 3, 4, 5, 6 }));
    ASSERT_EQ(chain.parentJointIndex, (std::vector<std::int32_t>{ -1, -1, -1, 0, 1, 2 }));

    const float columnLimitDegrees = 3.0f * std::sqrt(2.0f);
    for (std::size_t i = 0; i < 3; ++i) {
        EXPECT_NEAR(chain.jointSettings[i].bendLimitRadians, 0.0f, 1e-6f) << "row 0 joint " << i;
        EXPECT_NEAR(RadToDeg(chain.jointSettings[i + 3].bendLimitRadians), columnLimitDegrees, 1e-3f) << "row 1 joint " << i;
    }

    const std::vector<float> effective = ComputeEffectiveBendLimits(chain, EnabledOptions());
    for (std::size_t i = 0; i < 3; ++i) {
        EXPECT_NEAR(effective[i], 0.0f, 1e-6f);
        EXPECT_NEAR(RadToDeg(effective[i + 3]), columnLimitDegrees, 1e-3f);
    }

    // Welded root children: no parent joint and an effective limit of at most one degree.
    std::size_t weldedRootChildren = 0;
    for (std::size_t i = 0; i < chain.parentJointIndex.size(); ++i) {
        if (chain.parentJointIndex[i] < 0 && RadToDeg(effective[i]) <= 1.0f) {
            ++weldedRootChildren;
        }
    }
    EXPECT_EQ(weldedRootChildren, 3u);

    ASSERT_EQ(detection.pmxJointIndexPerJoint.size(), 1u);
    EXPECT_EQ(detection.pmxJointIndexPerJoint[0], (std::vector<std::int32_t>{ 0, 1, 2, 3, 4, 5 }));
    EXPECT_TRUE(detection.diagnostics.bendLimitMissingBoneIndices.empty());
}

TEST(DynamicChainSolverBendLimitTests, JointWithoutPmxJointToItsParentIsFreeAndReported)
{
    SkeletonData skeleton;
    PhysicsData physics;
    BuildSkirtFixture(skeleton, physics, /*dropMiddleColumnJoint=*/true);

    const DynamicChainDetectionResult detection = DetectDynamicChains(skeleton, &physics, DynamicChainDetectionDefaults{});
    ASSERT_EQ(detection.chains.size(), 1u);
    const DynamicChainDefinition& chain = detection.chains[0];

    EXPECT_FLOAT_EQ(chain.jointSettings[4].bendLimitRadians, kPi); // bone 5, its column joint was removed.
    EXPECT_EQ(detection.pmxJointIndexPerJoint[0][4], -1);
    EXPECT_EQ(detection.diagnostics.bendLimitMissingBoneIndices, (std::vector<std::int32_t>{ 5 }));
}

TEST(DynamicChainSolverBendLimitTests, DetectionMapsPmxDampingToAStepFraction)
{
    SkeletonData skeleton;
    PhysicsData physics;
    BuildSkirtFixture(skeleton, physics, false);
    physics.rigidBodies[1].linearDamping = 1.0f;

    const DynamicChainDetectionResult detection = DetectDynamicChains(skeleton, &physics, DynamicChainDetectionDefaults{});
    ASSERT_EQ(detection.chains.size(), 1u);
    EXPECT_NEAR(detection.chains[0].jointSettings[0].pmxLinearDamping, 1.0f, 1e-6f);
    EXPECT_NEAR(detection.chains[0].jointSettings[0].damping, 1.0f - std::exp(-1.0f / 60.0f), 1e-6f);
}

} // namespace gte
