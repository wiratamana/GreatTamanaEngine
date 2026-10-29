// Tests for ECS/TransformHierarchy.h on a plain Registry (no GPU, no ImGui).

#include "ECS/TransformHierarchy.h"

#include "ECS/Components/Transform.h"
#include "ECS/Registry.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <random>
#include <vector>

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

TEST(TransformHierarchyTest, DestroyEntityAndDescendantsDestroysWholeChain)
{
    Registry registry;
    const Entity parent = registry.CreateEntity();
    registry.AddComponent<Transform>(parent);
    const Entity child = registry.CreateEntity();
    registry.AddComponent<Transform>(child);
    const Entity grandchild = registry.CreateEntity();
    registry.AddComponent<Transform>(grandchild);

    ASSERT_TRUE(SetParent(registry, child, parent));
    ASSERT_TRUE(SetParent(registry, grandchild, child));

    DestroyEntityAndDescendants(registry, parent);

    EXPECT_FALSE(registry.IsAlive(parent));
    EXPECT_FALSE(registry.IsAlive(child));
    EXPECT_FALSE(registry.IsAlive(grandchild));
}

TEST(TransformHierarchyTest, DestroyEntityAndDescendantsLeafOnlyRemovesItselfNotSiblings)
{
    Registry registry;
    const Entity parent = registry.CreateEntity();
    registry.AddComponent<Transform>(parent);
    const Entity leaf = registry.CreateEntity();
    registry.AddComponent<Transform>(leaf);
    const Entity sibling = registry.CreateEntity();
    registry.AddComponent<Transform>(sibling);

    ASSERT_TRUE(SetParent(registry, leaf, parent));
    ASSERT_TRUE(SetParent(registry, sibling, parent));

    DestroyEntityAndDescendants(registry, leaf);

    EXPECT_FALSE(registry.IsAlive(leaf));
    EXPECT_TRUE(registry.IsAlive(sibling));
    EXPECT_TRUE(registry.IsAlive(parent));
}

TEST(TransformHierarchyTest, DestroyEntityAndDescendantsOnAlreadyDeadEntityIsSafeNoOp)
{
    Registry registry;
    const Entity entity = registry.CreateEntity();
    registry.AddComponent<Transform>(entity);
    registry.DestroyEntity(entity);

    DestroyEntityAndDescendants(registry, entity); // Already dead.
    DestroyEntityAndDescendants(registry, kInvalidEntity); // Default/invalid.
    DestroyEntityAndDescendants(registry, Entity{}); // Default-constructed.

    // No crash, and nothing else in the registry is affected.
    EXPECT_FALSE(registry.IsAlive(entity));
}

TEST(TransformHierarchyTest, DestroyEntityAndDescendantsRemovesEveryDescendantInWiderTree)
{
    Registry registry;
    const Entity parent = registry.CreateEntity();
    registry.AddComponent<Transform>(parent);
    const Entity childA = registry.CreateEntity();
    registry.AddComponent<Transform>(childA);
    const Entity childB = registry.CreateEntity();
    registry.AddComponent<Transform>(childB);
    const Entity grandchildOfA = registry.CreateEntity();
    registry.AddComponent<Transform>(grandchildOfA);
    const Entity unrelated = registry.CreateEntity();
    registry.AddComponent<Transform>(unrelated);

    ASSERT_TRUE(SetParent(registry, childA, parent));
    ASSERT_TRUE(SetParent(registry, childB, parent));
    ASSERT_TRUE(SetParent(registry, grandchildOfA, childA));

    DestroyEntityAndDescendants(registry, parent);

    EXPECT_FALSE(registry.IsAlive(parent));
    EXPECT_FALSE(registry.IsAlive(childA));
    EXPECT_FALSE(registry.IsAlive(childB));
    EXPECT_FALSE(registry.IsAlive(grandchildOfA));
    EXPECT_TRUE(registry.IsAlive(unrelated));
}

// --- Sibling order and effective parent ---

TEST(TransformHierarchyTest, TiesAreOrderedByEntityIndexNotByStorageOrder)
{
    Registry registry;
    const Entity a = registry.CreateEntity();
    const Entity b = registry.CreateEntity();
    const Entity c = registry.CreateEntity();
    registry.AddComponent<Transform>(a);
    registry.AddComponent<Transform>(b);
    registry.AddComponent<Transform>(c);

    // Removing and re-adding b moves it to the end of the dense storage.
    registry.RemoveComponent<Transform>(b);
    registry.AddComponent<Transform>(b);

    EXPECT_EQ(GetChildren(registry, kInvalidEntity), (std::vector<Entity>{ a, b, c }));
}

TEST(TransformHierarchyTest, SiblingLessOrdersBySiblingIndexThenEntityIndexThenGeneration)
{
    EXPECT_TRUE(SiblingLess(1, Entity{ 9, 1 }, 2, Entity{ 0, 1 }));
    EXPECT_TRUE(SiblingLess(3, Entity{ 1, 1 }, 3, Entity{ 2, 1 }));
    EXPECT_TRUE(SiblingLess(3, Entity{ 1, 1 }, 3, Entity{ 1, 2 }));
    EXPECT_FALSE(SiblingLess(3, Entity{ 1, 2 }, 3, Entity{ 1, 2 }));
}

TEST(TransformHierarchyTest, EffectiveParentIgnoresDeadParentsAndParentsWithoutTransform)
{
    Registry registry;
    const Entity alive = registry.CreateEntity();
    registry.AddComponent<Transform>(alive);
    const Entity bare = registry.CreateEntity();
    const Entity dead = registry.CreateEntity();
    registry.AddComponent<Transform>(dead);
    registry.DestroyEntity(dead);

    Transform transform;
    transform.parent = alive;
    EXPECT_EQ(EffectiveParent(registry, transform), alive);
    transform.parent = bare;
    EXPECT_EQ(EffectiveParent(registry, transform), kInvalidEntity);
    transform.parent = dead;
    EXPECT_EQ(EffectiveParent(registry, transform), kInvalidEntity);
}

TEST(TransformHierarchyTest, MoveToLastSiblingAndSetSiblingIndexAgreeWithGetChildrenForADeadParent)
{
    Registry registry;
    const Entity root = registry.CreateEntity();
    registry.AddComponent<Transform>(root);
    const Entity parent = registry.CreateEntity();
    registry.AddComponent<Transform>(parent);
    const Entity orphan = registry.CreateEntity();
    registry.AddComponent<Transform>(orphan).parent = parent;
    registry.DestroyEntity(parent); // orphan now lists as a root.

    ASSERT_EQ(GetChildren(registry, kInvalidEntity).size(), 2u);

    MoveToLastSibling(registry, root);
    MoveToLastSibling(registry, orphan);
    EXPECT_EQ(GetChildren(registry, kInvalidEntity), (std::vector<Entity>{ root, orphan }));
    EXPECT_NE(registry.GetComponent<Transform>(root).siblingIndex, registry.GetComponent<Transform>(orphan).siblingIndex);

    EXPECT_TRUE(SetSiblingIndex(registry, orphan, 0));
    EXPECT_EQ(GetChildren(registry, kInvalidEntity), (std::vector<Entity>{ orphan, root }));
    EXPECT_EQ(registry.GetComponent<Transform>(orphan).siblingIndex, 0u);
    EXPECT_EQ(registry.GetComponent<Transform>(root).siblingIndex, 1u);
}

TEST(TransformHierarchyTest, ChildOfAParentWithoutTransformIsListedAsARoot)
{
    Registry registry;
    const Entity bare = registry.CreateEntity(); // No Transform.
    const Entity child = registry.CreateEntity();
    registry.AddComponent<Transform>(child).parent = bare;

    EXPECT_EQ(GetChildren(registry, kInvalidEntity), (std::vector<Entity>{ child }));
    EXPECT_TRUE(GetChildren(registry, bare).empty());
}

// --- Bulk destroy ---

// Builds `count` entities; entity i gets a random earlier entity as parent (or none).
std::vector<Entity> MakeRandomForest(Registry& registry, std::uint32_t count, std::vector<int>& outParentIndex)
{
    std::mt19937 rng(1234);
    std::vector<Entity> entities;
    outParentIndex.assign(count, -1);
    for (std::uint32_t i = 0; i < count; ++i) {
        const Entity entity = registry.CreateEntity();
        Transform& transform = registry.AddComponent<Transform>(entity);
        if (i > 0 && rng() % 4 != 0) {
            outParentIndex[i] = static_cast<int>(rng() % i);
            transform.parent = entities[static_cast<std::size_t>(outParentIndex[i])];
        }
        transform.siblingIndex = rng() % 4;
        entities.push_back(entity);
    }
    return entities;
}

TEST(TransformHierarchyTest, DestroyEntitiesAndDescendantsMatchesTheSubtreeRuleOnARandomForest)
{
    Registry registry;
    std::vector<int> parentIndex;
    const std::vector<Entity> entities = MakeRandomForest(registry, 400, parentIndex);

    std::vector<Entity> roots;
    std::vector<bool> expectedDead(entities.size(), false);
    for (std::size_t i = 0; i < entities.size(); ++i) {
        const bool isRoot = i % 17 == 3;
        if (isRoot) {
            roots.push_back(entities[i]);
        }
        expectedDead[i] = isRoot || (parentIndex[i] >= 0 && expectedDead[static_cast<std::size_t>(parentIndex[i])]);
    }

    DestroyEntitiesAndDescendants(registry, roots);

    for (std::size_t i = 0; i < entities.size(); ++i) {
        EXPECT_EQ(registry.IsAlive(entities[i]), !expectedDead[i]) << "entity " << i;
    }
}

TEST(TransformHierarchyTest, OverlappingRootsAreDestroyedOnce)
{
    Registry registry;
    const Entity parent = registry.CreateEntity();
    registry.AddComponent<Transform>(parent);
    const Entity child = registry.CreateEntity();
    registry.AddComponent<Transform>(child).parent = parent;
    const Entity grandchild = registry.CreateEntity();
    registry.AddComponent<Transform>(grandchild).parent = child;
    const Entity unrelated = registry.CreateEntity();
    registry.AddComponent<Transform>(unrelated);

    const std::vector<Entity> roots{ child, parent, child, grandchild, kInvalidEntity };
    DestroyEntitiesAndDescendants(registry, roots);

    EXPECT_FALSE(registry.IsAlive(parent));
    EXPECT_FALSE(registry.IsAlive(child));
    EXPECT_FALSE(registry.IsAlive(grandchild));
    EXPECT_TRUE(registry.IsAlive(unrelated));
    EXPECT_EQ(registry.AliveEntityCount(), 1u);
}

TEST(TransformHierarchyTest, DestroyOfAHundredThousandRootsFinishes)
{
    Registry registry;
    std::vector<Entity> roots;
    roots.reserve(100000);
    for (int i = 0; i < 100000; ++i) {
        const Entity entity = registry.CreateEntity();
        registry.AddComponent<Transform>(entity);
        roots.push_back(entity);
    }

    DestroyEntitiesAndDescendants(registry, roots);

    EXPECT_EQ(registry.AliveEntityCount(), 0u);
}

TEST(TransformHierarchyTest, DeepChainOf100000DoesNotOverflowTheStack)
{
    Registry registry;
    std::vector<Entity> chain;
    chain.reserve(100000);
    for (int i = 0; i < 100000; ++i) {
        const Entity entity = registry.CreateEntity();
        Transform& transform = registry.AddComponent<Transform>(entity);
        if (!chain.empty()) {
            transform.parent = chain.back(); // direct write: SetParent would be quadratic
        }
        chain.push_back(entity);
    }

    DestroyEntityAndDescendants(registry, chain.front());

    EXPECT_EQ(registry.AliveEntityCount(), 0u);
}

// --- AssignHierarchy ---

TEST(TransformHierarchyTest, AssignHierarchySetsParentsAndRenumbersSiblingsDenselyBySavedIndex)
{
    Registry registry;
    Entity e[4];
    for (Entity& entity : e) {
        entity = registry.CreateEntity();
        registry.AddComponent<Transform>(entity).position = Vec3(1.0f, 2.0f, 3.0f);
    }
    const Entity a = e[0], b = e[1], c = e[2], d = e[3];

    const std::vector<HierarchyAssignment> assignments{
        { a, kInvalidEntity, 0 },
        { c, a, 5 },
        { b, a, 2 },
        { d, a, 2 }, // ties with b: input order decides
    };
    AssignHierarchy(registry, assignments);

    EXPECT_EQ(GetChildren(registry, a), (std::vector<Entity>{ b, d, c }));
    EXPECT_EQ(registry.GetComponent<Transform>(b).siblingIndex, 0u);
    EXPECT_EQ(registry.GetComponent<Transform>(d).siblingIndex, 1u);
    EXPECT_EQ(registry.GetComponent<Transform>(c).siblingIndex, 2u);
    EXPECT_EQ(registry.GetComponent<Transform>(a).parent, kInvalidEntity);
    EXPECT_TRUE(ApproximatelyEqual(registry.GetComponent<Transform>(b).position, Vec3(1.0f, 2.0f, 3.0f)));
}

TEST(TransformHierarchyTest, AssignHierarchyLastAssignmentOfAChildWins)
{
    Registry registry;
    const Entity a = registry.CreateEntity();
    registry.AddComponent<Transform>(a);
    const Entity b = registry.CreateEntity();
    registry.AddComponent<Transform>(b);
    const Entity c = registry.CreateEntity();
    registry.AddComponent<Transform>(c);

    const std::vector<HierarchyAssignment> assignments{ { c, a, 0 }, { c, b, 0 } };
    AssignHierarchy(registry, assignments);

    EXPECT_EQ(registry.GetComponent<Transform>(c).parent, b);
}

TEST(TransformHierarchyTest, AssignHierarchyLeavesAChildARootForADeadOrTransformlessParent)
{
    Registry registry;
    const Entity child = registry.CreateEntity();
    registry.AddComponent<Transform>(child);
    const Entity bare = registry.CreateEntity(); // No Transform.
    const Entity dead = registry.CreateEntity();
    registry.AddComponent<Transform>(dead);
    registry.DestroyEntity(dead);

    AssignHierarchy(registry, std::vector<HierarchyAssignment>{ { child, bare, 0 } });
    EXPECT_EQ(registry.GetComponent<Transform>(child).parent, kInvalidEntity);

    AssignHierarchy(registry, std::vector<HierarchyAssignment>{ { child, dead, 0 } });
    EXPECT_EQ(registry.GetComponent<Transform>(child).parent, kInvalidEntity);

    AssignHierarchy(registry, std::vector<HierarchyAssignment>{ { child, child, 0 } });
    EXPECT_EQ(registry.GetComponent<Transform>(child).parent, kInvalidEntity);
}

TEST(TransformHierarchyTest, AssignHierarchyRejectsACycle)
{
    Registry registry;
    Entity e[3];
    for (Entity& entity : e) {
        entity = registry.CreateEntity();
        registry.AddComponent<Transform>(entity);
    }

    // e0 -> e1 -> e2 -> e0
    AssignHierarchy(registry, std::vector<HierarchyAssignment>{ { e[0], e[1], 0 }, { e[1], e[2], 0 }, { e[2], e[0], 0 } });

    int roots = 0;
    for (const Entity entity : e) {
        roots += registry.GetComponent<Transform>(entity).parent == kInvalidEntity ? 1 : 0;
        // Every parent chain must end at a root.
        Entity cursor = entity;
        for (int hops = 0; hops < 4 && cursor.IsValid(); ++hops) {
            cursor = registry.GetComponent<Transform>(cursor).parent;
        }
        EXPECT_FALSE(cursor.IsValid());
    }
    EXPECT_EQ(roots, 1);
}

TEST(TransformHierarchyTest, AssignHierarchyRejectsACycleThroughAnUnassignedParentLink)
{
    Registry registry;
    const Entity x = registry.CreateEntity();
    registry.AddComponent<Transform>(x);
    const Entity y = registry.CreateEntity();
    registry.AddComponent<Transform>(y).parent = x;

    AssignHierarchy(registry, std::vector<HierarchyAssignment>{ { x, y, 0 } }); // x under its own child

    EXPECT_EQ(registry.GetComponent<Transform>(x).parent, kInvalidEntity);
    EXPECT_EQ(registry.GetComponent<Transform>(y).parent, x);
}

TEST(TransformHierarchyTest, AssignHierarchyHandlesADeepChain)
{
    Registry registry;
    std::vector<Entity> chain;
    std::vector<HierarchyAssignment> assignments;
    chain.reserve(100000);
    assignments.reserve(100000);
    for (int i = 0; i < 100000; ++i) {
        const Entity entity = registry.CreateEntity();
        registry.AddComponent<Transform>(entity);
        assignments.push_back({ entity, chain.empty() ? kInvalidEntity : chain.back(), 0 });
        chain.push_back(entity);
    }

    AssignHierarchy(registry, assignments);

    EXPECT_EQ(registry.GetComponent<Transform>(chain.front()).parent, kInvalidEntity);
    EXPECT_EQ(registry.GetComponent<Transform>(chain.back()).parent, chain[chain.size() - 2]);
}

} // namespace
} // namespace gte
