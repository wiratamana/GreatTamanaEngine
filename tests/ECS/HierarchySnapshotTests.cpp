// Tests for ECS/HierarchySnapshot.h: the snapshot must list exactly what GetChildren() lists.

#include "ECS/HierarchySnapshot.h"

#include "ECS/Components/Transform.h"
#include "ECS/Registry.h"
#include "ECS/TransformHierarchy.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <random>
#include <span>
#include <vector>

namespace gte {
namespace {

std::vector<Entity> ToVector(std::span<const Entity> entities)
{
    return std::vector<Entity>(entities.begin(), entities.end());
}

Entity AddTransformEntity(Registry& registry, Entity parent = kInvalidEntity)
{
    const Entity entity = registry.CreateEntity();
    registry.AddComponent<Transform>(entity).parent = parent;
    return entity;
}

TEST(HierarchySnapshotTest, EmptyRegistryHasNoRoots)
{
    Registry registry;
    HierarchySnapshot snapshot;
    snapshot.Rebuild(registry);

    EXPECT_TRUE(snapshot.Children(kInvalidEntity).empty());
    EXPECT_EQ(snapshot.Size(), 0u);
}

TEST(HierarchySnapshotTest, MatchesGetChildrenForEveryParentOnARandomForest)
{
    constexpr std::uint32_t kCount = 500;
    std::mt19937 rng(1234);

    Registry registry;
    std::vector<Entity> entities;
    for (std::uint32_t i = 0; i < kCount; ++i) {
        const Entity parent = (i > 0 && rng() % 4 != 0) ? entities[rng() % i] : kInvalidEntity;
        entities.push_back(AddTransformEntity(registry, parent));
        registry.GetComponent<Transform>(entities.back()).siblingIndex = rng() % 4; // many ties
    }
    for (int i = 0; i < 100; ++i) {
        SetSiblingIndex(registry, entities[rng() % kCount], rng() % 5);
    }

    HierarchySnapshot snapshot;
    snapshot.Rebuild(registry);

    EXPECT_EQ(snapshot.Size(), static_cast<std::size_t>(kCount));
    EXPECT_EQ(ToVector(snapshot.Children(kInvalidEntity)), GetChildren(registry, kInvalidEntity));
    for (const Entity entity : entities) {
        EXPECT_EQ(ToVector(snapshot.Children(entity)), GetChildren(registry, entity)) << "entity " << entity.index;
    }
}

TEST(HierarchySnapshotTest, TiesAreOrderedByEntityIndexNotByStorageOrder)
{
    Registry registry;
    const Entity a = AddTransformEntity(registry);
    const Entity b = AddTransformEntity(registry);
    const Entity c = AddTransformEntity(registry);

    registry.RemoveComponent<Transform>(b);
    registry.AddComponent<Transform>(b);

    HierarchySnapshot snapshot;
    snapshot.Rebuild(registry);
    EXPECT_EQ(ToVector(snapshot.Children(kInvalidEntity)), (std::vector<Entity>{ a, b, c }));
}

TEST(HierarchySnapshotTest, DeadParentFallsBackToTheRootList)
{
    Registry registry;
    const Entity parent = AddTransformEntity(registry);
    const Entity child = AddTransformEntity(registry, parent);
    registry.DestroyEntity(parent);

    HierarchySnapshot snapshot;
    snapshot.Rebuild(registry);
    EXPECT_EQ(ToVector(snapshot.Children(kInvalidEntity)), (std::vector<Entity>{ child }));
    EXPECT_TRUE(snapshot.Children(parent).empty());
}

TEST(HierarchySnapshotTest, ParentWithoutTransformFallsBackToTheRootList)
{
    Registry registry;
    const Entity bare = registry.CreateEntity();
    const Entity child = AddTransformEntity(registry, bare);

    HierarchySnapshot snapshot;
    snapshot.Rebuild(registry);
    EXPECT_EQ(ToVector(snapshot.Children(kInvalidEntity)), (std::vector<Entity>{ child }));
    EXPECT_TRUE(snapshot.Children(bare).empty());
}

TEST(HierarchySnapshotTest, EntityWithoutATransformIsNotListed)
{
    Registry registry;
    registry.CreateEntity();
    const Entity withTransform = AddTransformEntity(registry);

    HierarchySnapshot snapshot;
    snapshot.Rebuild(registry);
    EXPECT_EQ(snapshot.Size(), 1u);
    EXPECT_EQ(ToVector(snapshot.Children(kInvalidEntity)), (std::vector<Entity>{ withTransform }));
}

TEST(HierarchySnapshotTest, UnknownEntityHasNoChildren)
{
    Registry registry;
    AddTransformEntity(registry);

    HierarchySnapshot snapshot;
    snapshot.Rebuild(registry);
    EXPECT_TRUE(snapshot.Children(Entity{ 9999, 1 }).empty());
}

TEST(HierarchySnapshotTest, StaleGenerationParentHasNoChildren)
{
    Registry registry;
    const Entity oldParent = AddTransformEntity(registry);
    registry.DestroyEntity(oldParent);

    // The new parent may reuse the freed slot with a newer generation.
    const Entity newParent = AddTransformEntity(registry);
    const Entity child = AddTransformEntity(registry, newParent);

    HierarchySnapshot snapshot;
    snapshot.Rebuild(registry);
    EXPECT_EQ(ToVector(snapshot.Children(newParent)), (std::vector<Entity>{ child }));
    EXPECT_TRUE(snapshot.Children(oldParent).empty());
}

TEST(HierarchySnapshotTest, RebuildAfterAChangeSeesTheChange)
{
    Registry registry;
    const Entity parent = AddTransformEntity(registry);
    const Entity child = AddTransformEntity(registry);

    HierarchySnapshot snapshot;
    snapshot.Rebuild(registry);
    EXPECT_TRUE(snapshot.Children(parent).empty());

    ASSERT_TRUE(SetParent(registry, child, parent));
    snapshot.Rebuild(registry);
    EXPECT_EQ(ToVector(snapshot.Children(parent)), (std::vector<Entity>{ child }));
    EXPECT_EQ(ToVector(snapshot.Children(kInvalidEntity)), (std::vector<Entity>{ parent }));

    registry.DestroyEntity(child);
    snapshot.Rebuild(registry);
    EXPECT_TRUE(snapshot.Children(parent).empty());
    EXPECT_EQ(snapshot.Size(), 1u);
}

} // namespace
} // namespace gte
