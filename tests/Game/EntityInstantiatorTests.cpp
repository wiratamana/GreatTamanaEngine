// Unit tests for EntityInstantiator.h's Instantiate() - the single shared
// function both PrimitiveGpuCatalog and MeshAssetGpuCatalog resolve down to
// (see GameInstantiationRefactorProposal.txt, Step 3.0). Hand-built
// EntityBlueprint values in, assert the resulting entities/components/
// parent-child structure - needs nothing but a Registry, no Renderer/GPU
// device at all (Tier 1 - see AGENTS.md, "Testability & Regression Safety").

#include "Game/Instantiation/EntityInstantiator.h"
#include "ECS/Components/MeshAssetSource.h"
#include "ECS/Components/MeshRenderer.h"
#include "ECS/Components/Name.h"
#include "ECS/Components/Transform.h"
#include "ECS/TransformHierarchy.h"

#include <gtest/gtest.h>

namespace gte {
namespace {

TEST(EntityInstantiatorTest, SingleNodeWithMeshCreatesTransformAndMeshRenderer)
{
    Registry registry;

    EntityBlueprint blueprint;
    blueprint.mesh = MeshHandle{ 1, 1 };
    blueprint.pipeline = PipelineHandle{ 2, 1 };
    blueprint.texture = TextureHandle{ 3, 1 };

    const Entity entity = Instantiate(registry, blueprint);

    ASSERT_TRUE(registry.IsAlive(entity));
    EXPECT_TRUE(registry.HasComponent<Transform>(entity));
    ASSERT_TRUE(registry.HasComponent<MeshRenderer>(entity));
    const MeshRenderer& renderer = registry.GetComponent<MeshRenderer>(entity);
    EXPECT_EQ(renderer.mesh, blueprint.mesh);
    EXPECT_EQ(renderer.pipeline, blueprint.pipeline);
    EXPECT_EQ(renderer.texture, blueprint.texture);
    EXPECT_FALSE(registry.HasComponent<Name>(entity));
    EXPECT_FALSE(registry.HasComponent<MeshAssetSource>(entity));
}

TEST(EntityInstantiatorTest, BareRootNodeWithNoMeshHasNoMeshRenderer)
{
    Registry registry;

    EntityBlueprint blueprint; // mesh left invalid - a bare hierarchy root.

    const Entity entity = Instantiate(registry, blueprint);

    ASSERT_TRUE(registry.IsAlive(entity));
    EXPECT_TRUE(registry.HasComponent<Transform>(entity));
    EXPECT_FALSE(registry.HasComponent<MeshRenderer>(entity));
}

TEST(EntityInstantiatorTest, NonEmptyNameAddsNameComponent)
{
    Registry registry;

    EntityBlueprint blueprint;
    blueprint.name = "Furina";

    const Entity entity = Instantiate(registry, blueprint);

    ASSERT_TRUE(registry.HasComponent<Name>(entity));
    EXPECT_EQ(registry.GetComponent<Name>(entity).value, "Furina");
}

TEST(EntityInstantiatorTest, EmptyNameAddsNoNameComponent)
{
    Registry registry;

    EntityBlueprint blueprint; // name left empty.

    const Entity entity = Instantiate(registry, blueprint);

    EXPECT_FALSE(registry.HasComponent<Name>(entity));
}

TEST(EntityInstantiatorTest, NonEmptySourcePathAddsMeshAssetSourceComponent)
{
    Registry registry;

    EntityBlueprint blueprint;
    blueprint.meshAssetSourcePath = "C:/Project/Furina.gta";

    const Entity entity = Instantiate(registry, blueprint);

    ASSERT_TRUE(registry.HasComponent<MeshAssetSource>(entity));
    EXPECT_EQ(registry.GetComponent<MeshAssetSource>(entity).gtaPath, "C:/Project/Furina.gta");
}

TEST(EntityInstantiatorTest, LocalTransformOverrideIsAppliedVerbatim)
{
    Registry registry;

    EntityBlueprint blueprint;
    blueprint.localTransform.position = Vec3{ 1.0f, 2.0f, 3.0f };
    blueprint.localTransform.scale = Vec3{ 2.0f, 2.0f, 2.0f };

    const Entity entity = Instantiate(registry, blueprint);

    const Transform& transform = registry.GetComponent<Transform>(entity);
    EXPECT_TRUE(ApproximatelyEqual(transform.position, Vec3{ 1.0f, 2.0f, 3.0f }));
    EXPECT_TRUE(ApproximatelyEqual(transform.scale, Vec3{ 2.0f, 2.0f, 2.0f }));
}

TEST(EntityInstantiatorTest, ChildrenAreAttachedUnderTheParentEntity)
{
    Registry registry;

    EntityBlueprint root;
    root.name = "Root";
    EntityBlueprintNode childA;
    childA.name = "PartA";
    childA.mesh = MeshHandle{ 1, 1 };
    childA.pipeline = PipelineHandle{ 1, 1 };
    EntityBlueprintNode childB;
    childB.name = "PartB";
    childB.mesh = MeshHandle{ 2, 1 };
    childB.pipeline = PipelineHandle{ 1, 1 };
    root.children.push_back(childA);
    root.children.push_back(childB);

    const Entity rootEntity = Instantiate(registry, root);

    ASSERT_TRUE(registry.IsAlive(rootEntity));
    EXPECT_FALSE(registry.HasComponent<MeshRenderer>(rootEntity));

    const std::vector<Entity> children = GetChildren(registry, rootEntity);
    ASSERT_EQ(children.size(), 2u);

    EXPECT_EQ(registry.GetComponent<Transform>(children[0]).parent, rootEntity);
    EXPECT_EQ(registry.GetComponent<Name>(children[0]).value, "PartA");
    EXPECT_EQ(registry.GetComponent<Transform>(children[1]).parent, rootEntity);
    EXPECT_EQ(registry.GetComponent<Name>(children[1]).value, "PartB");
}

TEST(EntityInstantiatorTest, ExplicitParentArgumentAttachesTheWholeBlueprintUnderIt)
{
    Registry registry;
    const Entity existingParent = registry.CreateEntity();
    registry.AddComponent<Transform>(existingParent);

    EntityBlueprint blueprint;
    blueprint.mesh = MeshHandle{ 1, 1 };
    blueprint.pipeline = PipelineHandle{ 1, 1 };

    const Entity entity = Instantiate(registry, blueprint, existingParent);

    EXPECT_EQ(registry.GetComponent<Transform>(entity).parent, existingParent);
}

TEST(EntityInstantiatorTest, DeeplyNestedChildrenAreAttachedRecursively)
{
    Registry registry;

    EntityBlueprint root;
    EntityBlueprintNode child;
    EntityBlueprintNode grandchild;
    grandchild.mesh = MeshHandle{ 5, 1 };
    grandchild.pipeline = PipelineHandle{ 1, 1 };
    child.children.push_back(grandchild);
    root.children.push_back(child);

    const Entity rootEntity = Instantiate(registry, root);
    const std::vector<Entity> children = GetChildren(registry, rootEntity);
    ASSERT_EQ(children.size(), 1u);
    const std::vector<Entity> grandchildren = GetChildren(registry, children[0]);
    ASSERT_EQ(grandchildren.size(), 1u);
    EXPECT_TRUE(registry.HasComponent<MeshRenderer>(grandchildren[0]));
}

} // namespace
} // namespace gte
