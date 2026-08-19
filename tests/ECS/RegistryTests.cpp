// Unit tests for Registry (src/ECS/Registry.h) - EntityManager +
// ComponentStorage<T> glued together, exercised with real entities and more
// than one component type (including the engine's first real component,
// Transform - see src/ECS/Components/Transform.h).

#include "ECS/Components/Transform.h"
#include "ECS/Registry.h"

#include <gtest/gtest.h>

namespace gte {
namespace {

struct Velocity {
    float dx = 0.0f;
    float dy = 0.0f;
};

TEST(RegistryTest, CreateEntityIsAliveAndUnique)
{
    Registry registry;
    const Entity a = registry.CreateEntity();
    const Entity b = registry.CreateEntity();

    EXPECT_TRUE(registry.IsAlive(a));
    EXPECT_TRUE(registry.IsAlive(b));
    EXPECT_NE(a, b);
    EXPECT_EQ(registry.AliveEntityCount(), 2u);
}

TEST(RegistryTest, AddComponentThenHasAndGet)
{
    Registry registry;
    const Entity e = registry.CreateEntity();

    Transform& transform = registry.AddComponent<Transform>(e);
    transform.position = Vec3(1.0f, 2.0f, 3.0f);

    EXPECT_TRUE(registry.HasComponent<Transform>(e));
    EXPECT_TRUE(ApproximatelyEqual(registry.GetComponent<Transform>(e).position, Vec3(1.0f, 2.0f, 3.0f)));
}

TEST(RegistryTest, AddComponentDefaultsMatchTransformsOwnDefaults)
{
    Registry registry;
    const Entity e = registry.CreateEntity();

    const Transform& transform = registry.AddComponent<Transform>(e);

    EXPECT_TRUE(ApproximatelyEqual(transform.position, Vec3::Zero()));
    EXPECT_TRUE(ApproximatelyEqual(transform.scale, Vec3::One()));
    EXPECT_TRUE(RepresentSameRotation(transform.rotation, Quat::Identity()));
}

TEST(RegistryTest, HasComponentIsFalseForUntouchedComponentType)
{
    Registry registry;
    const Entity e = registry.CreateEntity();

    // Never AddComponent<Velocity> anywhere in this test - the pool for
    // Velocity may not even exist yet on this Registry.
    EXPECT_FALSE(registry.HasComponent<Velocity>(e));
    EXPECT_EQ(registry.TryGetComponent<Velocity>(e), nullptr);
}

TEST(RegistryTest, EntityCanHaveMultipleDistinctComponentTypes)
{
    Registry registry;
    const Entity e = registry.CreateEntity();

    registry.AddComponent<Transform>(e).position = Vec3::One();
    registry.AddComponent<Velocity>(e) = Velocity{ 1.0f, 2.0f };

    EXPECT_TRUE(registry.HasComponent<Transform>(e));
    EXPECT_TRUE(registry.HasComponent<Velocity>(e));
    EXPECT_EQ(registry.GetComponent<Velocity>(e).dx, 1.0f);
}

TEST(RegistryTest, RemoveComponentDropsOnlyThatType)
{
    Registry registry;
    const Entity e = registry.CreateEntity();
    registry.AddComponent<Transform>(e);
    registry.AddComponent<Velocity>(e);

    EXPECT_TRUE(registry.RemoveComponent<Transform>(e));

    EXPECT_FALSE(registry.HasComponent<Transform>(e));
    EXPECT_TRUE(registry.HasComponent<Velocity>(e)); // untouched
}

TEST(RegistryTest, DestroyEntityRemovesItFromEveryComponentPool)
{
    Registry registry;
    const Entity e = registry.CreateEntity();
    registry.AddComponent<Transform>(e);
    registry.AddComponent<Velocity>(e);

    registry.DestroyEntity(e);

    EXPECT_FALSE(registry.IsAlive(e));
    EXPECT_FALSE(registry.HasComponent<Transform>(e));
    EXPECT_FALSE(registry.HasComponent<Velocity>(e));
}

TEST(RegistryTest, DestroyEntityIsSafeToCallOnAlreadyDeadOrNeverValidEntity)
{
    Registry registry;
    const Entity e = registry.CreateEntity();
    registry.DestroyEntity(e);

    registry.DestroyEntity(e); // already dead
    registry.DestroyEntity(Entity{}); // never valid at all

    EXPECT_EQ(registry.AliveEntityCount(), 0u);
}

TEST(RegistryTest, ReusedEntitySlotStartsWithNoComponentsFromThePreviousOccupant)
{
    Registry registry;
    const Entity first = registry.CreateEntity();
    registry.AddComponent<Transform>(first).position = Vec3(9.0f, 9.0f, 9.0f);
    registry.DestroyEntity(first);

    const Entity second = registry.CreateEntity(); // very likely reuses first.index

    EXPECT_FALSE(registry.HasComponent<Transform>(second));
}

TEST(RegistryTest, StorageAllowsDirectDenseIterationForASingleComponentType)
{
    Registry registry;
    const Entity a = registry.CreateEntity();
    const Entity b = registry.CreateEntity();
    registry.AddComponent<Velocity>(a) = Velocity{ 1.0f, 0.0f };
    registry.AddComponent<Velocity>(b) = Velocity{ 2.0f, 0.0f };

    float sum = 0.0f;
    ComponentStorage<Velocity>& storage = registry.Storage<Velocity>();
    for (std::size_t i = 0; i < storage.Size(); ++i) {
        sum += storage.ComponentAt(i).dx;
    }

    EXPECT_FLOAT_EQ(sum, 3.0f);
}

TEST(RegistryTest, TransformLocalToWorldMatrixMatchesMat4TRS)
{
    Registry registry;
    const Entity e = registry.CreateEntity();
    Transform& transform = registry.AddComponent<Transform>(e);
    transform.position = Vec3(1.0f, 2.0f, 3.0f);
    transform.rotation = Quat::FromAxisAngle(Vec3::Up(), kHalfPi);
    transform.scale = Vec3(2.0f, 2.0f, 2.0f);

    const Mat4 expected = Mat4::TRS(transform.position, transform.rotation, transform.scale);

    EXPECT_TRUE(ApproximatelyEqual(transform.LocalToWorldMatrix(), expected));
}

} // namespace
} // namespace gte
