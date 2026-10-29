// Tests for the bulk hierarchy paths of Scene/SceneBuilder.h: save order, parent/sibling restore, clear.

#include "Scene/SceneBuilder.h"

#include "ECS/Components/Name.h"
#include "ECS/Components/Transform.h"
#include "ECS/TransformHierarchy.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <random>
#include <string>
#include <vector>

namespace gte {
namespace {

// Entity i gets a random earlier entity as parent (or none), a tied sibling index and the name "e<i>".
std::vector<Entity> MakeNamedForest(Registry& registry, std::uint32_t count)
{
    std::mt19937 rng(1234);
    std::vector<Entity> entities;
    for (std::uint32_t i = 0; i < count; ++i) {
        const Entity entity = registry.CreateEntity();
        Transform& transform = registry.AddComponent<Transform>(entity);
        if (i > 0 && rng() % 4 != 0) {
            transform.parent = entities[rng() % i];
        }
        transform.siblingIndex = rng() % 4;
        registry.AddComponent<Name>(entity).value = "e" + std::to_string(i);
        entities.push_back(entity);
    }
    return entities;
}

// Reference pre-order: a node, then its children in GetChildren() order.
void ReferenceWalk(Registry& registry, Entity entity, std::vector<Entity>& out)
{
    out.push_back(entity);
    for (const Entity child : GetChildren(registry, entity)) {
        ReferenceWalk(registry, child, out);
    }
}

std::string NameOf(const SceneEntityRecord& record)
{
    return record.components["Name"]["value"].get<std::string>();
}

TEST(SceneHierarchyBulkTest, SceneSaveOrderMatchesTheRecursiveReference)
{
    Registry registry;
    const std::vector<Entity> entities = MakeNamedForest(registry, 300);

    std::vector<Entity> expected;
    for (const Entity root : GetChildren(registry, kInvalidEntity)) {
        ReferenceWalk(registry, root, expected);
    }
    ASSERT_EQ(expected.size(), entities.size());

    AssetDatabase database;
    const SceneDocument document = BuildSceneDocumentFromRegistry(registry, database);
    ASSERT_EQ(document.entities.size(), expected.size());

    for (std::size_t i = 0; i < expected.size(); ++i) {
        EXPECT_EQ(NameOf(document.entities[i]), registry.GetComponent<Name>(expected[i]).value) << "record " << i;

        const Entity parent = registry.GetComponent<Transform>(expected[i]).parent;
        if (!parent.IsValid()) {
            EXPECT_FALSE(document.entities[i].parentIndex.has_value());
        } else {
            ASSERT_TRUE(document.entities[i].parentIndex.has_value());
            EXPECT_EQ(NameOf(document.entities[*document.entities[i].parentIndex]), registry.GetComponent<Name>(parent).value);
        }
    }
}

TEST(SceneHierarchyBulkTest, SceneLoadRestoresParentsAndSiblingOrder)
{
    Registry source;
    MakeNamedForest(source, 300);

    AssetDatabase database;
    const SceneDocument document = BuildSceneDocumentFromRegistry(source, database);

    // Same wiring the scene loader does: create bare entities, then one bulk assignment.
    Registry destination;
    std::vector<Entity> created;
    std::vector<HierarchyAssignment> assignments;
    for (const SceneEntityRecord& record : document.entities) {
        const Entity entity = destination.CreateEntity();
        destination.AddComponent<Transform>(entity);
        destination.AddComponent<Name>(entity).value = NameOf(record);
        created.push_back(entity);
    }
    for (std::size_t i = 0; i < document.entities.size(); ++i) {
        const SceneEntityRecord& record = document.entities[i];
        const Entity parent = record.parentIndex.has_value() ? created[*record.parentIndex] : kInvalidEntity;
        assignments.push_back(HierarchyAssignment{ created[i], parent, record.siblingIndex });
    }
    AssignHierarchy(destination, assignments);

    // Compare the whole tree, by names, in sibling order.
    const auto names = [](Registry& registry, const std::vector<Entity>& entities) {
        std::vector<std::string> out;
        for (const Entity entity : entities) {
            out.push_back(registry.GetComponent<Name>(entity).value);
        }
        return out;
    };
    EXPECT_EQ(names(destination, GetChildren(destination, kInvalidEntity)), names(source, GetChildren(source, kInvalidEntity)));
    // Per record: the restored children, by name, equal the saved children in document order.
    for (std::size_t i = 0; i < document.entities.size(); ++i) {
        const std::vector<std::string> restored = names(destination, GetChildren(destination, created[i]));

        std::vector<std::string> saved;
        for (std::size_t j = 0; j < document.entities.size(); ++j) {
            if (document.entities[j].parentIndex == i) {
                saved.push_back(NameOf(document.entities[j]));
            }
        }
        EXPECT_EQ(restored, saved) << "record " << i; // document order is the source sibling order
    }
}

TEST(SceneHierarchyBulkTest, SceneLoadRejectsACycleInTheDocument)
{
    Registry registry;
    std::vector<Entity> created;
    for (int i = 0; i < 3; ++i) {
        created.push_back(registry.CreateEntity());
        registry.AddComponent<Transform>(created.back());
    }

    // Records 0 -> 1 -> 2 -> 0, as a corrupt document would describe.
    const std::vector<HierarchyAssignment> assignments{
        { created[0], created[1], 0 }, { created[1], created[2], 0 }, { created[2], created[0], 0 } };
    AssignHierarchy(registry, assignments);

    for (const Entity entity : created) {
        Entity cursor = entity;
        for (int hops = 0; hops < 4 && cursor.IsValid(); ++hops) {
            cursor = registry.GetComponent<Transform>(cursor).parent;
        }
        EXPECT_FALSE(cursor.IsValid()) << "parent chain of entity " << entity.index << " does not end";
    }
}

TEST(SceneHierarchyBulkTest, ClearEntireSceneDestroysALargeForest)
{
    Registry registry;
    MakeNamedForest(registry, 50000);

    ClearEntireScene(registry);

    EXPECT_EQ(registry.AliveEntityCount(), 0u);
}

} // namespace
} // namespace gte
