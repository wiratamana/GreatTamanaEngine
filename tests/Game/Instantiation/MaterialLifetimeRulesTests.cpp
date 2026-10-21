#include "Game/Instantiation/MaterialLifetimeRules.h"

#include "ECS/Components/MeshRenderer.h"
#include "ECS/Components/Transform.h"
#include "ECS/Registry.h"

#include <gtest/gtest.h>

#include <vector>

namespace gte {
namespace {

MaterialTextureHandle Handle(std::uint32_t index) { return MaterialTextureHandle{ index, 1 }; }

TEST(MaterialLifetimeRules, CanDrawWithMaterialCoversAllFourCombinations)
{
    EXPECT_TRUE(CanDrawWithMaterial(false, false));
    EXPECT_TRUE(CanDrawWithMaterial(false, true));
    EXPECT_FALSE(CanDrawWithMaterial(true, false));
    EXPECT_TRUE(CanDrawWithMaterial(true, true));
}

TEST(MaterialLifetimeRules, CollectReturnsDistinctValidHandlesOnly)
{
    Registry registry;
    const Entity a = registry.CreateEntity();
    const Entity b = registry.CreateEntity();
    const Entity c = registry.CreateEntity();
    const Entity noRenderer = registry.CreateEntity();
    registry.AddComponent<Transform>(noRenderer);

    registry.AddComponent<MeshRenderer>(a, MeshRenderer{ {}, {}, Handle(3) });
    registry.AddComponent<MeshRenderer>(b, MeshRenderer{ {}, {}, Handle(3) }); // Shares the handle with `a`.
    registry.AddComponent<MeshRenderer>(c, MeshRenderer{}); // Invalid handle.

    const std::unordered_set<MaterialTextureHandle> inUse = CollectMaterialTexturesInUse(registry);
    ASSERT_EQ(inUse.size(), 1u);
    EXPECT_TRUE(inUse.contains(Handle(3)));
}

TEST(MaterialLifetimeRules, SelectUnusedKeepsInputOrderAndDropsUsed)
{
    const std::vector<MaterialTextureHandle> cached = { Handle(5), Handle(1), Handle(4), Handle(2) };
    const std::unordered_set<MaterialTextureHandle> inUse = { Handle(1), Handle(2) };

    const std::vector<MaterialTextureHandle> unused = SelectUnusedMaterials(cached, inUse);
    ASSERT_EQ(unused.size(), 2u);
    EXPECT_EQ(unused[0], Handle(5));
    EXPECT_EQ(unused[1], Handle(4));
}

TEST(MaterialLifetimeRules, SweepRunsOnlyWhenPendingOnPeriodFrames)
{
    EXPECT_FALSE(ShouldRunMaterialSweep(false, 0));
    EXPECT_FALSE(ShouldRunMaterialSweep(false, 30));
    EXPECT_TRUE(ShouldRunMaterialSweep(true, 0));
    EXPECT_TRUE(ShouldRunMaterialSweep(true, 30));
    EXPECT_TRUE(ShouldRunMaterialSweep(true, 60));
    EXPECT_FALSE(ShouldRunMaterialSweep(true, 1));
    EXPECT_FALSE(ShouldRunMaterialSweep(true, 31));
}

TEST(MaterialLifetimeRules, SweepPeriodZeroDoesNotDivideByZero)
{
    EXPECT_TRUE(ShouldRunMaterialSweep(true, 7, 0));
    EXPECT_FALSE(ShouldRunMaterialSweep(false, 7, 0));
}

} // namespace
} // namespace gte
