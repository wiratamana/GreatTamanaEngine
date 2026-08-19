// Unit tests for ComponentStorage<T> (src/ECS/ComponentStorage.h) - the
// sparse-set itself, exercised with hand-built Entity values (no
// EntityManager involved - see EntityManagerTests.cpp for that, and
// RegistryTests.cpp for the two glued together).

#include "ECS/ComponentStorage.h"

#include <gtest/gtest.h>

namespace gte {
namespace {

struct Position {
    float x = 0.0f;
    float y = 0.0f;

    friend bool operator==(const Position& a, const Position& b)
    {
        return a.x == b.x && a.y == b.y;
    }
};

TEST(ComponentStorageTest, AddThenHasAndGet)
{
    ComponentStorage<Position> storage;
    const Entity e{ 0, 1 };

    storage.Add(e, Position{ 1.0f, 2.0f });

    EXPECT_TRUE(storage.Has(e));
    EXPECT_EQ(storage.Get(e), (Position{ 1.0f, 2.0f }));
    EXPECT_EQ(storage.Size(), 1u);
}

TEST(ComponentStorageTest, AddWithNoArgsDefaultConstructs)
{
    ComponentStorage<Position> storage;
    const Entity e{ 0, 1 };

    storage.Add(e);

    EXPECT_EQ(storage.Get(e), (Position{ 0.0f, 0.0f }));
}

TEST(ComponentStorageTest, HasIsFalseForNeverAddedEntity)
{
    ComponentStorage<Position> storage;
    const Entity e{ 5, 1 };

    EXPECT_FALSE(storage.Has(e));
    EXPECT_EQ(storage.TryGet(e), nullptr);
}

TEST(ComponentStorageTest, AddTwiceOverwritesInPlaceRatherThanDuplicating)
{
    ComponentStorage<Position> storage;
    const Entity e{ 0, 1 };

    storage.Add(e, Position{ 1.0f, 1.0f });
    storage.Add(e, Position{ 9.0f, 9.0f });

    EXPECT_EQ(storage.Get(e), (Position{ 9.0f, 9.0f }));
    EXPECT_EQ(storage.Size(), 1u); // did not grow - same slot overwritten
}

TEST(ComponentStorageTest, RemoveReturnsTrueAndDropsComponent)
{
    ComponentStorage<Position> storage;
    const Entity e{ 0, 1 };
    storage.Add(e, Position{ 1.0f, 2.0f });

    EXPECT_TRUE(storage.Remove(e));

    EXPECT_FALSE(storage.Has(e));
    EXPECT_EQ(storage.Size(), 0u);
}

TEST(ComponentStorageTest, RemoveOfAbsentEntityReturnsFalse)
{
    ComponentStorage<Position> storage;
    const Entity e{ 0, 1 };

    EXPECT_FALSE(storage.Remove(e));
}

TEST(ComponentStorageTest, RemoveIsSafeToCallTwice)
{
    ComponentStorage<Position> storage;
    const Entity e{ 0, 1 };
    storage.Add(e, Position{});

    EXPECT_TRUE(storage.Remove(e));
    EXPECT_FALSE(storage.Remove(e)); // already gone - false, not a crash
}

TEST(ComponentStorageTest, SwapRemoveKeepsRemainingEntitiesReachable)
{
    // Regression-style test for the sparse-set's swap-with-last removal:
    // removing a NON-last dense entry must move the last entry into the
    // freed slot and fix up ITS sparse pointer too, not just shrink the
    // dense array.
    ComponentStorage<Position> storage;
    const Entity a{ 0, 1 };
    const Entity b{ 1, 1 };
    const Entity c{ 2, 1 };
    storage.Add(a, Position{ 1.0f, 0.0f });
    storage.Add(b, Position{ 2.0f, 0.0f });
    storage.Add(c, Position{ 3.0f, 0.0f }); // last in the dense array

    storage.Remove(a); // a is dense index 0 - c gets swapped into its slot

    EXPECT_FALSE(storage.Has(a));
    EXPECT_TRUE(storage.Has(b));
    EXPECT_TRUE(storage.Has(c));
    EXPECT_EQ(storage.Get(b), (Position{ 2.0f, 0.0f }));
    EXPECT_EQ(storage.Get(c), (Position{ 3.0f, 0.0f }));
    EXPECT_EQ(storage.Size(), 2u);
}

TEST(ComponentStorageTest, DenseIterationVisitsEveryLiveComponentExactlyOnce)
{
    ComponentStorage<Position> storage;
    const Entity a{ 0, 1 };
    const Entity b{ 1, 1 };
    storage.Add(a, Position{ 1.0f, 0.0f });
    storage.Add(b, Position{ 2.0f, 0.0f });

    float sum = 0.0f;
    for (std::size_t i = 0; i < storage.Size(); ++i) {
        sum += storage.ComponentAt(i).x;
    }

    EXPECT_FLOAT_EQ(sum, 3.0f);
}

TEST(ComponentStorageTest, RemoveViaBasePointerInterfaceWorks)
{
    // Registry (see RegistryTests.cpp) only ever talks to pools through
    // IComponentPool when destroying an entity - verify that path works
    // too, not just the typed API.
    ComponentStorage<Position> storage;
    const Entity e{ 0, 1 };
    storage.Add(e, Position{});

    IComponentPool& asBase = storage;
    EXPECT_TRUE(asBase.Has(e));
    EXPECT_TRUE(asBase.Remove(e));
    EXPECT_FALSE(storage.Has(e));
}

TEST(ComponentStorageTest, ReusedEntityIndexWithDifferentGenerationIsNotConfusedWithThePrevious)
{
    // Same index, different generation - e.g. what happens after
    // EntityManager recycles a slot (see EntityManagerTests.cpp). The old
    // handle must not appear to have a component just because the slot
    // index matches.
    ComponentStorage<Position> storage;
    const Entity first{ 0, 1 };
    const Entity second{ 0, 2 };
    storage.Add(first, Position{ 5.0f, 5.0f });
    storage.Remove(first);

    EXPECT_FALSE(storage.Has(second));

    storage.Add(second, Position{ 7.0f, 7.0f });
    EXPECT_TRUE(storage.Has(second));
    EXPECT_FALSE(storage.Has(first));
    EXPECT_EQ(storage.Get(second), (Position{ 7.0f, 7.0f }));
}

} // namespace
} // namespace gte
