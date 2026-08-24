// Unit tests for ECS/TransformHierarchy.h - the free functions implementing
// Transform's parent/child hierarchy (ComputeWorldMatrix()/
// ComputeWorldTransform(), SetParent()/IsDescendantOf(), GetChildren()/
// SetSiblingIndex()/MoveToLastSibling()). All Tier 1: nothing here needs
// anything beyond a plain Registry - no Renderer, no live GPU device, no
// ImGui (see AGENTS.md, "Testability & Regression Safety").

#include "ECS/TransformHierarchy.h"

#include "ECS/Components/Transform.h"
#include "ECS/Registry.h"

#include <gtest/gtest.h>

namespace gte {
namespace {

TEST(TransformHierarchyTest, ComputeWorldMatrixForUnparentedEntityMatchesLocalToWorldMatrix)
{
    Registry registry;
    const Entity entity = registry.CreateEntity();
    Transform& transform = registry.AddComponent<Transform>(entity);
    transform.position = Vec3(1.0f, 2.0f, 3.0f);

    EXPECT_TRUE(ApproximatelyEqual(ComputeWorldMatrix(registry, entity), transform.LocalToWorldMatrix()));
}

TEST(TransformHierarchyTest, ComputeWorldMatrixForEntityWithNoTransformIsIdentity)
{
    Registry registry;
    const Entity entity = registry.CreateEntity(); // No Transform added at all.

    EXPECT_TRUE(ApproximatelyEqual(ComputeWorldMatrix(registry, entity), Mat4::Identity()));
}

TEST(TransformHierarchyTest, ComputeWorldMatrixComposesParentAndChildLocalTransforms)
{
    Registry registry;
    const Entity parent = registry.CreateEntity();
    Transform& parentTransform = registry.AddComponent<Transform>(parent);
    parentTransform.position = Vec3(10.0f, 0.0f, 0.0f);

    const Entity child = registry.CreateEntity();
    Transform& childTransform = registry.AddComponent<Transform>(child);
    childTransform.position = Vec3(1.0f, 0.0f, 0.0f);
    childTransform.parent = parent;

    const Mat4 expected = parentTransform.LocalToWorldMatrix() * childTransform.LocalToWorldMatrix();
    EXPECT_TRUE(ApproximatelyEqual(ComputeWorldMatrix(registry, child), expected));

    // The child's own WORLD position should be parent (10,0,0) + child
    // local offset (1,0,0) = (11,0,0).
    const Vec3 childWorldPosition = ComputeWorldMatrix(registry, child).TransformPoint(Vec3::Zero());
    EXPECT_TRUE(ApproximatelyEqual(childWorldPosition, Vec3(11.0f, 0.0f, 0.0f)));
}

TEST(TransformHierarchyTest, ComputeWorldMatrixComposesThreeLevelChain)
{
    Registry registry;
    const Entity grandparent = registry.CreateEntity();
    registry.AddComponent<Transform>(grandparent).position = Vec3(1.0f, 0.0f, 0.0f);

    const Entity parent = registry.CreateEntity();
    Transform& parentTransform = registry.AddComponent<Transform>(parent);
    parentTransform.position = Vec3(1.0f, 0.0f, 0.0f);
    parentTransform.parent = grandparent;

    const Entity child = registry.CreateEntity();
    Transform& childTransform = registry.AddComponent<Transform>(child);
    childTransform.position = Vec3(1.0f, 0.0f, 0.0f);
    childTransform.parent = parent;

    const Vec3 childWorldPosition = ComputeWorldMatrix(registry, child).TransformPoint(Vec3::Zero());
    EXPECT_TRUE(ApproximatelyEqual(childWorldPosition, Vec3(3.0f, 0.0f, 0.0f)));
}

TEST(TransformHierarchyTest, ComputeWorldMatrixTreatsDeadParentAsNoParent)
{
    Registry registry;
    const Entity parent = registry.CreateEntity();
    registry.AddComponent<Transform>(parent).position = Vec3(5.0f, 0.0f, 0.0f);

    const Entity child = registry.CreateEntity();
    Transform& childTransform = registry.AddComponent<Transform>(child);
    childTransform.position = Vec3(1.0f, 0.0f, 0.0f);
    childTransform.parent = parent;

    registry.DestroyEntity(parent); // Never explicitly detached first.

    // Falls back to the child's own local transform, exactly as if it had
    // never been parented - never crashes/hangs on the dangling reference.
    EXPECT_TRUE(ApproximatelyEqual(ComputeWorldMatrix(registry, child), childTransform.LocalToWorldMatrix()));
}

TEST(TransformHierarchyTest, ComputeWorldTransformDecomposesBackToPositionRotationScale)
{
    Registry registry;
    const Entity parent = registry.CreateEntity();
    registry.AddComponent<Transform>(parent).position = Vec3(2.0f, 0.0f, 0.0f);

    const Entity child = registry.CreateEntity();
    Transform& childTransform = registry.AddComponent<Transform>(child);
    childTransform.position = Vec3(3.0f, 0.0f, 0.0f);
    childTransform.parent = parent;

    const Transform world = ComputeWorldTransform(registry, child);
    EXPECT_TRUE(ApproximatelyEqual(world.position, Vec3(5.0f, 0.0f, 0.0f)));
    EXPECT_TRUE(ApproximatelyEqual(world.scale, Vec3::One()));
}

TEST(TransformHierarchyTest, IsDescendantOfDetectsDirectAndIndirectDescendants)
{
    Registry registry;
    const Entity grandparent = registry.CreateEntity();
    registry.AddComponent<Transform>(grandparent);

    const Entity parent = registry.CreateEntity();
    registry.AddComponent<Transform>(parent).parent = grandparent;

    const Entity child = registry.CreateEntity();
    registry.AddComponent<Transform>(child).parent = parent;

    const Entity unrelated = registry.CreateEntity();
    registry.AddComponent<Transform>(unrelated);

    EXPECT_TRUE(IsDescendantOf(registry, parent, grandparent));
    EXPECT_TRUE(IsDescendantOf(registry, child, grandparent)); // indirect
    EXPECT_TRUE(IsDescendantOf(registry, child, parent));
    EXPECT_FALSE(IsDescendantOf(registry, unrelated, grandparent));
    EXPECT_FALSE(IsDescendantOf(registry, grandparent, child)); // wrong direction
}

TEST(TransformHierarchyTest, SetParentAttachesChildAndAppendsAsLastSibling)
{
    Registry registry;
    const Entity parent = registry.CreateEntity();
    registry.AddComponent<Transform>(parent);

    const Entity childA = registry.CreateEntity();
    registry.AddComponent<Transform>(childA);
    const Entity childB = registry.CreateEntity();
    registry.AddComponent<Transform>(childB);

    EXPECT_TRUE(SetParent(registry, childA, parent));
    EXPECT_TRUE(SetParent(registry, childB, parent));

    EXPECT_EQ(registry.GetComponent<Transform>(childA).parent, parent);
    EXPECT_EQ(registry.GetComponent<Transform>(childB).parent, parent);

    const std::vector<Entity> children = GetChildren(registry, parent);
    ASSERT_EQ(children.size(), 2u);
    EXPECT_EQ(children[0], childA);
    EXPECT_EQ(children[1], childB);
}

TEST(TransformHierarchyTest, SetParentRejectsSelfParenting)
{
    Registry registry;
    const Entity entity = registry.CreateEntity();
    registry.AddComponent<Transform>(entity);

    EXPECT_FALSE(SetParent(registry, entity, entity));
    EXPECT_EQ(registry.GetComponent<Transform>(entity).parent, kInvalidEntity);
}

TEST(TransformHierarchyTest, SetParentRejectsCycles)
{
    Registry registry;
    const Entity a = registry.CreateEntity();
    registry.AddComponent<Transform>(a);
    const Entity b = registry.CreateEntity();
    registry.AddComponent<Transform>(b);

    ASSERT_TRUE(SetParent(registry, b, a)); // b is a child of a.

    // Attaching a (b's own parent) as a child of b would create a cycle -
    // must be rejected, leaving a's parent untouched (still root).
    EXPECT_FALSE(SetParent(registry, a, b));
    EXPECT_EQ(registry.GetComponent<Transform>(a).parent, kInvalidEntity);
    EXPECT_EQ(registry.GetComponent<Transform>(b).parent, a);
}

TEST(TransformHierarchyTest, SetParentRejectsMissingTransform)
{
    Registry registry;
    const Entity entityWithTransform = registry.CreateEntity();
    registry.AddComponent<Transform>(entityWithTransform);

    const Entity entityWithoutTransform = registry.CreateEntity(); // No Transform.

    EXPECT_FALSE(SetParent(registry, entityWithoutTransform, entityWithTransform));
    EXPECT_FALSE(SetParent(registry, entityWithTransform, entityWithoutTransform));
}

TEST(TransformHierarchyTest, SetParentWithWorldPositionStaysPreservesWorldPosition)
{
    Registry registry;
    const Entity parent = registry.CreateEntity();
    registry.AddComponent<Transform>(parent).position = Vec3(10.0f, 0.0f, 0.0f);

    const Entity child = registry.CreateEntity();
    Transform& childTransform = registry.AddComponent<Transform>(child);
    childTransform.position = Vec3(11.0f, 0.0f, 0.0f); // Currently at world (11,0,0), no parent yet.

    const Vec3 worldBefore = ComputeWorldMatrix(registry, child).TransformPoint(Vec3::Zero());

    ASSERT_TRUE(SetParent(registry, child, parent, /*worldPositionStays=*/true));

    const Vec3 worldAfter = ComputeWorldMatrix(registry, child).TransformPoint(Vec3::Zero());
    EXPECT_TRUE(ApproximatelyEqual(worldBefore, worldAfter));
    // Local position should now be relative to the parent: (11,0,0) - (10,0,0) = (1,0,0).
    EXPECT_TRUE(ApproximatelyEqual(registry.GetComponent<Transform>(child).position, Vec3(1.0f, 0.0f, 0.0f)));
}

TEST(TransformHierarchyTest, SetParentWithoutWorldPositionStaysKeepsLocalFieldsAsAuthored)
{
    Registry registry;
    const Entity parent = registry.CreateEntity();
    registry.AddComponent<Transform>(parent).position = Vec3(10.0f, 0.0f, 0.0f);

    const Entity child = registry.CreateEntity();
    Transform& childTransform = registry.AddComponent<Transform>(child);
    childTransform.position = Vec3(1.0f, 0.0f, 0.0f);

    ASSERT_TRUE(SetParent(registry, child, parent, /*worldPositionStays=*/false));

    // Local position field is untouched - now interpreted relative to the
    // parent, so the child's WORLD position becomes (11,0,0), not (1,0,0).
    EXPECT_TRUE(ApproximatelyEqual(registry.GetComponent<Transform>(child).position, Vec3(1.0f, 0.0f, 0.0f)));
    const Vec3 worldAfter = ComputeWorldMatrix(registry, child).TransformPoint(Vec3::Zero());
    EXPECT_TRUE(ApproximatelyEqual(worldAfter, Vec3(11.0f, 0.0f, 0.0f)));
}

TEST(TransformHierarchyTest, SetParentToInvalidEntityDetachesToRoot)
{
    Registry registry;
    const Entity parent = registry.CreateEntity();
    registry.AddComponent<Transform>(parent);
    const Entity child = registry.CreateEntity();
    registry.AddComponent<Transform>(child);

    ASSERT_TRUE(SetParent(registry, child, parent));
    ASSERT_TRUE(SetParent(registry, child, kInvalidEntity));

    EXPECT_EQ(registry.GetComponent<Transform>(child).parent, kInvalidEntity);
}

TEST(TransformHierarchyTest, GetChildrenReturnsOnlyDirectChildrenSortedBySiblingIndex)
{
    Registry registry;
    const Entity parent = registry.CreateEntity();
    registry.AddComponent<Transform>(parent);

    const Entity childA = registry.CreateEntity();
    registry.AddComponent<Transform>(childA);
    const Entity childB = registry.CreateEntity();
    registry.AddComponent<Transform>(childB);
    const Entity grandchild = registry.CreateEntity();
    registry.AddComponent<Transform>(grandchild);

    ASSERT_TRUE(SetParent(registry, childA, parent));
    ASSERT_TRUE(SetParent(registry, childB, parent));
    ASSERT_TRUE(SetParent(registry, grandchild, childA)); // Not a direct child of `parent`.

    const std::vector<Entity> children = GetChildren(registry, parent);
    ASSERT_EQ(children.size(), 2u);
    EXPECT_EQ(children[0], childA);
    EXPECT_EQ(children[1], childB);
}

TEST(TransformHierarchyTest, SetSiblingIndexReordersSiblingsAndRenumbersDensely)
{
    Registry registry;
    const Entity a = registry.CreateEntity();
    registry.AddComponent<Transform>(a);
    const Entity b = registry.CreateEntity();
    registry.AddComponent<Transform>(b);
    const Entity c = registry.CreateEntity();
    registry.AddComponent<Transform>(c);

    // All three default to parent == kInvalidEntity (root) - append each to
    // the end explicitly so their initial order is deterministic.
    MoveToLastSibling(registry, a);
    MoveToLastSibling(registry, b);
    MoveToLastSibling(registry, c);
    ASSERT_EQ(GetChildren(registry, kInvalidEntity), (std::vector<Entity>{ a, b, c }));

    // Move `c` to the front.
    EXPECT_TRUE(SetSiblingIndex(registry, c, 0));

    const std::vector<Entity> reordered = GetChildren(registry, kInvalidEntity);
    ASSERT_EQ(reordered.size(), 3u);
    EXPECT_EQ(reordered[0], c);
    EXPECT_EQ(reordered[1], a);
    EXPECT_EQ(reordered[2], b);

    // Sibling indices should be densely renumbered 0..2 in the new order.
    EXPECT_EQ(registry.GetComponent<Transform>(c).siblingIndex, 0u);
    EXPECT_EQ(registry.GetComponent<Transform>(a).siblingIndex, 1u);
    EXPECT_EQ(registry.GetComponent<Transform>(b).siblingIndex, 2u);
}

TEST(TransformHierarchyTest, SetSiblingIndexClampsOutOfRangeToTheEnd)
{
    Registry registry;
    const Entity a = registry.CreateEntity();
    registry.AddComponent<Transform>(a);
    const Entity b = registry.CreateEntity();
    registry.AddComponent<Transform>(b);
    MoveToLastSibling(registry, a);
    MoveToLastSibling(registry, b);

    EXPECT_TRUE(SetSiblingIndex(registry, a, 999));

    const std::vector<Entity> children = GetChildren(registry, kInvalidEntity);
    ASSERT_EQ(children.size(), 2u);
    EXPECT_EQ(children[0], b);
    EXPECT_EQ(children[1], a);
}

TEST(TransformHierarchyTest, SetSiblingIndexReturnsFalseForEntityWithNoTransform)
{
    Registry registry;
    const Entity entity = registry.CreateEntity(); // No Transform.

    EXPECT_FALSE(SetSiblingIndex(registry, entity, 0));
}

TEST(TransformHierarchyTest, MoveToLastSiblingAppendsAfterCurrentMaxSiblingIndex)
{
    Registry registry;
    const Entity a = registry.CreateEntity();
    registry.AddComponent<Transform>(a);
    const Entity b = registry.CreateEntity();
    registry.AddComponent<Transform>(b);

    MoveToLastSibling(registry, a);
    MoveToLastSibling(registry, b);
    // Move `a` to the end again - should now come after `b`.
    MoveToLastSibling(registry, a);

    const std::vector<Entity> children = GetChildren(registry, kInvalidEntity);
    ASSERT_EQ(children.size(), 2u);
    EXPECT_EQ(children[0], b);
    EXPECT_EQ(children[1], a);
}

} // namespace
} // namespace gte
