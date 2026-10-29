// Tests for the Hierarchy panel's flat rows and drop logic. No ImGui, no device.

#include "Editor/Panels/HierarchyRows.h"

#include "ECS/Components/Transform.h"
#include "ECS/HierarchySnapshot.h"
#include "ECS/Registry.h"
#include "ECS/TransformHierarchy.h"
#include "Editor/Selection.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <random>
#include <unordered_set>
#include <utility>
#include <vector>

namespace gte {
namespace {

Entity AddNode(Registry& registry, Entity parent = kInvalidEntity, std::uint32_t siblingIndex = 0)
{
    const Entity entity = registry.CreateEntity();
    Transform& transform = registry.AddComponent<Transform>(entity);
    transform.parent = parent;
    transform.siblingIndex = siblingIndex;
    return entity;
}

std::vector<HierarchyRow> Flatten(Registry& registry, const std::unordered_set<Entity>& collapsed = {})
{
    HierarchySnapshot snapshot;
    snapshot.Rebuild(registry);
    std::vector<HierarchyRow> rows;
    FlattenHierarchyRows(snapshot, collapsed, rows);
    return rows;
}

std::vector<Entity> EntitiesOf(const std::vector<HierarchyRow>& rows)
{
    std::vector<Entity> entities;
    for (const HierarchyRow& row : rows) {
        entities.push_back(row.entity);
    }
    return entities;
}

// Reference walk through GetChildren, one recursion level per tree level.
void WalkRecursively(Registry& registry, Entity parent, std::uint32_t depth,
    std::vector<std::pair<Entity, std::uint32_t>>& out)
{
    for (const Entity child : GetChildren(registry, parent)) {
        out.emplace_back(child, depth);
        WalkRecursively(registry, child, depth + 1, out);
    }
}

TEST(HierarchyRowsTest, FlattenIsDepthFirstInSiblingOrder)
{
    Registry registry;
    const Entity root2 = AddNode(registry, kInvalidEntity, 1);
    const Entity root1 = AddNode(registry, kInvalidEntity, 0);
    const Entity b = AddNode(registry, root1, 1);
    const Entity a = AddNode(registry, root1, 0);
    const Entity a1 = AddNode(registry, a, 0);

    const std::vector<HierarchyRow> rows = Flatten(registry);
    EXPECT_EQ(EntitiesOf(rows), (std::vector<Entity>{ root1, a, a1, b, root2 }));
}

TEST(HierarchyRowsTest, CollapsedNodeKeepsItsRowAndHidesDescendants)
{
    Registry registry;
    const Entity root = AddNode(registry);
    const Entity a = AddNode(registry, root, 0);
    AddNode(registry, a, 0);
    const Entity b = AddNode(registry, root, 1);

    const std::vector<HierarchyRow> rows = Flatten(registry, { a });
    ASSERT_EQ(EntitiesOf(rows), (std::vector<Entity>{ root, a, b }));
    EXPECT_TRUE(rows[1].hasChildren);
    EXPECT_FALSE(rows[1].expanded);
    EXPECT_TRUE(rows[0].expanded);
}

TEST(HierarchyRowsTest, DepthIsTheNumberOfAncestors)
{
    Registry registry;
    Entity parent = kInvalidEntity;
    for (int i = 0; i < 5; ++i) {
        parent = AddNode(registry, parent);
    }

    const std::vector<HierarchyRow> rows = Flatten(registry);
    ASSERT_EQ(rows.size(), 5u);
    for (std::uint32_t i = 0; i < rows.size(); ++i) {
        EXPECT_EQ(rows[i].depth, i);
    }
}

TEST(HierarchyRowsTest, LeafHasNoChildrenFlag)
{
    Registry registry;
    const Entity root = AddNode(registry);
    AddNode(registry, root);

    const std::vector<HierarchyRow> rows = Flatten(registry);
    ASSERT_EQ(rows.size(), 2u);
    EXPECT_TRUE(rows[0].hasChildren);
    EXPECT_FALSE(rows[1].hasChildren);
}

TEST(HierarchyRowsTest, EmptyCollapsedSetMeansEverythingIsOpen)
{
    Registry registry;
    const Entity root = AddNode(registry);
    const Entity child = AddNode(registry, root);
    AddNode(registry, child);

    const std::vector<HierarchyRow> rows = Flatten(registry);
    ASSERT_EQ(rows.size(), 3u);
    for (const HierarchyRow& row : rows) {
        EXPECT_TRUE(row.expanded);
    }
}

TEST(HierarchyRowsTest, AVeryDeepChainDoesNotOverflowTheStack)
{
    constexpr std::uint32_t kDepth = 100000;
    Registry registry;
    Entity parent = kInvalidEntity;
    for (std::uint32_t i = 0; i < kDepth; ++i) {
        parent = AddNode(registry, parent); // direct parent assignment: SetParent would be quadratic here
    }

    const std::vector<HierarchyRow> rows = Flatten(registry);
    ASSERT_EQ(rows.size(), kDepth);
    EXPECT_EQ(rows.back().depth, kDepth - 1);
}

TEST(HierarchyRowsTest, SiblingPositionIsTheIndexInTheParentsChildList)
{
    Registry registry;
    const Entity root = AddNode(registry);
    const Entity c2 = AddNode(registry, root, 2);
    const Entity c0 = AddNode(registry, root, 0);
    const Entity c1 = AddNode(registry, root, 1);

    const std::vector<HierarchyRow> rows = Flatten(registry);
    ASSERT_EQ(EntitiesOf(rows), (std::vector<Entity>{ root, c0, c1, c2 }));
    EXPECT_EQ(rows[0].siblingPosition, 0u);
    EXPECT_EQ(rows[1].siblingPosition, 0u);
    EXPECT_EQ(rows[2].siblingPosition, 1u);
    EXPECT_EQ(rows[3].siblingPosition, 2u);
    EXPECT_EQ(rows[2].parent, root);
    EXPECT_EQ(rows[0].parent, kInvalidEntity);
}

TEST(HierarchyRowsTest, PruneRemovesDeadEntitiesFromTheCollapsedSet)
{
    Registry registry;
    const Entity alive = AddNode(registry);
    const Entity dead = AddNode(registry);
    std::unordered_set<Entity> collapsed{ alive, dead };

    registry.DestroyEntity(dead);
    PruneCollapsedEntities(registry, collapsed);

    EXPECT_EQ(collapsed.size(), 1u);
    EXPECT_TRUE(collapsed.contains(alive));
}

TEST(HierarchyRowsTest, SameRowsAsTheOldRecursiveWalk)
{
    Registry registry;
    std::mt19937 rng(1234);
    std::vector<Entity> created;
    for (int i = 0; i < 400; ++i) {
        const bool asRoot = created.empty() || rng() % 5 == 0;
        const Entity parent = asRoot ? kInvalidEntity : created[rng() % created.size()];
        created.push_back(AddNode(registry, parent, rng() % 4)); // small range: many ties
    }

    std::vector<std::pair<Entity, std::uint32_t>> expected;
    WalkRecursively(registry, kInvalidEntity, 0, expected);

    const std::vector<HierarchyRow> rows = Flatten(registry);
    ASSERT_EQ(rows.size(), expected.size());
    for (std::size_t i = 0; i < rows.size(); ++i) {
        EXPECT_EQ(rows[i].entity, expected[i].first) << i;
        EXPECT_EQ(rows[i].depth, expected[i].second) << i;
    }
}

TEST(HierarchyRowsTest, InsertIndexForTiesAndDenseGroups)
{
    // Group [A,B,C,D] = positions 0..3.
    EXPECT_EQ(ComputeSiblingInsertIndex(3, 0, false), 2u); // drag A before D -> [B,C,A,D]
    EXPECT_EQ(ComputeSiblingInsertIndex(3, 0, true), 3u);  // drag A after D  -> [B,C,D,A]
    EXPECT_EQ(ComputeSiblingInsertIndex(0, 3, false), 0u); // drag D before A
    EXPECT_EQ(ComputeSiblingInsertIndex(0, 3, true), 1u);  // drag D after A
    EXPECT_EQ(ComputeSiblingInsertIndex(2, 1, true), 2u);  // drag B after C -> [A,C,B,D]
    EXPECT_EQ(ComputeSiblingInsertIndex(2, 1, false), 1u); // drag B before C: stays in place
    EXPECT_EQ(ComputeSiblingInsertIndex(3, std::nullopt, false), 3u); // dragged from another parent
    EXPECT_EQ(ComputeSiblingInsertIndex(3, std::nullopt, true), 4u);
}

TEST(HierarchyRowsTest, ReorderInTheSameParentLeavesTheTransformBitIdentical)
{
    Registry registry;
    const Entity parent = AddNode(registry);
    registry.GetComponent<Transform>(parent).scale = Vec3(2.0f, 1.0f, 0.5f);
    registry.GetComponent<Transform>(parent).rotation = Quat::FromEulerDegrees(15.0f, 40.0f, 5.0f);

    std::vector<Entity> children;
    for (int i = 0; i < 4; ++i) { // equal sibling indexes: every child ties
        const Entity child = AddNode(registry, parent, 0);
        Transform& transform = registry.GetComponent<Transform>(child);
        transform.position = Vec3(1.0f + static_cast<float>(i), 0.37f * static_cast<float>(i), -2.0f);
        transform.rotation = Quat::FromEulerDegrees(10.0f * static_cast<float>(i), 33.0f, 7.0f);
        transform.scale = Vec3(1.0f, 1.5f, 0.75f);
        children.push_back(child);
    }
    const Entity dragged = children[0];
    const Entity target = children[3];
    const Transform before = registry.GetComponent<Transform>(dragged);

    HierarchyViewState state;
    UpdateHierarchyRows(registry, state);
    const auto targetRow = std::find_if(state.rows.begin(), state.rows.end(),
        [target](const HierarchyRow& row) { return row.entity == target; });
    ASSERT_NE(targetRow, state.rows.end());

    PendingHierarchyAction action;
    action.kind = PendingHierarchyAction::Kind::Reorder;
    action.dragged = dragged;
    action.target = target;
    action.targetParent = targetRow->parent;
    action.targetPosition = targetRow->siblingPosition;
    action.insertAfter = false;
    ASSERT_TRUE(ExecuteHierarchyAction(registry, state.snapshot, action));

    EXPECT_EQ(GetChildren(registry, parent), (std::vector<Entity>{ children[1], children[2], children[0], children[3] }));

    const Transform& after = registry.GetComponent<Transform>(dragged);
    EXPECT_EQ(std::memcmp(&before.position, &after.position, sizeof(Vec3)), 0);
    EXPECT_EQ(std::memcmp(&before.rotation, &after.rotation, sizeof(Quat)), 0);
    EXPECT_EQ(std::memcmp(&before.scale, &after.scale, sizeof(Vec3)), 0);
}

TEST(HierarchyRowsTest, ReorderUnderAnotherParentMovesTheEntity)
{
    Registry registry;
    const Entity p1 = AddNode(registry, kInvalidEntity, 0);
    const Entity p2 = AddNode(registry, kInvalidEntity, 1);
    const Entity moved = AddNode(registry, p1);
    const Entity t0 = AddNode(registry, p2, 0);
    const Entity t1 = AddNode(registry, p2, 1);

    HierarchyViewState state;
    UpdateHierarchyRows(registry, state);

    PendingHierarchyAction action;
    action.kind = PendingHierarchyAction::Kind::Reorder;
    action.dragged = moved;
    action.target = t1;
    action.targetParent = p2;
    action.targetPosition = 1;
    action.insertAfter = false;
    ASSERT_TRUE(ExecuteHierarchyAction(registry, state.snapshot, action));

    EXPECT_EQ(GetChildren(registry, p2), (std::vector<Entity>{ t0, moved, t1 }));
    EXPECT_TRUE(GetChildren(registry, p1).empty());
}

TEST(HierarchyRowsTest, HiddenPanelReportsNoRowsAndReleasesBuffersAfterALongRun)
{
    Registry registry;
    AddNode(registry);
    HierarchyViewState state;
    UpdateHierarchyRows(registry, state);
    NoteHierarchyVisible(state);
    ASSERT_EQ(state.rows.size(), 1u);

    const Selection selection{};
    EXPECT_EQ(BuildHierarchyStateReport(registry, state, selection, 0, 10).rows.size(), 1u);

    NoteHierarchyHidden(state);
    const HierarchyStateReport hidden = BuildHierarchyStateReport(registry, state, selection, 0, 10);
    EXPECT_FALSE(hidden.visible);
    EXPECT_TRUE(hidden.rows.empty());

    for (std::uint32_t i = 0; i < kHierarchyHiddenFramesBeforeRelease; ++i) {
        NoteHierarchyHidden(state);
    }
    EXPECT_TRUE(state.rows.empty());
    EXPECT_EQ(state.rows.capacity(), 0u);
    EXPECT_EQ(state.snapshot.Size(), 0u);
}

} // namespace
} // namespace gte
