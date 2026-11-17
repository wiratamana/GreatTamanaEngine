// Checks the test that tells the Hi-Z demo its scene was replaced by another one.

#include "ECS/Registry.h"
#include "Editor/HizDemoSceneOpener.h"

#include <gtest/gtest.h>

namespace gte {
namespace {

HizDemoState ActiveDemo(std::vector<Entity> created)
{
    HizDemoState state;
    state.active = true;
    state.created = std::move(created);
    return state;
}

TEST(HizDemoSceneReplacedTest, AnInactiveDemoIsNeverReplaced)
{
    Registry registry;
    EXPECT_FALSE(HizDemoSceneReplaced(registry, HizDemoState{}));
}

TEST(HizDemoSceneReplacedTest, ALiveEntityKeepsTheDemoAlive)
{
    Registry registry;
    const Entity camera = registry.CreateEntity();
    const Entity wall = registry.CreateEntity();
    registry.DestroyEntity(wall);
    EXPECT_FALSE(HizDemoSceneReplaced(registry, ActiveDemo({ camera, wall })));
}

TEST(HizDemoSceneReplacedTest, WhenEveryEntityIsGoneTheSceneWasReplaced)
{
    Registry registry;
    const Entity camera = registry.CreateEntity();
    const Entity wall = registry.CreateEntity();
    registry.DestroyEntity(camera);
    registry.DestroyEntity(wall);
    EXPECT_TRUE(HizDemoSceneReplaced(registry, ActiveDemo({ camera, wall })));
}

TEST(HizDemoSceneReplacedTest, AnActiveDemoWithoutEntitiesIsNotJudged)
{
    Registry registry;
    EXPECT_FALSE(HizDemoSceneReplaced(registry, ActiveDemo({})));
}

} // namespace
} // namespace gte
