// Unit tests for ColliderBounds (src/Physics/ColliderBounds.h): a collider that is out of reach must never move a particle.

#include "Physics/ColliderBounds.h"
#include "Physics/Collider.h"

#include <gtest/gtest.h>

#include <cmath>
#include <cstring>
#include <random>

namespace gte {
namespace {

Collider RandomCollider(std::mt19937& rng)
{
    std::uniform_real_distribution<float> u(-1.0f, 1.0f);
    std::uniform_real_distribution<float> size(0.0f, 3.0f);
    Collider c;
    c.shape = static_cast<ColliderShape>(rng() % 3);
    c.center = Vec3(u(rng) * 10.0f, u(rng) * 10.0f, u(rng) * 10.0f);
    c.rotation = Normalize(Quat(u(rng), u(rng), u(rng), u(rng) + 1.5f));
    c.size = Vec3(size(rng), size(rng), size(rng));
    return c;
}

} // namespace

TEST(ColliderBoundsTests, OutOfReachMeansTheExactSolverLeavesTheParticleUntouched)
{
    std::mt19937 rng(42);
    std::uniform_real_distribution<float> u(-1.0f, 1.0f);
    std::uniform_real_distribution<float> radius(0.0f, 1.0f);
    int rejected = 0;
    for (int i = 0; i < 20000; ++i) {
        const Collider collider = RandomCollider(rng);
        const ColliderReach reach = ComputeColliderReach(collider);
        VerletParticle particle;
        particle.position = Vec3(u(rng) * 14.0f, u(rng) * 14.0f, u(rng) * 14.0f);
        particle.collisionRadius = radius(rng);
        const float dx = particle.position.x - reach.center.x;
        const float dy = particle.position.y - reach.center.y;
        const float dz = particle.position.z - reach.center.z;
        const float limit = reach.bound + reach.perRadius * std::max(0.0f, particle.collisionRadius);
        if (dx * dx + dy * dy + dz * dz > limit * limit) {
            ++rejected;
            const Vec3 before = particle.position;
            SolveCollision(particle, collider);
            ASSERT_EQ(std::memcmp(&before, &particle.position, sizeof(Vec3)), 0) << "shape " << static_cast<int>(collider.shape);
        }
    }
    EXPECT_GT(rejected, 5000); // the test must really exercise the reject branch
}

TEST(ColliderBoundsTests, PenetratingParticlesAreNeverRejected)
{
    std::mt19937 rng(7);
    std::uniform_real_distribution<float> u(-1.0f, 1.0f);
    for (int i = 0; i < 20000; ++i) {
        const Collider collider = RandomCollider(rng);
        const ColliderReach reach = ComputeColliderReach(collider);
        VerletParticle particle;
        particle.collisionRadius = 0.25f;
        particle.position = collider.center + Vec3(u(rng), u(rng), u(rng)) * 2.0f;
        const Vec3 before = particle.position;
        SolveCollision(particle, collider);
        if (std::memcmp(&before, &particle.position, sizeof(Vec3)) != 0) { // the solver pushed it
            const float dx = before.x - reach.center.x;
            const float dy = before.y - reach.center.y;
            const float dz = before.z - reach.center.z;
            const float limit = reach.bound + reach.perRadius * particle.collisionRadius;
            EXPECT_FALSE(dx * dx + dy * dy + dz * dz > limit * limit) << "shape " << static_cast<int>(collider.shape);
        }
    }
}

TEST(ColliderBoundsTests, NonUnitBoxRotationAndNaNNeverReject)
{
    Collider box;
    box.shape = ColliderShape::Box;
    box.size = Vec3(1.0f, 1.0f, 1.0f);
    box.rotation = Quat(0.0f, 0.0f, 0.0f, 3.0f);
    EXPECT_TRUE(std::isinf(ComputeColliderReach(box).bound));

    Collider sphere;
    sphere.size = Vec3(1.0f, 0.0f, 0.0f);
    const ColliderReach reach = ComputeColliderReach(sphere);
    const float nan = std::nanf("");
    const float limit = reach.bound;
    EXPECT_FALSE(nan * nan > limit * limit); // a NaN distance is never "out of reach"
}

TEST(ColliderBoundsTests, SetPadsToFourLanesWithFarPaddingLanes)
{
    std::vector<Collider> colliders(5);
    ColliderReachSet set;
    ComputeColliderReach(colliders, set);
    EXPECT_EQ(set.count, 5u);
    EXPECT_EQ(set.padded, 8u);
    EXPECT_GT(set.X()[7], 1.0e17f);
}

} // namespace gte
