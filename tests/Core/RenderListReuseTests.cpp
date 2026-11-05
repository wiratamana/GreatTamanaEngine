// Tier-1 tests for reusing the frame render list and the candidate list when nothing they read has changed.

#include "Core/ClusterCandidates.h"
#include "ECS/Components/MeshRenderer.h"
#include "ECS/Components/Transform.h"
#include "ECS/Registry.h"
#include "Game/RenderSystem.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <cstring>
#include <limits>
#include <vector>

namespace gte {
namespace {

struct World {
    Registry registry;
    RenderSystem system;
    std::vector<Entity> entities;

    explicit World(int count)
    {
        for (int i = 0; i < count; ++i) {
            const Entity e = registry.CreateEntity();
            registry.AddComponent<Transform>(e).position = Vec3(static_cast<float>(i), 1.0f, 2.0f);
            MeshRenderer& renderer = registry.AddComponent<MeshRenderer>(e);
            renderer.mesh = MeshHandle{ 1, 1 };
            renderer.pipeline = PipelineHandle{ 1, 1 };
            entities.push_back(e);
        }
    }

    // Builds the frame list; returns its epoch and checks it equals a fresh collect.
    std::uint64_t Frame()
    {
        RenderSystem::SharedListScope scope(system);
        const std::vector<DrawCommand>& shared = scope.Build(registry);
        const std::vector<DrawCommand> fresh = RenderSystem::CollectRenderables(registry);
        EXPECT_EQ(shared.size(), fresh.size());
        if (shared.size() == fresh.size()) {
            for (std::size_t i = 0; i < fresh.size(); ++i) {
                EXPECT_EQ(shared[i].entity, fresh[i].entity);
                EXPECT_EQ(shared[i].mesh, fresh[i].mesh);
                EXPECT_EQ(shared[i].pipeline, fresh[i].pipeline);
                EXPECT_EQ(std::memcmp(shared[i].model.Data(), fresh[i].model.Data(), sizeof(float) * 16), 0) << i;
            }
        }
        return scope.Epoch();
    }

    // Two quiet frames so a snapshot exists and has been used once.
    std::uint64_t Settle()
    {
        Frame();
        const std::uint64_t epoch = Frame();
        EXPECT_EQ(Frame(), epoch);
        return epoch;
    }
};

TEST(RenderListReuseTests, UnchangedRenderablesKeepTheEpoch)
{
    World world(50);
    const std::uint64_t epoch = world.Settle();
    for (int i = 0; i < 10; ++i) {
        EXPECT_EQ(world.Frame(), epoch);
    }
}

TEST(RenderListReuseTests, ATransformThatIsNotARenderableTransformDoesNotMatter)
{
    World world(5);
    const std::uint64_t epoch = world.Settle();
    const Entity camera = world.registry.CreateEntity();
    world.registry.AddComponent<Transform>(camera).position = Vec3(9.0f, 9.0f, 9.0f);
    world.registry.TryGetComponent<Transform>(camera)->position = Vec3(1.0f, 2.0f, 3.0f);
    EXPECT_EQ(world.Frame(), epoch);
}

TEST(RenderListReuseTests, EveryInputOfTheListBumpsTheEpoch)
{
    const auto expectBump = [](const char* what, auto&& change) {
        World world(40);
        const std::uint64_t epoch = world.Settle();
        change(world);
        EXPECT_GT(world.Frame(), epoch) << what;
    };
    expectBump("position", [](World& w) { w.registry.TryGetComponent<Transform>(w.entities[17])->position.x += 0.5f; });
    expectBump("last entity position", [](World& w) { w.registry.TryGetComponent<Transform>(w.entities[39])->position.z = 7.0f; });
    expectBump("rotation", [](World& w) {
        w.registry.TryGetComponent<Transform>(w.entities[3])->rotation = Quat::FromEulerDegrees(0.0f, 30.0f, 0.0f);
    });
    expectBump("scale", [](World& w) { w.registry.TryGetComponent<Transform>(w.entities[0])->scale = Vec3(2.0f, 2.0f, 2.0f); });
    expectBump("mesh", [](World& w) { w.registry.TryGetComponent<MeshRenderer>(w.entities[8])->mesh = MeshHandle{ 2, 1 }; });
    expectBump("mesh generation", [](World& w) { w.registry.TryGetComponent<MeshRenderer>(w.entities[8])->mesh.generation = 2; });
    expectBump("pipeline", [](World& w) { w.registry.TryGetComponent<MeshRenderer>(w.entities[9])->pipeline = PipelineHandle{ 2, 1 }; });
    expectBump("texture", [](World& w) {
        w.registry.TryGetComponent<MeshRenderer>(w.entities[10])->texture = MaterialTextureHandle{ 3, 1 };
    });
    expectBump("renderer added", [](World& w) {
        const Entity e = w.registry.CreateEntity();
        w.registry.AddComponent<Transform>(e);
        w.registry.AddComponent<MeshRenderer>(e);
    });
    expectBump("renderer removed", [](World& w) { w.registry.DestroyEntity(w.entities[12]); });
    expectBump("renderer component removed", [](World& w) { w.registry.RemoveComponent<MeshRenderer>(w.entities[12]); });
    expectBump("transform removed", [](World& w) { w.registry.RemoveComponent<Transform>(w.entities[20]); });
}

TEST(RenderListReuseTests, ANaNTransformDoesNotLoopForever)
{
    World world(3);
    world.registry.TryGetComponent<Transform>(world.entities[1])->position.x = std::numeric_limits<float>::quiet_NaN();
    const std::uint64_t epoch = world.Settle(); // Bitwise equal to itself: reusable.
    EXPECT_EQ(world.Frame(), epoch);
}

TEST(RenderListReuseTests, ParentedRenderablesAreAlwaysRebuilt)
{
    World world(6);
    const Entity parent = world.registry.CreateEntity();
    world.registry.AddComponent<Transform>(parent);
    world.registry.TryGetComponent<Transform>(world.entities[2])->parent = parent;
    std::uint64_t last = world.Frame();
    for (int i = 0; i < 6; ++i) {
        // Only the parent moves; the child's own Transform bytes stay equal, yet its world matrix changes.
        world.registry.TryGetComponent<Transform>(parent)->position.x = static_cast<float>(i);
        const std::uint64_t epoch = world.Frame(); // Frame() checks the list against a fresh collect.
        EXPECT_GT(epoch, last);
        last = epoch;
    }
}

TEST(RenderListReuseTests, AnAnimatedListStaysCorrectAndBacksOff)
{
    World world(30);
    std::uint64_t last = world.Frame();
    for (int i = 0; i < 100; ++i) {
        world.registry.TryGetComponent<Transform>(world.entities[i % 30])->position.y += 0.25f;
        const std::uint64_t epoch = world.Frame();
        EXPECT_GT(epoch, last) << i;
        last = epoch;
    }
    // Once it stops moving it is reused again after the back-off ends.
    std::uint64_t epoch = 0;
    for (int i = 0; i < 80; ++i) {
        epoch = world.Frame();
    }
    EXPECT_EQ(world.Frame(), epoch);
}

TEST(RenderListReuseTests, TwoRegistriesNeverShareASnapshot)
{
    World a(4);
    World b(4);
    a.Settle();
    RenderSystem::SharedListScope scope(a.system);
    const std::vector<DrawCommand>& other = scope.Build(b.registry);
    EXPECT_EQ(other.size(), 4u);
    EXPECT_EQ(other[0].entity, b.entities[0]);
}

ClusterCandidatePair Pair(std::uint32_t mesh)
{
    ClusterCandidatePair pair;
    pair.mesh = MeshHandle{ mesh, 1 };
    pair.pipeline = PipelineHandle{ 1, 1 };
    pair.resolved = true;
    pair.facts.meshHasClusters = true;
    pair.facts.meshHasIndexBuffer = true;
    pair.facts.layout = VertexLayout::PositionNormal;
    pair.clusterBounds = AABB{ Vec3(-1.0f, -1.0f, 0.0f), Vec3(1.0f, 1.0f, 0.0f) };
    return pair;
}

TEST(RenderListReuseTests, ACandidatePairComparesEveryFact)
{
    const ClusterCandidatePair base = Pair(1);
    EXPECT_TRUE(base.SameAs(Pair(1)));
    EXPECT_FALSE(base.SameAs(Pair(2)));

    const auto differs = [&](auto&& edit) {
        ClusterCandidatePair other = Pair(1);
        edit(other);
        return !base.SameAs(other);
    };
    EXPECT_TRUE(differs([](ClusterCandidatePair& p) { p.resolved = false; }));
    EXPECT_TRUE(differs([](ClusterCandidatePair& p) { p.facts.meshHasClusters = false; }));
    EXPECT_TRUE(differs([](ClusterCandidatePair& p) { p.facts.meshSynthesizable = true; }));
    EXPECT_TRUE(differs([](ClusterCandidatePair& p) { p.facts.meshHasIndexBuffer = false; }));
    EXPECT_TRUE(differs([](ClusterCandidatePair& p) { p.facts.layout = VertexLayout::PositionNormalUv; }));
    EXPECT_TRUE(differs([](ClusterCandidatePair& p) { p.facts.gpuSkinned = true; }));
    EXPECT_TRUE(differs([](ClusterCandidatePair& p) { p.clusterBounds.reset(); }));
    EXPECT_TRUE(differs([](ClusterCandidatePair& p) { p.clusterBounds->max.x = 1.5f; }));
}

TEST(RenderListReuseTests, TheReuseTracksEachPairOnceAndStopsAtTheLimit)
{
    ClusterCandidateReuse reuse;
    EXPECT_TRUE(reuse.Note(Pair(1)));
    EXPECT_TRUE(reuse.Note(Pair(1)));
    EXPECT_EQ(reuse.pairs.size(), 1u);
    for (std::uint32_t mesh = 2; reuse.pairs.size() < ClusterCandidateReuse::kMaxPairs; ++mesh) {
        EXPECT_TRUE(reuse.Note(Pair(mesh)));
    }
    EXPECT_FALSE(reuse.Note(Pair(1000)));
    EXPECT_TRUE(reuse.Note(Pair(1))); // A known pair still passes.
}

} // namespace
} // namespace gte
