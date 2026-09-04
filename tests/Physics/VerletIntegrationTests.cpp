// Unit tests for IntegrateParticle (src/Physics/VerletIntegration.h) - the
// Position-Verlet integrator at the core of the Phase 1 physics foundation.
// No ECS/GPU/Renderer involved - pure math against hand-picked inputs.

#include "Physics/VerletIntegration.h"

#include <gtest/gtest.h>

#include <cmath>

namespace gte {
namespace {

TEST(VerletIntegrationTests, ParticleAtRestStaysAtRestUnderZeroAccelerationZeroDamping)
{
    VerletParticle particle;
    particle.position = Vec3(1.0f, 2.0f, 3.0f);
    particle.previousPosition = Vec3(1.0f, 2.0f, 3.0f); // no implied velocity

    IntegrateParticle(particle, 1.0f / 60.0f, Vec3::Zero(), 0.0f);

    EXPECT_TRUE(ApproximatelyEqual(particle.position, Vec3(1.0f, 2.0f, 3.0f)));
}

// Newton's first law - the core Verlet correctness check: an object in
// motion (implied by position - previousPosition) continues in a straight
// line at constant "speed" under zero acceleration/zero damping.
TEST(VerletIntegrationTests, ImpliedVelocityContinuesInStraightLineUnderNoForceNoDamping)
{
    const float dt = 1.0f / 60.0f;
    VerletParticle particle;
    particle.position = Vec3(1.0f, 0.0f, 0.0f);
    particle.previousPosition = Vec3(0.0f, 0.0f, 0.0f); // implied velocity of (1,0,0) units/step

    IntegrateParticle(particle, dt, Vec3::Zero(), 0.0f);

    // previousPosition becomes the old position; new position moves by the
    // exact same implied step again.
    EXPECT_TRUE(ApproximatelyEqual(particle.previousPosition, Vec3(1.0f, 0.0f, 0.0f)));
    EXPECT_TRUE(ApproximatelyEqual(particle.position, Vec3(2.0f, 0.0f, 0.0f)));
}

TEST(VerletIntegrationTests, DampingOfOneFullyKillsImpliedVelocityAfterOneStep)
{
    const float dt = 1.0f / 60.0f;
    VerletParticle particle;
    particle.position = Vec3(1.0f, 0.0f, 0.0f);
    particle.previousPosition = Vec3(0.0f, 0.0f, 0.0f);

    IntegrateParticle(particle, dt, Vec3::Zero(), 1.0f);

    // velocity term is fully damped -> newPosition == position (no motion),
    // previousPosition still moves up to the old position.
    EXPECT_TRUE(ApproximatelyEqual(particle.position, Vec3(1.0f, 0.0f, 0.0f)));
    EXPECT_TRUE(ApproximatelyEqual(particle.previousPosition, Vec3(1.0f, 0.0f, 0.0f)));

    // A second step now has zero implied velocity, so it stays put.
    IntegrateParticle(particle, dt, Vec3::Zero(), 1.0f);
    EXPECT_TRUE(ApproximatelyEqual(particle.position, Vec3(1.0f, 0.0f, 0.0f)));
}

TEST(VerletIntegrationTests, PinnedParticleNeverMovesRegardlessOfAccelerationOrDamping)
{
    VerletParticle particle;
    particle.position = Vec3(5.0f, 5.0f, 5.0f);
    particle.previousPosition = Vec3(0.0f, 0.0f, 0.0f); // would otherwise imply large velocity
    particle.pinned = true;

    IntegrateParticle(particle, 1.0f / 30.0f, Vec3(0.0f, -50.0f, 0.0f), 0.2f);

    EXPECT_TRUE(ApproximatelyEqual(particle.position, Vec3(5.0f, 5.0f, 5.0f)));
    // previousPosition tracks position every call for a pinned particle.
    EXPECT_TRUE(ApproximatelyEqual(particle.previousPosition, Vec3(5.0f, 5.0f, 5.0f)));
}

// Gravity-only free-fall over N fixed steps should match the closed-form
// 0.5 * g * t^2 within a small tolerance - validates the integration formula
// itself, not just the API contract.
TEST(VerletIntegrationTests, GravityOnlyFreeFallMatchesClosedFormWithinTolerance)
{
    const float dt = 1.0f / 240.0f; // small step for good closed-form accuracy
    const float gravity = -9.8f;
    const int steps = 240; // 1 second total

    VerletParticle particle;
    particle.position = Vec3::Zero();
    particle.previousPosition = Vec3::Zero();

    float elapsed = 0.0f;
    for (int i = 0; i < steps; ++i) {
        IntegrateParticle(particle, dt, Vec3(0.0f, gravity, 0.0f), 0.0f);
        elapsed += dt;
    }

    const float expectedY = 0.5f * gravity * elapsed * elapsed;
    EXPECT_NEAR(particle.position.y, expectedY, std::fabs(expectedY) * 0.01f + 1e-3f);
}

} // namespace
} // namespace gte
