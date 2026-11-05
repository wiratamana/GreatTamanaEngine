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

Entity MakeNamed(Registry& registry, const std::string& name)
{
    const Entity entity = registry.CreateEntity();
    registry.AddComponent<Name>(entity).value = name;
    return entity;
}

TEST(EntityQueryTest, IgnoredEntityDoesNotCollide)
{
    Registry registry;
    const Entity a = MakeNamed(registry, "Cube");

    EXPECT_EQ(MakeUniqueEntityName(registry, "Cube", a), "Cube");

    MakeNamed(registry, "Cube");
    EXPECT_EQ(MakeUniqueEntityName(registry, "Cube", a), "Cube (1)");
}

TEST(EntityQueryTest, FirstFreeSuffixIsUsedEvenWithAGap)
{
    Registry registry;
    MakeNamed(registry, "X");
    MakeNamed(registry, "X (2)");

    EXPECT_EQ(MakeUniqueEntityName(registry, "X"), "X (1)");
}

TEST(EntityQueryTest, MakeUniqueEntityNameIsCaseSensitive)
{
    Registry registry;
    MakeNamed(registry, "Cube");

    EXPECT_EQ(MakeUniqueEntityName(registry, "cube"), "cube");
}

TEST(EntityQueryTest, NameOfAnEntityWithEmptyNameValueNeverCollides)
{
    Registry registry;
    MakeNamed(registry, "");

    EXPECT_EQ(MakeUniqueEntityName(registry, "Cube"), "Cube");
}

TEST(EntityQueryTest, AssignUniqueEntityNameKeepsStemWhenFirst)
{
    Registry registry;
    const Entity root = MakeNamed(registry, "Furina");

    const AssignedEntityName assigned = AssignUniqueEntityName(registry, root, "");

    EXPECT_EQ(assigned.name, "Furina");
    EXPECT_FALSE(assigned.adjusted);
    EXPECT_EQ(registry.TryGetComponent<Name>(root)->value, "Furina");
}

TEST(EntityQueryTest, AssignUniqueEntityNameAddsSuffixForSecondInstance)
{
    Registry registry;

    // Spawn-then-assign, one instance at a time, like the real spawn path.
    const Entity first = MakeNamed(registry, "Furina");
    EXPECT_EQ(AssignUniqueEntityName(registry, first, "").name, "Furina");

    const Entity second = MakeNamed(registry, "Furina");
    const AssignedEntityName secondName = AssignUniqueEntityName(registry, second, "");
    EXPECT_EQ(secondName.name, "Furina (1)");
    EXPECT_TRUE(secondName.adjusted);

    const Entity third = MakeNamed(registry, "Furina");
    const AssignedEntityName thirdName = AssignUniqueEntityName(registry, third, "");
    EXPECT_EQ(thirdName.name, "Furina (2)");
    EXPECT_TRUE(thirdName.adjusted);
    EXPECT_EQ(registry.TryGetComponent<Name>(third)->value, "Furina (2)");
}

TEST(EntityQueryTest, AssignUniqueEntityNameUsesRequestedBase)
{
    Registry registry;
    const Entity a = MakeNamed(registry, "Furina");
    const Entity b = MakeNamed(registry, "Furina");

    const AssignedEntityName first = AssignUniqueEntityName(registry, a, "Hero");
    EXPECT_EQ(first.name, "Hero");
    EXPECT_FALSE(first.adjusted);

    const AssignedEntityName second = AssignUniqueEntityName(registry, b, "Hero");
    EXPECT_EQ(second.name, "Hero (1)");
    EXPECT_TRUE(second.adjusted);
}

TEST(EntityQueryTest, AssignUniqueEntityNameCollidesWithChildPartNames)
{
    Registry registry;
    MakeNamed(registry, "Body");
    const Entity root = MakeNamed(registry, "Furina");

    EXPECT_EQ(AssignUniqueEntityName(registry, root, "Body").name, "Body (1)");
}

TEST(EntityQueryTest, AssignUniqueEntityNameCreatesNameComponentWhenMissing)
{
    Registry registry;
    const Entity entity = registry.CreateEntity();

    const AssignedEntityName assigned = AssignUniqueEntityName(registry, entity, "");

    EXPECT_EQ(assigned.name, "Entity");
    EXPECT_FALSE(assigned.adjusted);
    ASSERT_NE(registry.TryGetComponent<Name>(entity), nullptr);
    EXPECT_EQ(registry.TryGetComponent<Name>(entity)->value, "Entity");
}

TEST(EntityQueryTest, AssignUniqueEntityNameOnDeadEntityChangesNothing)
{
    Registry registry;
    const Entity dead = MakeNamed(registry, "Gone");
    registry.DestroyEntity(dead);

    const AssignedEntityName assigned = AssignUniqueEntityName(registry, dead, "Hero");

    EXPECT_TRUE(assigned.name.empty());
    EXPECT_FALSE(assigned.adjusted);
    EXPECT_FALSE(IsEntityNameInUse(registry, "Hero"));
}

TEST(EntityQueryTest, RenamingToItsOwnCurrentNameIsNotAdjusted)
{
    Registry registry;
    const Entity entity = MakeNamed(registry, "Hero");

    const AssignedEntityName assigned = AssignUniqueEntityName(registry, entity, "Hero");

    EXPECT_EQ(assigned.name, "Hero");
    EXPECT_FALSE(assigned.adjusted);
}

} // namespace
} // namespace gte
