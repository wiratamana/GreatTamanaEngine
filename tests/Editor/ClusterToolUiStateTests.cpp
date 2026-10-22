// Unit tests for the pure helpers of the Inspector cluster tool.

#include "Editor/ClusterToolUiState.h"

#include <cmath>
#include <limits>

#include <gtest/gtest.h>

namespace gte {
namespace {

TEST(ClusterToolUiStateTest, MaxVerticesIsClamped)
{
    EXPECT_EQ(ClampClusterMaxVertices(-5), 3);
    EXPECT_EQ(ClampClusterMaxVertices(64), 64);
    EXPECT_EQ(ClampClusterMaxVertices(1000), 255);
}

TEST(ClusterToolUiStateTest, MaxTrianglesIsRoundedToMultipleOfFour)
{
    EXPECT_EQ(ClampClusterMaxTriangles(0), 4);
    EXPECT_EQ(ClampClusterMaxTriangles(125), 124);
    EXPECT_EQ(ClampClusterMaxTriangles(124), 124);
    EXPECT_EQ(ClampClusterMaxTriangles(513), 512);
    EXPECT_EQ(ClampClusterMaxTriangles(100000), 512);
}

TEST(ClusterToolUiStateTest, ConeWeightIsClampedAndFinite)
{
    EXPECT_FLOAT_EQ(ClampClusterConeWeight(-1.0f), 0.0f);
    EXPECT_FLOAT_EQ(ClampClusterConeWeight(2.0f), 1.0f);
    EXPECT_FLOAT_EQ(ClampClusterConeWeight(0.25f), 0.25f);
    EXPECT_FLOAT_EQ(ClampClusterConeWeight(std::numeric_limits<float>::quiet_NaN()), 0.5f);
}

TEST(ClusterToolUiStateTest, StatusLineShowsCountsAndTime)
{
    EXPECT_EQ(FormatClusterStatusLine(8100, 123.5f, 3136374, 524000, 4200.0),
        "8100 clusters, avg 123.5 tris, vertices 3136374 -> 524000, 4200 ms");
}

TEST(ClusterToolUiStateTest, MissingFileIsRefusedAndCachedWithinTheRecheckInterval)
{
    ClusterToolUiState state;
    const auto t0 = std::chrono::steady_clock::time_point{} + std::chrono::seconds(10);
    const ClusterEligibilityCache& first = RefreshClusterEligibility(state, "this/path/does/not/exist.gta", t0);
    EXPECT_FALSE(first.refusalReason.empty());
    EXPECT_TRUE(first.valid);
    EXPECT_EQ(first.lastCheck, t0);

    // Inside the interval nothing is re-read, so lastCheck stays.
    RefreshClusterEligibility(state, "this/path/does/not/exist.gta", t0 + std::chrono::milliseconds(100));
    EXPECT_EQ(state.eligibility.lastCheck, t0);
}

} // namespace
} // namespace gte
