// Unit tests for EntityManager (src/ECS/EntityManager.h/.cpp) - entity
// handle allocation/recycling only, no components involved at all (see
// ComponentStorageTests.cpp/RegistryTests.cpp for those).

#include "ECS/EntityManager.h"

#include <gtest/gtest.h>

namespace gte {
namespace {

TEST(EntityManagerTest, CreateReturnsValidAliveEntity)
{
    EntityManager manager;
    const Entity e = manager.Create();

    EXPECT_TRUE(e.IsValid());
    EXPECT_TRUE(manager.IsAlive(e));
}

TEST(EntityManagerTest, DefaultConstructedOrInvalidEntityIsNeverAlive)
{
    EntityManager manager;

    EXPECT_FALSE(manager.IsAlive(Entity{}));
    EXPECT_FALSE(manager.IsAlive(kInvalidEntity));
}

TEST(EntityManagerTest, CreateReturnsDistinctEntitiesForDistinctIndices)
{
    EntityManager manager;
    const Entity a = manager.Create();
    const Entity b = manager.Create();

    EXPECT_NE(a, b);
    EXPECT_NE(a.index, b.index);
    EXPECT_TRUE(manager.IsAlive(a));
    EXPECT_TRUE(manager.IsAlive(b));
}

TEST(EntityManagerTest, DestroyMakesEntityNoLongerAlive)
{
    EntityManager manager;
    const Entity e = manager.Create();

    manager.Destroy(e);

    EXPECT_FALSE(manager.IsAlive(e));
}

TEST(EntityManagerTest, DestroyIsSafeToCallTwice)
{
    EntityManager manager;
    const Entity e = manager.Create();

    manager.Destroy(e);
    manager.Destroy(e); // must not crash/assert/underflow AliveCount()

    EXPECT_FALSE(manager.IsAlive(e));
    EXPECT_EQ(manager.AliveCount(), 0u);
}

TEST(EntityManagerTest, DestroyOfNeverCreatedEntityIsNoOp)
{
    EntityManager manager;

    manager.Destroy(Entity{ 123, 1 }); // never returned by Create() on this manager

    EXPECT_EQ(manager.AliveCount(), 0u);
}

TEST(EntityManagerTest, ReusesFreedSlotWithBumpedGeneration)
{
    EntityManager manager;
    const Entity first = manager.Create();
    manager.Destroy(first);
    const Entity second = manager.Create();

    EXPECT_EQ(first.index, second.index); // slot reused
    EXPECT_NE(first.generation, second.generation); // generation bumped forward
    EXPECT_NE(first, second);
}

TEST(EntityManagerTest, StaleHandleFromReusedSlotIsNotAlive)
{
    EntityManager manager;
    const Entity first = manager.Create();
    manager.Destroy(first);
    const Entity second = manager.Create();
    (void)second;

    EXPECT_FALSE(manager.IsAlive(first)); // stale - same index, old generation
}

TEST(EntityManagerTest, AliveCountTracksCreateAndDestroy)
{
    EntityManager manager;
    EXPECT_EQ(manager.AliveCount(), 0u);

    const Entity a = manager.Create();
    const Entity b = manager.Create();
    EXPECT_EQ(manager.AliveCount(), 2u);

    manager.Destroy(a);
    EXPECT_EQ(manager.AliveCount(), 1u);

    manager.Destroy(b);
    EXPECT_EQ(manager.AliveCount(), 0u);
}

} // namespace
} // namespace gte
