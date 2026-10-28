#include "Game/Instantiation/MeshClusterTableSelection.h"

#include <gtest/gtest.h>

#include <string>
#include <vector>

namespace gte {
namespace {

MeshCluster MakeCluster(std::uint32_t firstIndex, std::uint32_t indexCount)
{
    MeshCluster cluster{};
    cluster.sphere[3] = 1.0f;
    cluster.cone[3] = 1.0f;
    cluster.aabbMin[0] = cluster.aabbMin[1] = cluster.aabbMin[2] = -1.0f;
    cluster.aabbMax[0] = cluster.aabbMax[1] = cluster.aabbMax[2] = 1.0f;
    cluster.firstIndex = firstIndex;
    cluster.indexCount = indexCount;
    return cluster;
}

constexpr std::uint32_t kLimit = 1000;

TEST(MeshClusterTableSelection, AllSlicesKeptKeepsTableUnchanged)
{
    const std::vector<MeshCluster> clusters = { MakeCluster(0, 6), MakeCluster(6, 3), MakeCluster(9, 3) };
    const std::vector<ClusterSliceRange> slices = { { 0, 9, true }, { 9, 3, true } };

    std::string why;
    const auto table = SelectClusterTableForKeptSlices(clusters, slices, 12, kLimit, &why);
    ASSERT_TRUE(table.has_value()) << why;
    ASSERT_EQ(table->size(), 3u);
    EXPECT_EQ((*table)[2].firstIndex, 9u);
}

TEST(MeshClusterTableSelection, DroppedSliceRebasesFollowingOffsets)
{
    const std::vector<MeshCluster> clusters = { MakeCluster(0, 3), MakeCluster(3, 6), MakeCluster(9, 3) };
    const std::vector<ClusterSliceRange> slices = { { 0, 3, true }, { 3, 6, false }, { 9, 3, true } };

    std::string why;
    const auto table = SelectClusterTableForKeptSlices(clusters, slices, 6, kLimit, &why);
    ASSERT_TRUE(table.has_value()) << why;
    ASSERT_EQ(table->size(), 2u);
    EXPECT_EQ((*table)[0].firstIndex, 0u);
    EXPECT_EQ((*table)[1].firstIndex, 3u);
}

// A textured slice keeps only its own clusters, rebased to the slice's own index buffer.
TEST(MeshClusterTableSelection, EachSingleSliceOfTwoGetsItsOwnRebasedTable)
{
    const std::vector<MeshCluster> clusters = { MakeCluster(0, 6), MakeCluster(6, 3), MakeCluster(9, 6), MakeCluster(15, 3) };
    std::vector<ClusterSliceRange> slices = { { 0, 9, false }, { 9, 9, false } };

    std::string why;
    slices[0].keep = true;
    const auto first = SelectClusterTableForKeptSlices(clusters, slices, 9, kLimit, &why);
    ASSERT_TRUE(first.has_value()) << why;
    ASSERT_EQ(first->size(), 2u);
    EXPECT_EQ((*first)[0].firstIndex, 0u);
    EXPECT_EQ((*first)[1].firstIndex, 6u);

    slices[0].keep = false;
    slices[1].keep = true;
    const auto second = SelectClusterTableForKeptSlices(clusters, slices, 9, kLimit, &why);
    ASSERT_TRUE(second.has_value()) << why;
    ASSERT_EQ(second->size(), 2u);
    EXPECT_EQ((*second)[0].firstIndex, 0u);
    EXPECT_EQ((*second)[0].indexCount, 6u);
    EXPECT_EQ((*second)[1].firstIndex, 6u);
    EXPECT_EQ((*second)[1].indexCount, 3u);
}

TEST(MeshClusterTableSelection, ClusterStraddlingSliceEdgeIsRefused)
{
    const std::vector<MeshCluster> clusters = { MakeCluster(0, 12) };
    const std::vector<ClusterSliceRange> slices = { { 0, 6, true }, { 6, 6, false } };

    std::string why;
    EXPECT_FALSE(SelectClusterTableForKeptSlices(clusters, slices, 6, kLimit, &why).has_value());
    EXPECT_FALSE(why.empty());
}

TEST(MeshClusterTableSelection, GapInKeptRangeFailsValidation)
{
    const std::vector<MeshCluster> clusters = { MakeCluster(0, 3), MakeCluster(6, 3) };
    const std::vector<ClusterSliceRange> slices = { { 0, 9, true } };

    std::string why;
    EXPECT_FALSE(SelectClusterTableForKeptSlices(clusters, slices, 9, kLimit, &why).has_value());
    EXPECT_FALSE(why.empty());
}

TEST(MeshClusterTableSelection, TableAboveLimitIsRefused)
{
    const std::vector<MeshCluster> clusters = { MakeCluster(0, 3), MakeCluster(3, 3), MakeCluster(6, 3) };
    const std::vector<ClusterSliceRange> slices = { { 0, 9, true } };

    std::string why;
    EXPECT_FALSE(SelectClusterTableForKeptSlices(clusters, slices, 9, /*maxClusters=*/2, &why).has_value());
    EXPECT_TRUE(SelectClusterTableForKeptSlices(clusters, slices, 9, /*maxClusters=*/3, &why).has_value());
}

TEST(MeshClusterTableSelection, NothingKeptOrNoClustersIsRefused)
{
    const std::vector<MeshCluster> clusters = { MakeCluster(0, 3) };
    const std::vector<ClusterSliceRange> noneKept = { { 0, 3, false } };

    std::string why;
    EXPECT_FALSE(SelectClusterTableForKeptSlices(clusters, noneKept, 0, kLimit, &why).has_value());
    EXPECT_FALSE(SelectClusterTableForKeptSlices({}, noneKept, 3, kLimit, &why).has_value());
}

} // namespace
} // namespace gte
