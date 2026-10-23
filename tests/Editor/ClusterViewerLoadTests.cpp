#include "Editor/ClusterViewerLoad.h"

#include <gtest/gtest.h>

namespace gte {
namespace {

// A clustered mesh that loads cleanly.
ClusterViewerLoadInput GoodInput()
{
    ClusterViewerLoadInput in;
    in.pathSupported = true;
    in.decodeOk = true;
    in.vertexCount = 100;
    in.indexCount = 300;
    in.indicesValid = true;
    in.clusterCount = 4;
    in.maxClusters = 1000;
    in.tableValid = true;
    return in;
}

TEST(ClusterViewerLoadTest, GoodMeshHasClusterModesAndNoBanner)
{
    const ClusterViewerLoadDecision d = DecideClusterViewerLoad(GoodInput());
    EXPECT_TRUE(d.showMesh);
    EXPECT_TRUE(d.clusterModes);
    EXPECT_EQ(d.banner, ClusterViewerBanner::None);
    EXPECT_TRUE(d.text.empty());
    EXPECT_FALSE(d.offerCreateCopy);
}

TEST(ClusterViewerLoadTest, NoDeviceSupportShowsNothing)
{
    ClusterViewerLoadInput in = GoodInput();
    in.pathSupported = false;
    const ClusterViewerLoadDecision d = DecideClusterViewerLoad(in);
    EXPECT_FALSE(d.showMesh);
    EXPECT_FALSE(d.clusterModes);
    EXPECT_EQ(d.banner, ClusterViewerBanner::NoDeviceSupport);
    EXPECT_EQ(d.text, "The cluster path is not supported on this device.");
}

TEST(ClusterViewerLoadTest, FailedDecodeShowsTheError)
{
    ClusterViewerLoadInput in = GoodInput();
    in.decodeOk = false;
    in.decodeError = "bad magic";
    const ClusterViewerLoadDecision d = DecideClusterViewerLoad(in);
    EXPECT_FALSE(d.showMesh);
    EXPECT_EQ(d.banner, ClusterViewerBanner::DecodeFailed);
    EXPECT_EQ(d.text, "bad magic");
}

TEST(ClusterViewerLoadTest, EmptyMeshIsNotShown)
{
    ClusterViewerLoadInput in = GoodInput();
    in.indexCount = 0;
    const ClusterViewerLoadDecision d = DecideClusterViewerLoad(in);
    EXPECT_FALSE(d.showMesh);
    EXPECT_EQ(d.banner, ClusterViewerBanner::EmptyMesh);
}

TEST(ClusterViewerLoadTest, BadIndicesAreNotShown)
{
    ClusterViewerLoadInput in = GoodInput();
    in.indicesValid = false;
    const ClusterViewerLoadDecision d = DecideClusterViewerLoad(in);
    EXPECT_FALSE(d.showMesh);
    EXPECT_FALSE(d.clusterModes);
    EXPECT_EQ(d.banner, ClusterViewerBanner::BadIndices);
    EXPECT_EQ(d.text, "The index list points outside the vertex array. The mesh is not shown.");
}

TEST(ClusterViewerLoadTest, UnclusteredMeshWorksInNormalModeAndOffersACopy)
{
    ClusterViewerLoadInput in = GoodInput();
    in.clusterCount = 0;
    in.tableValid = false;
    const ClusterViewerLoadDecision d = DecideClusterViewerLoad(in);
    EXPECT_TRUE(d.showMesh);
    EXPECT_FALSE(d.clusterModes);
    EXPECT_EQ(d.banner, ClusterViewerBanner::NoTable);
    EXPECT_TRUE(d.offerCreateCopy);
    EXPECT_EQ(d.text, "This mesh has no cluster table. Cluster modes are unavailable.");
}

TEST(ClusterViewerLoadTest, DamagedTableNamesTheReason)
{
    ClusterViewerLoadInput tail = GoodInput();
    tail.clusterCount = 0;
    tail.clusterTailRejection = "tail too short";
    const ClusterViewerLoadDecision a = DecideClusterViewerLoad(tail);
    EXPECT_TRUE(a.showMesh);
    EXPECT_FALSE(a.clusterModes);
    EXPECT_EQ(a.banner, ClusterViewerBanner::DamagedTable);
    EXPECT_EQ(a.text, "The cluster table is damaged: tail too short. Cluster modes are unavailable.");
    EXPECT_FALSE(a.offerCreateCopy);

    ClusterViewerLoadInput gap = GoodInput();
    gap.tableValid = false;
    gap.tableWhy = "gap at index 12";
    const ClusterViewerLoadDecision b = DecideClusterViewerLoad(gap);
    EXPECT_EQ(b.banner, ClusterViewerBanner::DamagedTable);
    EXPECT_NE(b.text.find("gap at index 12"), std::string::npos);
}

TEST(ClusterViewerLoadTest, TooManyClustersTurnsClusterModesOff)
{
    ClusterViewerLoadInput in = GoodInput();
    in.clusterCount = 2000;
    const ClusterViewerLoadDecision d = DecideClusterViewerLoad(in);
    EXPECT_TRUE(d.showMesh);
    EXPECT_FALSE(d.clusterModes);
    EXPECT_EQ(d.banner, ClusterViewerBanner::TooManyClusters);
    EXPECT_EQ(d.text, "too many clusters for cluster modes (2000 > 1000)");
}

TEST(ClusterViewerLoadTest, FirstApplicableBannerWins)
{
    ClusterViewerLoadInput in = GoodInput();
    in.pathSupported = false;
    in.decodeOk = false;
    in.indicesValid = false;
    EXPECT_EQ(DecideClusterViewerLoad(in).banner, ClusterViewerBanner::NoDeviceSupport);

    in.pathSupported = true;
    EXPECT_EQ(DecideClusterViewerLoad(in).banner, ClusterViewerBanner::DecodeFailed);

    in.decodeOk = true;
    EXPECT_EQ(DecideClusterViewerLoad(in).banner, ClusterViewerBanner::BadIndices);

    in.indicesValid = true;
    in.tableValid = false;
    in.clusterCount = 2000;
    EXPECT_EQ(DecideClusterViewerLoad(in).banner, ClusterViewerBanner::DamagedTable);
}

} // namespace
} // namespace gte
