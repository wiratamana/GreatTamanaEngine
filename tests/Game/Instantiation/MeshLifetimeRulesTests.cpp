#include "Game/Instantiation/MeshLifetimeRules.h"

#include "ECS/Components/MeshRenderer.h"
#include "ECS/Registry.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <vector>

namespace gte {
namespace {

MeshHandle HandleOf(std::uint32_t index) { return MeshHandle{ index, 1 }; }

bool Contains(const std::vector<MeshHandle>& handles, MeshHandle handle)
{
    return std::find(handles.begin(), handles.end(), handle) != handles.end();
}

TEST(MeshLifetimeRules, EmptyRegistryHasNoMeshesInUse)
{
    Registry registry;
    EXPECT_TRUE(CollectMeshesInUse(registry).empty());
}

TEST(MeshLifetimeRules, CollectReturnsValidHandlesOnly)
{
    Registry registry;
    registry.AddComponent<MeshRenderer>(registry.CreateEntity(), MeshRenderer{ HandleOf(4), {}, {} });
    registry.AddComponent<MeshRenderer>(registry.CreateEntity(), MeshRenderer{});

    const MeshHandleKeySet inUse = CollectMeshesInUse(registry);
    ASSERT_EQ(inUse.size(), 1u);
    EXPECT_TRUE(inUse.contains(MeshHandleKey(HandleOf(4))));
}

TEST(MeshLifetimeRules, UnreferencedOwnedMeshIsReleased)
{
    const std::vector<MeshHandle> owned = { HandleOf(1), HandleOf(2) };
    const MeshSweepPlan plan = PlanMeshSweep(owned, {}, {}, {});
    EXPECT_EQ(plan.release.size(), 2u);
    EXPECT_FALSE(plan.stillPending);
}

TEST(MeshLifetimeRules, CachedPartIsKept)
{
    const std::vector<MeshHandle> owned = { HandleOf(1), HandleOf(2) };
    const std::vector<MeshHandle> cached = { HandleOf(1) };
    const MeshSweepPlan plan = PlanMeshSweep(owned, cached, {}, {});
    ASSERT_EQ(plan.release.size(), 1u);
    EXPECT_EQ(plan.release[0], HandleOf(2));
}

TEST(MeshLifetimeRules, EntityHeldMeshIsKeptAndKeepsSweepPending)
{
    const std::vector<MeshHandle> owned = { HandleOf(1), HandleOf(2) };
    const MeshHandleKeySet inUse = { MeshHandleKey(HandleOf(2)) };
    const MeshSweepPlan plan = PlanMeshSweep(owned, {}, inUse, {});
    ASSERT_EQ(plan.release.size(), 1u);
    EXPECT_EQ(plan.release[0], HandleOf(1));
    EXPECT_TRUE(plan.stillPending);
}

TEST(MeshLifetimeRules, SkinningBindingKeepsMeshWithoutPending)
{
    const std::vector<MeshHandle> owned = { HandleOf(1), HandleOf(2) };
    const std::vector<MeshHandle> bindings = { HandleOf(1) };
    const MeshSweepPlan plan = PlanMeshSweep(owned, {}, {}, bindings);
    EXPECT_FALSE(Contains(plan.release, HandleOf(1)));
    EXPECT_TRUE(Contains(plan.release, HandleOf(2)));
    EXPECT_FALSE(plan.stillPending);
}

TEST(MeshLifetimeRules, NotOwnedMeshesAreNeverReleased)
{
    const std::vector<MeshHandle> owned = { HandleOf(1) };
    const std::vector<MeshHandle> cached = { HandleOf(9) };
    const MeshSweepPlan plan = PlanMeshSweep(owned, cached, {}, {});
    EXPECT_FALSE(Contains(plan.release, HandleOf(9)));
    EXPECT_TRUE(Contains(plan.release, HandleOf(1)));
}

TEST(MeshLifetimeRules, GenerationIsPartOfTheKey)
{
    const std::vector<MeshHandle> owned = { MeshHandle{ 1, 2 } };
    const std::vector<MeshHandle> cached = { MeshHandle{ 1, 1 } };
    EXPECT_EQ(PlanMeshSweep(owned, cached, {}, {}).release.size(), 1u);
}

} // namespace
} // namespace gte
