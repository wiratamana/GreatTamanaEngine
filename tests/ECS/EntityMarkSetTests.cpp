// Tier-1 tests for EntityMarkSet (src/ECS/EntityMarkSet.h). No device involved.

#include "ECS/EntityMarkSet.h"

#include <gtest/gtest.h>

#include <array>

namespace gte {
namespace {

Entity MakeEntity(std::uint32_t index, std::uint32_t generation = 1)
{
    Entity entity;
    entity.index = index;
    entity.generation = generation;
    return entity;
}

TEST(EntityMarkSetTest, AnEmptySetContainsNothing)
{
    const EntityMarkSet set;
    EXPECT_TRUE(set.Empty());
    EXPECT_FALSE(set.Contains(MakeEntity(0)));
    EXPECT_FALSE(set.Contains(MakeEntity(1000)));
    EXPECT_FALSE(set.Contains(kInvalidEntity));
}

TEST(EntityMarkSetTest, InsertedEntitiesAreContained)
{
    EntityMarkSet set;
    const std::array<Entity, 3> entities = { MakeEntity(2), MakeEntity(50), MakeEntity(7) };
    set.Insert(entities);
    EXPECT_EQ(set.Size(), 3u);
    for (const Entity entity : entities) {
        EXPECT_TRUE(set.Contains(entity));
    }
    EXPECT_FALSE(set.Contains(MakeEntity(3)));
}

TEST(EntityMarkSetTest, AStaleGenerationOfTheSameIndexIsNotContained)
{
    EntityMarkSet set;
    set.Insert(MakeEntity(5, 2));
    EXPECT_TRUE(set.Contains(MakeEntity(5, 2)));
    EXPECT_FALSE(set.Contains(MakeEntity(5, 1)));
    EXPECT_FALSE(set.Contains(MakeEntity(5, 3)));
}

TEST(EntityMarkSetTest, AnInvalidEntityIsIgnored)
{
    EntityMarkSet set;
    set.Insert(kInvalidEntity);
    EXPECT_TRUE(set.Empty());
    EXPECT_FALSE(set.Contains(kInvalidEntity));
}

TEST(EntityMarkSetTest, ClearForgetsEverythingAndTheSetKeepsWorking)
{
    EntityMarkSet set;
    set.Insert(MakeEntity(1));
    set.Insert(MakeEntity(9));
    set.Clear();
    EXPECT_TRUE(set.Empty());
    EXPECT_FALSE(set.Contains(MakeEntity(1)));
    EXPECT_FALSE(set.Contains(MakeEntity(9)));

    set.Insert(MakeEntity(9));
    EXPECT_TRUE(set.Contains(MakeEntity(9)));
    EXPECT_FALSE(set.Contains(MakeEntity(1)));
    EXPECT_EQ(set.Size(), 1u);
}

TEST(EntityMarkSetTest, ADuplicateInsertCountsOnce)
{
    EntityMarkSet set;
    set.Insert(MakeEntity(4));
    set.Insert(MakeEntity(4));
    EXPECT_EQ(set.Size(), 1u);
}

} // namespace
} // namespace gte
