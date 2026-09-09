// Unit tests for Game::DeleteEntityByName() (src/Game/Game.h/.cpp,
// network-impl-3 campaign) - the one half of Phase 3's two new engine-command
// methods that needs no live Renderer/GPU device at all (unlike
// InstantiatePrimitive(), which touches CreatePrimitiveEntity() ->
// MeshInstantiationSystem -> PrimitiveGpuCatalog and stays in this codebase's
// accepted "Tier 2, no automated coverage yet" bucket - see AGENTS.md,
// "Testability & Regression Safety", and this campaign's own
// PHASE3_COMPLETION_REPORT.md/PHASE6 strategy document). Game() itself never
// requires a Renderer to construct, so this whole file is genuinely
// Tier-1-testable: it constructs a real Game, reaches into its ECS world via
// GetRegistry() to hand-build entities/Name components/a parent-child
// relationship, then exercises DeleteEntityByName() directly.

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

} // namespace
} // namespace gte
