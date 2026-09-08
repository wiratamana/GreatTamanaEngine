// Unit tests for SolveCollision (src/Physics/Collider.h) - the unified,
// shape-tagged collision dispatcher added by
// task_manager/verlet-integration-9's PHASE1
// (PHASE1_GENERIC_SHAPE_COLLISION_MATH_BOX_AND_CAPSULE.md). These are
// deliberately thin - they exist purely to prove SolveCollision() actually
// routes to the right underlying function and repacks fields correctly, NOT
// to re-test the underlying math already covered by SphereColliderTests.cpp/
// BoxColliderTests.cpp/CapsuleColliderTests.cpp.

#include "Physics/Collider.h"

#include "Physics/BoxCollider.h"
#include "Physics/CapsuleCollider.h"
#include "Physics/SphereCollider.h"

#include <gtest/gtest.h>

namespace gte {
namespace {

TEST(ColliderTests, SphereShapedColliderMatchesSolveSphereCollisionDirectly)
{
    VerletParticle a;
    a.position = Vec3(0.5f, 0.0f, 0.0f);
    a.previousPosition = Vec3(0.4f, 0.0f, 0.0f);
    VerletParticle b = a;

    const Collider collider{ ColliderShape::Sphere, Vec3::Zero(), Quat::Identity(), Vec3(2.0f, 0.0f, 0.0f) };
    SolveCollision(a, collider);

    const SphereCollider sphere{ Vec3::Zero(), 2.0f };
    SolveSphereCollision(b, sphere);

    EXPECT_TRUE(ApproximatelyEqual(a.position, b.position));
    EXPECT_TRUE(ApproximatelyEqual(a.previousPosition, b.previousPosition));
}

TEST(ColliderTests, BoxShapedColliderMatchesSolveBoxCollisionDirectly)
{
    VerletParticle a;
    a.position = Vec3(0.1f, 0.8f, 0.1f);
    VerletParticle b = a;

    const Vec3 halfExtents(2.0f, 1.0f, 3.0f);
    const Collider collider{ ColliderShape::Box, Vec3::Zero(), Quat::Identity(), halfExtents };
    SolveCollision(a, collider);

    const BoxCollider box{ Vec3::Zero(), Quat::Identity(), halfExtents };
    SolveBoxCollision(b, box);

    EXPECT_TRUE(ApproximatelyEqual(a.position, b.position));
}

TEST(ColliderTests, CapsuleShapedColliderMatchesSolveCapsuleCollisionDirectly)
{
    VerletParticle a;
    a.position = Vec3(0.2f, 0.0f, 0.0f);
    VerletParticle b = a;

    const Collider collider{ ColliderShape::Capsule, Vec3::Zero(), Quat::Identity(), Vec3(0.5f, 2.0f, 0.0f) };
    SolveCollision(a, collider);

    const CapsuleCollider capsule{ Vec3::Zero(), Quat::Identity(), 0.5f, 2.0f };
    SolveCapsuleCollision(b, capsule);

    EXPECT_TRUE(ApproximatelyEqual(a.position, b.position));
}

} // namespace
} // namespace gte
