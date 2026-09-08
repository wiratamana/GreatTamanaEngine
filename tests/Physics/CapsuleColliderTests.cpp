// Unit tests for SolveCapsuleCollision (src/Physics/CapsuleCollider.h) - the
// oriented-capsule collision projection added by
// task_manager/verlet-integration-9's PHASE1
// (PHASE1_GENERIC_SHAPE_COLLISION_MATH_BOX_AND_CAPSULE.md). No ECS/GPU/
// Renderer/SkeletonData involved - purely plain Vec3/Quat/VerletParticle
// math, mirroring SphereColliderTests.cpp's own structure/tone.

#include "Physics/CapsuleCollider.h"

#include <gtest/gtest.h>

#include <cmath>

namespace gte {
namespace {

TEST(CapsuleColliderTests, ParticleFullyOutsideCapsuleIsUntouched)
{
    VerletParticle particle;
    particle.position = Vec3(10.0f, 0.0f, 0.0f);
    particle.previousPosition = Vec3(9.5f, 0.0f, 0.0f);

    const CapsuleCollider collider{ Vec3::Zero(), Quat::Identity(), 1.0f, 2.0f };
    const Vec3 previousPositionBefore = particle.previousPosition;

    SolveCapsuleCollision(particle, collider);

    EXPECT_TRUE(ApproximatelyEqual(particle.position, Vec3(10.0f, 0.0f, 0.0f)));
    EXPECT_TRUE(ApproximatelyEqual(particle.previousPosition, previousPositionBefore));
}

TEST(CapsuleColliderTests, ParticleBesideTheCylindricalBodyIsProjectedRadiallyOutward)
{
    VerletParticle particle;
    // Capsule with local +Y axis (unrotated), segment from (0,-1,0) to (0,1,0), radius 0.5.
    const CapsuleCollider collider{ Vec3::Zero(), Quat::Identity(), 0.5f, 2.0f };
    // Particle beside the segment's midpoint, penetrating sideways.
    particle.position = Vec3(0.2f, 0.0f, 0.0f);

    SolveCapsuleCollision(particle, collider);

    // Segment midpoint is (0,0,0), closest point on the segment to the (unmoved-Y) particle is (0,0,0).
    const Vec3 segStart(0.0f, -1.0f, 0.0f);
    const Vec3 segEnd(0.0f, 1.0f, 0.0f);
    // Closest point on segment to the ORIGINAL particle position.
    const Vec3 closest(0.0f, 0.0f, 0.0f);
    EXPECT_NEAR((particle.position - closest).x * (particle.position - closest).x
                    + (particle.position - closest).y * (particle.position - closest).y
                    + (particle.position - closest).z * (particle.position - closest).z,
        collider.radius * collider.radius, 1e-3f);
    (void)segStart;
    (void)segEnd;
}

TEST(CapsuleColliderTests, ParticleBeyondTheEndCapIsProjectedFromTheNearestEndpointNotTheInfiniteLine)
{
    VerletParticle particle;
    const CapsuleCollider collider{ Vec3::Zero(), Quat::Identity(), 0.5f, 2.0f }; // segment (0,-1,0)-(0,1,0).
    // Beyond the +Y end (segment clamps at t=1, endpoint (0,1,0)), penetrating.
    particle.position = Vec3(0.2f, 1.05f, 0.0f);

    SolveCapsuleCollision(particle, collider);

    const Vec3 endpoint(0.0f, 1.0f, 0.0f);
    const float distFromEndpoint = Length(particle.position - endpoint);
    EXPECT_NEAR(distFromEndpoint, collider.radius, 1e-3f);
}

TEST(CapsuleColliderTests, RotatedCapsuleUsesItsOwnLocalPlusYAxis)
{
    VerletParticle particle;
    // Rotate the capsule 90 degrees around Z so its local +Y axis now points along world -X (or +X).
    const Quat rotation = Quat::FromAxisAngle(Vec3::Forward(), DegToRad(90.0f));
    const CapsuleCollider collider{ Vec3::Zero(), rotation, 0.5f, 2.0f };

    // In the capsule's own local space, place the particle beside the midpoint along local +X (sideways).
    const Vec3 localPenetrating(0.2f, 0.0f, 0.0f);
    particle.position = rotation.RotateVector(localPenetrating);

    SolveCapsuleCollision(particle, collider);

    // The result should still be `radius` away from the (rotated) segment's central axis point nearest it.
    const float distFromCenterAxis = Length(particle.position - collider.center);
    // Since we penetrated near the segment's midpoint (t=0.5), closest point is roughly `center`.
    EXPECT_NEAR(distFromCenterAxis, collider.radius, 1e-2f);
}

TEST(CapsuleColliderTests, NonPositiveHeightDegradesToAPureSphereAtCenter)
{
    VerletParticle particle;
    const CapsuleCollider collider{ Vec3(1.0f, 2.0f, 3.0f), Quat::Identity(), 0.5f, 0.0f };
    particle.position = collider.center + Vec3(0.1f, 0.0f, 0.0f);

    SolveCapsuleCollision(particle, collider);

    EXPECT_NEAR(Length(particle.position - collider.center), collider.radius, 1e-4f);
}

TEST(CapsuleColliderTests, PinnedParticleIsNeverMoved)
{
    VerletParticle particle;
    particle.position = Vec3::Zero();
    particle.pinned = true;

    const CapsuleCollider collider{ Vec3::Zero(), Quat::Identity(), 1.0f, 2.0f };

    SolveCapsuleCollision(particle, collider);

    EXPECT_TRUE(ApproximatelyEqual(particle.position, Vec3::Zero()));
}

TEST(CapsuleColliderTests, NonPositiveRadiusIsANoOp)
{
    VerletParticle particle;
    particle.position = Vec3(0.1f, 0.0f, 0.0f);

    const CapsuleCollider collider{ Vec3::Zero(), Quat::Identity(), 0.0f, 2.0f };

    SolveCapsuleCollision(particle, collider);

    EXPECT_TRUE(ApproximatelyEqual(particle.position, Vec3(0.1f, 0.0f, 0.0f)));
}

} // namespace
} // namespace gte
