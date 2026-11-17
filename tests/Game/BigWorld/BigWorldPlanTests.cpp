// Checks the Big World plan: quality tiers, sanitizing and the geometry helper.

#include "Game/BigWorld/BigWorldPlan.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

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

TEST(BigWorldPlanTest, LowerTiersMarchTheCloudsWithFewerSteps)
{
    const BigWorldPlan leanPlan = MakeBigWorldPlan(Quality::Lean);
    const BigWorldPlan mediumPlan = MakeBigWorldPlan(Quality::Medium);
    const BigWorldPlan bigPlan = MakeBigWorldPlan(Quality::Big);
    const FeatureTweak* lean = FindTweak(leanPlan, "VolumetricCloud");
    const FeatureTweak* medium = FindTweak(mediumPlan, "VolumetricCloud");
    const FeatureTweak* big = FindTweak(bigPlan, "VolumetricCloud");
    ASSERT_NE(lean, nullptr);
    ASSERT_NE(medium, nullptr);
    ASSERT_NE(big, nullptr);
    EXPECT_LT(std::stoi(ValueOf(*lean, "maxSteps")), std::stoi(ValueOf(*medium, "maxSteps")));
    EXPECT_EQ(ValueOf(*big, "maxSteps"), ""); // the full tier keeps the feature default
    EXPECT_NE(std::find(big->restoreKeys.begin(), big->restoreKeys.end(), "maxSteps"), big->restoreKeys.end());
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

TEST(BigWorldPlanTest, LakeOutlineIsAnUnevenClosedBlobAroundItsCentre)
{
    BigWorldPlan plan;
    plan.spotX = 0.0;
    plan.spotZ = 0.0;
    plan.headingDegrees = 90.0;
    plan.lakeDistance = 200.0;
    plan.lakeSize = 100.0;
    plan.lakeCorners = 14;
    const std::vector<PlanPoint> outline = LakeOutline(plan);
    ASSERT_EQ(outline.size(), 14u);
    double minR = 1e9;
    double maxR = 0.0;
    for (const PlanPoint& p : outline) {
        const double r = std::hypot(p.x - 200.0, p.z);
        minR = std::min(minR, r);
        maxR = std::max(maxR, r);
    }
    EXPECT_GT(minR, 20.0);
    EXPECT_LT(maxR, 100.0);
    EXPECT_GT(maxR - minR, 5.0); // not a circle
}

TEST(BigWorldPlanTest, LakeOutlineClampsTheCornerCount)
{
    BigWorldPlan plan;
    plan.lakeCorners = 2;
    EXPECT_EQ(LakeOutline(plan).size(), 6u);
    plan.lakeCorners = 500;
    EXPECT_EQ(LakeOutline(plan).size(), 32u);
}

TEST(BigWorldPlanTest, RiverPathRunsFromStartToEndAlongTheHeadingOnItsSide)
{
    BigWorldPlan plan;
    plan.spotX = 0.0;
    plan.spotZ = 0.0;
    plan.headingDegrees = 90.0; // +X; right is -Z
    plan.riverStart = -100.0;
    plan.riverEnd = 400.0;
    plan.riverSide = 100.0;
    plan.riverWiggle = 20.0;
    const std::vector<PlanPoint> path = RiverPath(plan);
    ASSERT_GE(path.size(), 5u);
    EXPECT_NEAR(path.front().x, -100.0, 1e-6);
    EXPECT_NEAR(path.back().x, 400.0, 1e-6);
    for (std::size_t i = 1; i < path.size(); ++i) {
        EXPECT_GT(path[i].x, path[i - 1].x); // never turns back
    }
    for (const PlanPoint& p : path) {
        EXPECT_LT(p.z, -75.0); // stays on the right-hand side within the wiggle
        EXPECT_GT(p.z, -125.0);
    }
    EXPECT_NEAR(path.back().z, -100.0, 1e-6); // ends straight
}

TEST(BigWorldPlanTest, SanitizeKeepsTheRiverLongEnough)
{
    BigWorldPlan plan;
    plan.riverStart = 100.0;
    plan.riverEnd = 90.0;
    const BigWorldPlan safe = SanitizeBigWorldPlan(plan);
    EXPECT_GE(safe.riverEnd - safe.riverStart, 50.0);
}

} // namespace
} // namespace gte::bigworld
