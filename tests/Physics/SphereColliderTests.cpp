// Unit tests for SolveSphereCollision (src/Physics/SphereCollider.h) - the
// bare-bone, single-sphere collision projection PHASE5 adds
// (verlet-integration-1 campaign,
// PHASE5_COLLISION_STABILITY_AND_PERFORMANCE_HARDENING.md, 3.1). No ECS/GPU/
// Renderer/SkeletonData involved - purely plain Vec3/VerletParticle math.

#include "Physics/SphereCollider.h"

#include <gtest/gtest.h>

#include <cmath>

namespace gte {
namespace {

TEST(SphereColliderTests, ParticleFullyOutsideSphereIsUntouched)
{
    VerletParticle particle;
    particle.position = Vec3(5.0f, 0.0f, 0.0f);
    particle.previousPosition = Vec3(4.5f, 0.0f, 0.0f);

    const SphereCollider collider{ Vec3::Zero(), 1.0f };
    const Vec3 previousPositionBefore = particle.previousPosition;

    SolveSphereCollision(particle, collider);

    EXPECT_TRUE(ApproximatelyEqual(particle.position, Vec3(5.0f, 0.0f, 0.0f)));
    EXPECT_TRUE(ApproximatelyEqual(particle.previousPosition, previousPositionBefore));
}

TEST(SphereColliderTests, ParticleInsideSphereIsProjectedExactlyOntoSurfaceAlongCorrectDirection)
{
    VerletParticle particle;
    particle.position = Vec3(0.5f, 0.0f, 0.0f); // inside a radius-2 sphere centered at (0,0,0).
    particle.previousPosition = Vec3(0.4f, 0.0f, 0.0f);

    const SphereCollider collider{ Vec3::Zero(), 2.0f };
    const Vec3 previousPositionBefore = particle.previousPosition;

    SolveSphereCollision(particle, collider);

    EXPECT_TRUE(ApproximatelyEqual(particle.position, Vec3(2.0f, 0.0f, 0.0f), 1e-4f))
        << "Particle should have been pushed straight out along +X to the sphere's surface.";
    EXPECT_NEAR(Length(particle.position - collider.center), collider.radius, 1e-4f);
    // previousPosition is left untouched (see SolveSphereCollision's own doc comment).
    EXPECT_TRUE(ApproximatelyEqual(particle.previousPosition, previousPositionBefore));
}

TEST(SphereColliderTests, ParticleInsideSphereOffCenterProjectsAlongCorrectDirection)
{
    VerletParticle particle;
    const SphereCollider collider{ Vec3(1.0f, 2.0f, 3.0f), 1.5f };
    particle.position = collider.center + Vec3(0.1f, 0.0f, 0.0f); // deep inside, offset along +X only.

    SolveSphereCollision(particle, collider);

    const Vec3 expected = collider.center + Vec3(1.5f, 0.0f, 0.0f);
    EXPECT_TRUE(ApproximatelyEqual(particle.position, expected, 1e-4f));
}

TEST(SphereColliderTests, PinnedParticleIsNeverMoved)
{
    VerletParticle particle;
    particle.position = Vec3::Zero(); // dead center - would otherwise trigger the degenerate branch.
    particle.pinned = true;

    const SphereCollider collider{ Vec3::Zero(), 1.0f };

    SolveSphereCollision(particle, collider);

    EXPECT_TRUE(ApproximatelyEqual(particle.position, Vec3::Zero()));
}

TEST(SphereColliderTests, DegenerateAtCenterCaseDoesNotProduceNaN)
{
    VerletParticle particle;
    particle.position = Vec3::Zero(); // exactly on the collider's own center.

    const SphereCollider collider{ Vec3::Zero(), 1.0f };

    SolveSphereCollision(particle, collider);

    ASSERT_TRUE(std::isfinite(particle.position.x));
    ASSERT_TRUE(std::isfinite(particle.position.y));
    ASSERT_TRUE(std::isfinite(particle.position.z));
    EXPECT_NEAR(Length(particle.position - collider.center), collider.radius, 1e-4f);
}

TEST(SphereColliderTests, NonPositiveRadiusIsANoOp)
{
    VerletParticle particle;
    particle.position = Vec3(0.1f, 0.0f, 0.0f);

    const SphereCollider collider{ Vec3::Zero(), 0.0f };

    SolveSphereCollision(particle, collider);

    EXPECT_TRUE(ApproximatelyEqual(particle.position, Vec3(0.1f, 0.0f, 0.0f)));
}

// task_manager/verlet-integration-10, PHASE2 - a joint particle's own
// PMX-rigid-body-shape-derived collision radius (VerletParticle::
// collisionRadius) inflates the effective push-out surface.
TEST(SphereColliderTests, ParticleWithNonZeroCollisionRadiusIsPushedFartherThanAZeroRadiusParticle)
{
    const SphereCollider collider{ Vec3::Zero(), 2.0f };

    VerletParticle zeroRadiusParticle;
    zeroRadiusParticle.position = Vec3(0.5f, 0.0f, 0.0f); // inside the sphere.
    SolveSphereCollision(zeroRadiusParticle, collider);

    VerletParticle inflatedParticle;
    inflatedParticle.position = Vec3(0.5f, 0.0f, 0.0f);
    inflatedParticle.collisionRadius = 0.3f;
    SolveSphereCollision(inflatedParticle, collider);

    const float zeroRadiusDistance = Length(zeroRadiusParticle.position - collider.center);
    const float inflatedDistance = Length(inflatedParticle.position - collider.center);
    EXPECT_GT(inflatedDistance, zeroRadiusDistance);
    EXPECT_NEAR(zeroRadiusDistance, collider.radius, 1e-4f);
    EXPECT_NEAR(inflatedDistance, collider.radius + 0.3f, 1e-4f);
}

TEST(SphereColliderTests, ZeroCollisionRadiusReproducesExactPreExistingBehavior)
{
    const SphereCollider collider{ Vec3(1.0f, 2.0f, 3.0f), 1.5f };

    VerletParticle explicitZero;
    explicitZero.position = collider.center + Vec3(0.1f, 0.0f, 0.0f);
    explicitZero.collisionRadius = 0.0f;
    SolveSphereCollision(explicitZero, collider);

    VerletParticle neverSet;
    neverSet.position = collider.center + Vec3(0.1f, 0.0f, 0.0f);
    SolveSphereCollision(neverSet, collider);

    EXPECT_TRUE(ApproximatelyEqual(explicitZero.position, neverSet.position));
}

} // namespace
} // namespace gte
