// Tier-1 tests of the chunked cull: history packing, thread counts and the instance box test against the cluster test.

#include "Math/Quat.h"
#include "Renderer/Culling/ClusterCullMath.h"

#include <gtest/gtest.h>

#include <cmath>
#include <cstdint>
#include <limits>
#include <random>
#include <vector>

namespace gte {
namespace {

TEST(ClusterChunk, ThreadCountIsOnePerInstanceAndChunk)
{
    EXPECT_EQ(ClusterChunkCount(0), 0u);
    EXPECT_EQ(ClusterChunkCount(1), 1u);
    EXPECT_EQ(ClusterChunkCount(16), 1u);
    EXPECT_EQ(ClusterChunkCount(17), 2u);
    EXPECT_EQ(ClusterCullThreadCount(50000, 22), 100000u);
    EXPECT_EQ(ClusterCullThreadCount(3, 16), 3u);
    EXPECT_EQ(ClusterCullThreadCount(3, 17), 6u);
    EXPECT_EQ(ClusterCullThreadCount(1, 200000), 12500u);
}

TEST(ClusterChunk, HistorySlotsAreDistinctAndFitTheWorkItemWords)
{
    constexpr std::uint32_t kInstances = 7;
    constexpr std::uint32_t kClusters = 37;
    std::vector<std::uint32_t> seen(ClusterCullThreadCount(kInstances, kClusters), 0u);
    for (std::uint32_t instance = 0; instance < kInstances; ++instance) {
        for (std::uint32_t cluster = 0; cluster < kClusters; ++cluster) {
            const ClusterHistorySlot slot = ClusterHistorySlotOf(instance, cluster, kInstances);
            ASSERT_LT(slot.word, seen.size());
            const std::uint32_t mask = 1u << slot.bit;
            EXPECT_EQ(seen[slot.word] & mask, 0u);
            seen[slot.word] |= mask;
        }
    }
    EXPECT_LE(seen.size(), std::uint64_t{ kInstances } * kClusters);
}

TEST(ClusterChunk, FirstPhaseEqualsThePerItemOracleOnEveryBit)
{
    std::mt19937 rng(7);
    for (int round = 0; round < 20000; ++round) {
        const std::uint32_t word = rng();
        const std::uint32_t inView = rng() & 0xFFFFu;
        const bool valid = (rng() & 1u) != 0;
        const ClusterChunkResult chunk = ClusterChunkPhaseFirst(word, valid, inView);
        for (std::uint32_t i = 0; i < kClusterChunkClusters; ++i) {
            const std::uint32_t itemWord = ((word >> i) & 1u) | (((word >> (16 + i)) & 1u) << 1);
            const ClusterPhaseResult item = ClusterPhaseFirst(itemWord, valid, ((inView >> i) & 1u) != 0);
            ASSERT_EQ(((chunk.emitMask >> i) & 1u) != 0, item.emit);
            ASSERT_EQ((chunk.word >> i) & 1u, item.word & kClusterHistoryVisible);
            ASSERT_EQ(((chunk.word >> (16 + i)) & 1u) != 0, (item.word & kClusterHistoryDrawnFirst) != 0);
        }
        ASSERT_EQ(chunk.emitMask >> 16, 0u);
    }
}

TEST(ClusterChunk, SecondPhaseEqualsThePerItemOracleOnEveryBit)
{
    std::mt19937 rng(11);
    for (int round = 0; round < 20000; ++round) {
        const std::uint32_t word = rng();
        const std::uint32_t visible = rng() & 0xFFFFu;
        const ClusterChunkResult chunk = ClusterChunkPhaseSecond(word, visible);
        for (std::uint32_t i = 0; i < kClusterChunkClusters; ++i) {
            const std::uint32_t itemWord = ((word >> i) & 1u) | (((word >> (16 + i)) & 1u) << 1);
            const ClusterPhaseResult item = ClusterPhaseSecond(itemWord, ((visible >> i) & 1u) != 0, false);
            ASSERT_EQ(((chunk.emitMask >> i) & 1u) != 0, item.emit);
            ASSERT_EQ((chunk.word >> i) & 1u, item.word & kClusterHistoryVisible);
        }
        ASSERT_EQ(chunk.word >> 16, 0u);
    }
}

TEST(ClusterChunk, ParamsCarryTheInstanceBoxOnlyWhenSet)
{
    const Mat4 vp = Mat4::PerspectiveFovLH_ZO(1.0f, 1.0f, 0.1f, 100.0f);
    ClusterCullParams params = MakeClusterCullParams(vp, Vec3(0.0f, 0.0f, 0.0f), false, 4, 5, ClusterCullFlags::Frustum);
    EXPECT_EQ(params.instanceBoxMin[3], 0.0f);
    SetClusterCullInstanceBox(params, AABB{ Vec3(-1.0f, -2.0f, -3.0f), Vec3(4.0f, 5.0f, 6.0f) });
    EXPECT_EQ(params.instanceBoxMin[3], 1.0f);
    EXPECT_EQ(params.instanceBoxMin[1], -2.0f);
    EXPECT_EQ(params.instanceBoxMax[2], 6.0f);
}

// Outside means every cluster the cluster test sees is outside too; Inside means none is. 40,000 random cases.
TEST(ClusterChunk, InstanceBoxClassNeverContradictsTheClusterTest)
{
    std::mt19937 rng(2024);
    const auto uniform = [&rng](float lo, float hi) { return lo + (hi - lo) * (static_cast<float>(rng() & 0xFFFFFF) / 16777216.0f); };
    std::uint32_t outsideCases = 0;
    std::uint32_t insideCases = 0;
    for (int round = 0; round < 40000; ++round) {
        const Mat4 view = Mat4::LookAtLH(Vec3(uniform(-20, 20), uniform(-20, 20), uniform(-20, 20)),
            Vec3(uniform(-5, 5), uniform(-5, 5), uniform(-5, 5)) + Vec3(0.0f, 0.0f, 1.0f), Vec3::Up());
        const Mat4 vp = Mat4::PerspectiveFovLH_ZO(uniform(0.4f, 1.8f), uniform(0.7f, 2.0f), 0.1f, uniform(50, 500)) * view;
        const auto planes = TryExtractFrustumPlanes(vp);
        if (!planes.has_value()) {
            continue;
        }
        const Mat4 world = Mat4::TRS(Vec3(uniform(-40, 40), uniform(-40, 40), uniform(-40, 40)),
            Quat::FromEulerDegrees(uniform(-180, 180), uniform(-180, 180), uniform(-180, 180)),
            Vec3(uniform(0.1f, 4), uniform(0.1f, 4), uniform(0.1f, 4)));
        std::vector<MeshCluster> clusters(1 + rng() % 24);
        for (MeshCluster& cluster : clusters) {
            for (int axis = 0; axis < 3; ++axis) {
                const float lo = uniform(-2, 2);
                cluster.aabbMin[axis] = lo;
                cluster.aabbMax[axis] = lo + uniform(0.0f, 1.5f);
            }
        }
        const std::optional<AABB> box = ClusterTableBounds(clusters);
        ASSERT_TRUE(box.has_value());
        const ClusterInstanceClass kind = ClusterInstanceFrustumClass(*box, world, *planes);
        outsideCases += kind == ClusterInstanceClass::Outside ? 1 : 0;
        insideCases += kind == ClusterInstanceClass::Inside ? 1 : 0;
        for (const MeshCluster& cluster : clusters) {
            const bool culled = ClusterFrustumCulled(cluster, world, *planes);
            if (kind == ClusterInstanceClass::Outside) {
                ASSERT_TRUE(culled) << "round " << round;
            }
            if (kind == ClusterInstanceClass::Inside) {
                ASSERT_FALSE(culled) << "round " << round;
            }
        }
    }
    EXPECT_GT(outsideCases, 2000u);
    EXPECT_GT(insideCases, 500u);
}

TEST(ClusterChunk, NonFiniteInstanceBoxIsPartial)
{
    const auto planes = TryExtractFrustumPlanes(Mat4::PerspectiveFovLH_ZO(1.0f, 1.0f, 0.1f, 100.0f));
    ASSERT_TRUE(planes.has_value());
    Mat4 world = Mat4::Identity();
    world(0, 3) = std::numeric_limits<float>::quiet_NaN();
    EXPECT_EQ(ClusterInstanceFrustumClass(AABB{ Vec3(-1, -1, -1), Vec3(1, 1, 1) }, world, *planes), ClusterInstanceClass::Partial);
}

} // namespace
} // namespace gte
