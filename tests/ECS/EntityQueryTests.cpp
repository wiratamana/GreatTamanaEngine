// Unit tests for EntityQuery (src/ECS/EntityQuery.h) - entity-by-name lookup
// plus Unity-style unique-naming, pure Registry logic with no GPU/Renderer/
// networking dependency of any kind (see AGENTS.md, "Testability &
// Regression Safety").

#include "ECS/Components/Name.h"
#include "ECS/EntityQuery.h"
#include "ECS/Registry.h"

#include <gtest/gtest.h>

namespace gte {
namespace {

TEST(EntityQueryTest, FindEntityByNameFindsTheRightEntityAmongSeveral)
{
    Registry registry;
    const Entity a = registry.CreateEntity();
    const Entity b = registry.CreateEntity();
    const Entity c = registry.CreateEntity();
    registry.AddComponent<Name>(a).value = "Alpha";
    registry.AddComponent<Name>(b).value = "Bravo";
    registry.AddComponent<Name>(c).value = "Charlie";

    EXPECT_EQ(FindEntityByName(registry, "Bravo"), b);
    EXPECT_EQ(FindEntityByName(registry, "Alpha"), a);
    EXPECT_EQ(FindEntityByName(registry, "Charlie"), c);
}

TEST(EntityQueryTest, FindEntityByNameReturnsInvalidForAnUnusedName)
{
    Registry registry;
    const Entity a = registry.CreateEntity();
    registry.AddComponent<Name>(a).value = "Alpha";

    EXPECT_EQ(FindEntityByName(registry, "DoesNotExist"), kInvalidEntity);
}

TEST(EntityQueryTest, FindEntityByNameReturnsInvalidForEmptyQueryEvenIfSomeEntityHasEmptyName)
{
    Registry registry;
    const Entity a = registry.CreateEntity();
    registry.AddComponent<Name>(a).value = ""; // Deliberately empty Name::value.

    // An empty query must never match, even though a live entity happens to
    // have an empty Name::value - see EntityQuery.h's own doc comment.
    EXPECT_EQ(FindEntityByName(registry, ""), kInvalidEntity);
}

TEST(EntityQueryTest, FindEntityByNameReturnsInvalidWhenNoEntityHasAnyNameAtAll)
{
    Registry registry;
    registry.CreateEntity();
    registry.CreateEntity();

    EXPECT_EQ(FindEntityByName(registry, "Anything"), kInvalidEntity);
}

TEST(EntityQueryTest, IsEntityNameInUseMirrorsFindEntityByName)
{
    Registry registry;
    const Entity a = registry.CreateEntity();
    registry.AddComponent<Name>(a).value = "Alpha";

    EXPECT_TRUE(IsEntityNameInUse(registry, "Alpha"));
    EXPECT_FALSE(IsEntityNameInUse(registry, "Beta"));
    EXPECT_FALSE(IsEntityNameInUse(registry, ""));
}

TEST(EntityQueryTest, MakeUniqueEntityNameReturnsBaseNameVerbatimWhenUnused)
{
    Registry registry;
    const Entity a = registry.CreateEntity();
    registry.AddComponent<Name>(a).value = "SomethingElse";

    EXPECT_EQ(MakeUniqueEntityName(registry, "Cube"), "Cube");
}

TEST(EntityQueryTest, MakeUniqueEntityNameReturnsSuffixedNameWhenBaseIsTaken)
{
    Registry registry;
    const Entity a = registry.CreateEntity();
    registry.AddComponent<Name>(a).value = "Cube";

    EXPECT_EQ(MakeUniqueEntityName(registry, "Cube"), "Cube (1)");
}

TEST(EntityQueryTest, MakeUniqueEntityNameProbesInOrderWhenSeveralSuffixesAreTaken)
{
    Registry registry;
    const Entity a = registry.CreateEntity();
    const Entity b = registry.CreateEntity();
    registry.AddComponent<Name>(a).value = "Cube";
    registry.AddComponent<Name>(b).value = "Cube (1)";

    EXPECT_EQ(MakeUniqueEntityName(registry, "Cube"), "Cube (2)");
}

TEST(EntityQueryTest, MakeUniqueEntityNameDoesNotTryToBeCleverAboutAnAlreadySuffixedBaseName)
{
    Registry registry;
    const Entity a = registry.CreateEntity();
    registry.AddComponent<Name>(a).value = "Thing (1)";

    // Deliberate, documented simplicity: no attempt to detect/increment an
    // existing "(N)" suffix - probes onward from "Thing (1) (1)" instead.
    EXPECT_EQ(MakeUniqueEntityName(registry, "Thing (1)"), "Thing (1) (1)");
}

} // namespace
} // namespace gte
