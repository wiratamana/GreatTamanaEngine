// Unit tests for SolveDistanceConstraint/SolveGoalConstraint
// (src/Physics/ChainConstraints.h) - the two independent constraint kinds
// the Phase 1 physics foundation provides. No ECS/GPU/Renderer involved.

#include "Physics/ChainConstraints.h"

#include <gtest/gtest.h>

#include <cmath>

namespace gte {
namespace {

TEST(ChainConstraintsTests, DistanceConstraintPullsStretchedParticlesTogetherByPredictedAmount)
{
    VerletParticle a;
    a.position = Vec3(0.0f, 0.0f, 0.0f);
    VerletParticle b;
    b.position = Vec3(2.0f, 0.0f, 0.0f); // stretched to length 2, rest length 1

    SolveDistanceConstraint(a, b, 1.0f, 1.0f);

    // Equal inverse mass (1.0 each) -> each particle moves exactly half of
    // the correction. diff = (2-1)/2 = 0.5, correction = (2,0,0)*0.5 = (1,0,0)
    // a += correction * 0.5 = (0.5,0,0); b -= correction * 0.5 -> (2,0,0)-(0.5,0,0) = (1.5,0,0)
    EXPECT_TRUE(ApproximatelyEqual(a.position, Vec3(0.5f, 0.0f, 0.0f)));
    EXPECT_TRUE(ApproximatelyEqual(b.position, Vec3(1.5f, 0.0f, 0.0f)));
}

TEST(ChainConstraintsTests, DistanceConstraintLeavesParticlesAtRestLengthUntouched)
{
    VerletParticle a;
    a.position = Vec3(0.0f, 0.0f, 0.0f);
    VerletParticle b;
    b.position = Vec3(1.0f, 0.0f, 0.0f); // already at rest length

    SolveDistanceConstraint(a, b, 1.0f, 1.0f);

    EXPECT_TRUE(ApproximatelyEqual(a.position, Vec3(0.0f, 0.0f, 0.0f)));
    EXPECT_TRUE(ApproximatelyEqual(b.position, Vec3(1.0f, 0.0f, 0.0f)));
}

TEST(ChainConstraintsTests, PinnedSideReceivesNoCorrectionOtherSideReceivesAllOfIt)
{
    VerletParticle a;
    a.position = Vec3(0.0f, 0.0f, 0.0f);
    a.pinned = true;
    a.inverseMass = 0.0f;
    VerletParticle b;
    b.position = Vec3(2.0f, 0.0f, 0.0f);

    SolveDistanceConstraint(a, b, 1.0f, 1.0f);

    // a is pinned/zero-invmass -> untouched. b gets the full correction:
    // invMassSum = 0 + 1 = 1, diff = 0.5, correction = (1,0,0)
    // b -= correction * (1/1) = (2,0,0) - (1,0,0) = (1,0,0)
    EXPECT_TRUE(ApproximatelyEqual(a.position, Vec3(0.0f, 0.0f, 0.0f)));
    EXPECT_TRUE(ApproximatelyEqual(b.position, Vec3(1.0f, 0.0f, 0.0f)));
}

// v2 regression (PHASE0 Revision Notes, finding #1): both particles have
// inverseMass == 0 but NEITHER is flagged `pinned` - the guard must check
// invMassSum directly, never divide by (near) zero.
TEST(ChainConstraintsTests, BothZeroInverseMassWithoutPinnedFlagIsSafeNoOp)
{
    VerletParticle a;
    a.position = Vec3(0.0f, 0.0f, 0.0f);
    a.inverseMass = 0.0f;
    a.pinned = false;
    VerletParticle b;
    b.position = Vec3(5.0f, 0.0f, 0.0f);
    b.inverseMass = 0.0f;
    b.pinned = false;

    SolveDistanceConstraint(a, b, 1.0f, 1.0f);

    EXPECT_TRUE(std::isfinite(a.position.x));
    EXPECT_TRUE(std::isfinite(b.position.x));
    EXPECT_TRUE(ApproximatelyEqual(a.position, Vec3(0.0f, 0.0f, 0.0f)));
    EXPECT_TRUE(ApproximatelyEqual(b.position, Vec3(5.0f, 0.0f, 0.0f)));
}

TEST(ChainConstraintsTests, GoalConstraintZeroStiffnessLeavesParticleUntouched)
{
    VerletParticle particle;
    particle.position = Vec3(1.0f, 1.0f, 1.0f);

    SolveGoalConstraint(particle, Vec3(10.0f, 10.0f, 10.0f), 0.0f);

    EXPECT_TRUE(ApproximatelyEqual(particle.position, Vec3(1.0f, 1.0f, 1.0f)));
}

TEST(ChainConstraintsTests, GoalConstraintFullStiffnessSnapsExactlyOntoTarget)
{
    VerletParticle particle;
    particle.position = Vec3(1.0f, 1.0f, 1.0f);

    SolveGoalConstraint(particle, Vec3(10.0f, 10.0f, 10.0f), 1.0f);

    EXPECT_TRUE(ApproximatelyEqual(particle.position, Vec3(10.0f, 10.0f, 10.0f)));
}

TEST(ChainConstraintsTests, GoalConstraintHalfStiffnessLandsExactlyHalfway)
{
    VerletParticle particle;
    particle.position = Vec3(0.0f, 0.0f, 0.0f);

    SolveGoalConstraint(particle, Vec3(10.0f, 0.0f, 0.0f), 0.5f);

    EXPECT_TRUE(ApproximatelyEqual(particle.position, Vec3(5.0f, 0.0f, 0.0f)));
}

} // namespace
} // namespace gte
