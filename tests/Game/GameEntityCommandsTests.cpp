// Unit tests for Game::DeleteEntityByName()/SetEntityTrs()/InstantiateLight()
// (src/Game/Game.h/.cpp, network-impl-3/network-impl-5 campaigns) - the
// engine-command methods that need no live Renderer/GPU device at all
// (unlike InstantiatePrimitive(), which touches CreatePrimitiveEntity() ->
// MeshInstantiationSystem -> PrimitiveGpuCatalog and stays in this codebase's
// accepted "Tier 2, no automated coverage yet" bucket - see AGENTS.md,
// "Testability & Regression Safety", and this campaign's own
// PHASE3_COMPLETION_REPORT.md/PHASE6 strategy document). Game() itself never
// requires a Renderer to construct, so this whole file is genuinely
// Tier-1-testable: it constructs a real Game, reaches into its ECS world via
// GetRegistry() to hand-build entities/Name components/a parent-child
// relationship, then exercises these methods directly.
//
// network-impl-5 campaign
// (PHASE2_GAME_LEVEL_SET_ENTITY_TRS_AND_INSTANTIATE_LIGHT_APIS.md) - adds
// SetEntityTrs()/InstantiateLight() coverage, plus a regression test proving
// the Step 3.4 DefaultDirectionalLightRotation() refactor left
// CreateDirectionalLightEntity()'s own observable behavior completely
// unchanged.

#include "ECS/Components/DirectionalLight.h"
#include "ECS/Components/Name.h"
#include "ECS/Components/Transform.h"
#include "ECS/TransformHierarchy.h"
#include "Game/Game.h"

#include <gtest/gtest.h>

namespace gte {
namespace {

TEST(GameEntityCommandsTest, DeleteEntityByNameDestroysTheNamedEntityAndItsDescendants)
{
    Game game;
    Registry& registry = game.GetRegistry();

    const Entity parent = registry.CreateEntity();
    registry.AddComponent<Transform>(parent);
    registry.AddComponent<Name>(parent).value = "Parent";

    const Entity child = registry.CreateEntity();
    registry.AddComponent<Transform>(child);
    registry.AddComponent<Name>(child).value = "Child";
    ASSERT_TRUE(SetParent(registry, child, parent));

    // An unrelated, sibling entity that must survive the delete untouched -
    // proves DestroyEntityAndDescendants() (reached transitively through
    // DeleteEntityByName()) is scoped to exactly the named entity's own
    // subtree, not the whole scene.
    const Entity unrelated = registry.CreateEntity();
    registry.AddComponent<Name>(unrelated).value = "Unrelated";

    const DeleteEntityOutcome outcome = game.DeleteEntityByName("Parent");

    EXPECT_TRUE(outcome.success);
    EXPECT_TRUE(outcome.errorMessage.empty());
    EXPECT_EQ(outcome.deletedEntityIndex, parent.index);
    EXPECT_EQ(outcome.deletedEntityGeneration, parent.generation);

    EXPECT_FALSE(registry.IsAlive(parent));
    EXPECT_FALSE(registry.IsAlive(child));
    EXPECT_TRUE(registry.IsAlive(unrelated));
}

TEST(GameEntityCommandsTest, DeleteEntityByNameFailsForANameThatWasNeverUsed)
{
    Game game;
    Registry& registry = game.GetRegistry();

    const Entity a = registry.CreateEntity();
    registry.AddComponent<Name>(a).value = "SomethingElse";

    const DeleteEntityOutcome outcome = game.DeleteEntityByName("DoesNotExist");

    EXPECT_FALSE(outcome.success);
    EXPECT_FALSE(outcome.errorMessage.empty());
    EXPECT_TRUE(registry.IsAlive(a)); // Untouched.
}

TEST(GameEntityCommandsTest, DeleteEntityByNameFailsForAnEmptyName)
{
    Game game;

    const DeleteEntityOutcome outcome = game.DeleteEntityByName("");

    EXPECT_FALSE(outcome.success);
    EXPECT_FALSE(outcome.errorMessage.empty());
}

// --- Game::SetEntityTrs() ---

TEST(GameEntityCommandsTest, SetEntityTrsChangesOnlyTranslationWhenOnlyTranslationSupplied)
{
    Game game;
    Registry& registry = game.GetRegistry();

    const Entity entity = registry.CreateEntity();
    Transform& transform = registry.AddComponent<Transform>(entity);
    const Quat originalRotation = Quat::FromEulerDegrees(10.0f, 20.0f, 30.0f);
    transform.rotation = originalRotation;
    transform.scale = Vec3{ 2.0f, 2.0f, 2.0f };
    registry.AddComponent<Name>(entity).value = "Thing";

    SetEntityTrsParams params;
    params.name = "Thing";
    params.hasTranslation = true;
    params.translation = Vec3{ 1.0f, 2.0f, 3.0f };

    const SetEntityTrsOutcome outcome = game.SetEntityTrs(params);

    EXPECT_TRUE(outcome.success);
    EXPECT_TRUE(outcome.errorMessage.empty());
    EXPECT_TRUE(outcome.translationChanged);
    EXPECT_FALSE(outcome.rotationChanged);
    EXPECT_FALSE(outcome.scaleChanged);

    EXPECT_EQ(transform.position, (Vec3{ 1.0f, 2.0f, 3.0f }));
    EXPECT_TRUE(RepresentSameRotation(transform.rotation, originalRotation));
    EXPECT_EQ(transform.scale, (Vec3{ 2.0f, 2.0f, 2.0f }));

    EXPECT_EQ(outcome.resultingPosition, (Vec3{ 1.0f, 2.0f, 3.0f }));
    EXPECT_TRUE(RepresentSameRotation(outcome.resultingRotation, originalRotation));
    EXPECT_EQ(outcome.resultingScale, (Vec3{ 2.0f, 2.0f, 2.0f }));
}

TEST(GameEntityCommandsTest, SetEntityTrsChangesAllThreeWhenAllThreeSupplied)
{
    Game game;
    Registry& registry = game.GetRegistry();

    const Entity entity = registry.CreateEntity();
    registry.AddComponent<Transform>(entity);
    registry.AddComponent<Name>(entity).value = "Thing";

    SetEntityTrsParams params;
    params.name = "Thing";
    params.hasTranslation = true;
    params.translation = Vec3{ 1.0f, 2.0f, 3.0f };
    params.hasRotationEulerDegrees = true;
    params.rotationEulerDegrees = Vec3{ 0.0f, 90.0f, 0.0f };
    params.hasScale = true;
    params.scale = Vec3{ 2.0f, 3.0f, 4.0f };

    const SetEntityTrsOutcome outcome = game.SetEntityTrs(params);

    EXPECT_TRUE(outcome.success);
    EXPECT_TRUE(outcome.translationChanged);
    EXPECT_TRUE(outcome.rotationChanged);
    EXPECT_TRUE(outcome.scaleChanged);

    EXPECT_EQ(outcome.resultingPosition, (Vec3{ 1.0f, 2.0f, 3.0f }));
    EXPECT_TRUE(RepresentSameRotation(outcome.resultingRotation, Quat::FromEulerDegrees(0.0f, 90.0f, 0.0f)));
    EXPECT_EQ(outcome.resultingScale, (Vec3{ 2.0f, 3.0f, 4.0f }));
}

TEST(GameEntityCommandsTest, SetEntityTrsNoOpCallStillReportsSuccessAndEchoesCurrentTransform)
{
    Game game;
    Registry& registry = game.GetRegistry();

    const Entity entity = registry.CreateEntity();
    Transform& transform = registry.AddComponent<Transform>(entity);
    transform.position = Vec3{ 5.0f, 6.0f, 7.0f };
    registry.AddComponent<Name>(entity).value = "Thing";

    SetEntityTrsParams params;
    params.name = "Thing";
    // hasTranslation/hasRotationEulerDegrees/hasScale all left false.

    const SetEntityTrsOutcome outcome = game.SetEntityTrs(params);

    EXPECT_TRUE(outcome.success);
    EXPECT_FALSE(outcome.translationChanged);
    EXPECT_FALSE(outcome.rotationChanged);
    EXPECT_FALSE(outcome.scaleChanged);
    EXPECT_EQ(outcome.resultingPosition, (Vec3{ 5.0f, 6.0f, 7.0f }));
}

TEST(GameEntityCommandsTest, SetEntityTrsFailsWithEntityNotFoundForAnUnknownName)
{
    Game game;

    SetEntityTrsParams params;
    params.name = "DoesNotExist";

    const SetEntityTrsOutcome outcome = game.SetEntityTrs(params);

    EXPECT_FALSE(outcome.success);
    EXPECT_TRUE(outcome.entityNotFound);
    EXPECT_FALSE(outcome.errorMessage.empty());
}

TEST(GameEntityCommandsTest, SetEntityTrsFailsWithoutEntityNotFoundWhenEntityHasNoTransform)
{
    Game game;
    Registry& registry = game.GetRegistry();

    const Entity entity = registry.CreateEntity();
    // Deliberately NO Transform component added.
    registry.AddComponent<Name>(entity).value = "NoTransform";

    SetEntityTrsParams params;
    params.name = "NoTransform";
    params.hasTranslation = true;
    params.translation = Vec3{ 1.0f, 1.0f, 1.0f };

    const SetEntityTrsOutcome outcome = game.SetEntityTrs(params);

    EXPECT_FALSE(outcome.success);
    EXPECT_FALSE(outcome.entityNotFound);
    EXPECT_FALSE(outcome.errorMessage.empty());
}

// --- Game::InstantiateLight() ---

TEST(GameEntityCommandsTest, InstantiateLightDefaultCallMatchesCreateDirectionalLightEntityRotation)
{
    Game game;
    Registry& registry = game.GetRegistry();

    const Entity editorLight = game.CreateDirectionalLightEntity();
    const Transform& editorTransform = registry.GetComponent<Transform>(editorLight);

    InstantiateLightParams params;
    params.requestedName = "NetworkLight";
    // lightType empty, hasRotationEulerDegrees false - use every default.

    const InstantiateLightOutcome outcome = game.InstantiateLight(params);
    ASSERT_TRUE(outcome.success);

    const Entity spawned{ outcome.entityIndex, outcome.entityGeneration };
    const Transform& spawnedTransform = registry.GetComponent<Transform>(spawned);

    EXPECT_TRUE(RepresentSameRotation(spawnedTransform.rotation, editorTransform.rotation));
}

TEST(GameEntityCommandsTest, InstantiateLightExplicitRotationOverridesDefault)
{
    Game game;
    Registry& registry = game.GetRegistry();

    InstantiateLightParams params;
    params.requestedName = "RotatedLight";
    params.hasRotationEulerDegrees = true;
    params.rotationEulerDegrees = Vec3{ 0.0f, 180.0f, 0.0f };

    const InstantiateLightOutcome outcome = game.InstantiateLight(params);
    ASSERT_TRUE(outcome.success);

    const Entity spawned{ outcome.entityIndex, outcome.entityGeneration };
    const Transform& transform = registry.GetComponent<Transform>(spawned);
    EXPECT_TRUE(RepresentSameRotation(transform.rotation, Quat::FromEulerDegrees(0.0f, 180.0f, 0.0f)));
}

TEST(GameEntityCommandsTest, InstantiateLightFailsForAnUnrecognizedLightTypeAndCreatesNoEntity)
{
    Game game;
    Registry& registry = game.GetRegistry();

    const std::size_t beforeCount = registry.AliveEntityCount();

    InstantiateLightParams params;
    params.lightType = "point";
    params.requestedName = "ShouldNotExist";

    const InstantiateLightOutcome outcome = game.InstantiateLight(params);

    EXPECT_FALSE(outcome.success);
    EXPECT_FALSE(outcome.errorMessage.empty());
    EXPECT_EQ(registry.AliveEntityCount(), beforeCount);
}

TEST(GameEntityCommandsTest, InstantiateLightReportsUnresolvedParentButStillSucceeds)
{
    Game game;

    InstantiateLightParams params;
    params.requestedName = "OrphanLight";
    params.hasParent = true;
    params.parentName = "NoSuchParent";

    const InstantiateLightOutcome outcome = game.InstantiateLight(params);

    EXPECT_TRUE(outcome.success);
    EXPECT_TRUE(outcome.parentRequestedButNotFound);
    EXPECT_EQ(outcome.requestedParentName, "NoSuchParent");
}

TEST(GameEntityCommandsTest, InstantiateLightRoundTripsColorIlluminanceAndActiveOntoTheComponent)
{
    Game game;
    Registry& registry = game.GetRegistry();

    InstantiateLightParams params;
    params.requestedName = "TintedLight";
    params.color = Vec3{ 0.25f, 0.5f, 0.75f };
    params.illuminanceLux = 12345.0f;
    params.active = false;

    const InstantiateLightOutcome outcome = game.InstantiateLight(params);
    ASSERT_TRUE(outcome.success);

    const Entity spawned{ outcome.entityIndex, outcome.entityGeneration };
    const DirectionalLight& light = registry.GetComponent<DirectionalLight>(spawned);
    EXPECT_EQ(light.color, (Vec3{ 0.25f, 0.5f, 0.75f }));
    EXPECT_FLOAT_EQ(light.illuminanceLux, 12345.0f);
    EXPECT_FALSE(light.active);
}

// --- Regression: Step 3.4's DefaultDirectionalLightRotation() refactor must
// not change CreateDirectionalLightEntity()'s own observable behavior. ---

TEST(GameEntityCommandsTest, CreateDirectionalLightEntityBehaviorIsUnchangedAfterRefactor)
{
    Game game;
    Registry& registry = game.GetRegistry();

    const Entity entity = game.CreateDirectionalLightEntity();

    ASSERT_TRUE(registry.HasComponent<Transform>(entity));
    ASSERT_TRUE(registry.HasComponent<DirectionalLight>(entity));
    ASSERT_TRUE(registry.HasComponent<Name>(entity));

    const Transform& transform = registry.GetComponent<Transform>(entity);
    EXPECT_TRUE(RepresentSameRotation(transform.rotation, Quat::FromEulerDegrees(45.0f, -30.0f, 0.0f)));

    const Name& name = registry.GetComponent<Name>(entity);
    EXPECT_EQ(name.value, "Directional Light");

    const DirectionalLight& light = registry.GetComponent<DirectionalLight>(entity);
    EXPECT_EQ(light.color, Vec3::One());
    EXPECT_FLOAT_EQ(light.illuminanceLux, 100000.0f);
    EXPECT_TRUE(light.active);
}

} // namespace
} // namespace gte
