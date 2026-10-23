// Unit tests for the Inspector cluster status text, in particular the status of a clone source.

#include "Editor/ClusterStatusText.h"

#include <string>

#include <gtest/gtest.h>

namespace gte {
namespace {

MeshClusterFinishedBuild MakeInPlaceBuild(bool success, bool poorFill, ClusterWeldMode weld)
{
    MeshClusterFinishedBuild build;
    build.path = "mesh.gta";
    build.outcome.success = success;
    build.outcome.message = "boom";
    build.outcome.report.clusterCount = 10;
    build.outcome.report.avgTrianglesPerCluster = 96.0f;
    build.outcome.report.vertexCountBefore = 300;
    build.outcome.report.vertexCountAfter = 100;
    build.outcome.report.milliseconds = 25.0;
    build.outcome.report.poorFill = poorFill;
    build.outcome.report.weld = weld;
    return build;
}

MeshClusterFinishedBuild MakeCloneBuild(bool success, bool fellBack)
{
    MeshClusterFinishedBuild build = MakeInPlaceBuild(success, true, ClusterWeldMode::PositionOnly);
    build.wasClone = true;
    build.createdPath = "dir/mesh_clustered.gta";
    build.weldUsed = ClusterWeldMode::PositionOnly;
    build.weldFellBack = fellBack;
    return build;
}

TEST(ClusterStatusTextTest, NothingToReportIsQuietAndEmpty)
{
    const ClusterStatus status = DescribeClusterStatus(ClusterStatusInput{});
    EXPECT_TRUE(status.text.empty());
    EXPECT_TRUE(status.note.empty());
}

TEST(ClusterStatusTextTest, BuildingAndBusyWinOverTheLastResult)
{
    const MeshClusterFinishedBuild last = MakeInPlaceBuild(true, false, ClusterWeldMode::Exact);
    ClusterStatusInput input;
    input.last = &last;
    input.startRefusedBusy = true;
    EXPECT_EQ(DescribeClusterStatus(input).text, "Busy: another cluster build is running.");
    input.building = true;
    EXPECT_EQ(DescribeClusterStatus(input).text, "Building clusters...");
}

TEST(ClusterStatusTextTest, InPlaceTextsAreUnchanged)
{
    const MeshClusterFinishedBuild good = MakeInPlaceBuild(true, true, ClusterWeldMode::Exact);
    ClusterStatusInput input;
    input.last = &good;
    ClusterStatus status = DescribeClusterStatus(input);
    EXPECT_EQ(status.tone, ClusterStatusTone::Good);
    EXPECT_EQ(status.text, "10 clusters, avg 96.0 tris, vertices 300 -> 100, 25 ms");
    EXPECT_EQ(status.note, "Low cluster fill. Try the Position only weld.");

    const MeshClusterFinishedBuild positionOnly = MakeInPlaceBuild(true, true, ClusterWeldMode::PositionOnly);
    input.last = &positionOnly;
    EXPECT_EQ(DescribeClusterStatus(input).note, "Low cluster fill.");

    const MeshClusterFinishedBuild failed = MakeInPlaceBuild(false, false, ClusterWeldMode::Exact);
    input.last = &failed;
    status = DescribeClusterStatus(input);
    EXPECT_EQ(status.tone, ClusterStatusTone::Bad);
    EXPECT_EQ(status.text, "Cluster build failed: boom");
}

TEST(ClusterStatusTextTest, CloneSourceShowsTheCloneTextAndNoWeldNote)
{
    const MeshClusterFinishedBuild clone = MakeCloneBuild(true, true);
    ClusterStatusInput input;
    input.last = &clone;
    const ClusterStatus status = DescribeClusterStatus(input);
    EXPECT_EQ(status.tone, ClusterStatusTone::Good);
    EXPECT_EQ(status.text,
        "Created mesh_clustered.gta: 10 clusters, avg 96.0 tris, weld Position only (Exact was poor), 25 ms");
    EXPECT_TRUE(status.note.empty());
}

TEST(ClusterStatusTextTest, FailedCloneIsReportedAsFailedClone)
{
    const MeshClusterFinishedBuild clone = MakeCloneBuild(false, false);
    ClusterStatusInput input;
    input.last = &clone;
    const ClusterStatus status = DescribeClusterStatus(input);
    EXPECT_EQ(status.tone, ClusterStatusTone::Bad);
    EXPECT_EQ(status.text, "Clone failed: boom");
}

TEST(ClusterStatusTextTest, CloneSourceShowsProgressWhileReading)
{
    ClusterStatusInput input;
    input.readingForClone = true;
    EXPECT_EQ(DescribeClusterStatus(input).text, "Creating clustered copy...");
}

} // namespace
} // namespace gte
