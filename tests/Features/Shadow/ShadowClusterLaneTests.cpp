// Cascade cluster lanes: shape, pass names and the camera built from a cascade matrix. No device involved.

#include "Features/Shadow/ShadowFeature.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <set>
#include <string>

namespace gte {
namespace {

TEST(ShadowClusterLaneTest, OneLaneAndFourPartSlotsPerCascade)
{
    EXPECT_EQ(kShadowClusterLanes, kMaxShadowCascades);
    EXPECT_EQ(kShadowClusterPartSlots, 4u);
}

TEST(ShadowClusterLaneTest, TheCascadeLanesShareTheWorkingBuffersOfLaneZero)
{
    EXPECT_TRUE(kShadowClusterShape.shareLaneBuffers);
    EXPECT_EQ(kShadowClusterShape.lanes, kShadowClusterLanes);
    EXPECT_EQ(kShadowClusterShape.partSlots, kShadowClusterPartSlots);

    fx::ClusterDrawState state{ kShadowClusterBaseName, kShadowClusterShape };
    ASSERT_TRUE(state.Valid()) << state.Error();
    for (std::uint32_t slot = 0; slot < kShadowClusterPartSlots; ++slot) {
        for (std::uint32_t lane = 1; lane < kShadowClusterLanes; ++lane) {
            EXPECT_EQ(state.Visible(lane, slot), state.Visible(0, slot));
            EXPECT_EQ(state.Commands(lane, slot), state.Commands(0, slot));
            EXPECT_EQ(state.Count(lane, slot), state.Count(0, slot));
        }
    }
    // Every cascade still has its own pass names, so the profiler shows four lanes.
    EXPECT_STREQ(state.PassName(3, 0, fx::ClusterPassKind::Draw), "Shadow.Cluster.L3.P0.Draw");
}

TEST(ShadowClusterLaneTest, AllFourCascadesFitTheStateBudgetWhereFourSeparateLanesDoNot)
{
    constexpr std::uint64_t kInstances = 100000;
    constexpr std::uint64_t kWork = 2200000;
    fx::ClusterDrawState shared{ "Test.ShadowShared", kShadowClusterShape };
    fx::ClusterDrawState separate{ "Test.ShadowSeparate", fx::ClusterDrawShape{ kShadowClusterLanes, kShadowClusterPartSlots } };
    constexpr rg::ViewScope kPrimary = rg::ViewScope::Primary;
    int sharedFit = 0;
    int separateFit = 0;
    for (std::uint32_t lane = 0; lane < kShadowClusterLanes; ++lane) {
        sharedFit += shared.TryReserveChainBytes(kPrimary, lane, 0, kInstances, kWork, false) == fx::ClusterReserveResult::Ok;
        separateFit += separate.TryReserveChainBytes(kPrimary, lane, 0, kInstances, kWork, false) == fx::ClusterReserveResult::Ok;
    }
    EXPECT_EQ(sharedFit, 4);
    EXPECT_EQ(separateFit, 2);
}

TEST(ShadowClusterLaneTest, DeclaredPassesSkipTheStatsPass)
{
    const fx::ClusterDrawState state{ kShadowClusterBaseName, fx::ClusterDrawShape{ kShadowClusterLanes, kShadowClusterPartSlots } };
    ASSERT_TRUE(state.Valid()) << state.Error();

    const std::vector<std::string> declared = ShadowClusterDeclaredPasses(state.AllPassNames());
    EXPECT_EQ(declared.size(), std::size_t{ kShadowClusterLanes } * kShadowClusterPartSlots * 3);
    for (const std::string& name : declared) {
        EXPECT_EQ(name.rfind("Shadow.Cluster.", 0), 0u) << name;
        EXPECT_EQ(name.find(".Stats"), std::string::npos) << name;
    }
    const std::set<std::string> unique(declared.begin(), declared.end());
    EXPECT_EQ(unique.size(), declared.size());
}

TEST(ShadowClusterLaneTest, NamesNeverCollideWithTheCascadeDrawPasses)
{
    const fx::ClusterDrawState state{ kShadowClusterBaseName, fx::ClusterDrawShape{ kShadowClusterLanes, kShadowClusterPartSlots } };
    ASSERT_TRUE(state.Valid());
    for (const std::string& name : state.AllPassNames()) {
        EXPECT_NE(name, "Shadow.Cascade0.Draw");
        EXPECT_NE(name, "Shadow.Cascade3.Draw");
    }
}

TEST(ShadowClusterLaneTest, OrthographicCascadeUsesTheViewDirection)
{
    const Mat4 cascade = Mat4::OrthographicLH_ZO(-20.0f, 20.0f, -20.0f, 20.0f, 0.1f, 200.0f);
    const fx::ClusterCamera camera = fx::MakeClusterCamera(cascade, Vec3{});
    EXPECT_TRUE(camera.valid);
    EXPECT_TRUE(camera.orthographic);
    const Vec3 d = camera.eyeOrDirection;
    EXPECT_NEAR(d.x * d.x + d.y * d.y + d.z * d.z, 1.0f, 1e-4f);
}

TEST(ShadowClusterLaneTest, PerspectiveMatrixKeepsTheEye)
{
    const Mat4 vp = Mat4::PerspectiveFovLH_ZO(1.0f, 1.5f, 0.1f, 100.0f);
    const fx::ClusterCamera camera = fx::MakeClusterCamera(vp, Vec3(4.0f, 5.0f, 6.0f));
    EXPECT_FALSE(camera.orthographic);
    EXPECT_FLOAT_EQ(camera.eyeOrDirection.y, 5.0f);
}

} // namespace
} // namespace gte
