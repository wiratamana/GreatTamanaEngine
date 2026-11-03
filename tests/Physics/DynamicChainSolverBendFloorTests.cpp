// Bend limit floor: cumulative sum rules and a rotating-pivot strand that must move but stay inside its cones.

#include "Physics/DynamicChainSolver.h"
#include "Physics/PhysicsDiagnostics.h"
#include "Physics/TimeScaling.h"
#include "Math/MathTypes.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace gte {
namespace {

constexpr float kTolDeg = 1e-3f;

// Chain with the given tree parents and own limits (degrees; 180 = free).
DynamicChainDefinition BuildChain(const std::vector<std::int32_t>& parents, const std::vector<float>& ownDegrees)
{
    const std::size_t jointCount = parents.size();
    DynamicChainDefinition definition;
    definition.jointBoneIndices.resize(jointCount);
    definition.parentJointIndex = parents;
    definition.jointSettings.assign(jointCount, DynamicJointSettings{});
    definition.restLengths.assign(jointCount, 1.0f);
    definition.collisionEnabled = false;
    for (std::size_t i = 0; i < jointCount; ++i) {
        definition.jointBoneIndices[i] = static_cast<std::int32_t>(i);
        definition.jointSettings[i].bendLimitRadians = ownDegrees[i] >= 180.0f ? kPi : DegToRad(ownDegrees[i]);
    }
    return definition;
}

DynamicChainDefinition BuildLine(std::size_t jointCount, float ownDegrees)
{
    return BuildChain(DynamicChainDefinition::MakeLinearParentIndices(jointCount), std::vector<float>(jointCount, ownDegrees));
}

StepOptions Options(float floorDegrees, float scale = 1.0f, float minDegrees = 0.0f)
{
    StepOptions options;
    options.bendLimitsEnabled = true;
    options.bendLimitScale = scale;
    options.bendLimitMinRadians = DegToRad(minDegrees);
    options.bendJointFloorRadians = DegToRad(floorDegrees);
    return options;
}

void ExpectDegrees(const std::vector<float>& actual, const std::vector<float>& expectedDegrees)
{
    ASSERT_EQ(actual.size(), expectedDegrees.size());
    for (std::size_t i = 0; i < actual.size(); ++i) {
        EXPECT_NEAR(RadToDeg(actual[i]), expectedDegrees[i], kTolDeg) << "joint " << i;
    }
}

// ---- Rotating pivot scenario ------------------------------------------------

constexpr std::size_t kScenarioJoints = 19;
constexpr float kScenarioRest = 0.45f;
constexpr float kScenarioStep = 1.0f / 120.0f;
constexpr int kHoldSteps = 60;   // 0.5 s.
constexpr int kTotalSteps = 720; // 6 s.

struct ScenarioResult {
    double meanDistance = 0.0;
    float tipMaxDistance = 0.0f;
    float rootChildMaxDistance = 0.0f;
    float maxConeExcessDegrees = -180.0f;
    float maxStretch = 0.0f;
    bool allFinite = true;
    std::uint32_t reseeds = 0;
};

bool IsFinite(const Vec3& v)
{
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}

// Animated joint i: straight down from the pivot, rotated about Z by angleDegrees.
Vec3 AnimatedJoint(const Vec3& pivot, std::size_t i, float angleDegrees)
{
    const float radians = DegToRad(angleDegrees);
    const float distance = kScenarioRest * static_cast<float>(i + 1);
    return pivot + Vec3(std::sin(radians) * distance, -std::cos(radians) * distance, 0.0f);
}

ScenarioResult RunRotatingPivot(float floorDegrees)
{
    DynamicChainDefinition definition;
    definition.jointBoneIndices.resize(kScenarioJoints);
    definition.parentJointIndex = DynamicChainDefinition::MakeLinearParentIndices(kScenarioJoints);
    definition.jointSettings.assign(kScenarioJoints, DynamicJointSettings{});
    definition.restLengths.assign(kScenarioJoints, kScenarioRest);
    definition.collisionEnabled = false;
    const float damping = PmxDampingToStepFraction(1.0f, 1.0f);
    for (std::size_t i = 0; i < kScenarioJoints; ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(kScenarioJoints - 1);
        DynamicJointSettings& js = definition.jointSettings[i];
        definition.jointBoneIndices[i] = static_cast<std::int32_t>(i);
        js.mass = 30.0f * std::pow(0.01f / 30.0f, t);
        js.damping = damping;
        js.stiffness = 0.02f;
        js.bendLimitRadians = 0.0f;
    }

    const float floorRadians = DegToRad(floorDegrees);
    std::vector<float> cumulative;
    ComputeCumulativeBendLimits(definition, floorRadians, cumulative);

    StepDiagnostics diagnostics;
    StepOptions options = Options(floorDegrees);
    options.diagnostics = &diagnostics;

    const Vec3 pivot(0.0f, 20.0f, 0.0f);
    const Vec3 gravity(0.0f, -98.0f, 0.0f);
    DynamicChainRuntimeState state;
    ScenarioResult result;
    double distanceSum = 0.0;
    std::size_t samples = 0;

    std::vector<Vec3> animated(kScenarioJoints);
    for (int step = 0; step < kTotalSteps; ++step) {
        const float time = static_cast<float>(step) * kScenarioStep;
        const float angleDegrees = time >= 0.5f ? 30.0f * std::sin(2.0f * kPi * (time - 0.5f)) : 0.0f;
        for (std::size_t i = 0; i < kScenarioJoints; ++i) {
            animated[i] = AnimatedJoint(pivot, i, angleDegrees);
        }
        StepDynamicChain(definition, pivot, animated, state, kScenarioStep, gravity, WindSettings{}, {}, options);
        if (step < kHoldSteps) {
            continue;
        }

        for (std::size_t i = 0; i < kScenarioJoints; ++i) {
            const Vec3 simPos = state.particles[i].position;
            result.allFinite = result.allFinite && IsFinite(simPos);
            const float distance = Length(simPos - animated[i]);
            distanceSum += distance;
            ++samples;
            if (i == 0) {
                result.rootChildMaxDistance = std::max(result.rootChildMaxDistance, distance);
            }
            if (i == kScenarioJoints - 1) {
                result.tipMaxDistance = std::max(result.tipMaxDistance, distance);
            }

            const Vec3 simParent = i == 0 ? pivot : state.particles[i - 1].position;
            const Vec3 animParent = i == 0 ? pivot : animated[i - 1];
            const Vec3 simSegment = simPos - simParent;
            const float excess = VectorAngleDegrees(simSegment, animated[i] - animParent) - RadToDeg(cumulative[i]);
            result.maxConeExcessDegrees = std::max(result.maxConeExcessDegrees, excess);
            result.maxStretch = std::max(result.maxStretch, std::fabs(Length(simSegment) / kScenarioRest - 1.0f));
        }
    }
    result.meanDistance = samples > 0 ? distanceSum / static_cast<double>(samples) : 0.0;
    result.reseeds = diagnostics.reseeds;
    return result;
}

void ExpectCommonGuarantees(const ScenarioResult& r)
{
    EXPECT_TRUE(r.allFinite);
    EXPECT_EQ(r.reseeds, 0u);
    EXPECT_LE(r.rootChildMaxDistance, 0.001f);
    EXPECT_LE(r.maxConeExcessDegrees, 0.1f);
    EXPECT_LE(r.maxStretch, 0.001f);
}

} // namespace

// ---- Sum rules --------------------------------------------------------------

TEST(DynamicChainSolverBendFloorTests, FloorGrowsTheLimitByOneFloorPerJoint)
{
    const DynamicChainDefinition definition = BuildLine(5, 0.0f);
    ExpectDegrees(ComputeEffectiveBendLimits(definition, Options(3.0f)), { 0.0f, 3.0f, 6.0f, 9.0f, 12.0f });
}

TEST(DynamicChainSolverBendFloorTests, FloorNeverLowersAnOwnLimit)
{
    const DynamicChainDefinition definition = BuildChain({ -1, 0, 1, 2 }, { 0.0f, 1.0f, 10.0f, 0.0f });
    std::vector<float> cumulative;
    std::vector<float> flooredOwn;
    ComputeCumulativeBendLimits(definition, DegToRad(3.0f), cumulative, &flooredOwn);
    ExpectDegrees(flooredOwn, { 0.0f, 3.0f, 10.0f, 3.0f });
    ExpectDegrees(cumulative, { 0.0f, 3.0f, 13.0f, 16.0f });
}

TEST(DynamicChainSolverBendFloorTests, RootChildrenAreNeverTouchedByTheFloor)
{
    const DynamicChainDefinition own5 = BuildChain({ -1, 0 }, { 5.0f, 0.0f });
    std::vector<float> cumulative;
    std::vector<float> flooredOwn;
    ComputeCumulativeBendLimits(own5, DegToRad(3.0f), cumulative, &flooredOwn);
    EXPECT_NEAR(RadToDeg(flooredOwn[0]), 5.0f, kTolDeg);
    EXPECT_NEAR(RadToDeg(cumulative[0]), 5.0f, kTolDeg);
    EXPECT_NEAR(RadToDeg(cumulative[1]), 8.0f, kTolDeg);

    const DynamicChainDefinition own0 = BuildChain({ -1, 0 }, { 0.0f, 0.0f });
    for (const float floorDegrees : { 3.0f, 45.0f }) {
        ComputeCumulativeBendLimits(own0, DegToRad(floorDegrees), cumulative, &flooredOwn);
        EXPECT_FLOAT_EQ(flooredOwn[0], 0.0f);
        EXPECT_FLOAT_EQ(cumulative[0], 0.0f);
    }
}

TEST(DynamicChainSolverBendFloorTests, FreeJointStaysFreeAndTheSumIsCappedAtPi)
{
    const DynamicChainDefinition definition = BuildChain({ -1, 0, 1 }, { 0.0f, 180.0f, 0.0f });
    std::vector<float> cumulative;
    std::vector<float> flooredOwn;
    ComputeCumulativeBendLimits(definition, DegToRad(3.0f), cumulative, &flooredOwn);
    EXPECT_FLOAT_EQ(flooredOwn[1], kPi);
    EXPECT_FLOAT_EQ(cumulative[1], kPi);
    EXPECT_FLOAT_EQ(cumulative[2], kPi);
}

TEST(DynamicChainSolverBendFloorTests, BranchChildrenBothAddTheFloorToTheParent)
{
    const DynamicChainDefinition definition = BuildChain({ -1, 0, 0 }, { 0.0f, 0.0f, 0.0f });
    std::vector<float> cumulative;
    ComputeCumulativeBendLimits(definition, DegToRad(3.0f), cumulative);
    ExpectDegrees(cumulative, { 0.0f, 3.0f, 3.0f });
}

TEST(DynamicChainSolverBendFloorTests, ZeroFloorReproducesThePlainSumExactly)
{
    const DynamicChainDefinition definition
        = BuildChain({ -1, 0, 0, 1, -1, 4, 5 }, { 2.0f, 5.0f, 7.0f, 0.0f, 10.0f, 180.0f, 20.0f });
    const std::size_t count = definition.parentJointIndex.size();

    std::vector<float> expected(count, 0.0f);
    for (std::size_t i = 0; i < count; ++i) {
        const std::int32_t parent = definition.parentJointIndex[i];
        const float parentCumulative = parent >= 0 ? expected[static_cast<std::size_t>(parent)] : 0.0f;
        expected[i] = std::min(kPi, definition.jointSettings[i].bendLimitRadians + parentCumulative);
    }

    std::vector<float> cumulative;
    ComputeCumulativeBendLimits(definition, 0.0f, cumulative);
    EXPECT_EQ(cumulative, expected);

    StepOptions options;
    options.bendLimitsEnabled = true;
    std::vector<float> effective = ComputeEffectiveBendLimits(definition, options);
    for (std::size_t i = 0; i < count; ++i) {
        expected[i] = std::clamp(expected[i] * 1.0f + 0.0f, 0.0f, kPi);
    }
    EXPECT_EQ(effective, expected);
}

TEST(DynamicChainSolverBendFloorTests, ScaleAndMinimumApplyAfterTheFloor)
{
    const DynamicChainDefinition definition = BuildLine(3, 0.0f);
    ExpectDegrees(ComputeEffectiveBendLimits(definition, Options(3.0f, 2.0f, 1.0f)), { 1.0f, 7.0f, 13.0f });
}

TEST(DynamicChainSolverBendFloorTests, DisabledLimitsIgnoreTheFloorAndLargeFloorsStayCapped)
{
    const DynamicChainDefinition definition = BuildLine(5, 0.0f);

    StepOptions disabled = Options(3.0f);
    disabled.bendLimitsEnabled = false;
    for (const float limit : ComputeEffectiveBendLimits(definition, disabled)) {
        EXPECT_EQ(limit, kPi);
    }

    const std::vector<float> big = ComputeEffectiveBendLimits(definition, Options(45.0f));
    ExpectDegrees(big, { 0.0f, 45.0f, 90.0f, 135.0f, 180.0f });
    for (const float limit : big) {
        EXPECT_TRUE(std::isfinite(limit));
    }
}

// ---- Rotating pivot ---------------------------------------------------------

TEST(DynamicChainSolverBendFloorTests, ZeroFloorKeepsTheRotatingStrandWelded)
{
    const ScenarioResult r = RunRotatingPivot(0.0f);
    ExpectCommonGuarantees(r);
    EXPECT_LE(r.meanDistance, 0.001);
    EXPECT_LE(r.tipMaxDistance, 0.01f);
}

TEST(DynamicChainSolverBendFloorTests, OneDegreeFloorLetsTheRotatingStrandSwing)
{
    const ScenarioResult r = RunRotatingPivot(1.0f);
    ExpectCommonGuarantees(r);
    EXPECT_GE(r.meanDistance, 0.15);
    EXPECT_LE(r.meanDistance, 0.60);
    EXPECT_GE(r.tipMaxDistance, 0.6f);
    EXPECT_LE(r.tipMaxDistance, 2.6f);
}

TEST(DynamicChainSolverBendFloorTests, ThreeDegreeFloorLetsTheRotatingStrandSwingMore)
{
    const ScenarioResult r = RunRotatingPivot(3.0f);
    ExpectCommonGuarantees(r);
    EXPECT_GE(r.meanDistance, 0.20);
    EXPECT_LE(r.meanDistance, 0.90);
    EXPECT_GE(r.tipMaxDistance, 1.2f);
    EXPECT_LE(r.tipMaxDistance, 4.9f);
}

} // namespace gte
