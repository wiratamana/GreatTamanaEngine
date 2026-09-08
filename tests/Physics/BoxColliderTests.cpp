// Unit tests for SolveBoxCollision (src/Physics/BoxCollider.h) - the
// oriented-box collision projection added by
// task_manager/verlet-integration-9's PHASE1
// (PHASE1_GENERIC_SHAPE_COLLISION_MATH_BOX_AND_CAPSULE.md). No ECS/GPU/
// Renderer/SkeletonData involved - purely plain Vec3/Quat/VerletParticle
// math, mirroring SphereColliderTests.cpp's own structure/tone.

#include "Physics/BoxCollider.h"

#include <gtest/gtest.h>

#include <cmath>

namespace gte {
namespace {

TEST(BoxColliderTests, ParticleFullyOutsideBoxIsUntouched)
{
    VerletParticle particle;
    particle.position = Vec3(10.0f, 0.0f, 0.0f);
    particle.previousPosition = Vec3(9.5f, 0.0f, 0.0f);

    const BoxCollider collider{ Vec3::Zero(), Quat::Identity(), Vec3(1.0f, 1.0f, 1.0f) };
    const Vec3 previousPositionBefore = particle.previousPosition;

    SolveBoxCollision(particle, collider);

    EXPECT_TRUE(ApproximatelyEqual(particle.position, Vec3(10.0f, 0.0f, 0.0f)));
    EXPECT_TRUE(ApproximatelyEqual(particle.previousPosition, previousPositionBefore));
}

TEST(BoxColliderTests, ParticlePenetratingAlongShortestAxisIsPushedToThatFace)
{
    VerletParticle particle;
    const BoxCollider collider{ Vec3::Zero(), Quat::Identity(), Vec3(2.0f, 1.0f, 3.0f) };
    // Near-center, but closer to the Y-face (escape 0.2) than X (escape 1.9) or Z (escape 2.9).
    particle.position = Vec3(0.1f, 0.8f, 0.1f);

    SolveBoxCollision(particle, collider);

    EXPECT_NEAR(std::fabs(particle.position.y), collider.halfExtents.y, 1e-4f)
        << "Should have been pushed out along the Y face (smallest escape distance).";
    EXPECT_TRUE(particle.position.y > 0.0f) << "Sign should match the original local Y sign.";
    EXPECT_NEAR(particle.position.x, 0.1f, 1e-4f) << "X should be unchanged.";
    EXPECT_NEAR(particle.position.z, 0.1f, 1e-4f) << "Z should be unchanged.";
}

TEST(BoxColliderTests, ParticleAtExactCenterPicksAFixedDeterministicFaceWithoutNaN)
{
    VerletParticle particle;
    const BoxCollider collider{ Vec3::Zero(), Quat::Identity(), Vec3(2.0f, 1.0f, 3.0f) };
    particle.position = collider.center; // exactly at center - every local axis is 0.

    SolveBoxCollision(particle, collider);

    ASSERT_TRUE(std::isfinite(particle.position.x));
    ASSERT_TRUE(std::isfinite(particle.position.y));
    ASSERT_TRUE(std::isfinite(particle.position.z));
    // Smallest half-extent axis is Y (1.0) - sign(0) treated as positive.
    EXPECT_NEAR(particle.position.y, collider.halfExtents.y, 1e-4f);
    EXPECT_NEAR(particle.position.x, 0.0f, 1e-4f);
    EXPECT_NEAR(particle.position.z, 0.0f, 1e-4f);
}

TEST(BoxColliderTests, RotatedBoxPushesOutAlongItsOwnLocalAxes)
{
    VerletParticle particle;
    // Box rotated 90 degrees around Z - its local +X axis now points along world +Y.
    const Quat rotation = Quat::FromAxisAngle(Vec3::Forward(), DegToRad(90.0f));
    const BoxCollider collider{ Vec3::Zero(), rotation, Vec3(1.0f, 3.0f, 3.0f) };

    // Place the particle penetrating along the box's LOCAL +X (i.e. world +Y after rotation).
    const Vec3 localPenetrating(0.5f, 0.1f, 0.1f);
    particle.position = collider.center + rotation.RotateVector(localPenetrating);

    SolveBoxCollision(particle, collider);

    // Transform result back into the box's local space and confirm it lands on a face there.
    const Vec3 resultLocal = rotation.Inverse().RotateVector(particle.position - collider.center);
    EXPECT_NEAR(std::fabs(resultLocal.x), collider.halfExtents.x, 1e-3f)
        << "Push-out should land on the box's own local X face, proving rotation was applied.";
    EXPECT_NEAR(resultLocal.y, 0.1f, 1e-3f);
    EXPECT_NEAR(resultLocal.z, 0.1f, 1e-3f);
}

TEST(BoxColliderTests, PinnedParticleIsNeverMoved)
{
    VerletParticle particle;
    particle.position = Vec3::Zero(); // dead center - would otherwise be deeply penetrating.
    particle.pinned = true;

    const BoxCollider collider{ Vec3::Zero(), Quat::Identity(), Vec3(1.0f, 1.0f, 1.0f) };

    SolveBoxCollision(particle, collider);

    EXPECT_TRUE(ApproximatelyEqual(particle.position, Vec3::Zero()));
}

TEST(BoxColliderTests, DegenerateBoxWithOneOrMoreNonPositiveHalfExtentIsANoOp)
{
    const Vec3 degenerateExtents[] = {
        Vec3(0.0f, 1.0f, 1.0f),
        Vec3(-1.0f, 1.0f, 1.0f),
        Vec3(0.0f, 0.0f, 0.0f),
    };

    for (const Vec3& halfExtents : degenerateExtents) {
        VerletParticle particle;
        particle.position = Vec3::Zero(); // exactly at collider.center.

        const BoxCollider collider{ Vec3::Zero(), Quat::Identity(), halfExtents };

        SolveBoxCollision(particle, collider);

        EXPECT_TRUE(ApproximatelyEqual(particle.position, Vec3::Zero()))
            << "Degenerate half-extents (" << halfExtents.x << ", " << halfExtents.y << ", " << halfExtents.z
            << ") should be a no-op.";
    }
}

} // namespace
} // namespace gte
