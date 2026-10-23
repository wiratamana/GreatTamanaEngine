#include "Editor/ClusterViewerStats.h"

#include <gtest/gtest.h>

namespace gte {
namespace {

ClusterViewCounts Counts(std::uint32_t total, std::uint32_t frustum, std::uint32_t cone, std::uint32_t visible)
{
    ClusterViewCounts c;
    c.total = total;
    c.passedFrustum = frustum;
    c.passedCone = cone;
    c.visible = visible;
    return c;
}

TEST(ClusterViewerStatsTest, ReasonsAreDifferencesAndSumToTotal)
{
    const ClusterViewerStats s = BuildClusterViewerStats(Counts(100, 60, 50, 40));
    ASSERT_TRUE(s.valid);
    EXPECT_EQ(s.byFrustum, 40u);
    EXPECT_EQ(s.byCone, 10u);
    EXPECT_EQ(s.byOcclusion, 10u);
    EXPECT_EQ(s.visible, 40u);
    EXPECT_EQ(s.visible + s.byFrustum + s.byCone + s.byOcclusion, s.total);
    EXPECT_DOUBLE_EQ(s.visiblePercent, 40.0);
}

TEST(ClusterViewerStatsTest, NothingAppliedKeepsEverything)
{
    const ClusterViewerStats s = BuildClusterViewerStats(Counts(10, 10, 10, 10));
    ASSERT_TRUE(s.valid);
    EXPECT_EQ(s.byFrustum + s.byCone + s.byOcclusion, 0u);
    EXPECT_EQ(s.visible, s.total);
}

TEST(ClusterViewerStatsTest, SingleReasonOnly)
{
    const ClusterViewerStats cone = BuildClusterViewerStats(Counts(10, 10, 7, 7));
    ASSERT_TRUE(cone.valid);
    EXPECT_EQ(cone.byFrustum, 0u);
    EXPECT_EQ(cone.byCone, 3u);
    EXPECT_EQ(cone.byOcclusion, 0u);
}

TEST(ClusterViewerStatsTest, BrokenNestingIsRejectedWithoutWrapping)
{
    const ClusterViewerStats a = BuildClusterViewerStats(Counts(100, 100, 120, 50));
    EXPECT_FALSE(a.valid);
    EXPECT_NE(a.error.find("passedCone 120"), std::string::npos);
    EXPECT_EQ(a.byCone, 0u);

    const ClusterViewerStats b = BuildClusterViewerStats(Counts(100, 80, 70, 75));
    EXPECT_FALSE(b.valid);
    EXPECT_EQ(b.byOcclusion, 0u);

    const ClusterViewerStats c = BuildClusterViewerStats(Counts(100, 101, 50, 40));
    EXPECT_FALSE(c.valid);
    EXPECT_EQ(c.byFrustum, 0u);
}

TEST(ClusterViewerStatsTest, EmptyMeshHasNoPercent)
{
    const ClusterViewerStats s = BuildClusterViewerStats(Counts(0, 0, 0, 0));
    EXPECT_TRUE(s.valid);
    EXPECT_DOUBLE_EQ(s.visiblePercent, 0.0);
}

TEST(ClusterViewerStatsTest, TriangleCountAbove32BitsStaysExact)
{
    ClusterViewCounts c = Counts(1, 1, 1, 1);
    c.visibleTriangles = (std::uint64_t{ 1 } << 33) + 7;
    EXPECT_EQ(BuildClusterViewerStats(c).visibleTriangles, (std::uint64_t{ 1 } << 33) + 7);
}

} // namespace
} // namespace gte
