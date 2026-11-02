// Stability of StepDynamicChain(): welded strands under an accelerating anchor, the divergence guard, cone vs collision.

#include "Physics/Collider.h"
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

constexpr float kStep = 1.0f / 120.0f;

struct Strand {
    DynamicChainDefinition definition;
    std::vector<Vec3> shape; // joint positions relative to the anchor.
};

// A bent strand of `jointCount` joints: rest lengths 0.57..1.95, masses 30..0.01, one own limit everywhere.
Strand BuildStrand(std::size_t jointCount, float ownLimitRadians)
{
    Strand strand;
    DynamicChainDefinition& definition = strand.definition;
    definition.jointBoneIndices.resize(jointCount);
    definition.parentJointIndex = DynamicChainDefinition::MakeLinearParentIndices(jointCount);
    definition.jointSettings.assign(jointCount, DynamicJointSettings{});
    definition.restLengths.resize(jointCount);
    definition.collisionEnabled = false;

    Vec3 position = Vec3::Zero();
    const float last = static_cast<float>(jointCount - 1);
    for (std::size_t i = 0; i < jointCount; ++i) {
        const float t = static_cast<float>(i) / last;
        const float rest = 0.57f + (1.95f - 0.57f) * t;
        const float angle = 0.12f * static_cast<float>(i);
        position += Vec3(std::sin(angle), -std::cos(angle), 0.0f) * rest;

        definition.jointBoneIndices[i] = static_cast<std::int32_t>(i);
        definition.restLengths[i] = rest;
        DynamicJointSettings& settings = definition.jointSettings[i];
        settings.mass = 30.0f * std::pow(0.01f / 30.0f, t);
        settings.damping = 0.0165f;
        settings.bendLimitRadians = ownLimitRadians;
        strand.shape.push_back(position);
    }
    return strand;
}

std::vector<Vec3> TargetsAt(const Strand& strand, const Vec3& anchor)
{
    std::vector<Vec3> targets(strand.shape.size());
    for (std::size_t i = 0; i < targets.size(); ++i) {
        targets[i] = anchor + strand.shape[i];
    }
    return targets;
}

bool IsSane(const Vec3& v, float bound)
{
    return std::fabs(v.x) <= bound && std::fabs(v.y) <= bound && std::fabs(v.z) <= bound;
}

StepOptions BendOptions(StepDiagnostics* diagnostics)
{
    StepOptions options;
    options.bendLimitsEnabled = true;
    options.diagnostics = diagnostics;
    return options;
}

} // namespace

// The anchor speeds up 8x in one frame; a welded strand must follow without any runaway.
TEST(DynamicChainSolverStabilityTests, WeldedHeavyToLightStrandSurvivesAnchorAcceleration)
{
    constexpr std::size_t kJoints = 19;
    const Strand strand = BuildStrand(kJoints, 0.0f);

    DynamicChainRuntimeState state;
    StepDiagnostics diagnostics;
    const StepOptions options = BendOptions(&diagnostics);
    const Vec3 gravity(0.0f, -98.0f, 0.0f);

    Vec3 anchor = Vec3::Zero();
    for (int frame = 0; frame < 150; ++frame) {
        const float heading = 0.02f * static_cast<float>(frame);
        const float length = frame < 120 ? 0.06f : 0.5f;
        const Vec3 next = anchor + Vec3(std::cos(heading), 0.0f, std::sin(heading)) * length;

        for (int step = 1; step <= 2; ++step) {
            const Vec3 root = Lerp(anchor, next, 0.5f * static_cast<float>(step));
            const std::vector<Vec3> targets = TargetsAt(strand, root);
            StepDynamicChain(strand.definition, root, targets, state, kStep, gravity, WindSettings{}, {}, options);

            for (std::size_t i = 0; i < kJoints; ++i) {
                const VerletParticle& particle = state.particles[i];
                ASSERT_LE(Length(particle.position - targets[i]), 1.0f) << "frame " << frame << " joint " << i;
                ASSERT_TRUE(IsSane(particle.position, 100.0f)) << "frame " << frame << " joint " << i;
                ASSERT_TRUE(IsSane(particle.position - particle.previousPosition, 100.0f)) << "frame " << frame << " joint " << i;
            }
        }
        anchor = next;
    }
    EXPECT_EQ(diagnostics.nanGuardHits, 0u) << "the guard must not be what keeps the strand stable";
    EXPECT_EQ(state.reseedCount, 0u);
}

// The cone rebuilds the chain at exact rest lengths, so a welded strand never stretches.
TEST(DynamicChainSolverStabilityTests, ConePassKeepsEveryRodAtItsRestLength)
{
    constexpr std::size_t kJoints = 19;
    const Strand strand = BuildStrand(kJoints, 0.0f);

    DynamicChainRuntimeState state;
    const StepOptions options = BendOptions(nullptr);
    Vec3 root = Vec3::Zero();
    for (int step = 0; step < 240; ++step) {
        root += Vec3(0.3f, 0.0f, 0.1f) * (step % 7 == 0 ? 2.0f : 0.2f);
        StepDynamicChain(strand.definition, root, TargetsAt(strand, root), state, kStep, Vec3(0.0f, -98.0f, 0.0f), WindSettings{}, {}, options);
        for (std::size_t i = 0; i < kJoints; ++i) {
            const Vec3 parent = i == 0 ? root : state.particles[i - 1].position;
            ASSERT_NEAR(Length(state.particles[i].position - parent), strand.definition.restLengths[i], 1e-3f)
                << "step " << step << " joint " << i;
        }
    }
}

// A chain thrown far from its animated pose reseeds and is counted.
TEST(DynamicChainSolverStabilityTests, RunawayChainReseedsAndIsCounted)
{
    const Strand strand = BuildStrand(4, kPi);
    DynamicChainRuntimeState state;
    const Vec3 root = Vec3::Zero();
    const std::vector<Vec3> targets = TargetsAt(strand, root);
    StepDynamicChain(strand.definition, root, targets, state, kStep, Vec3::Zero(), WindSettings{});

    for (VerletParticle& particle : state.particles) {
        particle.position += Vec3(1.0e6f, 0.0f, 0.0f);
        particle.previousPosition = particle.position;
    }

    StepDiagnostics diagnostics;
    StepOptions options;
    options.diagnostics = &diagnostics;
    StepDynamicChain(strand.definition, root, targets, state, kStep, Vec3::Zero(), WindSettings{}, {}, options);

    EXPECT_GE(diagnostics.nanGuardHits, 1u);
    EXPECT_EQ(diagnostics.reseeds, 1u);
    EXPECT_EQ(state.reseedCount, 1u);
    for (std::size_t i = 0; i < targets.size(); ++i) {
        EXPECT_LE(Length(state.particles[i].position - targets[i]), 1e-4f) << "joint " << i;
    }
}

// Collision may push a joint, but the finished pose still obeys every cone.
TEST(DynamicChainSolverStabilityTests, FinalPoseObeysTheConeAfterCollision)
{
    constexpr std::size_t kJoints = 4;
    Strand strand = BuildStrand(kJoints, DegToRad(5.0f));
    strand.definition.collisionEnabled = true;
    std::vector<float> cumulative;
    ComputeCumulativeBendLimits(strand.definition, cumulative);

    Collider sphere;
    sphere.shape = ColliderShape::Sphere;
    sphere.center = strand.shape[2] + Vec3(0.4f, 0.0f, 0.0f);
    sphere.size = Vec3(0.8f, 0.0f, 0.0f);
    const std::vector<Collider> colliders{ sphere };

    DynamicChainRuntimeState state;
    const StepOptions options = BendOptions(nullptr);
    const std::vector<Vec3> targets = TargetsAt(strand, Vec3::Zero());
    for (int step = 0; step < 120; ++step) {
        StepDynamicChain(strand.definition, Vec3::Zero(), targets, state, kStep, Vec3(0.0f, -98.0f, 0.0f), WindSettings{}, colliders, options);
        for (std::size_t i = 0; i < kJoints; ++i) {
            const Vec3 parent = i == 0 ? Vec3::Zero() : state.particles[i - 1].position;
            const Vec3 animatedParent = i == 0 ? Vec3::Zero() : targets[i - 1];
            const float error = VectorAngleDegrees(state.particles[i].position - parent, targets[i] - animatedParent);
            ASSERT_LE(error, RadToDeg(cumulative[i]) + 0.05f) << "step " << step << " joint " << i;
        }
    }
}

// A welded joint cannot move, so a collider that overlaps it never pushes it.
TEST(DynamicChainSolverStabilityTests, WeldedJointsIgnoreCollision)
{
    Strand strand = BuildStrand(2, 0.0f);
    strand.definition.collisionEnabled = true;

    Collider sphere;
    sphere.shape = ColliderShape::Sphere;
    sphere.center = strand.shape[0];
    sphere.size = Vec3(2.0f, 0.0f, 0.0f);

    DynamicChainRuntimeState state;
    StepDiagnostics diagnostics;
    const std::vector<Vec3> targets = TargetsAt(strand, Vec3::Zero());
    for (int step = 0; step < 10; ++step) {
        StepDynamicChain(strand.definition, Vec3::Zero(), targets, state, kStep, Vec3::Zero(), WindSettings{},
            { sphere }, BendOptions(&diagnostics));
    }
    EXPECT_EQ(diagnostics.collisionPushedParticles, 0u);
    EXPECT_LE(Length(state.particles[0].position - targets[0]), 1e-4f);
    EXPECT_LE(Length(state.particles[1].position - targets[1]), 1e-4f);
}

} // namespace gte
