// CPU checks of the aerial perspective scale rule and its hold state.

#include "Features/Atmosphere/AtmosphereAerialPerspectiveMath.h"

#include <gtest/gtest.h>

#include <cmath>
#include <limits>

namespace gte {
namespace {

constexpr float kNaN = std::numeric_limits<float>::quiet_NaN();

void ExpectSmallScene(const AerialScale& s)
{
    EXPECT_FLOAT_EQ(s.maxDistanceKm, 0.5f);
    EXPECT_FLOAT_EQ(s.exaggeration, 8.0f);
    EXPECT_FLOAT_EQ(s.shadowBlend, 0.0f);
}

SunShadowReceiver FittedReceiver(float lastSplitMeters)
{
    SunShadowReceiver r;
    r.cascadeCount = 2;
    r.splitFar = { lastSplitMeters * 0.3f, lastSplitMeters, lastSplitMeters, lastSplitMeters };
    r.depthFitted = true;
    return r;
}

TEST(AerialScaleTest, SmallSceneKeepsTheDefaults)
{
    ExpectSmallScene(ComputeAerialScale(0.04f));
    const AerialScale edge = ComputeAerialScale(0.4f);
    EXPECT_FLOAT_EQ(edge.maxDistanceKm, 0.5f);
    EXPECT_FLOAT_EQ(edge.exaggeration, 8.0f);
}

TEST(AerialScaleTest, LargeSceneUsesTheGridValue)
{
    const AerialScale s = ComputeAerialScale(10.0f);
    EXPECT_NEAR(s.maxDistanceKm, 13.45f, 0.01f);
    EXPECT_FLOAT_EQ(s.exaggeration, 1.0f);
    EXPECT_FLOAT_EQ(s.shadowBlend, 1.0f);
    EXPECT_FLOAT_EQ(ComputeAerialScale(100.0f).maxDistanceKm, 30.0f);
}

TEST(AerialScaleTest, BadInputGivesTheSmallSceneValues)
{
    ExpectSmallScene(ComputeAerialScale(kNaN));
    ExpectSmallScene(ComputeAerialScale(0.0f));
    ExpectSmallScene(ComputeAerialScale(-3.0f));
    ExpectSmallScene(ComputeAerialScale(std::numeric_limits<float>::infinity()));
}

TEST(AerialScaleTest, OutputNeverDecreasesAsTheInputGrows)
{
    AerialScale previous = ComputeAerialScale(0.001f);
    for (float km = 0.002f; km < 200.0f; km *= 1.01f) {
        const AerialScale s = ComputeAerialScale(km);
        EXPECT_GE(s.maxDistanceKm, previous.maxDistanceKm) << km;
        EXPECT_GE(s.shadowBlend, previous.shadowBlend) << km;
        EXPECT_LE(s.exaggeration, previous.exaggeration) << km;
        previous = s;
    }
}

TEST(AerialScaleTest, ValuesInsideOneGridCellGiveTheSameOutput)
{
    EXPECT_FLOAT_EQ(ComputeAerialScale(9.00f).maxDistanceKm, ComputeAerialScale(9.02f).maxDistanceKm);
    EXPECT_FLOAT_EQ(ComputeAerialScale(2.51f).maxDistanceKm, ComputeAerialScale(2.55f).maxDistanceKm);
}

TEST(AerialScaleTest, ManualModeUsesTheSettingsWithFullShadows)
{
    AtmosphereSettings s;
    s.autoScale = false;
    s.aerialPerspectiveMaxDistanceKm = 7.0f;
    s.aerialPerspectiveScatteringExaggeration = 3.0f;
    AerialScaleState state;
    const SunShadowReceiver r = FittedReceiver(10000.0f);
    const AerialScaleResult result = ResolveAerialScale(state, &r, s, 5);
    EXPECT_FLOAT_EQ(result.scale.maxDistanceKm, 7.0f);
    EXPECT_FLOAT_EQ(result.scale.exaggeration, 3.0f);
    EXPECT_FLOAT_EQ(result.scale.shadowBlend, 1.0f);
    EXPECT_FALSE(result.held);
    EXPECT_FALSE(state.valid);
}

TEST(AerialScaleTest, NoStateEverUsesTheSettingsWithoutAirShadows)
{
    const AtmosphereSettings s;
    AerialScaleState state;
    const AerialScaleResult result = ResolveAerialScale(state, nullptr, s, 3);
    EXPECT_FLOAT_EQ(result.scale.maxDistanceKm, s.aerialPerspectiveMaxDistanceKm);
    EXPECT_FLOAT_EQ(result.scale.exaggeration, s.aerialPerspectiveScatteringExaggeration);
    EXPECT_FLOAT_EQ(result.scale.shadowBlend, 0.0f);
    EXPECT_FALSE(result.held);
}

TEST(AerialScaleTest, FittedCascadesSetTheScaleFromTheLastSplit)
{
    const AtmosphereSettings s;
    AerialScaleState state;
    const SunShadowReceiver r = FittedReceiver(10000.0f);
    const AerialScaleResult result = ResolveAerialScale(state, &r, s, 10);
    EXPECT_NEAR(result.scale.maxDistanceKm, 13.45f, 0.01f);
    EXPECT_FALSE(result.held);
    EXPECT_TRUE(state.valid);
}

TEST(AerialScaleTest, UnfittedCascadesDoNotUpdateTheState)
{
    const AtmosphereSettings s;
    AerialScaleState state;
    SunShadowReceiver r = FittedReceiver(10000.0f);
    ResolveAerialScale(state, &r, s, 10);

    r.depthFitted = false;
    r.splitFar = { 1.0f, 2.0f, 2.0f, 2.0f };
    const AerialScaleResult result = ResolveAerialScale(state, &r, s, 11);
    EXPECT_NEAR(result.scale.maxDistanceKm, 13.45f, 0.01f);
    EXPECT_TRUE(result.held);
}

TEST(AerialScaleTest, HoldRuleLastsOneHundredTwentyFrames)
{
    const AtmosphereSettings s;
    for (const bool expiredCase : { false, true }) {
        AerialScaleState state;
        const SunShadowReceiver r = FittedReceiver(10000.0f);
        ResolveAerialScale(state, &r, s, 1000);

        const std::uint64_t frame = expiredCase ? 1000 + 121 : 1000 + 119;
        const AerialScaleResult result = ResolveAerialScale(state, nullptr, s, frame);
        if (expiredCase) {
            EXPECT_FALSE(result.held);
            EXPECT_FLOAT_EQ(result.scale.maxDistanceKm, s.aerialPerspectiveMaxDistanceKm);
            EXPECT_FLOAT_EQ(result.scale.shadowBlend, 0.0f);
        } else {
            EXPECT_TRUE(result.held);
            EXPECT_NEAR(result.scale.maxDistanceKm, 13.45f, 0.01f);
        }
    }
}

TEST(AerialScaleTest, ARestartedFrameCounterDropsTheState)
{
    const AtmosphereSettings s;
    AerialScaleState state;
    const SunShadowReceiver r = FittedReceiver(10000.0f);
    ResolveAerialScale(state, &r, s, 500);
    const AerialScaleResult result = ResolveAerialScale(state, nullptr, s, 3);
    EXPECT_FALSE(result.held);
    EXPECT_FLOAT_EQ(result.scale.shadowBlend, 0.0f);
}

} // namespace
} // namespace gte
