// Tier-1 tests for the per-frame CPU work in front of the cluster cull: closed-form TRS, finite check,
// streaming candidate builder, history hasher, single-group fast path and the shared render list.

#include "Core/ClusterCandidates.h"
#include "Core/FeatureKit/ClusterPartPlanning.h"
#include "ECS/Components/MeshRenderer.h"
#include "ECS/Components/Transform.h"
#include "ECS/Registry.h"
#include "Game/RenderSystem.h"
#include "Math/Mat4.h"
#include "Renderer/Culling/ClusterCullMath.h"

#include <gtest/gtest.h>

#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <random>
#include <vector>

namespace gte {
namespace {

bool SameElements(const Mat4& a, const Mat4& b)
{
    for (int i = 0; i < 16; ++i) {
        if (!(a.Data()[i] == b.Data()[i])) { // -0 equals +0 on purpose
            return false;
        }
    }
    return true;
}

TEST(CpuCullFloorTests, ClosedFormTrsEqualsTheProductChain)
{
    std::mt19937 rng(7);
    std::uniform_real_distribution<float> pos(-500.0f, 500.0f);
    std::uniform_real_distribution<float> angle(-3.0f, 3.0f);
    std::uniform_real_distribution<float> scale(-4.0f, 4.0f);
    for (int i = 0; i < 2000; ++i) {
        const Vec3 p(pos(rng), pos(rng), pos(rng));
        const Quat q = Quat::FromEulerDegrees(angle(rng) * 50.0f, angle(rng) * 50.0f, angle(rng) * 50.0f);
        const Vec3 s(scale(rng), scale(rng), scale(rng));
        const Mat4 chain = Mat4::Translation(p) * Mat4::FromQuat(q) * Mat4::Scaling(s);
        EXPECT_TRUE(SameElements(Mat4::TRS(p, q, s), chain)) << "case " << i;
    }
}

TEST(CpuCullFloorTests, IsFiniteMatrixCatchesEveryBadEntry)
{
    EXPECT_TRUE(IsFiniteMatrix(Mat4::Identity()));
    const float bad[] = { std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity(),
        -std::numeric_limits<float>::infinity() };
    for (int i = 0; i < 16; ++i) {
        for (const float value : bad) {
            Mat4 m = Mat4::Identity();
            m(i % 4, i / 4) = value;
            EXPECT_FALSE(IsFiniteMatrix(m)) << "entry " << i;
        }
        Mat4 big = Mat4::Identity();
        big(i % 4, i / 4) = std::numeric_limits<float>::max();
        EXPECT_TRUE(IsFiniteMatrix(big));
        Mat4 tiny = Mat4::Identity();
        tiny(i % 4, i / 4) = std::numeric_limits<float>::denorm_min();
        EXPECT_TRUE(IsFiniteMatrix(tiny));
    }
}

Entity MakeEntity(std::uint32_t index)
{
    Entity e;
    e.index = index;
    e.generation = 1;
    return e;
}

ClusterCandidateInput Input(std::uint32_t entity, std::uint32_t mesh, bool hasClusters)
{
    ClusterCandidateInput in;
    in.entity = MakeEntity(entity);
    in.mesh = fx::MeshToken{ mesh, 1 };
    in.world = Mat4::Identity();
    in.meshHasClusters = hasClusters;
    in.meshSynthesizable = true;
    in.meshHasIndexBuffer = true;
    in.layout = VertexLayout::PositionNormal;
    return in;
}

TEST(CpuCullFloorTests, BuilderReusesTheOutputAndDropsFewSynthesizedInstancesInOrder)
{
    // Mesh 1 has a table; mesh 2 is synthesized and has 2 instances (too few); mesh 3 is synthesized with 4.
    std::vector<ClusterCandidateInput> inputs;
    inputs.push_back(Input(1, 1, true));
    inputs.push_back(Input(2, 2, false));
    inputs.push_back(Input(3, 3, false));
    inputs.push_back(Input(4, 2, false));
    for (std::uint32_t i = 5; i < 8; ++i) {
        inputs.push_back(Input(i, 3, false));
    }
    inputs.push_back(Input(8, 1, true));

    fx::ClusterRejectCounts expectedCounts;
    const std::vector<fx::ClusterInstance> expected = SelectClusterCandidates(inputs, true, &expectedCounts);

    std::vector<fx::ClusterInstance> out(3); // Old content must be cleared.
    for (int round = 0; round < 2; ++round) {
        fx::ClusterRejectCounts counts;
        ClusterCandidateBuilder builder(out, inputs.size());
        for (const ClusterCandidateInput& in : inputs) {
            builder.Add(ClusterCandidateFacts{ in.meshHasClusters, in.meshSynthesizable, in.meshHasIndexBuffer, in.layout,
                           in.gpuSkinned },
                in.entity, in.mesh, in.material, in.world);
        }
        builder.Finish(&counts);
        ASSERT_EQ(out.size(), expected.size());
        for (std::size_t i = 0; i < out.size(); ++i) {
            EXPECT_EQ(out[i].entity, expected[i].entity);
        }
        EXPECT_EQ(counts.tooFewInstances, expectedCounts.tooFewInstances);
    }
    EXPECT_EQ(expected.size(), 6u); // 2 with a table + 4 of mesh 3
    EXPECT_EQ(expectedCounts.tooFewInstances, 2u);
}

TEST(CpuCullFloorTests, HistoryHasherEqualsTheArrayForm)
{
    const std::vector<std::uint32_t> ids{ 5, 1, 9, 1, 12, 3 };
    ClusterHistoryHasher hasher(77, 22, 3, ids.size());
    for (const std::uint32_t id : ids) {
        hasher.Add(id);
    }
    EXPECT_EQ(hasher.Finish(), ClusterHistoryKey(77, 22, ids, 3));
}

TEST(CpuCullFloorTests, SingleMeshAndMaterialGivesOneGroupInOrder)
{
    std::vector<fx::ClusterInstance> candidates(1000);
    for (std::uint32_t i = 0; i < candidates.size(); ++i) {
        candidates[i].entity = MakeEntity(i);
        candidates[i].mesh = fx::MeshToken{ 4, 1 };
        candidates[i].kind = ClusterMaterialKind::None;
    }
    const std::vector<fx::ClusterMeshGroup> groups = fx::GroupInstances(candidates, true);
    ASSERT_EQ(groups.size(), 1u);
    ASSERT_EQ(groups[0].instances.size(), candidates.size());
    for (std::uint32_t i = 0; i < candidates.size(); ++i) {
        ASSERT_EQ(groups[0].instances[i], i);
    }

    candidates.back().mesh = fx::MeshToken{ 5, 1 }; // One odd instance takes the general path.
    const std::vector<fx::ClusterMeshGroup> two = fx::GroupInstances(candidates, true);
    ASSERT_EQ(two.size(), 2u);
    EXPECT_EQ(two[0].instances.size(), candidates.size() - 1);
    EXPECT_EQ(two[1].instances.size(), 1u);
    EXPECT_TRUE(fx::GroupInstances({}, true).empty());
}

TEST(CpuCullFloorTests, SharedRenderListEqualsAFreshCollect)
{
    Registry registry;
    for (int i = 0; i < 5; ++i) {
        const Entity e = registry.CreateEntity();
        registry.AddComponent<Transform>(e).position = Vec3(static_cast<float>(i), 0.0f, 0.0f);
        registry.AddComponent<MeshRenderer>(e);
    }
    RenderSystem system;
    const std::vector<DrawCommand> fresh = RenderSystem::CollectRenderables(registry);
    {
        RenderSystem::SharedListScope scope(system);
        const std::vector<DrawCommand>& shared = scope.Build(registry);
        ASSERT_EQ(shared.size(), fresh.size());
        for (std::size_t i = 0; i < fresh.size(); ++i) {
            EXPECT_EQ(shared[i].entity, fresh[i].entity);
            EXPECT_TRUE(SameElements(shared[i].model, fresh[i].model));
        }
    }
}

} // namespace
} // namespace gte
