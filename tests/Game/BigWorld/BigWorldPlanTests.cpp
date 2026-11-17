// Checks the Big World plan: quality tiers, sanitizing and the geometry helper.

#include "Game/BigWorld/BigWorldPlan.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <limits>

namespace gte::bigworld {
namespace {

const FeatureTweak* FindTweak(const BigWorldPlan& plan, const std::string& feature)
{
    const auto it = std::find_if(plan.tweaks.begin(), plan.tweaks.end(),
        [&](const FeatureTweak& t) { return t.feature == feature; });
    return it == plan.tweaks.end() ? nullptr : &*it;
}

std::string ValueOf(const FeatureTweak& tweak, const std::string& key)
{
    for (const KeyValue& kv : tweak.values) {
        if (kv.key == key) {
            return kv.value;
        }
    }
    return {};
}

TEST(BigWorldPlanTest, QualityNamesRoundTripAndIgnoreCase)
{
    for (const Quality q : { Quality::Lean, Quality::Medium, Quality::Big }) {
        Quality parsed = Quality::Lean;
        ASSERT_TRUE(TryParseQuality(QualityName(q), parsed));
        EXPECT_EQ(parsed, q);
    }
    Quality shouted = Quality::Lean;
    EXPECT_TRUE(TryParseQuality("BIG", shouted));
    EXPECT_EQ(shouted, Quality::Big);
    EXPECT_FALSE(TryParseQuality("huge", shouted));
    EXPECT_FALSE(TryParseQuality("", shouted));
}

TEST(BigWorldPlanTest, EveryTierSwitchesCloudsAndGrassOn)
{
    for (const Quality q : { Quality::Lean, Quality::Medium, Quality::Big }) {
        const BigWorldPlan plan = MakeBigWorldPlan(q);
        EXPECT_EQ(plan.quality, q);
        const FeatureTweak* clouds = FindTweak(plan, "VolumetricCloud");
        const FeatureTweak* grass = FindTweak(plan, "Grass");
        ASSERT_NE(clouds, nullptr);
        ASSERT_NE(grass, nullptr);
        EXPECT_EQ(ValueOf(*clouds, "enabled"), "true");
        EXPECT_EQ(ValueOf(*grass, "enabled"), "true");
        EXPECT_FALSE(ValueOf(*grass, "preset").empty());
    }
}

TEST(BigWorldPlanTest, GrassPresetFollowsTheTier)
{
    EXPECT_EQ(ValueOf(*FindTweak(MakeBigWorldPlan(Quality::Lean), "Grass"), "preset"), "Lean");
    EXPECT_EQ(ValueOf(*FindTweak(MakeBigWorldPlan(Quality::Medium), "Grass"), "preset"), "Medium");
    EXPECT_EQ(ValueOf(*FindTweak(MakeBigWorldPlan(Quality::Big), "Grass"), "preset"), "Big");
}

TEST(BigWorldPlanTest, EverySetKeyIsSavedForRestoreExceptThePresetShortcut)
{
    const BigWorldPlan plan = MakeBigWorldPlan(Quality::Big);
    for (const FeatureTweak& tweak : plan.tweaks) {
        for (const KeyValue& kv : tweak.values) {
            if (kv.key == "preset") {
                // A preset changes many keys; the tweak must save at least the density and tile ones.
                EXPECT_NE(std::find(tweak.restoreKeys.begin(), tweak.restoreKeys.end(), "densityLod0"), tweak.restoreKeys.end());
                EXPECT_NE(std::find(tweak.restoreKeys.begin(), tweak.restoreKeys.end(), "heightTileTexels"), tweak.restoreKeys.end());
                continue;
            }
            EXPECT_NE(std::find(tweak.restoreKeys.begin(), tweak.restoreKeys.end(), kv.key), tweak.restoreKeys.end())
                << tweak.feature << "." << kv.key;
        }
    }
}

TEST(BigWorldPlanTest, LeanHasNoRiverAndTheOthersDo)
{
    EXPECT_FALSE(MakeBigWorldPlan(Quality::Lean).river);
    EXPECT_TRUE(MakeBigWorldPlan(Quality::Medium).river);
    EXPECT_TRUE(MakeBigWorldPlan(Quality::Big).river);
}

TEST(BigWorldPlanTest, SanitizeReplacesNonFiniteValuesAndClampsRanges)
{
    BigWorldPlan plan = MakeBigWorldPlan(Quality::Big);
    plan.spotX = std::numeric_limits<double>::quiet_NaN();
    plan.heightAboveGround = -5.0;
    plan.lookDownDegrees = 500.0;
    plan.nodeWaitSeconds = std::numeric_limits<double>::infinity();
    plan.minResidentNodes = -3;
    plan.terrainProfile.clear();
    const BigWorldPlan safe = SanitizeBigWorldPlan(plan);
    EXPECT_TRUE(std::isfinite(safe.spotX));
    EXPECT_GE(safe.heightAboveGround, 1.0);
    EXPECT_LE(safe.lookDownDegrees, 85.0);
    EXPECT_TRUE(std::isfinite(safe.nodeWaitSeconds));
    EXPECT_GE(safe.minResidentNodes, 1);
    EXPECT_EQ(safe.terrainProfile, "lean");
}

TEST(BigWorldPlanTest, SanitizeDropsEmptyTweaks)
{
    BigWorldPlan plan = MakeBigWorldPlan(Quality::Big);
    plan.tweaks.push_back({});
    FeatureTweak noValues;
    noValues.feature = "Grass";
    plan.tweaks.push_back(noValues);
    EXPECT_EQ(SanitizeBigWorldPlan(plan).tweaks.size(), 2u);
}

TEST(BigWorldPlanTest, PointAheadFollowsTheHeading)
{
    BigWorldPlan plan;
    plan.spotX = 10.0;
    plan.spotZ = 20.0;
    plan.headingDegrees = 0.0; // +Z
    PlanPoint p = PointAhead(plan, 100.0);
    EXPECT_NEAR(p.x, 10.0, 1e-9);
    EXPECT_NEAR(p.z, 120.0, 1e-9);
    plan.headingDegrees = 90.0; // +X
    p = PointAhead(plan, 100.0);
    EXPECT_NEAR(p.x, 110.0, 1e-9);
    EXPECT_NEAR(p.z, 20.0, 1e-9);
}

TEST(BigWorldPlanTest, PointAheadSideIsToTheRightOfTheHeading)
{
    BigWorldPlan plan;
    plan.spotX = 0.0;
    plan.spotZ = 0.0;
    plan.headingDegrees = 0.0; // facing +Z: right is +X
    PlanPoint p = PointAhead(plan, 0.0, 50.0);
    EXPECT_NEAR(p.x, 50.0, 1e-9);
    EXPECT_NEAR(p.z, 0.0, 1e-9);
    plan.headingDegrees = 90.0; // facing +X: right is -Z
    p = PointAhead(plan, 0.0, 50.0);
    EXPECT_NEAR(p.x, 0.0, 1e-9);
    EXPECT_NEAR(p.z, -50.0, 1e-9);
}

} // namespace
} // namespace gte::bigworld
