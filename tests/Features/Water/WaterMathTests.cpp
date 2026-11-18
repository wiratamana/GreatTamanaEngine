// Settings, wave table and GPU block tests for the Water feature.

#include "Features/Water/WaterMath.h"
#include "Features/Water/WaterTypes.h"

#include <gtest/gtest.h>

#include <cmath>
#include <cstddef>
#include <limits>

namespace gte {
namespace {

TEST(WaterMathTest, SanitizeClampsAndRepairsNonFiniteValues)
{
    WaterSettings s;
    s.waveCount = 99;
    s.waveHeight = std::numeric_limits<float>::quiet_NaN();
    s.choppiness = 5.0f;
    s.clarity = -3.0f;
    s.ssrSteps = 1;
    s.debugView = static_cast<WaterDebugView>(200);
    s.shallowColor = Vec3(std::numeric_limits<float>::infinity(), -1.0f, 0.5f);

    const WaterSettings safe = SanitizeWaterSettings(s);

    EXPECT_EQ(safe.waveCount, kWaterMaxWaves);
    EXPECT_FLOAT_EQ(safe.waveHeight, WaterSettings{}.waveHeight);
    EXPECT_FLOAT_EQ(safe.choppiness, 1.0f);
    EXPECT_FLOAT_EQ(safe.clarity, 0.5f);
    EXPECT_EQ(safe.ssrSteps, 4u);
    EXPECT_EQ(safe.debugView, WaterDebugView::Off);
    EXPECT_FLOAT_EQ(safe.shallowColor.x, WaterSettings{}.shallowColor.x);
    EXPECT_FLOAT_EQ(safe.shallowColor.y, 0.0f);
}

TEST(WaterMathTest, SanitizeKeepsDefaultsUnchanged)
{
    const WaterSettings d;
    const WaterSettings safe = SanitizeWaterSettings(d);
    EXPECT_EQ(safe.waveCount, d.waveCount);
    EXPECT_FLOAT_EQ(safe.waveHeight, d.waveHeight);
    EXPECT_FLOAT_EQ(safe.roughness, d.roughness);
    EXPECT_EQ(safe.ssr, d.ssr);
}

TEST(WaterMathTest, GpuBlockLayoutMatchesStd430)
{
    EXPECT_EQ(offsetof(WaterShadeParamsGpu, invViewProjection), 0u);
    EXPECT_EQ(offsetof(WaterShadeParamsGpu, viewProjection), 64u);
    EXPECT_EQ(offsetof(WaterShadeParamsGpu, eyeTime), 128u);
    EXPECT_EQ(offsetof(WaterShadeParamsGpu, wetInfo), 128u + 16u * 10u);
    EXPECT_EQ(offsetof(WaterShadeParamsGpu, counts), 128u + 16u * 11u);
    EXPECT_EQ(offsetof(WaterShadeParamsGpu, cloudShadowGrid), 128u + 16u * 12u);
    EXPECT_EQ(offsetof(WaterShadeParamsGpu, cloudShadowLayer), 128u + 16u * 13u);
    EXPECT_EQ(offsetof(WaterShadeParamsGpu, ripple), 128u + 16u * 14u);
    EXPECT_EQ(offsetof(WaterShadeParamsGpu, waves), 128u + 16u * 16u);
    EXPECT_EQ(sizeof(WaterShadeParamsGpu), 128u + 16u * 16u + 32u * kWaterMaxWaves);
}

TEST(WaterMathTest, WaveAmplitudesSumToTheWaveHeight)
{
    WaterSettings s;
    s.waveCount = 6;
    s.waveHeight = 3.0f;
    const WaterWaveTable table = BuildWaterWaves(SanitizeWaterSettings(s), 0.0, 0.0, 0.0);

    float sum = 0.0f;
    for (std::uint32_t i = 0; i < 6; ++i) {
        sum += table[i].ampPhase[0];
    }
    EXPECT_NEAR(sum, 3.0f, 1e-4f);
    EXPECT_FLOAT_EQ(table[6].ampPhase[0], 0.0f);
}

TEST(WaterMathTest, WavesAreLongestFirstAndDirectionsAreUnit)
{
    const WaterWaveTable table = BuildWaterWaves(SanitizeWaterSettings(WaterSettings{}), 0.0, 0.0, 0.0);
    for (std::uint32_t i = 0; i < 6; ++i) {
        const float len = std::hypot(table[i].dirAndK[0], table[i].dirAndK[1]);
        EXPECT_NEAR(len, 1.0f, 1e-5f);
        if (i > 0) {
            EXPECT_GT(table[i].dirAndK[2], table[i - 1].dirAndK[2]);
        }
    }
}

TEST(WaterMathTest, HeightHasNearZeroMeanOverAnAreaOfLargeWaves)
{
    WaterSettings s;
    s.waveCount = 1;
    s.waveHeight = 2.0f;
    s.baseWavelength = 50.0f;
    s.windDirectionDeg = 0.0f; // Waves travel along +X, so 50 m of X is one whole period.
    s.choppiness = 0.8f;
    const WaterWaveTable table = BuildWaterWaves(SanitizeWaterSettings(s), 0.0, 0.0, 0.0);

    double sum = 0.0;
    constexpr int kSamples = 4000;
    for (int i = 0; i < kSamples; ++i) {
        sum += WaterWaveHeight(table, 1, static_cast<float>(i) * 0.0125f, 0.0f);
    }
    EXPECT_NEAR(sum / kSamples, 0.0, 0.02);
}

TEST(WaterMathTest, EyeFoldedPhaseKeepsTheSurfaceContinuousWhenTheEyeMoves)
{
    const WaterSettings s = SanitizeWaterSettings(WaterSettings{});
    constexpr double kTime = 12.5;
    const double eyeX = 15123.7;
    const double eyeZ = -9042.3;
    const WaterWaveTable a = BuildWaterWaves(s, eyeX, eyeZ, kTime);
    const WaterWaveTable b = BuildWaterWaves(s, eyeX + 20.0, eyeZ - 7.0, kTime);

    // The same world point seen from two eyes must have the same height.
    const float hA = WaterWaveHeight(a, s.waveCount, 20.0f, -7.0f);
    const float hB = WaterWaveHeight(b, s.waveCount, 0.0f, 0.0f);
    EXPECT_NEAR(hA, hB, 5e-3f);
}

TEST(WaterMathTest, TimeMovesTheSurface)
{
    const WaterSettings s = SanitizeWaterSettings(WaterSettings{});
    const WaterWaveTable t0 = BuildWaterWaves(s, 0.0, 0.0, 0.0);
    const WaterWaveTable t1 = BuildWaterWaves(s, 0.0, 0.0, 1.7);
    EXPECT_NE(WaterWaveHeight(t0, s.waveCount, 3.0f, 4.0f), WaterWaveHeight(t1, s.waveCount, 3.0f, 4.0f));
}

TEST(WaterMathTest, ZeroSpeedFreezesTheSurface)
{
    WaterSettings s;
    s.waveSpeed = 0.0f;
    const WaterSettings safe = SanitizeWaterSettings(s);
    const WaterWaveTable t0 = BuildWaterWaves(safe, 0.0, 0.0, 0.0);
    const WaterWaveTable t1 = BuildWaterWaves(safe, 0.0, 0.0, 9.0);
    EXPECT_FLOAT_EQ(WaterWaveHeight(t0, safe.waveCount, 3.0f, 4.0f), WaterWaveHeight(t1, safe.waveCount, 3.0f, 4.0f));
}

TEST(WaterMathTest, ParamsCarrySettingsFlagsAndCamera)
{
    WaterSettings s;
    s.seaLevel = 12.0f;
    s.ssr = true;
    s.wetShore = false;
    s.waveCount = 4;
    s.clarity = 20.0f;
    s.debugView = WaterDebugView::Foam;

    WaterFrameInputs in;
    in.eye = Vec3(1.0f, 2.0f, 3.0f);
    in.jitterNdc = Vec2(0.25f, -0.5f);
    in.pixelScale = 0.002f;
    in.skyValid = true;
    in.timeSeconds = 3700.0;

    const WaterShadeParamsGpu p = MakeWaterShadeParams(SanitizeWaterSettings(s), in);

    EXPECT_FLOAT_EQ(p.eyeTime[1], 2.0f);
    EXPECT_NEAR(p.eyeTime[3], 100.0f, 1e-3f);
    EXPECT_FLOAT_EQ(p.viewInfo[0], 0.002f);
    EXPECT_FLOAT_EQ(p.viewInfo[1], 0.25f);
    EXPECT_FLOAT_EQ(p.viewInfo[2], -0.5f);
    EXPECT_FLOAT_EQ(p.viewInfo[3], 12.0f);
    EXPECT_FLOAT_EQ(p.shallowColor[3], 20.0f);
    EXPECT_EQ(p.counts[0], 4u);
    EXPECT_EQ(p.counts[1], static_cast<std::uint32_t>(WaterDebugView::Foam));
    EXPECT_EQ(p.counts[3], kWaterFlagSsr | kWaterFlagSky);
}

TEST(WaterMathTest, RippleLatticeWrapsWithTheEye)
{
    const WaterSettings s = SanitizeWaterSettings(WaterSettings{});
    WaterFrameInputs a;
    a.eye = Vec3(100.0f, 0.0f, 50.0f);
    WaterFrameInputs b = a;
    b.eye.x += 1024.0f;

    const WaterShadeParamsGpu pa = MakeWaterShadeParams(s, a);
    const WaterShadeParamsGpu pb = MakeWaterShadeParams(s, b);
    for (std::size_t i = 0; i < 2; ++i) {
        EXPECT_NEAR(pa.ripple[i].scaleOffsetPeriod[1], pb.ripple[i].scaleOffsetPeriod[1], 1e-2f);
        EXPECT_GT(pa.ripple[i].scaleOffsetPeriod[3], 0.0f);
    }
}

TEST(WaterMathTest, ParamsPackCloudShadowGridAndLayerWhenValid)
{
    const WaterSettings safe = SanitizeWaterSettings(WaterSettings{});

    WaterFrameInputs in;
    in.cloudShadowValid = true;
    in.cloudShadowCenterX = 250.0f;
    in.cloudShadowCenterZ = -125.0f;
    in.cloudShadowExtentM = 8000.0f;
    in.cloudShadowStrength = 0.85f;
    in.cloudShadowLayerBaseM = 1400.0f;
    in.cloudShadowLayerTopM = 3200.0f;
    in.cloudShadowMapSize = 128u;

    const WaterShadeParamsGpu p = MakeWaterShadeParams(safe, in);
    EXPECT_NE(p.counts[3] & kWaterFlagCloudShadow, 0u);
    EXPECT_FLOAT_EQ(p.cloudShadowGrid[0], 250.0f);
    EXPECT_FLOAT_EQ(p.cloudShadowGrid[1], -125.0f);
    EXPECT_FLOAT_EQ(p.cloudShadowGrid[2], 1.0f / 8000.0f);
    EXPECT_FLOAT_EQ(p.cloudShadowGrid[3], 0.85f);
    EXPECT_FLOAT_EQ(p.cloudShadowLayer[0], 1400.0f);
    EXPECT_FLOAT_EQ(p.cloudShadowLayer[1], 3200.0f);
    EXPECT_FLOAT_EQ(p.cloudShadowLayer[2], 1.0f);
    EXPECT_FLOAT_EQ(p.cloudShadowLayer[3], 128.0f);

    WaterFrameInputs zeroStrength = in;
    zeroStrength.cloudShadowStrength = 0.0f;
    const WaterShadeParamsGpu pOff = MakeWaterShadeParams(safe, zeroStrength);
    EXPECT_EQ(pOff.counts[3] & kWaterFlagCloudShadow, 0u);
    EXPECT_FLOAT_EQ(pOff.cloudShadowGrid[3], 0.0f);
}

} // namespace
} // namespace gte
