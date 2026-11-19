// Tier-1 tests for the VolumetricCloud settings, the layer math and the shader names.

#include "Core/FeatureKit/ScreenSpaceView.h"
#include "Features/Atmosphere/AtmosphereTypes.h"
#include "Features/VolumetricCloud/VolumetricCloudFeature.h"
#include "Features/VolumetricCloud/VolumetricCloudMath.h"
#include "Features/VolumetricCloud/VolumetricCloudTypes.h"
#include "Renderer/Vulkan/ShaderReflection.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <set>
#include <span>
#include <sstream>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace gte {
namespace {

TEST(VolumetricCloudMathTest, DefaultsAreAlreadySane)
{
    const VolumetricCloudSettings defaults;
    const VolumetricCloudSettings sanitized = SanitizeVolumetricCloudSettings(defaults);
    EXPECT_EQ(sanitized.debugView, VolumetricCloudDebugView::Off);
    EXPECT_FLOAT_EQ(sanitized.slice, defaults.slice);
    EXPECT_FLOAT_EQ(sanitized.tiling, defaults.tiling);
    EXPECT_EQ(sanitized.channel, defaults.channel);
    EXPECT_FALSE(sanitized.enabled);
}

TEST(VolumetricCloudMathTest, ClampsEveryRange)
{
    VolumetricCloudSettings s;
    s.slice = 7.0f;
    s.tiling = 1000.0f;
    s.channel = 99;
    const VolumetricCloudSettings high = SanitizeVolumetricCloudSettings(s);
    EXPECT_FLOAT_EQ(high.slice, 1.0f);
    EXPECT_FLOAT_EQ(high.tiling, 16.0f);
    EXPECT_EQ(high.channel, kCloudDebugChannelRgb);

    s.slice = -3.0f;
    s.tiling = 0.0f;
    const VolumetricCloudSettings low = SanitizeVolumetricCloudSettings(s);
    EXPECT_FLOAT_EQ(low.slice, 0.0f);
    EXPECT_FLOAT_EQ(low.tiling, 0.25f);
}

TEST(VolumetricCloudMathTest, NonFiniteNumbersFallBackToTheDefault)
{
    VolumetricCloudSettings s;
    s.slice = std::numeric_limits<float>::quiet_NaN();
    s.tiling = std::numeric_limits<float>::infinity();
    const VolumetricCloudSettings out = SanitizeVolumetricCloudSettings(s);
    EXPECT_FLOAT_EQ(out.slice, VolumetricCloudSettings{}.slice);
    EXPECT_FLOAT_EQ(out.tiling, VolumetricCloudSettings{}.tiling);
}

TEST(VolumetricCloudMathTest, UnknownDebugViewTurnsOff)
{
    VolumetricCloudSettings s;
    s.debugView = static_cast<VolumetricCloudDebugView>(200);
    EXPECT_EQ(SanitizeVolumetricCloudSettings(s).debugView, VolumetricCloudDebugView::Off);
}

TEST(VolumetricCloudMathTest, PushCarriesTheSettings)
{
    VolumetricCloudSettings s;
    s.slice = 0.25f;
    s.tiling = 3.0f;
    s.channel = 2;
    const VolumetricCloudSlicePush push = MakeVolumetricCloudSlicePush(s);
    EXPECT_FLOAT_EQ(push.slice, 0.25f);
    EXPECT_FLOAT_EQ(push.tiling, 3.0f);
    EXPECT_EQ(push.channel, 2u);
}

TEST(VolumetricCloudMathTest, AssetNamesLiveUnderTheCloudsFolder)
{
    for (const char* asset : { kCloudShapeAsset, kCloudDetailAsset, kCloudCurlAsset, kCloudWeatherAsset, kCloudBlueNoiseAsset }) {
        EXPECT_EQ(std::string(asset).rfind("clouds/", 0), 0u) << asset;
    }
}

TEST(VolumetricCloudMathTest, EverySettingHasAUniqueNameAndADefaultInsideItsRange)
{
    const VolumetricCloudSettings defaults;
    std::set<std::string> names;
    for (const VolumetricCloudFloatSetting& entry : kVolumetricCloudFloatSettings) {
        EXPECT_TRUE(names.insert(entry.name).second) << entry.name;
        EXPECT_LT(entry.low, entry.high) << entry.name;
        EXPECT_GE(defaults.*(entry.member), entry.low) << entry.name;
        EXPECT_LE(defaults.*(entry.member), entry.high) << entry.name;
    }
}

TEST(VolumetricCloudMathTest, SanitizingIsIdempotentAndKeepsTheLayerThick)
{
    VolumetricCloudSettings s;
    s.layerBaseM = 5000.0f;
    s.layerTopM = 100.0f;
    s.coverage = std::numeric_limits<float>::quiet_NaN();
    const VolumetricCloudSettings once = SanitizeVolumetricCloudSettings(s);
    EXPECT_GT(once.layerTopM, once.layerBaseM);
    EXPECT_FLOAT_EQ(once.coverage, VolumetricCloudSettings{}.coverage);
    const VolumetricCloudSettings twice = SanitizeVolumetricCloudSettings(once);
    EXPECT_FLOAT_EQ(twice.layerTopM, once.layerTopM);
    EXPECT_FLOAT_EQ(twice.layerBaseM, once.layerBaseM);
}

TEST(VolumetricCloudLayerTest, RemapIsLinearAndSurvivesAZeroWidthRange)
{
    EXPECT_FLOAT_EQ(CloudRemap(0.5f, 0.0f, 1.0f, 10.0f, 20.0f), 15.0f);
    EXPECT_FLOAT_EQ(CloudRemap(2.0f, 1.0f, 0.0f, 0.0f, 1.0f), -1.0f);
    EXPECT_TRUE(std::isfinite(CloudRemap(0.3f, 1.0f, 1.0f, 0.0f, 1.0f)));
    EXPECT_LT(CloudRemap(0.3f, 1.0f, 1.0f, 0.0f, 1.0f), -100.0f);
}

TEST(VolumetricCloudLayerTest, AltitudeMatchesTheDoublePrecisionSphere)
{
    const double radius = 6371000.0;
    for (const double horizontal : { 0.0, 1000.0, 20000.0, 90000.0 }) {
        for (const double y : { 0.0, 1500.0, 4000.0 }) {
            const double exact = std::sqrt(horizontal * horizontal + (radius + y) * (radius + y)) - radius;
            const float got = CloudAltitudeM(static_cast<float>(horizontal * horizontal), static_cast<float>(y),
                static_cast<float>(radius));
            EXPECT_NEAR(got, exact, 0.5) << horizontal << " " << y;
        }
    }
}

TEST(VolumetricCloudLayerTest, HeightFractionIsClamped)
{
    EXPECT_FLOAT_EQ(CloudHeightFraction(1500.0f, 1500.0f, 4000.0f), 0.0f);
    EXPECT_FLOAT_EQ(CloudHeightFraction(2750.0f, 1500.0f, 4000.0f), 0.5f);
    EXPECT_FLOAT_EQ(CloudHeightFraction(9000.0f, 1500.0f, 4000.0f), 1.0f);
    EXPECT_FLOAT_EQ(CloudHeightFraction(0.0f, 1500.0f, 4000.0f), 0.0f);
}

TEST(VolumetricCloudLayerTest, GradientsRoundTheBaseAndTheTop)
{
    EXPECT_FLOAT_EQ(CloudShapeGradient(0.0f, 0.8f, 0.07f, 0.12f), 0.0f);
    EXPECT_FLOAT_EQ(CloudShapeGradient(0.1f, 0.8f, 0.07f, 0.12f), 1.0f);
    EXPECT_FLOAT_EQ(CloudShapeGradient(0.95f, 0.5f, 0.07f, 0.12f), 0.0f);
    EXPECT_FLOAT_EQ(CloudDensityGradient(0.0f, 0.2f, 0.9f), 0.0f);
    EXPECT_FLOAT_EQ(CloudDensityGradient(0.5f, 0.2f, 0.9f), 0.5f);
    EXPECT_FLOAT_EQ(CloudDensityGradient(1.0f, 0.2f, 0.9f), 0.0f);
}

TEST(VolumetricCloudLayerTest, TopFalloffStartMovesTheRoundingUp)
{
    // Stop height 1.0: the old start (0.2) is already halfway down at 0.6, the new start (0.5) is still full.
    EXPECT_NEAR(CloudShapeGradient(0.6f, 1.0f, 0.07f, 0.0f, 0.2f), 0.5f, 1e-5f);
    EXPECT_NEAR(CloudShapeGradient(0.6f, 1.0f, 0.07f, 0.0f, 0.5f), 0.8f, 1e-5f);
    EXPECT_FLOAT_EQ(CloudShapeGradient(1.0f, 1.0f, 0.07f, 0.0f, 0.5f), 0.0f);
}

TEST(VolumetricCloudLayerTest, ShapeStretchMapsTheRangeToUnitAndClamps)
{
    EXPECT_FLOAT_EQ(CloudStretchShape(0.5f, 0.0f, 1.0f), 0.5f);
    EXPECT_FLOAT_EQ(CloudStretchShape(0.3f, 0.4f, 0.8f), 0.0f);
    EXPECT_FLOAT_EQ(CloudStretchShape(0.9f, 0.4f, 0.8f), 1.0f);
    EXPECT_NEAR(CloudStretchShape(0.6f, 0.4f, 0.8f), 0.5f, 1e-6f);
}

TEST(VolumetricCloudParamsTest, ShapeAndCurveSettingsReachTheSpareBlockSlots)
{
    VolumetricCloudSettings s = SanitizeVolumetricCloudSettings(VolumetricCloudSettings{});
    s.topFalloffStart = 0.6f;
    s.shapeLow = 0.4f;
    s.shapeHigh = 0.8f;
    s.densityCurve = 0.5f;
    const VolumetricCloudParamsGpu p = MakeVolumetricCloudParams(SanitizeVolumetricCloudSettings(s), VolumetricCloudFrameFacts{});
    EXPECT_FLOAT_EQ(p.skip[1], 0.6f);
    EXPECT_FLOAT_EQ(p.skip[2], 0.4f);
    EXPECT_FLOAT_EQ(p.skip[3], 0.8f);
    EXPECT_FLOAT_EQ(p.ambientColor[3], 0.5f);
    // A high end at or below the low end is lifted so the stretch never divides by zero.
    s.shapeLow = 0.9f;
    s.shapeHigh = 0.1f;
    const VolumetricCloudParamsGpu q = MakeVolumetricCloudParams(SanitizeVolumetricCloudSettings(s), VolumetricCloudFrameFacts{});
    EXPECT_GT(q.skip[3], q.skip[2]);
}

TEST(VolumetricCloudLightTest, SkyVisibilityRunsFromOpenSkyToTheFloor)
{
    EXPECT_FLOAT_EQ(CloudSkyVisibility(0.0f, 1.0f, 0.3f), 1.0f);
    EXPECT_NEAR(CloudSkyVisibility(100.0f, 1.0f, 0.3f), 0.3f, 1e-6f);
    EXPECT_FLOAT_EQ(CloudSkyVisibility(5.0f, 0.0f, 0.3f), 1.0f);
    float last = 2.0f;
    for (float tau = 0.0f; tau < 4.0f; tau += 0.25f) {
        const float v = CloudSkyVisibility(tau, 0.8f, 0.45f);
        EXPECT_LT(v, last);
        EXPECT_GE(v, 0.45f);
        last = v;
    }
    // A negative optical depth never brightens a sample past the open sky.
    EXPECT_FLOAT_EQ(CloudSkyVisibility(-3.0f, 1.0f, 0.2f), 1.0f);
}

TEST(VolumetricCloudLightTest, GroundLightReachesOnlyTheLowCloud)
{
    EXPECT_FLOAT_EQ(CloudGroundShare(0.0f), 1.0f);
    EXPECT_FLOAT_EQ(CloudGroundShare(0.5f), 0.25f);
    EXPECT_FLOAT_EQ(CloudGroundShare(1.0f), 0.0f);
    EXPECT_FLOAT_EQ(CloudGroundShare(3.0f), 0.0f);
}

TEST(VolumetricCloudLightTest, OctaveTintIsWhiteWithoutBlueShift)
{
    const Vec3 tint(0.9f, 0.97f, 1.1f);
    const Vec3 none = CloudOctaveTint(0.0f, tint);
    EXPECT_FLOAT_EQ(none.x, 1.0f);
    EXPECT_FLOAT_EQ(none.y, 1.0f);
    EXPECT_FLOAT_EQ(none.z, 1.0f);
    const Vec3 full = CloudOctaveTint(1.0f, tint);
    EXPECT_FLOAT_EQ(full.x, 0.9f);
    EXPECT_FLOAT_EQ(full.z, 1.1f);
    EXPECT_NEAR(CloudOctaveTint(0.5f, tint).z, 1.05f, 1e-6f);
}

TEST(VolumetricCloudParamsTest, SkyShadeSettingsReachTheBlockAndStayInRange)
{
    VolumetricCloudSettings s = SanitizeVolumetricCloudSettings(VolumetricCloudSettings{});
    EXPECT_FLOAT_EQ(s.skyOcclusion, 0.85f);
    EXPECT_FLOAT_EQ(s.skyFloor, 0.25f);
    s.skyOcclusion = 1.5f;
    s.skyOcclusionReachM = 800.0f;
    s.skyFloor = 0.2f;
    s.groundBounce = 3.0f;
    s.groundWarmth = 0.4f;
    s.msBlueShift = 0.9f;
    const VolumetricCloudParamsGpu p = MakeVolumetricCloudParams(SanitizeVolumetricCloudSettings(s), VolumetricCloudFrameFacts{});
    EXPECT_FLOAT_EQ(p.skyShade[0], 1.5f);
    EXPECT_FLOAT_EQ(p.skyShade[1], 800.0f);
    EXPECT_FLOAT_EQ(p.skyShade[2], 0.2f);
    EXPECT_FLOAT_EQ(p.skyShade[3], 3.0f);
    EXPECT_FLOAT_EQ(p.skyShade2[0], 0.4f);
    EXPECT_FLOAT_EQ(p.skyShade2[1], 0.9f);

    s.skyOcclusion = 99.0f;
    s.skyOcclusionReachM = -5.0f;
    s.skyFloor = std::numeric_limits<float>::quiet_NaN();
    s.groundBounce = 1000.0f;
    s.msBlueShift = 7.0f;
    const VolumetricCloudSettings clean = SanitizeVolumetricCloudSettings(s);
    EXPECT_FLOAT_EQ(clean.skyOcclusion, 4.0f);
    EXPECT_FLOAT_EQ(clean.skyOcclusionReachM, 10.0f);
    EXPECT_FLOAT_EQ(clean.skyFloor, VolumetricCloudSettings{}.skyFloor);
    EXPECT_FLOAT_EQ(clean.groundBounce, 20.0f);
    EXPECT_FLOAT_EQ(clean.msBlueShift, 1.0f);
}

// Double-precision twin of the shell intersection: the first and last parameter of the ray inside the shell.
struct ExactSegment {
    bool hit = false;
    double start = 0.0;
    double end = 0.0;
};

ExactSegment ExactShell(double cameraY, double dirY, double radius, double base, double top, double maxDistance)
{
    const double oy = radius + cameraY;
    const auto roots = [&](double height, double& nearT, double& farT) {
        const double r = radius + height;
        const double b = oy * dirY;
        const double disc = b * b - (oy * oy - r * r);
        if (disc < 0.0) {
            return false;
        }
        nearT = -b - std::sqrt(disc);
        farT = -b + std::sqrt(disc);
        return true;
    };
    double tn = 0.0, tf = 0.0, bn = 0.0, bf = 0.0, gn = 0.0, gf = 0.0;
    const bool topHit = roots(top, tn, tf);
    const bool baseHit = roots(base, bn, bf);
    ExactSegment seg;
    double start = 0.0;
    double end = 0.0;
    if (cameraY < base) {
        if (dirY < 0.0 && roots(0.0, gn, gf) && gn > -0.5) {
            return seg;
        }
        start = bf;
        end = tf;
    } else if (cameraY < top) {
        end = tf;
        if (baseHit && bn > 0.0) {
            end = std::min(end, bn);
        }
    } else {
        if (!topHit || tf <= 0.0) {
            return seg;
        }
        start = std::max(tn, 0.0);
        end = (baseHit && bn > 0.0) ? bn : tf;
    }
    end = std::min(end, maxDistance);
    if (!(end > start)) {
        return seg;
    }
    seg.hit = true;
    seg.start = start;
    seg.end = end;
    return seg;
}

TEST(VolumetricCloudLayerTest, ShellSegmentMatchesTheDoublePrecisionReference)
{
    const float radius = 6371000.0f;
    for (const float y : { 0.0f, 2.0f, 1400.0f, 1500.0f, 2500.0f, 4000.0f, 7000.0f }) {
        for (const float dirY : { -0.5f, -0.05f, -0.001f, 0.0f, 0.001f, 0.01f, 0.1f, 0.5f, 0.99f, 1.0f }) {
            const CloudShellSegment got = ComputeCloudShellSegment(y, dirY, radius, 1500.0f, 4000.0f, 1.0e7f);
            const ExactSegment want = ExactShell(y, dirY, radius, 1500.0, 4000.0, 1.0e7);
            ASSERT_EQ(got.hit, want.hit) << "y " << y << " dirY " << dirY;
            if (want.hit) {
                // Float rounding at 6371 km limits the far, almost horizontal rays to a few metres.
                const double tolerance = 5.0 + 1.0e-4 * want.end;
                EXPECT_NEAR(got.tStart, want.start, tolerance) << "y " << y << " dirY " << dirY;
                EXPECT_NEAR(got.tEnd, want.end, tolerance) << "y " << y << " dirY " << dirY;
            }
        }
    }
}

TEST(VolumetricCloudLayerTest, StraightUpAndDownSeeTheLayerThickness)
{
    const float r = 6371000.0f;
    const CloudShellSegment up = ComputeCloudShellSegment(0.0f, 1.0f, r, 1500.0f, 4000.0f, 1.0e6f);
    ASSERT_TRUE(up.hit);
    EXPECT_NEAR(up.tStart, 1500.0f, 1.0f);
    EXPECT_NEAR(up.tEnd, 4000.0f, 1.0f);

    const CloudShellSegment inside = ComputeCloudShellSegment(2500.0f, 1.0f, r, 1500.0f, 4000.0f, 1.0e6f);
    ASSERT_TRUE(inside.hit);
    EXPECT_FLOAT_EQ(inside.tStart, 0.0f);
    EXPECT_NEAR(inside.tEnd, 1500.0f, 1.0f);

    const CloudShellSegment above = ComputeCloudShellSegment(7000.0f, -1.0f, r, 1500.0f, 4000.0f, 1.0e6f);
    ASSERT_TRUE(above.hit);
    EXPECT_NEAR(above.tStart, 3000.0f, 1.0f);
    EXPECT_NEAR(above.tEnd, 5500.0f, 1.0f);
}

TEST(VolumetricCloudLayerTest, RaysThatCanNotSeeTheLayerMiss)
{
    const float r = 6371000.0f;
    // Below the layer looking at the ground, and above the layer looking away from it.
    EXPECT_FALSE(ComputeCloudShellSegment(2.0f, -0.2f, r, 1500.0f, 4000.0f, 1.0e6f).hit);
    EXPECT_FALSE(ComputeCloudShellSegment(7000.0f, 0.3f, r, 1500.0f, 4000.0f, 1.0e6f).hit);
}

TEST(VolumetricCloudLayerTest, MaxDistanceCutsTheSegment)
{
    const float r = 6371000.0f;
    const CloudShellSegment cut = ComputeCloudShellSegment(2.0f, 0.02f, r, 1500.0f, 4000.0f, 90000.0f);
    ASSERT_TRUE(cut.hit);
    EXPECT_FLOAT_EQ(cut.tEnd, 90000.0f);
    EXPECT_FALSE(ComputeCloudShellSegment(2.0f, 0.02f, r, 1500.0f, 4000.0f, 1000.0f).hit);
}

TEST(VolumetricCloudLayerTest, TheRangeReachesTheHorizonOfAGroundCamera)
{
    // The default range holds the whole 1500..3800 m layer seen along the horizon (~220 km exit, inside 250 km).
    const VolumetricCloudSettings safe = SanitizeVolumetricCloudSettings(VolumetricCloudSettings{});
    EXPECT_GE(safe.maxDistanceM, 250000.0f);
    const float r = safe.planetRadiusKm * 1000.0f;
    const CloudShellSegment horizon = ComputeCloudShellSegment(2.0f, 0.0f, r, safe.layerBaseM, safe.layerTopM, safe.maxDistanceM);
    ASSERT_TRUE(horizon.hit);
    EXPECT_NEAR(horizon.tStart, 138200.0f, 500.0f);
    EXPECT_NEAR(horizon.tEnd, 220020.0f, 500.0f);
    // One degree up the whole segment is inside the range too.
    const CloudShellSegment oneDegree = ComputeCloudShellSegment(2.0f, 0.017452f, r, safe.layerBaseM, safe.layerTopM, safe.maxDistanceM);
    ASSERT_TRUE(oneDegree.hit);
    EXPECT_NEAR(oneDegree.tStart, 66200.0f, 500.0f);
    EXPECT_NEAR(oneDegree.tEnd, 135330.0f, 500.0f);
    // The fade only starts near the end of the range.
    EXPECT_GE(safe.fadeStart, 0.9f);
}

TEST(VolumetricCloudLayerTest, TheFarWeatherTileKeepsTheWindPeriodAWholeNumberOfTiles)
{
    const VolumetricCloudSettings defaults = SanitizeVolumetricCloudSettings(VolumetricCloudSettings{});
    const double period = CloudWindPeriodM(defaults);
    ASSERT_GT(period, 0.0);
    const double farTile = static_cast<double>(std::llround(defaults.weatherScaleM * kCloudFarWeatherTileScale));
    EXPECT_DOUBLE_EQ(farTile, 70000.0);
    EXPECT_DOUBLE_EQ(std::fmod(period, farTile), 0.0);
    // A weather tile whose far tile does not divide the old period lengthens the period.
    VolumetricCloudSettings other = defaults;
    other.weatherScaleM = 20000.0f;
    other.shapeScaleM = 4000.0f;
    other.detailScaleM = 500.0f;
    const double otherFar = static_cast<double>(std::llround(other.weatherScaleM * kCloudFarWeatherTileScale));
    EXPECT_DOUBLE_EQ(std::fmod(CloudWindPeriodM(other), otherFar), 0.0);
}

TEST(VolumetricCloudParamsTest, TheFarFieldSettingsReachTheBlockAndTheHistoryFallbackIsFixed)
{
    VolumetricCloudSettings s = SanitizeVolumetricCloudSettings(VolumetricCloudSettings{});
    EXPECT_FLOAT_EQ(s.farWeatherStartM, 30000.0f);
    EXPECT_FLOAT_EQ(s.farWeatherMix, 0.5f);
    s.farWeatherStartM = 45000.0f;
    s.farWeatherMix = 0.25f;
    s.farCoverageLift = 0.2f;
    s.farAirScale = 0.4f;
    s.maxDistanceM = 400000.0f;
    VolumetricCloudFrameFacts facts;
    const VolumetricCloudParamsGpu p = MakeVolumetricCloudParams(SanitizeVolumetricCloudSettings(s), facts);
    EXPECT_FLOAT_EQ(p.skyShade2[2], 45000.0f);
    EXPECT_FLOAT_EQ(p.skyShade2[3], 0.25f);
    EXPECT_FLOAT_EQ(p.farField[0], 0.2f);
    EXPECT_FLOAT_EQ(p.farField[1], 0.4f);
    // The resolve pass keeps a fixed fallback distance, whatever the march range is.
    VolumetricCloudResolveFacts resolve;
    const VolumetricCloudResolveParamsGpu q = MakeVolumetricCloudResolveParams(SanitizeVolumetricCloudSettings(s), facts, resolve);
    EXPECT_FLOAT_EQ(q.blend[3], kCloudHistoryFallbackDistanceM);
    s.maxDistanceM = 100000.0f;
    EXPECT_FLOAT_EQ(MakeVolumetricCloudResolveParams(SanitizeVolumetricCloudSettings(s), facts, resolve).blend[3],
        kCloudHistoryFallbackDistanceM);
    // Out of range values are clamped.
    s.farAirScale = 5.0f;
    s.farCoverageLift = -1.0f;
    const VolumetricCloudSettings clamped = SanitizeVolumetricCloudSettings(s);
    EXPECT_FLOAT_EQ(clamped.farAirScale, 1.0f);
    EXPECT_FLOAT_EQ(clamped.farCoverageLift, 0.0f);
}

TEST(VolumetricCloudParamsTest, TheDetailAndStepSettingsReachTheBlockWithTheShippedDefaults)
{
    const VolumetricCloudSettings defaults = SanitizeVolumetricCloudSettings(VolumetricCloudSettings{});
    EXPECT_FLOAT_EQ(defaults.layerBaseM, 1500.0f);
    EXPECT_FLOAT_EQ(defaults.layerTopM, 3800.0f);
    EXPECT_FLOAT_EQ(defaults.detailScaleM, 500.0f);
    EXPECT_FLOAT_EQ(defaults.detailStrength, 0.72f);
    EXPECT_FLOAT_EQ(defaults.detailFadeDistM, 52000.0f);
    EXPECT_FLOAT_EQ(defaults.curlAmountM, 95.0f);
    EXPECT_FLOAT_EQ(defaults.curlScaleM, 3500.0f);
    EXPECT_FLOAT_EQ(defaults.wispiness, 0.16f);
    EXPECT_FLOAT_EQ(defaults.fineDetail, 0.15f);
    EXPECT_FLOAT_EQ(defaults.stepBaseM, 15.0f);
    EXPECT_FLOAT_EQ(defaults.stepGrowth, 0.007f);
    EXPECT_FLOAT_EQ(defaults.coarseMultiplier, 2.0f);
    EXPECT_FLOAT_EQ(defaults.clearSkip, 1.8f);
    EXPECT_FLOAT_EQ(defaults.maxSteps, 80.0f);
    EXPECT_FLOAT_EQ(defaults.sunSteps, 4.0f);
    EXPECT_FLOAT_EQ(defaults.minTransmittance, 0.012f);
    EXPECT_FLOAT_EQ(defaults.jitter, 1.0f);
    EXPECT_FLOAT_EQ(defaults.lightStride, 3.0f);
    EXPECT_FLOAT_EQ(defaults.maxStepDepth, 0.4f);
    EXPECT_FLOAT_EQ(defaults.lightDetail, 1.0f);
    EXPECT_FLOAT_EQ(defaults.inScatter, 0.48f);
    EXPECT_FLOAT_EQ(defaults.sunIntensity, 6.3f);
    EXPECT_FLOAT_EQ(defaults.ambientBottom, 0.42f);
    EXPECT_FLOAT_EQ(defaults.coverage, 0.8f);
    EXPECT_FLOAT_EQ(defaults.density, 1.70f);
    EXPECT_FLOAT_EQ(defaults.extinction, 0.048f);
    EXPECT_FLOAT_EQ(defaults.densityRamp, 0.28f);
    EXPECT_FLOAT_EQ(defaults.densityCurve, 1.10f);
    EXPECT_FLOAT_EQ(defaults.shapeLow, 0.26f);
    EXPECT_FLOAT_EQ(defaults.shapeHigh, 0.98f);
    EXPECT_FLOAT_EQ(defaults.peakMin, 0.32f);
    VolumetricCloudSettings s = defaults;
    s.curlAmountM = 120.0f;
    s.curlScaleM = 4500.0f;
    s.wispiness = 0.25f;
    s.fineDetail = 0.75f;
    s.maxStepDepth = 1.5f;
    s.inScatter = 0.3f;
    VolumetricCloudFrameFacts facts;
    const VolumetricCloudParamsGpu p = MakeVolumetricCloudParams(SanitizeVolumetricCloudSettings(s), facts);
    EXPECT_FLOAT_EQ(p.detailShape[0], 120.0f);
    EXPECT_FLOAT_EQ(p.detailShape[1], 4500.0f);
    EXPECT_FLOAT_EQ(p.detailShape[2], 0.25f);
    EXPECT_FLOAT_EQ(p.detailShape[3], 0.75f);
    EXPECT_FLOAT_EQ(p.farField[2], 1.5f);
    EXPECT_FLOAT_EQ(p.farField[3], 0.3f);
    // Out of range values are clamped.
    s.detailStrength = 9.0f;
    s.maxStepDepth = 0.0f;
    s.curlScaleM = 1.0f;
    s.inScatter = 7.0f;
    const VolumetricCloudSettings clamped = SanitizeVolumetricCloudSettings(s);
    EXPECT_FLOAT_EQ(clamped.detailStrength, 2.0f);
    EXPECT_FLOAT_EQ(clamped.maxStepDepth, 0.1f);
    EXPECT_FLOAT_EQ(clamped.curlScaleM, 200.0f);
    EXPECT_FLOAT_EQ(clamped.inScatter, 1.0f);
}

TEST(VolumetricCloudLayerTest, TheCloudsMoveAlongTheWindDirection)
{
    // The lookups read the noise at position + offset: the offset moves against the wind, so the pattern moves along it.
    double offset[2] = {};
    double step[2] = {};
    AdvanceCloudWind(offset, step, 10.0f, 0.0f, 2.0f, 0.0);
    EXPECT_NEAR(offset[0], -20.0, 1e-4);
    EXPECT_NEAR(offset[1], 0.0, 1e-4);
    EXPECT_NEAR(step[0], -20.0, 1e-4);
    AdvanceCloudWind(offset, step, 10.0f, 90.0f, 1.0f, 0.0);
    EXPECT_NEAR(offset[1], -10.0, 1e-4);
}

TEST(VolumetricCloudLayerTest, WindPeriodIsTheCommonRepeatOfTheNoiseTiles)
{
    const VolumetricCloudSettings defaults = SanitizeVolumetricCloudSettings(VolumetricCloudSettings{});
    EXPECT_DOUBLE_EQ(CloudWindPeriodM(defaults), 350000.0);
    VolumetricCloudSettings odd = defaults;
    odd.weatherScaleM = 12347.0f;
    odd.shapeScaleM = 5003.0f;
    odd.detailScaleM = 701.0f;
    EXPECT_DOUBLE_EQ(CloudWindPeriodM(odd), 0.0);
}

TEST(VolumetricCloudLayerTest, TheAtmosphereRadiusWinsOnlyWhenItLightsTheClouds)
{
    const VolumetricCloudSettings safe = SanitizeVolumetricCloudSettings(VolumetricCloudSettings{});
    VolumetricCloudFrameFacts facts;
    facts.atmospherePlanetRadiusKm = 6000.0f;
    EXPECT_FLOAT_EQ(CloudPlanetRadiusKm(safe, facts), safe.planetRadiusKm);
    facts.atmosphereLit = true;
    EXPECT_FLOAT_EQ(CloudPlanetRadiusKm(safe, facts), 6000.0f);
    EXPECT_FLOAT_EQ(MakeVolumetricCloudParams(safe, facts).eyeAndPlanetRadius[3], 6000.0f * 1000.0f);
    facts.atmospherePlanetRadiusKm = 0.0f;
    EXPECT_FLOAT_EQ(CloudPlanetRadiusKm(safe, facts), safe.planetRadiusKm);
    facts.atmospherePlanetRadiusKm = std::numeric_limits<float>::quiet_NaN();
    EXPECT_FLOAT_EQ(CloudPlanetRadiusKm(safe, facts), safe.planetRadiusKm);
}

TEST(VolumetricCloudLayerTest, WindKeepsSubMetreResolutionAfterTenHours)
{
    const VolumetricCloudSettings defaults = SanitizeVolumetricCloudSettings(VolumetricCloudSettings{});
    const double period = CloudWindPeriodM(defaults);
    double offset[2] = {};
    double step[2] = {};
    double unwrappedX = 0.0;
    for (int frame = 0; frame < 36000 * 60; ++frame) {
        AdvanceCloudWind(offset, step, 4.0f, 30.0f, 1.0f / 60.0f, period);
        unwrappedX += step[0];
    }
    EXPECT_GE(offset[0], 0.0);
    EXPECT_LT(offset[0], period);
    double expectedX = std::fmod(unwrappedX, period);
    expectedX += expectedX < 0.0 ? period : 0.0;
    EXPECT_NEAR(offset[0], expectedX, 1e-3);
    // One float step at the largest offset is far below a metre.
    const float value = static_cast<float>(period);
    EXPECT_LT(std::nextafter(value, 2.0f * value) - value, 0.1f);
}

TEST(VolumetricCloudParamsTest, TheStructureSettingsReachTheBlockWithTheShippedDefaults)
{
    const VolumetricCloudSettings defaults = SanitizeVolumetricCloudSettings(VolumetricCloudSettings{});
    EXPECT_FLOAT_EQ(defaults.shapeScaleM, 6250.0f);
    EXPECT_FLOAT_EQ(defaults.largeShapeMix, 0.20f);
    EXPECT_FLOAT_EQ(defaults.sizeVariation, 0.10f);
    EXPECT_FLOAT_EQ(defaults.shapeWorley, 0.70f);
    EXPECT_FLOAT_EQ(defaults.windShearM, 280.0f);
    EXPECT_FLOAT_EQ(defaults.bodyErosion, 0.42f);
    EXPECT_FLOAT_EQ(defaults.verticalStretch, 0.70f);
    EXPECT_FLOAT_EQ(defaults.baseVariation, 0.06f);
    EXPECT_FLOAT_EQ(defaults.baseRoundness, 0.09f);
    EXPECT_FLOAT_EQ(defaults.farSoftening, 0.7f);
    EXPECT_FLOAT_EQ(defaults.bodyThresholdMin, 0.10f);
    EXPECT_FLOAT_EQ(defaults.baseRagged, 0.04f);
    EXPECT_FLOAT_EQ(defaults.bodyWidth, 0.22f);
    VolumetricCloudSettings s = defaults;
    s.largeShapeMix = 0.35f;
    s.sizeVariation = 0.9f;
    s.shapeWorley = 0.8f;
    s.windShearM = 1000.0f;
    s.windDirection = 90.0f;
    s.bodyErosion = 0.6f;
    s.verticalStretch = 2.0f;
    s.baseVariation = 0.25f;
    s.farSoftening = 0.4f;
    s.bodyThresholdMin = 0.45f;
    s.baseRagged = 0.35f;
    s.bodyWidth = 0.3f;
    VolumetricCloudFrameFacts facts;
    const VolumetricCloudParamsGpu p = MakeVolumetricCloudParams(SanitizeVolumetricCloudSettings(s), facts);
    EXPECT_FLOAT_EQ(p.structure[0], 0.35f);
    EXPECT_FLOAT_EQ(p.structure[1], 0.9f);
    // The noise is read against the wind direction: 90 degrees leans the tops along +z, so the lookup shift is -z.
    EXPECT_NEAR(p.structure[2], 0.0f, 1e-3f);
    EXPECT_NEAR(p.structure[3], -1000.0f, 1e-3f);
    EXPECT_FLOAT_EQ(p.structure2[0], 0.6f);
    EXPECT_FLOAT_EQ(p.structure2[1], 2.0f);
    EXPECT_FLOAT_EQ(p.structure2[2], 0.25f);
    EXPECT_FLOAT_EQ(p.structure2[3], 0.4f);
    EXPECT_FLOAT_EQ(p.structure3[0], 0.8f);
    EXPECT_FLOAT_EQ(p.structure3[1], 0.45f);
    EXPECT_FLOAT_EQ(p.structure3[2], 0.35f);
    EXPECT_FLOAT_EQ(p.structure3[3], 0.3f);
    // Out of range values are clamped.
    s.verticalStretch = 0.0f;
    s.windShearM = -5.0f;
    const VolumetricCloudSettings clamped = SanitizeVolumetricCloudSettings(s);
    EXPECT_FLOAT_EQ(clamped.verticalStretch, 0.25f);
    EXPECT_FLOAT_EQ(clamped.windShearM, 0.0f);
}

TEST(VolumetricCloudParamsTest, TheLightTuningSettingsReachTheBlockWithTheShippedDefaults)
{
    const VolumetricCloudSettings defaults = SanitizeVolumetricCloudSettings(VolumetricCloudSettings{});
    EXPECT_FLOAT_EQ(defaults.ambientIntensity, 0.40f);
    EXPECT_FLOAT_EQ(defaults.ambientSaturation, 1.22f);
    EXPECT_FLOAT_EQ(defaults.msBlueShift, 0.88f);
    EXPECT_FLOAT_EQ(defaults.msDiffuse, 0.36f);
    EXPECT_FLOAT_EQ(defaults.msGlow, 0.30f);
    EXPECT_FLOAT_EQ(defaults.backwardG, -0.40f);
    EXPECT_FLOAT_EQ(defaults.backwardMix, 0.46f);
    EXPECT_FLOAT_EQ(defaults.silverIntensity, 0.09f);
    EXPECT_FLOAT_EQ(defaults.silverG, 0.85f);
    EXPECT_FLOAT_EQ(defaults.powder, 0.34f);
    EXPECT_FLOAT_EQ(defaults.sunWarmth, 0.25f);
    EXPECT_FLOAT_EQ(defaults.groundBounce, 1.5f);
    EXPECT_FLOAT_EQ(defaults.shadowDensity, 2.20f);
    VolumetricCloudSettings s = defaults;
    s.ambientSaturation = 0.4f;
    s.msDiffuse = 2.0f;
    s.msGlow = 3.0f;
    s.sunWarmth = 0.9f;
    VolumetricCloudFrameFacts facts;
    const VolumetricCloudParamsGpu p = MakeVolumetricCloudParams(SanitizeVolumetricCloudSettings(s), facts);
    EXPECT_FLOAT_EQ(p.lightTune[0], 0.4f);
    EXPECT_FLOAT_EQ(p.lightTune[1], 2.0f);
    EXPECT_FLOAT_EQ(p.lightTune[2], 3.0f);
    EXPECT_FLOAT_EQ(p.lightTune2[0], 0.9f);
    // Out of range values are clamped.
    s.ambientSaturation = 5.0f;
    s.msDiffuse = -1.0f;
    const VolumetricCloudSettings clamped = SanitizeVolumetricCloudSettings(s);
    EXPECT_FLOAT_EQ(clamped.ambientSaturation, 2.0f);
    EXPECT_FLOAT_EQ(clamped.msDiffuse, 0.0f);
}

TEST(VolumetricCloudParamsTest, TheWindPeriodHoldsAWholeNumberOfBigLobeTiles)
{
    const VolumetricCloudSettings defaults = SanitizeVolumetricCloudSettings(VolumetricCloudSettings{});
    const double period = CloudWindPeriodM(defaults);
    ASSERT_GT(period, 0.0);
    const double turretWeatherTile = static_cast<double>(std::llround(defaults.weatherScaleM / kCloudTurretWeatherScale));
    const double bigTile = static_cast<double>(std::llround(defaults.shapeScaleM * kCloudLargeShapeScale));
    const double midFloretTile = static_cast<double>(std::llround(defaults.shapeScaleM / kCloudMidFloretScale));
    const double farZoomTile = static_cast<double>(std::llround(defaults.shapeScaleM * kCloudFarZoom));
    const double detailTile = static_cast<double>(std::llround(defaults.detailScaleM));
    const double curlTile = static_cast<double>(std::llround(defaults.curlScaleM));
    EXPECT_DOUBLE_EQ(turretWeatherTile, 10000.0);
    EXPECT_DOUBLE_EQ(period / turretWeatherTile, 35.0);
    EXPECT_DOUBLE_EQ(bigTile, 12500.0);
    EXPECT_DOUBLE_EQ(midFloretTile, 5000.0);
    EXPECT_DOUBLE_EQ(farZoomTile, 17500.0);
    EXPECT_DOUBLE_EQ(detailTile, 500.0);
    EXPECT_DOUBLE_EQ(curlTile, 3500.0);
    EXPECT_NEAR(std::fmod(period, turretWeatherTile), 0.0, 1e-6);
    EXPECT_NEAR(std::fmod(period, bigTile), 0.0, 1e-6);
    EXPECT_NEAR(std::fmod(period, midFloretTile), 0.0, 1e-6);
    EXPECT_NEAR(std::fmod(period, farZoomTile), 0.0, 1e-6);
    EXPECT_NEAR(std::fmod(period, detailTile), 0.0, 1e-6);
    EXPECT_NEAR(std::fmod(period, curlTile), 0.0, 1e-6);
}

TEST(VolumetricCloudParamsTest, BlockLayoutMatchesTheShader)
{
    EXPECT_EQ(offsetof(VolumetricCloudParamsGpu, eyeAndPlanetRadius), 64u);
    EXPECT_EQ(offsetof(VolumetricCloudParamsGpu, sunDirectionAndIntensity), 80u);
    EXPECT_EQ(offsetof(VolumetricCloudParamsGpu, sunColorAndAmbient), 96u);
    EXPECT_EQ(offsetof(VolumetricCloudParamsGpu, ambientColor), 112u);
    EXPECT_EQ(offsetof(VolumetricCloudParamsGpu, layer), 128u);
    EXPECT_EQ(offsetof(VolumetricCloudParamsGpu, weather), 144u);
    EXPECT_EQ(offsetof(VolumetricCloudParamsGpu, noise), 160u);
    EXPECT_EQ(offsetof(VolumetricCloudParamsGpu, density), 176u);
    EXPECT_EQ(offsetof(VolumetricCloudParamsGpu, heightShape), 192u);
    EXPECT_EQ(offsetof(VolumetricCloudParamsGpu, march), 208u);
    EXPECT_EQ(offsetof(VolumetricCloudParamsGpu, counts), 224u);
    EXPECT_EQ(offsetof(VolumetricCloudParamsGpu, skip), 240u);
    EXPECT_EQ(offsetof(VolumetricCloudParamsGpu, lightMarch), 256u);
    EXPECT_EQ(offsetof(VolumetricCloudParamsGpu, lightPhase), 272u);
    EXPECT_EQ(offsetof(VolumetricCloudParamsGpu, lightScatter), 288u);
    EXPECT_EQ(offsetof(VolumetricCloudParamsGpu, lightMisc), 304u);
    EXPECT_EQ(offsetof(VolumetricCloudParamsGpu, drainePhase), 320u);
    EXPECT_EQ(offsetof(VolumetricCloudParamsGpu, atmosphereMode), 368u);
    EXPECT_EQ(offsetof(VolumetricCloudParamsGpu, atmosphereCamera), 384u);
    EXPECT_EQ(offsetof(VolumetricCloudParamsGpu, skyShade), 400u);
    EXPECT_EQ(offsetof(VolumetricCloudParamsGpu, skyShade2), 416u);
    EXPECT_EQ(offsetof(VolumetricCloudParamsGpu, farField), 432u);
    EXPECT_EQ(offsetof(VolumetricCloudParamsGpu, detailShape), 448u);
    EXPECT_EQ(offsetof(VolumetricCloudParamsGpu, structure), 464u);
    EXPECT_EQ(offsetof(VolumetricCloudParamsGpu, structure2), 480u);
    EXPECT_EQ(offsetof(VolumetricCloudParamsGpu, structure3), 496u);
    EXPECT_EQ(offsetof(VolumetricCloudParamsGpu, lightTune), 512u);
    EXPECT_EQ(offsetof(VolumetricCloudParamsGpu, lightTune2), 528u);
    EXPECT_EQ(offsetof(VolumetricCloudParamsGpu, shadowGrid), 544u);
    EXPECT_EQ(offsetof(VolumetricCloudParamsGpu, shadowShift), 560u);
    EXPECT_EQ(offsetof(VolumetricCloudParamsGpu, shadowTune), 576u);
    EXPECT_EQ(sizeof(VolumetricCloudParamsGpu), 592u);
}

TEST(VolumetricCloudParamsTest, TheAtmosphereModePassesTheSunUntouchedAndTheCamera)
{
    VolumetricCloudSettings s = SanitizeVolumetricCloudSettings(VolumetricCloudSettings{});
    s.airScale = 3.0f;
    s.skyAmbient = 0.5f;
    VolumetricCloudFrameFacts facts;
    facts.sunIlluminance = Vec3(3.0f, 2.0f, 1.0f);
    facts.sunTint = Vec3(0.1f, 0.1f, 0.1f);
    // A sun under the horizon still goes in as it is: the tables hold the planet's shadow.
    facts.sunDirection = Vec3(0.0f, -1.0f, 0.0f);
    facts.atmosphereLit = true;
    facts.skyIntensity = 6.0f;
    facts.atmosphereCameraKm = Vec3(0.0f, 6373.0f, 0.0f);
    const VolumetricCloudParamsGpu p = MakeVolumetricCloudParams(s, facts);
    EXPECT_FLOAT_EQ(p.sunColorAndAmbient[0], 3.0f);
    EXPECT_FLOAT_EQ(p.sunColorAndAmbient[1], 2.0f);
    EXPECT_FLOAT_EQ(p.sunColorAndAmbient[2], 1.0f);
    EXPECT_FLOAT_EQ(p.atmosphereMode[0], 1.0f);
    EXPECT_FLOAT_EQ(p.atmosphereMode[1], 6.0f);
    EXPECT_FLOAT_EQ(p.atmosphereMode[2], 3.0f);
    EXPECT_FLOAT_EQ(p.atmosphereMode[3], 0.5f);
    EXPECT_FLOAT_EQ(p.atmosphereCamera[1], 6373.0f);

    facts.atmosphereLit = false;
    EXPECT_FLOAT_EQ(MakeVolumetricCloudParams(s, facts).atmosphereMode[0], 0.0f);
}

TEST(VolumetricCloudParamsTest, TheAtmosphereIsAskedForByDefaultAndTheSettingsStaySane)
{
    const VolumetricCloudSettings defaults;
    EXPECT_FLOAT_EQ(defaults.atmosphereMode, 1.0f);
    EXPECT_FLOAT_EQ(defaults.airScale, 1.0f);
    EXPECT_FLOAT_EQ(defaults.skyAmbient, 3.5f);
    EXPECT_FLOAT_EQ(defaults.msAttenuation, 0.52f);
    EXPECT_FLOAT_EQ(defaults.msContribution, 0.62f);
    // Without the tables the facts keep the standalone look: the sun goes in tinted and the flag stays off.
    VolumetricCloudFrameFacts facts;
    facts.sunTint = Vec3(0.5f, 0.5f, 0.5f);
    facts.sunDirection = Vec3(0.0f, 1.0f, 0.0f);
    facts.sunIlluminance = Vec3(2.0f, 2.0f, 2.0f);
    const VolumetricCloudParamsGpu p = MakeVolumetricCloudParams(SanitizeVolumetricCloudSettings(defaults), facts);
    EXPECT_FLOAT_EQ(p.atmosphereMode[0], 0.0f);
    EXPECT_FLOAT_EQ(p.sunColorAndAmbient[0], 1.0f);
}

TEST(VolumetricCloudLightingTest, AirLeavesASceneThatHoldsTheSameAirLightUnchanged)
{
    // A sky pixel: scene = air light of the distance + the air's transmittance times the sky behind.
    const float airLight = 0.3f;
    const float airTransmittance = 0.6f;
    const float behind = 2.0f;
    const float scene = airLight + airTransmittance * behind;
    // No cloud: the pixel stays as it is.
    EXPECT_NEAR(CloudAerialComposite(scene, 1.0f, 0.0f, airTransmittance, airLight), scene, 1e-6f);
    // A solid cloud shows its own light through the air plus the air light, and none of the sky behind.
    EXPECT_NEAR(CloudAerialComposite(scene, 0.0f, 5.0f, airTransmittance, airLight), 5.0f * airTransmittance + airLight, 1e-6f);
    // Half-covered: the sky behind is dimmed by the cloud only, the air light stays whole.
    const float half = CloudAerialComposite(scene, 0.5f, 1.0f, airTransmittance, airLight);
    EXPECT_NEAR(half, airLight + airTransmittance * (1.0f + 0.5f * behind), 1e-5f);
}

TEST(VolumetricCloudLightingTest, WithoutAirTheCompositeIsTheOldSceneTimesTransmittancePlusLight)
{
    EXPECT_FLOAT_EQ(CloudAerialComposite(2.0f, 0.25f, 0.7f, 1.0f, 0.0f), 2.0f * 0.25f + 0.7f);
}

TEST(VolumetricCloudParamsTest, PacksTheLightingSettings)
{
    VolumetricCloudSettings s = SanitizeVolumetricCloudSettings(VolumetricCloudSettings{});
    s.sunSteps = 6.0f;
    s.phaseModel = 1.0f;
    s.hazeDistanceM = 30000.0f;
    const VolumetricCloudParamsGpu p = MakeVolumetricCloudParams(s, VolumetricCloudFrameFacts{});
    EXPECT_FLOAT_EQ(p.lightMarch[0], 6.0f);
    EXPECT_FLOAT_EQ(p.lightMisc[0], 1.0f);
    EXPECT_FLOAT_EQ(p.lightMisc[2], 30000.0f);
    EXPECT_FLOAT_EQ(p.drainePhase[2], kCloudDraineAlpha);
    EXPECT_FLOAT_EQ(p.lightPhase[0], s.forwardG);
}

TEST(VolumetricCloudParamsTest, PacksTheSettingsAndTheFrame)
{
    VolumetricCloudSettings s = SanitizeVolumetricCloudSettings(VolumetricCloudSettings{});
    VolumetricCloudFrameFacts facts;
    facts.eye = Vec3(1.0f, 2.0f, 3.0f);
    facts.sunDirection = Vec3(0.0f, 1.0f, 0.0f);
    facts.sunIlluminance = Vec3(3.0f, 3.0f, 3.0f);
    facts.windOffset[0] = 5.0f;
    facts.windOffset[1] = -6.0f;
    facts.frameIndex = 77;
    const VolumetricCloudParamsGpu p = MakeVolumetricCloudParams(s, facts);
    EXPECT_FLOAT_EQ(p.eyeAndPlanetRadius[0], 1.0f);
    EXPECT_FLOAT_EQ(p.eyeAndPlanetRadius[1], 2.0f);
    EXPECT_FLOAT_EQ(p.eyeAndPlanetRadius[3], 6371000.0f);
    EXPECT_FLOAT_EQ(p.layer[0], s.layerBaseM);
    EXPECT_FLOAT_EQ(p.layer[1], s.layerTopM);
    EXPECT_FLOAT_EQ(p.weather[1], 5.0f);
    EXPECT_FLOAT_EQ(p.weather[2], -6.0f);
    EXPECT_FLOAT_EQ(p.weather[3], s.coverage);
    EXPECT_FLOAT_EQ(p.sunColorAndAmbient[0], 3.0f);
    EXPECT_EQ(p.counts[0], static_cast<std::uint32_t>(s.maxSteps));
    EXPECT_EQ(p.counts[1], 77u);
}

TEST(VolumetricCloudParamsTest, ASunBelowTheHorizonGivesNoSunLight)
{
    VolumetricCloudFrameFacts facts;
    facts.sunDirection = Vec3(0.0f, -1.0f, 0.0f);
    const VolumetricCloudParamsGpu p = MakeVolumetricCloudParams(VolumetricCloudSettings{}, facts);
    EXPECT_FLOAT_EQ(p.sunColorAndAmbient[0], 0.0f);
    EXPECT_GT(p.ambientColor[2], 0.0f);
}

// Integral of a phase function over the sphere: 2 pi times the integral over cos(theta).
template <typename Phase>
double SphereIntegral(Phase&& phase)
{
    constexpr int kSamples = 400000;
    double sum = 0.0;
    for (int i = 0; i < kSamples; ++i) {
        const double c = -1.0 + 2.0 * (i + 0.5) / kSamples;
        sum += phase(static_cast<float>(c));
    }
    return sum * (2.0 / kSamples) * 2.0 * 3.14159265358979;
}

TEST(VolumetricCloudLightingTest, PhaseFunctionsIntegrateToOne)
{
    for (const float g : { -0.5f, -0.2f, 0.0f, 0.3f, 0.8f }) {
        EXPECT_NEAR(SphereIntegral([&](float c) { return CloudHenyeyGreenstein(c, g); }), 1.0, 2e-3) << g;
        EXPECT_NEAR(SphereIntegral([&](float c) { return CloudDraine(c, g, kCloudDraineAlpha); }), 1.0, 2e-3) << g;
    }
    // A very forward lobe needs a finer grid than the others, so it gets a looser bound.
    EXPECT_NEAR(SphereIntegral([&](float c) { return CloudHenyeyGreenstein(c, kCloudDraineHgG); }), 1.0, 2e-2);
}

TEST(VolumetricCloudLightingTest, MixedPhasesIntegrateToOneAtEveryOctave)
{
    VolumetricCloudSettings s = SanitizeVolumetricCloudSettings(VolumetricCloudSettings{});
    for (const float model : { 0.0f, 1.0f }) {
        s.phaseModel = model;
        for (const float eccentricity : { 1.0f, 0.5f, 0.25f, 0.0f }) {
            const double integral = SphereIntegral([&](float c) { return CloudPhase(s, c, eccentricity); });
            EXPECT_NEAR(integral, 1.0, 2e-2) << "model " << model << " ecc " << eccentricity;
        }
    }
}

TEST(VolumetricCloudLightingTest, ForwardLobeFacesTheSunAndIsotropicAtZeroEccentricity)
{
    const VolumetricCloudSettings s = SanitizeVolumetricCloudSettings(VolumetricCloudSettings{});
    EXPECT_GT(CloudPhase(s, 1.0f, 1.0f), CloudPhase(s, -1.0f, 1.0f));
    EXPECT_NEAR(CloudPhase(s, 0.7f, 0.0f), 1.0f / (4.0f * 3.14159265f), 1e-5f);
}

TEST(VolumetricCloudLightingTest, BeerFallsMonotonicallyFromOne)
{
    EXPECT_FLOAT_EQ(CloudBeer(0.0f), 1.0f);
    EXPECT_FLOAT_EQ(CloudBeer(-3.0f), 1.0f);
    EXPECT_NEAR(CloudBeer(1.0f), std::exp(-1.0f), 1e-6f);
    EXPECT_LT(CloudBeer(2.0f), CloudBeer(1.0f));
}

TEST(VolumetricCloudLightingTest, SunScatterFallsWithOpticalDepthAndThinCloudsShowTheSilverEdge)
{
    VolumetricCloudSettings s = SanitizeVolumetricCloudSettings(VolumetricCloudSettings{});
    EXPECT_GT(CloudSunScatter(s, 0.0f, 0.1f), CloudSunScatter(s, 0.0f, 3.0f));
    const float withSilver = CloudSunScatter(s, 0.99f, 0.05f);
    s.silverIntensity = 0.0f;
    EXPECT_GT(withSilver, CloudSunScatter(s, 0.99f, 0.05f));
    // Away from the sun the silver lobe adds almost nothing.
    s = SanitizeVolumetricCloudSettings(VolumetricCloudSettings{});
    const float plain = CloudSunScatter(s, -0.5f, 0.05f);
    s.silverIntensity = 0.0f;
    EXPECT_NEAR(plain, CloudSunScatter(s, -0.5f, 0.05f), 1e-3f);
}

TEST(VolumetricCloudLightingTest, MultipleScatteringKeepsDeepCloudsFromGoingBlack)
{
    VolumetricCloudSettings one = SanitizeVolumetricCloudSettings(VolumetricCloudSettings{});
    one.msOctaves = 1.0f;
    one.silverIntensity = 0.0f;
    VolumetricCloudSettings many = one;
    many.msOctaves = 4.0f;
    EXPECT_GT(CloudSunScatter(many, -0.3f, 6.0f), CloudSunScatter(one, -0.3f, 6.0f));
}

TEST(VolumetricCloudLightingTest, StepIntegrationConservesEnergyForAnyStepSize)
{
    // A white furnace: the source equals 1, so light + transmittance must come to exactly 1.
    const float extinction = 0.02f;
    const float length = 600.0f;
    const float exact = 1.0f - std::exp(-extinction * length);
    for (const int steps : { 1, 3, 17, 200 }) {
        float light = 0.0f;
        float transmittance = 1.0f;
        for (int i = 0; i < steps; ++i) {
            CloudAccumulateStep(light, transmittance, 1.0f, CloudStepOpacity(extinction, length / steps));
        }
        EXPECT_NEAR(light + transmittance, 1.0f, 1e-5f) << steps;
        EXPECT_NEAR(light, exact, 1e-5f) << steps;
    }
}

TEST(VolumetricCloudLightingTest, StepOpacityStaysInsideZeroAndOne)
{
    EXPECT_FLOAT_EQ(CloudStepOpacity(0.0f, 100.0f), 0.0f);
    EXPECT_FLOAT_EQ(CloudStepOpacity(-1.0f, 100.0f), 0.0f);
    EXPECT_LE(CloudStepOpacity(1000.0f, 1000.0f), 1.0f);
    EXPECT_GT(CloudStepOpacity(0.01f, 50.0f), 0.0f);
}

TEST(VolumetricCloudLightingTest, EveryLightingSettingIsReachableAndSane)
{
    const VolumetricCloudSettings d;
    EXPECT_GE(d.sunSteps, 1.0f);
    EXPECT_GE(d.msOctaves, 1.0f);
    EXPECT_LT(d.msAttenuation, 1.0f + 1e-6f);
    EXPECT_LT(d.backwardG, 0.0f);
    EXPECT_FLOAT_EQ(d.forwardG, 0.65f);
}

const ReflectedDescriptorBinding* Find(const ShaderReflectionResult& result, const std::string& name)
{
    const auto it = std::find_if(result.descriptorBindings.begin(), result.descriptorBindings.end(),
        [&](const ReflectedDescriptorBinding& binding) { return binding.name == name; });
    return it == result.descriptorBindings.end() ? nullptr : &*it;
}

ShaderReflectionResult ReflectCompute(const char* path)
{
    const ShaderStageFile stage{ VK_SHADER_STAGE_COMPUTE_BIT, path };
    return ReflectShader(std::span<const ShaderStageFile>(&stage, 1));
}

void ExpectViewerShader(const char* path, const char* sampler)
{
    const ShaderReflectionResult result = ReflectCompute(path);
    const ReflectedDescriptorBinding* source = Find(result, sampler);
    ASSERT_NE(source, nullptr) << sampler;
    EXPECT_EQ(source->type, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
    const ReflectedDescriptorBinding* output = Find(result, "destinationImage");
    ASSERT_NE(output, nullptr);
    EXPECT_EQ(output->type, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE);
    EXPECT_EQ(output->access, ReflectedAccess::WriteOnly);
    EXPECT_EQ(result.descriptorBindings.size(), 2u);
    ASSERT_TRUE(result.hasPushConstantRange);
    EXPECT_EQ(result.pushConstantRange.offset, 0u);
    EXPECT_EQ(result.pushConstantRange.size, sizeof(VolumetricCloudSlicePush));
}

TEST(VolumetricCloudShaderNamesTest, MarchExposesBoundNames)
{
    const ShaderReflectionResult result = ReflectCompute(GTE_TEST_CLOUD_MARCH_SPV_PATH);
    const ReflectedDescriptorBinding* params = Find(result, "params");
    ASSERT_NE(params, nullptr);
    EXPECT_EQ(params->type, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
    EXPECT_EQ(params->access, ReflectedAccess::ReadOnly);
    for (const char* sampler : { "sourceDepth", "shapeVolume", "detailVolume", "weatherMap", "blueNoise", "curlMap",
             "transmittanceLut", "multiScatteringLut", "skyViewLut" }) {
        const ReflectedDescriptorBinding* binding = Find(result, sampler);
        ASSERT_NE(binding, nullptr) << sampler;
        EXPECT_EQ(binding->type, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER) << sampler;
    }
    const ReflectedDescriptorBinding* atmosphere = Find(result, "atmosphere");
    ASSERT_NE(atmosphere, nullptr);
    EXPECT_EQ(atmosphere->type, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
    EXPECT_EQ(atmosphere->access, ReflectedAccess::ReadOnly);
    for (const char* output : { "lowColorOut", "lowAuxOut" }) {
        const ReflectedDescriptorBinding* binding = Find(result, output);
        ASSERT_NE(binding, nullptr) << output;
        EXPECT_EQ(binding->type, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE) << output;
        EXPECT_EQ(binding->access, ReflectedAccess::WriteOnly) << output;
    }
    EXPECT_EQ(result.descriptorBindings.size(), 14u);
    EXPECT_FALSE(result.hasPushConstantRange);
}

TEST(VolumetricCloudShaderNamesTest, ResolveExposesBoundNames)
{
    const ShaderReflectionResult result = ReflectCompute(GTE_TEST_CLOUD_RESOLVE_SPV_PATH);
    const ReflectedDescriptorBinding* params = Find(result, "params");
    ASSERT_NE(params, nullptr);
    EXPECT_EQ(params->type, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
    EXPECT_EQ(params->access, ReflectedAccess::ReadOnly);
    for (const char* sampler : { "sourceColor", "sourceDepth", "lowColor", "lowAux", "historyColor", "historyAux",
             "sunShafts" }) {
        const ReflectedDescriptorBinding* binding = Find(result, sampler);
        ASSERT_NE(binding, nullptr) << sampler;
        EXPECT_EQ(binding->type, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER) << sampler;
    }
    for (const char* output : { "historyColorOut", "historyAuxOut", "destinationImage" }) {
        const ReflectedDescriptorBinding* binding = Find(result, output);
        ASSERT_NE(binding, nullptr) << output;
        EXPECT_EQ(binding->type, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE) << output;
        EXPECT_EQ(binding->access, ReflectedAccess::WriteOnly) << output;
    }
    EXPECT_EQ(result.descriptorBindings.size(), 11u);
    EXPECT_FALSE(result.hasPushConstantRange);
}

TEST(VolumetricCloudShaderNamesTest, SunShaftsExposesBoundNames)
{
    const ShaderReflectionResult result = ReflectCompute(GTE_TEST_CLOUD_SUN_SHAFTS_SPV_PATH);
    const ReflectedDescriptorBinding* params = Find(result, "params");
    ASSERT_NE(params, nullptr);
    EXPECT_EQ(params->type, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
    EXPECT_EQ(params->access, ReflectedAccess::ReadOnly);
    for (const char* sampler : { "historyColor", "sceneDepth" }) {
        const ReflectedDescriptorBinding* binding = Find(result, sampler);
        ASSERT_NE(binding, nullptr) << sampler;
        EXPECT_EQ(binding->type, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER) << sampler;
    }
    const ReflectedDescriptorBinding* out = Find(result, "shaftsOut");
    ASSERT_NE(out, nullptr);
    EXPECT_EQ(out->type, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE);
    EXPECT_EQ(out->access, ReflectedAccess::WriteOnly);
    EXPECT_EQ(result.descriptorBindings.size(), 4u);
    EXPECT_FALSE(result.hasPushConstantRange);
}

TEST(VolumetricCloudTemporalTest, ResolveBlockLayoutMatchesTheShader)
{
    static_assert(sizeof(VolumetricCloudResolveParamsGpu) == 256);
    EXPECT_EQ(offsetof(VolumetricCloudResolveParamsGpu, invViewProjection), 0u);
    EXPECT_EQ(offsetof(VolumetricCloudResolveParamsGpu, prevViewProjection), 64u);
    EXPECT_EQ(offsetof(VolumetricCloudResolveParamsGpu, eye), 128u);
    EXPECT_EQ(offsetof(VolumetricCloudResolveParamsGpu, windDelta), 144u);
    EXPECT_EQ(offsetof(VolumetricCloudResolveParamsGpu, blend), 160u);
    EXPECT_EQ(offsetof(VolumetricCloudResolveParamsGpu, motion), 176u);
    EXPECT_EQ(offsetof(VolumetricCloudResolveParamsGpu, counts), 192u);
    EXPECT_EQ(offsetof(VolumetricCloudResolveParamsGpu, sizes), 208u);
    EXPECT_EQ(offsetof(VolumetricCloudResolveParamsGpu, sunRay), 224u);
    EXPECT_EQ(offsetof(VolumetricCloudResolveParamsGpu, sunRayColor), 240u);
    EXPECT_EQ(sizeof(VolumetricCloudResolveParamsGpu), 256u);
    EXPECT_EQ(offsetof(VolumetricCloudParamsGpu, lowRes), 336u);
    EXPECT_EQ(offsetof(VolumetricCloudParamsGpu, fullSize), 352u);
}

// A view ray rebuilt from the clip space of a pixel and projected back must land on the same pixel, also
// when the camera stands thousands of metres from the origin.
TEST(VolumetricCloudTemporalTest, RelativeCameraRoundTripsRaysFarFromTheOrigin)
{
    const Vec3 eye(2000.0f, 2.0f, -5000.0f);
    const Mat4 view = Mat4::LookAtLH(eye, eye + Vec3(0.2f, 0.05f, 1.0f), Vec3::Up());
    const Mat4 projection = Mat4::PerspectiveFovLH_ZO(1.0471976f, 884.0f / 432.0f, 0.1f, 1000.0f);
    const EyeRelativeCamera camera = MakeEyeRelativeCamera(projection * view, eye);
    ASSERT_TRUE(camera.valid);
    const float width = 884.0f;
    const float height = 432.0f;
    double worst = 0.0;
    for (float px = 5.5f; px < width; px += 97.0f) {
        for (float py = 5.5f; py < height; py += 101.0f) {
            const float ndcX = px / width * 2.0f - 1.0f;
            const float ndcY = py / height * 2.0f - 1.0f;
            const Vec4 nearPoint = camera.inverse * Vec4(ndcX, ndcY, 0.0f, 1.0f);
            const Vec4 farPoint = camera.inverse * Vec4(ndcX, ndcY, 1.0f, 1.0f);
            const Vec3 ray = Normalize(farPoint.XYZ() / farPoint.w - nearPoint.XYZ() / nearPoint.w);
            for (const float distance : { 3000.0f, 15000.0f, 45000.0f }) {
                const Vec4 clip = camera.viewProjection * Vec4(ray.x * distance, ray.y * distance, ray.z * distance, 1.0f);
                const double x = (clip.x / clip.w * 0.5f + 0.5f) * width - px;
                const double y = (clip.y / clip.w * 0.5f + 0.5f) * height - py;
                worst = std::max(worst, std::max(std::abs(x), std::abs(y)));
            }
        }
    }
    EXPECT_LT(worst, 0.02);
}

TEST(VolumetricCloudTemporalTest, DivisorRoundsToOneThroughSix)
{
    VolumetricCloudSettings s;
    const auto divisorOf = [&](float value) {
        s.resolutionDivisor = value;
        return CloudResolutionDivisor(SanitizeVolumetricCloudSettings(s), 1280);
    };
    EXPECT_EQ(divisorOf(1.0f), 1u);
    EXPECT_EQ(divisorOf(1.4f), 1u);
    EXPECT_EQ(divisorOf(2.0f), 2u);
    EXPECT_EQ(divisorOf(2.4f), 2u);
    EXPECT_EQ(divisorOf(2.6f), 3u);
    EXPECT_EQ(divisorOf(3.0f), 3u);
    EXPECT_EQ(divisorOf(3.6f), 4u);
    EXPECT_EQ(divisorOf(4.0f), 4u);
    EXPECT_EQ(divisorOf(5.0f), 5u);
    EXPECT_EQ(divisorOf(6.0f), 6u);
    EXPECT_EQ(divisorOf(99.0f), 6u);
}

TEST(VolumetricCloudTemporalTest, TheAutomaticDivisorKeepsTheMarchGridAboutSixHundredThirtyTwoTexelsWide)
{
    const VolumetricCloudSettings automatic = SanitizeVolumetricCloudSettings(VolumetricCloudSettings{});
    EXPECT_FLOAT_EQ(automatic.resolutionDivisor, 0.0f);
    EXPECT_FLOAT_EQ(kCloudAutoMarchTexels, 632.0f);
    EXPECT_EQ(CloudResolutionDivisor(automatic, 0), 4u);
    EXPECT_EQ(CloudResolutionDivisor(automatic, 632), 1u);
    EXPECT_EQ(CloudResolutionDivisor(automatic, 1264), 2u);
    EXPECT_EQ(CloudResolutionDivisor(automatic, 1280), 2u);
    EXPECT_EQ(CloudResolutionDivisor(automatic, 1920), 3u);
    EXPECT_EQ(CloudResolutionDivisor(automatic, 2560), 4u);
    EXPECT_EQ(CloudResolutionDivisor(automatic, 3840), 6u);
    EXPECT_EQ(CloudResolutionDivisor(automatic, 5120), 6u);
    VolumetricCloudSettings fixed = automatic;
    fixed.resolutionDivisor = 3.0f;
    EXPECT_EQ(CloudResolutionDivisor(fixed, 1920), 3u);
}

TEST(VolumetricCloudTemporalTest, BayerOffsetsVisitEveryCellOncePerCycle)
{
    for (const std::uint32_t divisor : { 2u, 3u, 4u, 5u, 6u }) {
        const std::uint32_t cells = divisor * divisor;
        std::set<std::pair<std::uint32_t, std::uint32_t>> seen;
        for (std::uint32_t i = 0; i < cells; ++i) {
            const CloudSampleOffset o = CloudBayerOffset(1000 + i, divisor);
            EXPECT_LT(o.x, divisor);
            EXPECT_LT(o.y, divisor);
            seen.insert({ o.x, o.y });
        }
        EXPECT_EQ(seen.size(), cells) << divisor;
        const CloudSampleOffset a = CloudBayerOffset(5, divisor);
        const CloudSampleOffset b = CloudBayerOffset(5 + cells, divisor);
        EXPECT_EQ(a.x, b.x);
        EXPECT_EQ(a.y, b.y);
    }
    const CloudSampleOffset one = CloudBayerOffset(7, 1);
    EXPECT_EQ(one.x, 0u);
    EXPECT_EQ(one.y, 0u);
}

TEST(VolumetricCloudTemporalTest, ConsecutiveBayerSamplesMostlyLandFarApart)
{
    // Frame k and k+1 never march the same pixel, and most pairs are not even neighbours.
    int far = 0;
    for (std::uint32_t k = 0; k < 16; ++k) {
        const CloudSampleOffset a = CloudBayerOffset(k, 4);
        const CloudSampleOffset b = CloudBayerOffset(k + 1, 4);
        const int dx = std::abs(static_cast<int>(a.x) - static_cast<int>(b.x));
        const int dy = std::abs(static_cast<int>(a.y) - static_cast<int>(b.y));
        EXPECT_GE(dx + dy, 1) << k;
        far += std::max(dx, dy) >= 2 ? 1 : 0;
    }
    EXPECT_GE(far, 12);
    // Divisors 5 and 6 never march the same pixel on two consecutive frames and keep them apart too.
    for (const std::uint32_t divisor : { 5u, 6u }) {
        int apart = 0;
        for (std::uint32_t k = 0; k < divisor * divisor; ++k) {
            const CloudSampleOffset a = CloudBayerOffset(k, divisor);
            const CloudSampleOffset b = CloudBayerOffset(k + 1, divisor);
            const int dx = std::abs(static_cast<int>(a.x) - static_cast<int>(b.x));
            const int dy = std::abs(static_cast<int>(a.y) - static_cast<int>(b.y));
            EXPECT_GE(dx + dy, 1) << divisor << " " << k;
            apart += std::max(dx, dy) >= 2 ? 1 : 0;
        }
        EXPECT_GE(apart, static_cast<int>(divisor * divisor * 2 / 3)) << divisor;
    }
}

TEST(VolumetricCloudTemporalTest, TapIsUsableOnlyWhereItSawTheSameSurface)
{
    const float sky = kCloudNoSurfaceKm;
    // A march that saw the whole sky serves a sky pixel, with or without cloud.
    EXPECT_TRUE(CloudTapUsable(sky, 0.0f, 1.0f, sky));
    EXPECT_TRUE(CloudTapUsable(sky, 3.0f, 0.4f, sky));
    // A march that stopped at a mountain 0.5 km away can not stand in for a sky pixel next to it.
    EXPECT_FALSE(CloudTapUsable(0.5f, 0.0f, 1.0f, sky));
    EXPECT_FALSE(CloudTapUsable(0.5f, 0.3f, 0.5f, sky));
    // Open-sky taps are rejected on opaque foreground and distant mountain geometry to prevent ridge bleed.
    EXPECT_FALSE(CloudTapUsable(sky, 3.0f, 0.4f, 0.5f));
    EXPECT_FALSE(CloudTapUsable(sky, 0.0f, 1.0f, 0.5f));
    EXPECT_FALSE(CloudTapUsable(sky, 5.0f, 0.4f, 8.0f));
    // Taps that stopped on the same mountain slope remain usable when clear or holding foreground cloud.
    EXPECT_TRUE(CloudTapUsable(8.2f, 0.0f, 1.0f, 8.0f));
    EXPECT_TRUE(CloudTapUsable(8.2f, 5.0f, 0.4f, 8.0f));
    EXPECT_TRUE(CloudTapUsable(0.5f, 0.3f, 0.5f, 0.5f));
    // A neighbouring surface at nearly the same distance is accepted.
    EXPECT_TRUE(CloudTapUsable(0.5f, 0.3f, 0.5f, 0.505f));
}

TEST(VolumetricCloudTemporalTest, ResolveParamsCarryTheTemporalSettings)
{
    VolumetricCloudSettings s = SanitizeVolumetricCloudSettings(VolumetricCloudSettings{});
    s.resolutionDivisor = 4.0f;
    VolumetricCloudFrameFacts facts;
    facts.frameIndex = 3;
    facts.viewWidth = 884;
    facts.viewHeight = 432;
    VolumetricCloudResolveFacts resolve;
    resolve.historyValid = true;
    resolve.lowWidth = 221;
    resolve.lowHeight = 108;
    resolve.windDelta[0] = 2.0f;
    resolve.windDelta[1] = 3.0f;
    const VolumetricCloudResolveParamsGpu p = MakeVolumetricCloudResolveParams(s, facts, resolve);
    const CloudSampleOffset o = CloudBayerOffset(3, 4);
    EXPECT_EQ(p.counts[0], 4u);
    EXPECT_EQ(p.counts[1], o.x);
    EXPECT_EQ(p.counts[2], o.y);
    EXPECT_EQ(p.counts[3], 1u);
    EXPECT_EQ(p.sizes[0], 884u);
    EXPECT_EQ(p.sizes[3], 108u);
    EXPECT_FLOAT_EQ(p.windDelta[0], 2.0f);
    EXPECT_FLOAT_EQ(p.windDelta[2], 3.0f);
    EXPECT_FLOAT_EQ(p.blend[0], s.historyBlend);
    EXPECT_FLOAT_EQ(p.blend[1], s.freshBlend);
    EXPECT_FLOAT_EQ(p.motion[0], s.motionBlend);
    EXPECT_FLOAT_EQ(p.motion[1], s.historySharp);
    EXPECT_FLOAT_EQ(p.motion[2], 1.0f);

    // Without accumulation the new samples replace the picture and no history is read.
    s.temporal = 0.0f;
    const VolumetricCloudResolveParamsGpu off = MakeVolumetricCloudResolveParams(s, facts, resolve);
    EXPECT_EQ(off.counts[3], 0u);
    EXPECT_FLOAT_EQ(off.blend[0], 1.0f);
    EXPECT_FLOAT_EQ(off.blend[1], 1.0f);
    EXPECT_FLOAT_EQ(off.motion[1], 0.0f);
    EXPECT_FLOAT_EQ(off.motion[2], 0.0f);

    // The march block carries the same offset as the resolve block.
    const VolumetricCloudParamsGpu march = MakeVolumetricCloudParams(s, facts);
    EXPECT_EQ(march.lowRes[0], 4u);
    EXPECT_EQ(march.lowRes[1], o.x);
    EXPECT_EQ(march.lowRes[2], o.y);
    EXPECT_EQ(march.fullSize[0], 884u);
    // The sub-pixel jitter travels in percent of a pixel.
    EXPECT_EQ(march.lowRes[3], 35u);
    s.subpixelJitter = 0.5f;
    EXPECT_EQ(MakeVolumetricCloudParams(s, facts).lowRes[3], 50u);
    s.subpixelJitter = 0.0f;
    EXPECT_EQ(MakeVolumetricCloudParams(s, facts).lowRes[3], 0u);
}

TEST(VolumetricCloudTemporalTest, TheTemporalDefaultsAreTheSampleAveragingOnes)
{
    const VolumetricCloudSettings d = SanitizeVolumetricCloudSettings(VolumetricCloudSettings{});
    EXPECT_FLOAT_EQ(d.freshBlend, 0.06f);
    EXPECT_FLOAT_EQ(d.motionBlend, 0.10f);
    EXPECT_FLOAT_EQ(d.clampExpand, 0.85f);
    EXPECT_FLOAT_EQ(d.jitter, 1.0f);
    EXPECT_FLOAT_EQ(d.historyBlend, 0.0f);
    EXPECT_FLOAT_EQ(d.historySharp, 1.0f);
    EXPECT_FLOAT_EQ(d.adaptiveBlend, 1.0f);
    EXPECT_FLOAT_EQ(d.subpixelJitter, 0.35f);
    EXPECT_FLOAT_EQ(d.resolutionDivisor, 0.0f);
    // Divisor 3 walks a 9 frame cycle: the own-sample index advances every 9 frames.
    EXPECT_EQ(CloudOwnSampleIndex(9 * 5, 3), 5u);
    EXPECT_EQ(CloudOwnSampleIndex(9 * 5 + 8, 3), 5u);
}

TEST(VolumetricCloudShaderNamesTest, Slice3dExposesBoundNamesAndPushBlock)
{
    ExpectViewerShader(GTE_TEST_CLOUD_SLICE3D_SPV_PATH, "noiseVolume");
}

TEST(VolumetricCloudShaderNamesTest, Slice2dExposesBoundNamesAndPushBlock)
{
    ExpectViewerShader(GTE_TEST_CLOUD_SLICE2D_SPV_PATH, "noiseTexture");
}

TEST(VolumetricCloudPresetTest, PresetsChangeOnlyCoverageAndDensity)
{
    VolumetricCloudSettings base;
    base.enabled = true;
    base.layerBaseM = 300.0f;
    base.layerTopM = 1800.0f;
    base.resolutionDivisor = 2.0f;
    base.sunIntensity = 5.0f;
    base.coverage = 0.1f;
    base.density = 3.0f;
    float previousCoverage = -1.0f;
    for (std::size_t i = 0; i < kVolumetricCloudPresetNames.size(); ++i) {
        const VolumetricCloudSettings out = ApplyVolumetricCloudPreset(base, static_cast<VolumetricCloudPreset>(i));
        EXPECT_TRUE(out.enabled) << kVolumetricCloudPresetNames[i];
        EXPECT_FLOAT_EQ(out.layerBaseM, 300.0f);
        EXPECT_FLOAT_EQ(out.layerTopM, 1800.0f);
        EXPECT_FLOAT_EQ(out.resolutionDivisor, 2.0f);
        EXPECT_FLOAT_EQ(out.sunIntensity, 5.0f);
        EXPECT_GT(out.coverage, previousCoverage) << "coverage must grow from clear to overcast";
        previousCoverage = out.coverage;
        const VolumetricCloudSettings again = SanitizeVolumetricCloudSettings(out);
        EXPECT_FLOAT_EQ(again.coverage, out.coverage);
        EXPECT_FLOAT_EQ(again.density, out.density);
    }
}

TEST(VolumetricCloudPresetTest, ScatteredIsTheDefaultLookAndNamesAreUnique)
{
    VolumetricCloudSettings base;
    base.coverage = 0.9f;
    const VolumetricCloudSettings scattered = ApplyVolumetricCloudPreset(base, VolumetricCloudPreset::Scattered);
    const VolumetricCloudSettings defaults;
    EXPECT_FLOAT_EQ(scattered.coverage, defaults.coverage);
    EXPECT_FLOAT_EQ(scattered.density, defaults.density);
    const std::set<std::string> names(kVolumetricCloudPresetNames.begin(), kVolumetricCloudPresetNames.end());
    EXPECT_EQ(names.size(), kVolumetricCloudPresetNames.size());
    EXPECT_EQ(static_cast<std::size_t>(VolumetricCloudPreset::Overcast) + 1, kVolumetricCloudPresetNames.size());
}

// The history needs a camera basis. The engine's own inverse is too coarse far from the origin and can
// fail the square-pixel test, so the basis comes from the eye-relative inverse.
TEST(VolumetricCloudTemporalTest, RelativeBasisStaysValidFarFromTheOrigin)
{
    const Vec3 eye(2000.0f, 2.0f, -5000.0f);
    const Vec3 forward = Normalize(Vec3(0.2f, 0.05f, 1.0f));
    const Mat4 view = Mat4::LookAtLH(eye, eye + forward, Vec3::Up());
    for (const Extent2D extent : { Extent2D{ 703, 408 }, Extent2D{ 923, 588 }, Extent2D{ 1343, 688 }, Extent2D{ 223, 188 },
             Extent2D{ 884, 432 } }) {
        const float aspect = static_cast<float>(extent.width) / static_cast<float>(extent.height);
        const Mat4 projection = Mat4::PerspectiveFovLH_ZO(1.0471976f, aspect, 0.1f, 1000.0f);
        const EyeRelativeCamera camera = MakeEyeRelativeCamera(projection * view, eye);
        ASSERT_TRUE(camera.valid);
        const fx::CameraBasis basis = fx::DeriveCameraBasis(Vec3::Zero(), camera.inverse, extent);
        EXPECT_TRUE(basis.valid) << extent.width << "x" << extent.height;
        EXPECT_GT(Dot(basis.forward, forward), 0.999f) << extent.width << "x" << extent.height;
    }
}

struct GlslMember {
    std::string name;
    std::uint32_t offset = 0;
};

std::string ReadShaderText(const char* file)
{
    std::ifstream in(std::filesystem::path(GTE_TEST_CLOUD_SHADER_DIR) / file, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

// Members of `buffer <block> { ... } params;` with their std430 offsets; every member is a mat4 or a 4-vector.
std::vector<GlslMember> ParseBlock(const std::string& source, const std::string& block)
{
    std::vector<GlslMember> members;
    const std::size_t head = source.find("buffer " + block);
    const std::size_t open = source.find('{', head);
    const std::size_t close = source.find("} params;", open);
    if (head == std::string::npos || open == std::string::npos || close == std::string::npos) {
        return members;
    }
    std::istringstream lines(source.substr(open + 1, close - open - 1));
    std::string line;
    std::uint32_t offset = 0;
    while (std::getline(lines, line)) {
        line = line.substr(0, line.find("//"));
        std::istringstream words(line);
        std::string type;
        std::string name;
        if (!(words >> type >> name) || name.empty() || name.back() != ';') {
            continue;
        }
        name.pop_back();
        members.push_back({ name, offset });
        offset += type == "mat4" ? 64u : 16u;
    }
    return members;
}

void ExpectBlock(const std::vector<GlslMember>& glsl, const std::vector<std::pair<const char*, std::size_t>>& cpp)
{
    ASSERT_EQ(glsl.size(), cpp.size());
    for (std::size_t i = 0; i < cpp.size(); ++i) {
        EXPECT_EQ(glsl[i].name, cpp[i].first);
        EXPECT_EQ(glsl[i].offset, cpp[i].second) << cpp[i].first;
    }
}

TEST(VolumetricCloudParamsTest, GlslBlocksHaveTheCppMembersAtTheCppOffsets)
{
#define GTE_CLOUD_MEMBER(type, member) { #member, offsetof(type, member) }
    ExpectBlock(ParseBlock(ReadShaderText("VolumetricCloud.comp"), "VolumetricCloudParams"),
        { GTE_CLOUD_MEMBER(VolumetricCloudParamsGpu, invViewProjection),
            GTE_CLOUD_MEMBER(VolumetricCloudParamsGpu, eyeAndPlanetRadius),
            GTE_CLOUD_MEMBER(VolumetricCloudParamsGpu, sunDirectionAndIntensity),
            GTE_CLOUD_MEMBER(VolumetricCloudParamsGpu, sunColorAndAmbient),
            GTE_CLOUD_MEMBER(VolumetricCloudParamsGpu, ambientColor), GTE_CLOUD_MEMBER(VolumetricCloudParamsGpu, layer),
            GTE_CLOUD_MEMBER(VolumetricCloudParamsGpu, weather), GTE_CLOUD_MEMBER(VolumetricCloudParamsGpu, noise),
            GTE_CLOUD_MEMBER(VolumetricCloudParamsGpu, density), GTE_CLOUD_MEMBER(VolumetricCloudParamsGpu, heightShape),
            GTE_CLOUD_MEMBER(VolumetricCloudParamsGpu, march), GTE_CLOUD_MEMBER(VolumetricCloudParamsGpu, counts),
            GTE_CLOUD_MEMBER(VolumetricCloudParamsGpu, skip), GTE_CLOUD_MEMBER(VolumetricCloudParamsGpu, lightMarch),
            GTE_CLOUD_MEMBER(VolumetricCloudParamsGpu, lightPhase), GTE_CLOUD_MEMBER(VolumetricCloudParamsGpu, lightScatter),
            GTE_CLOUD_MEMBER(VolumetricCloudParamsGpu, lightMisc), GTE_CLOUD_MEMBER(VolumetricCloudParamsGpu, drainePhase),
            GTE_CLOUD_MEMBER(VolumetricCloudParamsGpu, lowRes), GTE_CLOUD_MEMBER(VolumetricCloudParamsGpu, fullSize),
            GTE_CLOUD_MEMBER(VolumetricCloudParamsGpu, atmosphereMode),
            GTE_CLOUD_MEMBER(VolumetricCloudParamsGpu, atmosphereCamera),
            GTE_CLOUD_MEMBER(VolumetricCloudParamsGpu, skyShade), GTE_CLOUD_MEMBER(VolumetricCloudParamsGpu, skyShade2),
            GTE_CLOUD_MEMBER(VolumetricCloudParamsGpu, farField), GTE_CLOUD_MEMBER(VolumetricCloudParamsGpu, detailShape),
            GTE_CLOUD_MEMBER(VolumetricCloudParamsGpu, structure), GTE_CLOUD_MEMBER(VolumetricCloudParamsGpu, structure2),
            GTE_CLOUD_MEMBER(VolumetricCloudParamsGpu, structure3), GTE_CLOUD_MEMBER(VolumetricCloudParamsGpu, lightTune),
            GTE_CLOUD_MEMBER(VolumetricCloudParamsGpu, lightTune2), GTE_CLOUD_MEMBER(VolumetricCloudParamsGpu, shadowGrid),
            GTE_CLOUD_MEMBER(VolumetricCloudParamsGpu, shadowShift), GTE_CLOUD_MEMBER(VolumetricCloudParamsGpu, shadowTune) });
    ExpectBlock(ParseBlock(ReadShaderText("VolumetricCloudResolve.comp"), "VolumetricCloudResolveParams"),
        { GTE_CLOUD_MEMBER(VolumetricCloudResolveParamsGpu, invViewProjection),
            GTE_CLOUD_MEMBER(VolumetricCloudResolveParamsGpu, prevViewProjection),
            GTE_CLOUD_MEMBER(VolumetricCloudResolveParamsGpu, eye),
            GTE_CLOUD_MEMBER(VolumetricCloudResolveParamsGpu, windDelta),
            GTE_CLOUD_MEMBER(VolumetricCloudResolveParamsGpu, blend),
            GTE_CLOUD_MEMBER(VolumetricCloudResolveParamsGpu, motion),
            GTE_CLOUD_MEMBER(VolumetricCloudResolveParamsGpu, counts),
            GTE_CLOUD_MEMBER(VolumetricCloudResolveParamsGpu, sizes), GTE_CLOUD_MEMBER(VolumetricCloudResolveParamsGpu, sunRay),
            GTE_CLOUD_MEMBER(VolumetricCloudResolveParamsGpu, sunRayColor) });
    ExpectBlock(ParseBlock(ReadShaderText("VolumetricCloudSunShafts.comp"), "VolumetricCloudSunShaftsParams"),
        { GTE_CLOUD_MEMBER(VolumetricCloudSunShaftsParamsGpu, sunUvAndReachAndIntensity),
            GTE_CLOUD_MEMBER(VolumetricCloudSunShaftsParamsGpu, decayTapsAnisoContrast),
            GTE_CLOUD_MEMBER(VolumetricCloudSunShaftsParamsGpu, sunColorAndHazeMix),
            GTE_CLOUD_MEMBER(VolumetricCloudSunShaftsParamsGpu, frameFadeAspectActive),
            GTE_CLOUD_MEMBER(VolumetricCloudSunShaftsParamsGpu, depthParams) });
#undef GTE_CLOUD_MEMBER
}

TEST(VolumetricCloudLightTest, ShaderConstantsMatchTheCpuTwinsAndTheAtmosphereDefault)
{
    const std::string lighting = ReadShaderText("VolumetricCloudLighting.glsl");
    EXPECT_NE(lighting.find("const vec3 kOctaveTint = vec3(0.99, 0.995, 1.004);"), std::string::npos);
    const std::string atmosphere = ReadShaderText("VolumetricCloudAtmosphere.glsl");
    EXPECT_NE(atmosphere.find("const float kReferenceSkyIntensity = 6.0;"), std::string::npos);
    // The clouds' sky light is tied to the atmosphere's default sky intensity, not to the live setting.
    EXPECT_FLOAT_EQ(AtmosphereSettings{}.skyIntensity, 6.0f);
}

TEST(VolumetricCloudTemporalTest, ResolveShaderTapRuleUsesTheSameToleranceAsTheReference)
{
    const std::string source = ReadShaderText("VolumetricCloudResolve.comp");
    EXPECT_NE(source.find("const float kRelative = 0.03;"), std::string::npos);
    EXPECT_NE(source.find("const float kSlackKm = 0.02;"), std::string::npos);
    EXPECT_NE(source.find("const float kClearTransmittance = 0.999;"), std::string::npos);
}

TEST(VolumetricCloudTemporalTest, EachPixelsOwnSamplesStepByTheGoldenRatio)
{
    // Within one Bayer sub-block cycle, all frames share the same temporal jitter phase for coherent reconstruction.
    for (std::uint64_t r = 1; r < 16; ++r) {
        EXPECT_FLOAT_EQ(CloudMarchJitter(0.3f, 16 * 5 + r, 4), CloudMarchJitter(0.3f, 16 * 5, 4)) << r;
    }
    for (const float blue : { 0.0f, 0.3f, 0.77f }) {
        std::vector<float> jitters;
        for (std::uint64_t k = 0; k < 16; ++k) {
            jitters.push_back(CloudMarchJitter(blue, 7 + k * 16, 4));
            EXPECT_GE(jitters.back(), 0.0f);
            EXPECT_LT(jitters.back(), 1.0f);
        }
        for (std::size_t k = 1; k < jitters.size(); ++k) {
            double step = static_cast<double>(jitters[k]) - jitters[k - 1];
            step -= std::floor(step);
            EXPECT_NEAR(step, 0.618034, 2e-4) << k;
        }
        // Sixteen consecutive own samples cover [0, 1) evenly without clustering.
        std::sort(jitters.begin(), jitters.end());
        for (std::size_t k = 1; k < jitters.size(); ++k) {
            EXPECT_GT(jitters[k] - jitters[k - 1], 0.03f);
        }
    }
    // Own samples spaced by divisor^2 frames remain distinct across [0, 1) for every divisor.
    for (const std::uint32_t divisor : { 1u, 2u, 3u, 4u, 5u, 6u }) {
        const std::uint64_t cycle = std::uint64_t{ divisor } * divisor;
        std::vector<float> ownJitters;
        for (std::uint64_t k = 0; k < 16; ++k) {
            ownJitters.push_back(CloudMarchJitter(0.3f, 7 + k * cycle, divisor));
        }
        std::sort(ownJitters.begin(), ownJitters.end());
        for (std::size_t k = 1; k < ownJitters.size(); ++k) {
            EXPECT_GT(ownJitters[k] - ownJitters[k - 1], 0.002f) << divisor;
        }
    }
    EXPECT_EQ(CloudOwnSampleIndex(16 * 300, 4), 300u & 255u);
}

TEST(VolumetricCloudShaderTest, MarchShaderUsesTheOwnSampleJitterAndBlendsTheLight)
{
    const std::string march = gte::ReadShaderText("VolumetricCloud.comp");
    EXPECT_NE(march.find("texelFetch(blueNoise, pixel & 127, 0).r"), std::string::npos);
    EXPECT_NE(march.find("uint cycle = (params.counts.y / max(params.lowRes.x * params.lowRes.x, 1u)) & 255u;"), std::string::npos);
    EXPECT_NE(march.find("fract(bn + float(cycle) * 0.61803398875)"), std::string::npos);
    EXPECT_NE(march.find("mix(exp(-(sunDepthA - minSunDepth)), exp(-(sunDepthB - minSunDepth)), blend)"), std::string::npos);
    EXPECT_NE(march.find("mix(skyVisibilityA, skyVisibilityB, blend)"), std::string::npos);
}

TEST(VolumetricCloudShaderTest, DetailIsCurlWarpedBillowAndWispErosionAndTheCurveComesAfterTheErosion)
{
    const std::string march = gte::ReadShaderText("VolumetricCloud.comp");
    EXPECT_NE(march.find("layout(binding = 12) uniform sampler2D curlMap;"), std::string::npos);
    EXPECT_NE(march.find("vec4 detailShape;"), std::string::npos);
    EXPECT_NE(march.find("textureLod(curlMap,"), std::string::npos);
    EXPECT_NE(march.find("float billowErosion"), std::string::npos);
    EXPECT_NE(march.find("float wispErosion"), std::string::npos);
    // The density curve is applied to the eroded body, not to the body before the erosion.
    EXPECT_NE(march.find("float result = CloudCurve(Saturate(Remap(shape, erosion, 1.0, 0.0, 1.0)) * bodyGate) * densityFactor;"), std::string::npos);
    // The exponent lives in one place, CloudCurve(); CloudShape() returns the linear body.
    EXPECT_NE(march.find("float CloudCurve(float body)"), std::string::npos);
    EXPECT_NE(march.find("return Saturate(body);"), std::string::npos);
    EXPECT_NE(march.find("float puff ="), std::string::npos);
    // The body cut level has a floor and the ramp above it can be narrowed (params.structure3.yw).
    EXPECT_NE(march.find("float threshold = max(1.0 - min(rawEff, 0.68), params.structure3.y);"), std::string::npos);
    EXPECT_NE(march.find("min(threshold + params.structure3.w, 1.0)"), std::string::npos);
    const std::string lighting = gte::ReadShaderText("VolumetricCloudLighting.glsl");
    EXPECT_NE(lighting.find("return CloudCurve(shape) * factor;"), std::string::npos);
    // A dense sample is shortened so that one step holds at most maxStepDepth optical depths.
    EXPECT_NE(march.find("extinction * stepLength > params.farField.z"), std::string::npos);
    EXPECT_NE(march.find("t += marchStep;"), std::string::npos);
}

TEST(VolumetricCloudShaderTest, TheShapeHasTwoScalesWindShearAndSoftBases)
{
    const std::string march = gte::ReadShaderText("VolumetricCloud.comp");
    // The big lobe octave: same volume at 2 x the tile (the CPU twin is kCloudLargeShapeScale).
    EXPECT_NE(march.find("const float kLargeShapeScale = 2.0;"), std::string::npos);
    EXPECT_FLOAT_EQ(kCloudLargeShapeScale, 2.0f);
    EXPECT_FLOAT_EQ(kCloudTurretWeatherScale, 2.5f);
    EXPECT_NE(march.find("if (largeShare > 0.02)"), std::string::npos);
    EXPECT_NE(march.find("shapeUv / kLargeShapeScale + kLargeShapeOffset"), std::string::npos);
    EXPECT_NE(march.find("float ShapeValue(vec4 sn)"), std::string::npos);
    // Wind shear: the noise and weather lookups move with the height fraction.
    EXPECT_NE(march.find("vec2 shear = params.structure.zw * heightFraction;"), std::string::npos);
    EXPECT_NE(march.find("params.structure.zw"), std::string::npos);
    // Macro condensation base plane and height-masked crown erosion mask.
    EXPECT_NE(march.find("const float kFarZoom = 2.8;"), std::string::npos);
    EXPECT_FLOAT_EQ(kCloudFarZoom, 2.8f);
    EXPECT_NE(march.find("float crease = (1.0 - fbm) * params.structure3.x;"), std::string::npos);
    EXPECT_NE(march.find("float shape = Remap(sn.r, crease - 0.22, 1.0, 0.0, 1.0);"), std::string::npos);
    EXPECT_NE(march.find("float baseShift = clamp((0.5 - shapeLarge) * params.structure2.z * 0.08, -0.005, 0.008);"), std::string::npos);
    EXPECT_NE(march.find("float roundBottom = smoothstep(baseShift,"), std::string::npos);
    EXPECT_NE(march.find("erosion = min(erosion,"), std::string::npos);
    EXPECT_NE(march.find("float crownSculpt ="), std::string::npos);
    EXPECT_NE(march.find("float shellMask = mix(crownSculpt,"), std::string::npos);
}

TEST(VolumetricCloudShaderTest, EveryRayGrowsItsStepsToFitTheStepBudget)
{
    const std::string march = gte::ReadShaderText("VolumetricCloud.comp");
    // The per-ray growth: step = stepBaseM + growth * t, with growth raised until the segment fits 85 percent of maxSteps.
    EXPECT_NE(march.find("const float kStepBudgetShare = 0.85;"), std::string::npos);
    EXPECT_NE(march.find("pow((tEnd + stepOffset) / (tStart + stepOffset), 1.0 / stepBudget) - 1.0"), std::string::npos);
    EXPECT_NE(march.find("float stepOffset = params.march.x / rayGrowth;"), std::string::npos);
    EXPECT_NE(march.find("stepLength = (t + stepOffset) * rayGrowth;"), std::string::npos);
    // The old fixed-growth step must be gone.
    EXPECT_EQ(march.find("baseStep + t * growth"), std::string::npos);
    // The flat density fade is the only fade left and the far field has its own uniforms.
    EXPECT_NE(march.find("params.farField.x * farRamp"), std::string::npos);
    EXPECT_NE(march.find("kFarWeatherTileScale"), std::string::npos);
}

TEST(VolumetricCloudShaderTest, TheResolveAveragesSamplesAndTheMarchJittersInsideThePixel)
{
    const std::string resolve = gte::ReadShaderText("VolumetricCloudResolve.comp");
    // The sample count lives in the history aux z; a marched pixel blends 1 / (count + 1) down to the freshBlend floor.
    EXPECT_NE(resolve.find("alpha = max(alpha, 1.0 / (effectiveCount + 1.0));"), std::string::npos);
    EXPECT_NE(resolve.find("imageStore(historyAuxOut, pixel, vec4(cloudDistanceKm, pixelEndKm, newCount, 0.0));"), std::string::npos);
    EXPECT_NE(resolve.find("vec4 CatmullRomWeights(float f)"), std::string::npos);
    EXPECT_NE(resolve.find("cubic = clamp(cubic, innerMin, innerMax);"), std::string::npos);
    const std::string march = gte::ReadShaderText("VolumetricCloud.comp");
    EXPECT_NE(march.find("vec2 rayNdc = ndc + pixelShift * 2.0 / vec2(fullSize);"), std::string::npos);
    EXPECT_NE(march.find("float(params.lowRes.w) * 0.01"), std::string::npos);
}

TEST(VolumetricCloudShaderTest, TheSunLightIsScaledByTheInScatterChance)
{
    const std::string march = gte::ReadShaderText("VolumetricCloud.comp");
    const std::string lighting = gte::ReadShaderText("VolumetricCloudLighting.glsl");
    EXPECT_NE(lighting.find("float InScatterProbability(float lodDensity, float h, float cosTheta"), std::string::npos);
    EXPECT_NE(lighting.find("scatter *= mix(1.0, InScatterProbability("), std::string::npos);
    EXPECT_NE(march.find("li.lodDensity = Saturate(CloudCurve(shape) * factor);"), std::string::npos);
}

} // namespace
} // namespace gte

TEST(VolumetricCloudShaderTest, ImageAccessIsBoundedByTheBoundImages)
{
    const std::string march = gte::ReadShaderText("VolumetricCloud.comp");
    const std::string resolve = gte::ReadShaderText("VolumetricCloudResolve.comp");
    EXPECT_NE(march.find("imageSize(lowAuxOut)"), std::string::npos);
    EXPECT_NE(resolve.find("imageSize(destinationImage)"), std::string::npos);
    EXPECT_NE(resolve.find("textureSize(historyColor, 0)"), std::string::npos);
    EXPECT_NE(resolve.find("textureSize(lowColor, 0)"), std::string::npos);
}

TEST(VolumetricCloudShaderTest, AStillPixelReadsItsHistoryExactly)
{
    // A history fraction of 1e-4 texel made the rgba16f store round a still pixel down one half-float step per frame.
    const std::string resolve = gte::ReadShaderText("VolumetricCloudResolve.comp");
    EXPECT_NE(resolve.find("const float kSnapTexels = 0.004;"), std::string::npos);
    EXPECT_NE(resolve.find("previousTexel = mix(previousTexel, snapped, lessThan(abs(previousTexel - snapped), vec2(kSnapTexels)));"), std::string::npos);
}

// The starburst and the ray taps respect scene depth and cloud transmittance, and HDR sky glare has a steep knee.
TEST(VolumetricCloudShaderTest, StarburstAndRaysRespectSceneDepth)
{
    const std::string resolve = gte::ReadShaderText("VolumetricCloudResolve.comp");
    EXPECT_NE(resolve.find("float SkyShare(vec2 uv, float ownDepth)"), std::string::npos);
    EXPECT_NE(resolve.find("seen *= SkyShare(uv, ownDepth);"), std::string::npos);
    EXPECT_NE(resolve.find("color += SunStar(uv, rawDepth) * cloud.a;"), std::string::npos);
    EXPECT_NE(resolve.find("step(kClearDepthThreshold, textureLod(sourceDepth, sunUv, 0.0).r)"), std::string::npos);
    EXPECT_NE(resolve.find("const float kSceneHdrKnee = 64.0;"), std::string::npos);
    EXPECT_NE(resolve.find("vec3 color = SceneBehindCloud(scene, cloud.a) + cloud.rgb;"), std::string::npos);
}

TEST(VolumetricCloudShaderTest, NoImplicitLodInTheComputeShaders)
{
    for (const char* name : { "VolumetricCloud.comp", "VolumetricCloudResolve.comp", "VolumetricCloudLighting.glsl",
             "VolumetricCloudAtmosphere.glsl", "VolumetricCloudSlice2D.comp", "VolumetricCloudSlice3D.comp" }) {
        const std::string source = gte::ReadShaderText(name);
        for (std::size_t at = source.find("texture("); at != std::string::npos; at = source.find("texture(", at + 1)) {
            const bool part = at > 0 && (std::isalnum(static_cast<unsigned char>(source[at - 1])) || source[at - 1] == '_');
            EXPECT_TRUE(part) << name << " uses texture() without an explicit level of detail";
        }
    }
}

namespace gte {
namespace {

TEST(VolumetricCloudMathTest, TheShadowMapIsCentredOnTheCrossingOfTheEyesSunRay)
{
    VolumetricCloudSettings s;
    s.layerBaseM = 1500.0f;
    s.layerTopM = 3500.0f;
    s.shadowRangeKm = 24.0f;
    const Vec3 sun = Normalize(Vec3{ 0.0f, 0.5f, 1.0f });
    const CloudShadowGrid grid = MakeCloudShadowGrid(s, Vec3{ 100.0f, 100.0f, 0.0f }, sun, 128);
    // Mid layer 2500 m, eye 100 m: 2400 m up along the ray moves it 2400 * sun.z / sun.y toward +z.
    EXPECT_NEAR(grid.centerZ, 2400.0 * static_cast<double>(sun.z) / static_cast<double>(sun.y), grid.texelM);
    EXPECT_NEAR(grid.centerX, 100.0, grid.texelM);
    EXPECT_NEAR(std::fmod(grid.centerZ, static_cast<double>(grid.texelM)), 0.0, 1e-6);
}

TEST(VolumetricCloudMathTest, TheShadowMapStaysCentredOnTheGroundBelowAHighCamera)
{
    VolumetricCloudSettings s;
    s.layerBaseM = 1500.0f;
    s.layerTopM = 3500.0f;
    s.shadowRangeKm = 24.0f;
    const Vec3 sun = Normalize(Vec3{ 0.3f, 0.5f, 1.0f });
    const CloudShadowGrid low = MakeCloudShadowGrid(s, Vec3{ 100.0f, 150.0f, 0.0f }, sun, 128);
    const CloudShadowGrid high = MakeCloudShadowGrid(s, Vec3{ 100.0f, 3000.0f, 0.0f }, sun, 128);
    EXPECT_DOUBLE_EQ(high.centerX, low.centerX);
    EXPECT_DOUBLE_EQ(high.centerZ, low.centerZ);
}

TEST(VolumetricCloudMathTest, TheMarchReadsTheShadowMapForTheAirOnlyWithAShaftStrength)
{
    VolumetricCloudSettings s;
    VolumetricCloudFrameFacts facts;
    VolumetricCloudParamsGpu p = MakeVolumetricCloudParams(s, facts);
    EXPECT_FLOAT_EQ(p.shadowTune[3], 0.0f);
    CloudShadowMap map;
    map.centerX = 10.0f;
    map.centerZ = 20.0f;
    map.extentM = 1000.0f;
    map.shaftStrength = 0.75f;
    map.shaftSharpness = 2.0f;
    SetCloudShaftLookup(p, map);
    EXPECT_FLOAT_EQ(p.shadowGrid[0], 0.001f);
    EXPECT_FLOAT_EQ(p.shadowGrid[1], 10.0f);
    EXPECT_FLOAT_EQ(p.shadowGrid[2], 20.0f);
    EXPECT_FLOAT_EQ(p.shadowGrid[3], 0.0f);
    EXPECT_FLOAT_EQ(p.shadowTune[3], 0.75f);
    EXPECT_FLOAT_EQ(p.shadowShift[2], 2.0f);
}

TEST(VolumetricCloudLightingTest, SilverLobeAndSourceLumaAreSmoothlyBounded)
{
    const VolumetricCloudSettings defaults = SanitizeVolumetricCloudSettings(VolumetricCloudSettings{});
    EXPECT_FLOAT_EQ(kCloudSilverPhaseCap, 4.0f);
    EXPECT_FLOAT_EQ(kCloudSilverThinDepthScale, 0.90f);
    EXPECT_FLOAT_EQ(kCloudSunScatterShoulder, 1.85f);
    EXPECT_FLOAT_EQ(kCloudMaxSourceLuma, 12.0f);
    const float peak = CloudSunScatter(defaults, 1.0f, 0.0f);
    const float nearSun = CloudSunScatter(defaults, 0.99f, 0.0f);
    const float shoulder = CloudSunScatter(defaults, 0.9f, 0.0f);
    EXPECT_LT(peak, kCloudSunScatterShoulder);
    EXPECT_GT(peak, nearSun);
    EXPECT_GT(nearSun, shoulder);

    // Thin optical depth gates the silver lining so thick cores blocking the sun never turn into fireballs.
    VolumetricCloudSettings noSilver = defaults;
    noSilver.silverIntensity = 0.0f;
    EXPECT_GT(CloudSunScatter(defaults, 1.0f, 0.1f) - CloudSunScatter(noSilver, 1.0f, 0.1f), 0.10f);
    EXPECT_LT(CloudSunScatter(defaults, 1.0f, 3.0f) - CloudSunScatter(noSilver, 1.0f, 3.0f), 0.01f);
    const auto lumaOf = [](const Vec3& c) { return 0.2126f * c.x + 0.7152f * c.y + 0.0722f * c.z; };
    const Vec3 clampedNeg = BoundCloudLuma(Vec3(-2.0f, 0.0f, -5.0f));
    EXPECT_FLOAT_EQ(clampedNeg.x, 0.0f);
    EXPECT_FLOAT_EQ(clampedNeg.y, 0.0f);
    EXPECT_FLOAT_EQ(clampedNeg.z, 0.0f);

    const Vec3 mid = BoundCloudLuma(Vec3(2.5f, 2.5f, 2.5f));
    EXPECT_GT(lumaOf(mid), 2.47f);

    const Vec3 tinted = BoundCloudLuma(Vec3(120.0f, 60.0f, 30.0f));
    EXPECT_NEAR(tinted.x / tinted.y, 2.0f, 1e-5f);
    EXPECT_NEAR(tinted.y / tinted.z, 2.0f, 1e-5f);
    EXPECT_LT(lumaOf(tinted), kCloudMaxSourceLuma);

    float prevLuma = 0.0f;
    for (int step = 1; step <= 200; ++step) {
        const float v = static_cast<float>(step) * 0.2f;
        const float curLuma = lumaOf(BoundCloudLuma(Vec3(v, v, v)));
        EXPECT_GT(curLuma, prevLuma);
        EXPECT_LT(curLuma - prevLuma, 0.25f);
        EXPECT_LT(curLuma, kCloudMaxSourceLuma);
        prevLuma = curLuma;
    }
    EXPECT_LT(lumaOf(BoundCloudLuma(Vec3(10000.0f, 10000.0f, 10000.0f))), kCloudMaxSourceLuma + 1e-4f);
}

TEST(VolumetricCloudLightingTest, SceneHdrAboveTheKneeFadesWithFourthPowerOfTransmittance)
{
    EXPECT_FLOAT_EQ(kCloudSceneHdrKnee, 64.0f);
    const Vec3 sky(36.0f, 24.0f, 12.0f);
    const Vec3 skyThrough = CloudSceneBehind(sky, 0.25f);
    EXPECT_FLOAT_EQ(skyThrough.x, 9.0f);
    EXPECT_FLOAT_EQ(skyThrough.y, 6.0f);
    EXPECT_FLOAT_EQ(skyThrough.z, 3.0f);

    const Vec3 sunDisc(60000.0f, 45000.0f, 30000.0f);
    const Vec3 open = CloudSceneBehind(sunDisc, 1.0f);
    EXPECT_NEAR(open.x, sunDisc.x, 1e-2f);
    const Vec3 blocked = CloudSceneBehind(sunDisc, 0.0f);
    EXPECT_FLOAT_EQ(blocked.x, 0.0f);

    const Vec3 thin = CloudSceneBehind(sunDisc, 0.1f);
    EXPECT_LT(thin.x, 15.0f);
    EXPECT_NEAR(thin.x / thin.y, 4.0f / 3.0f, 1e-5f);
    EXPECT_NEAR(thin.y / thin.z, 3.0f / 2.0f, 1e-5f);

    // Continuous across the knee.
    const Vec3 below = CloudSceneBehind(Vec3(63.99f, 32.0f, 16.0f), 0.5f);
    const Vec3 above = CloudSceneBehind(Vec3(64.01f, 32.005f, 16.0025f), 0.5f);
    EXPECT_NEAR(below.x, above.x, 0.02f);
}

TEST(VolumetricCloudMathTest, HighSunShaftFadeKeepsTheConfiguredFloorAcrossAllElevations)
{
    const VolumetricCloudSettings defaults = SanitizeVolumetricCloudSettings(VolumetricCloudSettings{});
    EXPECT_FLOAT_EQ(defaults.shaftHighSunKeep, 0.7f);

    VolumetricCloudSettings bad = defaults;
    bad.shaftHighSunKeep = 4.0f;
    EXPECT_FLOAT_EQ(SanitizeVolumetricCloudSettings(bad).shaftHighSunKeep, 1.0f);
    bad.shaftHighSunKeep = -1.0f;
    EXPECT_FLOAT_EQ(SanitizeVolumetricCloudSettings(bad).shaftHighSunKeep, 0.0f);
    bad.shaftHighSunKeep = std::numeric_limits<float>::quiet_NaN();
    EXPECT_FLOAT_EQ(SanitizeVolumetricCloudSettings(bad).shaftHighSunKeep, 0.7f);

    const auto sunUpAtDeg = [](double deg) { return static_cast<float>(std::sin(deg * 3.14159265358979323846 / 180.0)); };
    EXPECT_FLOAT_EQ(CloudShaftSunFade(sunUpAtDeg(0.0), 0.7f), 1.0f);
    EXPECT_GT(CloudShaftSunFade(sunUpAtDeg(35.0), 0.7f), 0.8f);
    EXPECT_GT(CloudShaftSunFade(sunUpAtDeg(60.0), 0.7f), 0.7f);
    EXPECT_FLOAT_EQ(CloudShaftSunFade(sunUpAtDeg(88.0), 0.7f), 0.7f);
    EXPECT_FLOAT_EQ(CloudShaftSunFade(sunUpAtDeg(90.0), 0.7f), 0.7f);

    for (const float keep : { 0.0f, 0.35f, 0.7f, 1.0f }) {
        const float lowSun = CloudShaftSunFade(sunUpAtDeg(0.0), keep);
        EXPECT_FLOAT_EQ(lowSun, 1.0f);
        for (const double deg : { 0.0, 35.0, 60.0, 88.0, 90.0 }) {
            EXPECT_GE(CloudShaftSunFade(sunUpAtDeg(deg), keep), keep * lowSun - 1e-6f) << "deg " << deg << " keep " << keep;
        }
        float previous = lowSun;
        for (int step = 1; step <= 180; ++step) {
            const double deg = static_cast<double>(step) * 0.5;
            const float current = CloudShaftSunFade(sunUpAtDeg(deg), keep);
            EXPECT_LE(current, previous + 1e-6f) << "deg " << deg;
            EXPECT_LT(std::abs(current - previous), 0.02f) << "deg " << deg;
            EXPECT_GE(current, keep * lowSun - 1e-6f) << "deg " << deg;
            previous = current;
        }
    }

    VolumetricCloudSettings s = defaults;
    s.shaftMist = 0.5f;
    EXPECT_FLOAT_EQ(ComputeCloudShadowStrengths(s, sunUpAtDeg(0.0)).shaftStrength, 0.0f);
    const CloudShadowStrengths low = ComputeCloudShadowStrengths(s, sunUpAtDeg(10.0));
    EXPECT_FLOAT_EQ(low.shaftStrength, s.shaftStrength);
    EXPECT_FLOAT_EQ(low.beamHazePerKm, s.shaftHaze);
    EXPECT_FLOAT_EQ(low.shaftHazePerM, s.shaftMist * 0.001f);

    CloudShadowStrengths prev = low;
    for (const double deg : { 35.0, 60.0, 88.0, 90.0 }) {
        const CloudShadowStrengths cur = ComputeCloudShadowStrengths(s, sunUpAtDeg(deg));
        EXPECT_GE(cur.shaftStrength, s.shaftHighSunKeep * low.shaftStrength - 1e-6f) << deg;
        EXPECT_GE(cur.beamHazePerKm, s.shaftHighSunKeep * low.beamHazePerKm - 1e-6f) << deg;
        EXPECT_GE(cur.shaftHazePerM, s.shaftHighSunKeep * low.shaftHazePerM - 1e-6f) << deg;
        EXPECT_GT(cur.beamHazePerKm, 0.0f) << deg;
        EXPECT_LE(cur.shaftStrength, prev.shaftStrength + 1e-6f) << deg;
        EXPECT_LE(cur.beamHazePerKm, prev.beamHazePerKm + 1e-6f) << deg;
        prev = cur;
    }

    // Continuous over the whole 0..90 degree sweep including the horizon ramp.
    float prevBeam = ComputeCloudShadowStrengths(s, sunUpAtDeg(0.0)).beamHazePerKm;
    for (int step = 1; step <= 900; ++step) {
        const float curBeam = ComputeCloudShadowStrengths(s, sunUpAtDeg(static_cast<double>(step) * 0.1)).beamHazePerKm;
        EXPECT_LT(std::abs(curBeam - prevBeam), 0.05f) << step;
        prevBeam = curBeam;
    }
}

TEST(VolumetricCloudMathTest, SunRayFadeStaysPositiveOverheadAndDropsOutsideTheView)
{
    const float centerUv[2] = { 0.5f, 0.5f };
    const float aspect = 16.0f / 9.0f;
    const auto sunUpAtDeg = [](double deg) { return static_cast<float>(std::sin(deg * 3.14159265358979323846 / 180.0)); };
    EXPECT_FLOAT_EQ(CloudSunRayFade(sunUpAtDeg(0.0), centerUv, aspect), 0.0f);
    const float at35 = CloudSunRayFade(sunUpAtDeg(35.0), centerUv, aspect);
    EXPECT_FLOAT_EQ(at35, 1.0f);
    float prev = at35;
    for (const double deg : { 60.0, 72.0, 85.0, 88.0, 90.0 }) {
        const float cur = CloudSunRayFade(sunUpAtDeg(deg), centerUv, aspect);
        EXPECT_GT(cur, 0.0f) << deg;
        EXPECT_LE(cur, prev + 1e-6f) << deg;
        prev = cur;
    }
    const float farOutside[2] = { 2.0f, 0.5f };
    EXPECT_FLOAT_EQ(CloudSunRayFade(sunUpAtDeg(35.0), farOutside, aspect), 0.0f);
}

TEST(VolumetricCloudEndpointTest, ExposesShaftHighSunKeepAndReadOnlyShaftState)
{
    VolumetricCloudFeature feature;
    fx::IFeatureDebugEndpoint* endpoint = feature.DebugEndpoint();
    ASSERT_NE(endpoint, nullptr);

    const auto findField = [](const fx::FeatureDebugResult& r, std::string_view key) -> const fx::FeatureDebugField* {
        for (const fx::FeatureDebugField& f : r.fields) {
            if (f.key == key) {
                return &f;
            }
        }
        return nullptr;
    };

    const fx::FeatureDebugResult state = endpoint->GetState();
    ASSERT_TRUE(state.ok);
    for (const char* key : { "forwardG", "shaftHighSunKeep", "game.sunUp", "game.shaftStrength", "game.beamHazePerKm",
             "game.shaftHazePerM", "game.shadow.extentM", "game.shadow.texelM", "game.shadow.nearExtentM",
             "game.shadow.nearTexelM" }) {
        ASSERT_NE(findField(state, key), nullptr) << key;
    }
    EXPECT_NEAR(std::get<double>(findField(state, "forwardG")->value), 0.65, 1e-6);
    EXPECT_NEAR(std::get<double>(findField(state, "shaftHighSunKeep")->value), 0.7, 1e-6);
    EXPECT_DOUBLE_EQ(std::get<double>(findField(state, "game.shadow.extentM")->value), 0.0);
    EXPECT_DOUBLE_EQ(std::get<double>(findField(state, "game.shadow.texelM")->value), 0.0);
    EXPECT_DOUBLE_EQ(std::get<double>(findField(state, "game.shadow.nearExtentM")->value), 0.0);
    EXPECT_DOUBLE_EQ(std::get<double>(findField(state, "game.shadow.nearTexelM")->value), 0.0);

    const std::vector<fx::FeatureDebugArg> validSet{ { "shaftHighSunKeep", "0.45" } };
    const fx::FeatureDebugResult updated = endpoint->Set(validSet);
    ASSERT_TRUE(updated.ok);
    EXPECT_NEAR(std::get<double>(findField(updated, "shaftHighSunKeep")->value), 0.45, 1e-6);

    const std::vector<fx::FeatureDebugArg> clampSet{ { "shaftHighSunKeep", "5.0" } };
    const fx::FeatureDebugResult clamped = endpoint->Set(clampSet);
    ASSERT_TRUE(clamped.ok);
    EXPECT_DOUBLE_EQ(std::get<double>(findField(clamped, "shaftHighSunKeep")->value), 1.0);

    for (const char* roKey : { "game.sunUp", "game.shaftStrength", "game.beamHazePerKm", "game.shaftHazePerM",
             "game.shadow.extentM", "game.shadow.texelM", "game.shadow.nearExtentM", "game.shadow.nearTexelM" }) {
        const std::vector<fx::FeatureDebugArg> roSet{ { roKey, "0.5" } };
        const fx::FeatureDebugResult bad = endpoint->Set(roSet);
        EXPECT_FALSE(bad.ok) << roKey;
        EXPECT_EQ(bad.badKey, roKey);
    }
}

TEST(VolumetricCloudEndpointTest, ExposesReadOnlyShadowNearCovers)
{
    VolumetricCloudFeature feature;
    fx::IFeatureDebugEndpoint* endpoint = feature.DebugEndpoint();
    ASSERT_NE(endpoint, nullptr);

    const auto findField = [](const fx::FeatureDebugResult& r, std::string_view key) -> const fx::FeatureDebugField* {
        for (const fx::FeatureDebugField& f : r.fields) {
            if (f.key == key) {
                return &f;
            }
        }
        return nullptr;
    };

    const fx::FeatureDebugResult state = endpoint->GetState();
    ASSERT_TRUE(state.ok);
    const fx::FeatureDebugField* nearCovers = findField(state, "game.shadow.nearCovers");
    ASSERT_NE(nearCovers, nullptr);
    ASSERT_TRUE(std::holds_alternative<bool>(nearCovers->value));
    EXPECT_FALSE(std::get<bool>(nearCovers->value));
    EXPECT_FALSE(VolumetricCloudViewSnapshot{}.shadowNearCovers);

    const std::vector<fx::FeatureDebugArg> roSet{ { "game.shadow.nearCovers", "true" } };
    const fx::FeatureDebugResult bad = endpoint->Set(roSet);
    EXPECT_FALSE(bad.ok);
    EXPECT_EQ(bad.badKey, "game.shadow.nearCovers");
    EXPECT_EQ(bad.message, "key is read-only");

    VolumetricCloudSettings s;
    const Vec3 eye{ -2000.0f, 12.0f, -800.0f };
    const Vec3 sun = Normalize(Vec3{ 0.4f, 0.57f, -0.71f });
    const CloudShadowGrid grid = MakeCloudShadowGrid(s, eye, sun, kCloudShadowMapSize);
    CloudShadowMap map;
    map.sunDirection = sun;
    map.centerX = static_cast<float>(grid.centerX);
    map.centerZ = static_cast<float>(grid.centerZ);
    map.extentM = grid.texelM * static_cast<float>(kCloudShadowMapSize);
    map.layerBaseM = s.layerBaseM;
    map.layerTopM = s.layerTopM;
    map.strength = 1.0f;
    EXPECT_TRUE(CloudShadowNearCovers(map, eye));
    EXPECT_FALSE(CloudShadowNearCovers(map, eye + Vec3{ map.extentM, 0.0f, 0.0f }));
}

TEST(VolumetricCloudShaderTest, TempDebugHooksAndAirDesaturationAreGoneAndEnergyCapsMatchCpp)
{
    const std::string march = ReadShaderText("VolumetricCloud.comp");
    const std::string lighting = ReadShaderText("VolumetricCloudLighting.glsl");
    EXPECT_EQ(march.find("TEMP"), std::string::npos);
    EXPECT_EQ(lighting.find("TEMP"), std::string::npos);
    EXPECT_EQ(march.find("gXoff"), std::string::npos);
    EXPECT_EQ(march.find("gDbgShape"), std::string::npos);
    EXPECT_EQ(lighting.find("gDiffuseMode"), std::string::npos);
    EXPECT_EQ(march.find("mix(airTransmittance,"), std::string::npos);
    EXPECT_NE(march.find("if (AtmosphereOn() && cloud.a < 1.0 && cloudDistance > 0.0)"), std::string::npos);
    EXPECT_NE(march.find("cloud.rgb = cloud.rgb * airTransmittance + (1.0 - cloud.a) * air * airGain;"), std::string::npos);
    EXPECT_NE(march.find("src = BoundCloudLuma(src, kMaxSourceLuma);"), std::string::npos);
    EXPECT_NE(lighting.find("const float kSilverPhaseCap = 4.0;"), std::string::npos);
    EXPECT_NE(lighting.find("const float kMaxSourceLuma = 7.5;"), std::string::npos);
}

TEST(VolumetricCloudShadowNearTest, ConstantsAndNearGridShareTheSnappedCentre)
{
    EXPECT_EQ(kCloudShadowMapSize, 128u);
    EXPECT_EQ(kCloudShadowNearScale, 8u);
    EXPECT_EQ(kCloudShadowMapRows, 256u);
    EXPECT_FLOAT_EQ(kCloudShadowFarEdgeFade, 0.06f);
    EXPECT_FLOAT_EQ(kCloudShadowNearEdgeFade, 0.15f);
    EXPECT_FLOAT_EQ(CloudShadowNearExtentM(24000.0f), 3000.0f);
    EXPECT_FLOAT_EQ(CloudShadowNearTexelM(187.5f), 23.4375f);

    const CloudShadowMap defaults;
    EXPECT_FLOAT_EQ(defaults.nearScale, 8.0f);
    EXPECT_EQ(defaults.mapRows, 256u);

    for (const float rangeKm : { 24.0f, 8.0f, 12.0f }) {
        VolumetricCloudSettings s;
        s.layerBaseM = 1500.0f;
        s.layerTopM = 5000.0f;
        s.shadowRangeKm = rangeKm;
        for (const Vec3& eye : { Vec3{ 137.0f, 420.0f, -891.0f }, Vec3{ -2000.0f, 12.0f, -800.0f } }) {
            for (const Vec3& rawSun : { Vec3{ 0.0f, 1.0f, 0.0f }, Vec3{ 0.4f, 0.57f, -0.71f } }) {
                const Vec3 sun = Normalize(rawSun);
                const CloudShadowGrid grid = MakeCloudShadowGrid(s, eye, sun, kCloudShadowMapSize);
                EXPECT_FLOAT_EQ(grid.nearTexelM, grid.texelM / static_cast<float>(kCloudShadowNearScale));
                EXPECT_NEAR(std::abs(std::remainder(grid.centerX, static_cast<double>(grid.texelM))), 0.0, 1e-6);
                EXPECT_NEAR(std::abs(std::remainder(grid.centerZ, static_cast<double>(grid.texelM))), 0.0, 1e-6);
                EXPECT_NEAR(std::abs(std::remainder(grid.centerX, static_cast<double>(grid.nearTexelM))), 0.0, 1e-6);
                EXPECT_NEAR(std::abs(std::remainder(grid.centerZ, static_cast<double>(grid.nearTexelM))), 0.0, 1e-6);

                CloudShadowMap m;
                m.sunDirection = sun;
                m.centerX = static_cast<float>(grid.centerX);
                m.centerZ = static_cast<float>(grid.centerZ);
                m.extentM = grid.texelM * static_cast<float>(kCloudShadowMapSize);
                m.layerBaseM = s.layerBaseM;
                m.layerTopM = s.layerTopM;
                m.strength = 1.0f;

                const Vec3 atCentre{ m.centerX, 0.5f * (m.layerBaseM + m.layerTopM), m.centerZ };
                const Vec2 farCentreUv = CloudShadowUv(m, atCentre);
                const Vec2 nearCentreUv = CloudShadowNearUv(m, atCentre);
                EXPECT_NEAR(farCentreUv.x, 0.5f, 1e-6f);
                EXPECT_NEAR(farCentreUv.y, 0.5f, 1e-6f);
                EXPECT_NEAR(nearCentreUv.x, 0.5f, 1e-6f);
                EXPECT_NEAR(nearCentreUv.y, 0.5f, 1e-6f);

                const Vec3 probe = eye + Vec3{ 180.0f, -5.0f, -95.0f };
                const Vec2 farUv = CloudShadowUv(m, probe);
                const Vec2 nearUv = CloudShadowNearUv(m, probe);
                EXPECT_NEAR(nearUv.x - 0.5f, (farUv.x - 0.5f) * static_cast<float>(kCloudShadowNearScale), 1e-5f);
                EXPECT_NEAR(nearUv.y - 0.5f, (farUv.y - 0.5f) * static_cast<float>(kCloudShadowNearScale), 1e-5f);
            }
        }
    }
}

TEST(VolumetricCloudShadowNearTest, EdgeFadeAndNearBlendAreZeroOutsideAndOneInside)
{
    EXPECT_FLOAT_EQ(CloudShadowEdgeFade(Vec2{ -0.01f, 0.5f }, kCloudShadowNearEdgeFade), 0.0f);
    EXPECT_FLOAT_EQ(CloudShadowEdgeFade(Vec2{ 1.01f, 0.5f }, kCloudShadowNearEdgeFade), 0.0f);
    EXPECT_FLOAT_EQ(CloudShadowEdgeFade(Vec2{ 0.5f, 0.0f }, kCloudShadowNearEdgeFade), 0.0f);
    EXPECT_FLOAT_EQ(CloudShadowEdgeFade(Vec2{ 0.5f, 1.0f }, kCloudShadowNearEdgeFade), 0.0f);
    EXPECT_FLOAT_EQ(CloudShadowEdgeFade(Vec2{ 0.15f, 0.5f }, kCloudShadowNearEdgeFade), 1.0f);
    EXPECT_FLOAT_EQ(CloudShadowEdgeFade(Vec2{ 0.85f, 0.85f }, kCloudShadowNearEdgeFade), 1.0f);
    EXPECT_FLOAT_EQ(CloudShadowEdgeFade(Vec2{ 0.5f, 0.5f }, kCloudShadowNearEdgeFade), 1.0f);
    EXPECT_FLOAT_EQ(CloudShadowEdgeFade(Vec2{ 0.075f, 0.5f }, kCloudShadowNearEdgeFade), 0.5f);

    CloudShadowMap m;
    m.sunDirection = Vec3::Up();
    m.extentM = 24000.0f; // Near extent = 3000 m, half-width = 1500 m, inner full-weight half-width = 1050 m.
    m.layerBaseM = 1500.0f;
    m.layerTopM = 4500.0f;
    m.strength = 1.0f;

    EXPECT_FLOAT_EQ(CloudShadowNearBlend(m, Vec3{ 0.0f, 0.0f, 0.0f }), 1.0f);
    EXPECT_FLOAT_EQ(CloudShadowNearBlend(m, Vec3{ 1000.0f, 0.0f, -1000.0f }), 1.0f);
    EXPECT_TRUE(CloudShadowNearCovers(m, Vec3{ 0.0f, 0.0f, 0.0f }));

    const float borderBlend = CloudShadowNearBlend(m, Vec3{ 1350.0f, 0.0f, 0.0f });
    EXPECT_GT(borderBlend, 0.0f);
    EXPECT_LT(borderBlend, 1.0f);
    EXPECT_TRUE(CloudShadowNearCovers(m, Vec3{ 1350.0f, 0.0f, 0.0f }));

    EXPECT_FLOAT_EQ(CloudShadowNearBlend(m, Vec3{ 1600.0f, 0.0f, 0.0f }), 0.0f);
    EXPECT_FALSE(CloudShadowNearCovers(m, Vec3{ 1600.0f, 0.0f, 0.0f }));

    CloudShadowMap legacy = m;
    legacy.mapRows = kCloudShadowMapSize;
    EXPECT_FLOAT_EQ(CloudShadowNearBlend(legacy, Vec3{ 0.0f, 0.0f, 0.0f }), 0.0f);
}

TEST(VolumetricCloudShadowNearTest, CpuLookupTwinKeepsFarBitIdenticalAndResolvesNearSingleClouds)
{
    constexpr std::uint32_t kSide = kCloudShadowMapSize;
    constexpr std::uint32_t kRows = kCloudShadowMapRows;
    std::vector<float> farOnly(static_cast<std::size_t>(kSide) * kSide, 1.0f);
    std::vector<float> stacked(static_cast<std::size_t>(kSide) * kRows, 1.0f);

    for (std::uint32_t y = 0; y < kSide; ++y) {
        for (std::uint32_t x = 0; x < kSide; ++x) {
            const float v = 0.25f + 0.6f * (static_cast<float>(x) / 127.0f) * (static_cast<float>(y) / 127.0f);
            farOnly[static_cast<std::size_t>(y) * kSide + x] = v;
            stacked[static_cast<std::size_t>(y) * kSide + x] = v;
            stacked[static_cast<std::size_t>(y + kSide) * kSide + x] = 0.05f;
        }
    }

    CloudShadowMap m;
    m.sunDirection = Normalize(Vec3{ 0.2f, 0.9f, -0.1f });
    m.centerX = 500.0f;
    m.centerZ = -300.0f;
    m.extentM = 24000.0f;
    m.layerBaseM = 1500.0f;
    m.layerTopM = 4500.0f;
    m.strength = 1.0f;

    CloudShadowMap mFar = m;
    mFar.mapRows = kSide;
    mFar.nearScale = 1.0f;

    // Outside the 3 km near map, stacked 128x256 lookups must stay bit-identical to 128x128 far-only lookups.
    const float alongGround = 0.5f * (m.layerBaseM + m.layerTopM) / m.sunDirection.y;
    const float baseReceiverX = m.centerX - m.sunDirection.x * alongGround;
    const float baseReceiverZ = m.centerZ - m.sunDirection.z * alongGround;
    for (int step = 0; step < 12; ++step) {
        const float offsetM = 1650.0f + static_cast<float>(step) * 950.0f;
        const Vec3 pos{ baseReceiverX + offsetM, 0.0f, baseReceiverZ - 0.5f * offsetM };
        EXPECT_FLOAT_EQ(
            SampleCloudSunTransmittance(stacked, kSide, kRows, m, pos),
            SampleCloudSunTransmittance(farOnly, kSide, kSide, mFar, pos))
            << step;
        EXPECT_FLOAT_EQ(
            SampleCloudSunTransmittanceFast(stacked, kSide, kRows, m, pos, 2.5f),
            SampleCloudSunTransmittanceFast(farOnly, kSide, kSide, mFar, pos, 2.5f))
            << step;
    }

    // A single ~180 m cloud puff written into the near region is resolved, while the far region stays flat.
    std::fill(farOnly.begin(), farOnly.end(), 0.85f);
    std::fill(stacked.begin(), stacked.begin() + static_cast<std::size_t>(kSide) * kSide, 0.85f);
    std::fill(stacked.begin() + static_cast<std::size_t>(kSide) * kSide, stacked.end(), 1.0f);
    const float nearTexelM = m.extentM / static_cast<float>(kSide * kCloudShadowNearScale);
    const float puffOffsetX = 200.0f;
    const float puffOffsetZ = -100.0f;
    for (std::uint32_t y = 0; y < kSide; ++y) {
        for (std::uint32_t x = 0; x < kSide; ++x) {
            const float lx = (static_cast<float>(x) + 0.5f - 0.5f * static_cast<float>(kSide)) * nearTexelM;
            const float lz = (static_cast<float>(y) + 0.5f - 0.5f * static_cast<float>(kSide)) * nearTexelM;
            const float dx = lx - puffOffsetX;
            const float dz = lz - puffOffsetZ;
            if (dx * dx + dz * dz <= 90.0f * 90.0f) {
                stacked[static_cast<std::size_t>(y + kSide) * kSide + x] = 0.15f;
            }
        }
    }
    const Vec3 underPuff{ baseReceiverX + puffOffsetX, 0.0f, baseReceiverZ + puffOffsetZ };
    const Vec3 besidePuff{ baseReceiverX + puffOffsetX + 240.0f, 0.0f, baseReceiverZ + puffOffsetZ };
    EXPECT_LT(SampleCloudSunTransmittance(stacked, kSide, kRows, m, underPuff), 0.25f);
    EXPECT_GT(SampleCloudSunTransmittance(stacked, kSide, kRows, m, besidePuff), 0.95f);
    EXPECT_NEAR(SampleCloudSunTransmittance(farOnly, kSide, kSide, mFar, underPuff), 0.85f, 1e-5f);
    EXPECT_NEAR(SampleCloudSunTransmittance(farOnly, kSide, kSide, mFar, besidePuff), 0.85f, 1e-5f);

    // Near region clamping never bleeds across row 127 <-> 128, and the border fade is smooth and monotonic.
    std::fill(stacked.begin(), stacked.begin() + static_cast<std::size_t>(kSide) * kSide, 0.8f);
    std::fill(stacked.begin() + static_cast<std::size_t>(kSide) * kSide, stacked.end(), 0.2f);
    EXPECT_NEAR(SampleCloudShadowRegionCubic(stacked, kSide, kSide, kSide, Vec2{ 0.5f, 0.001f }), 0.2f, 1e-5f);

    float prevT = SampleCloudSunTransmittance(stacked, kSide, kRows, m, Vec3{ baseReceiverX, 0.0f, baseReceiverZ });
    EXPECT_NEAR(prevT, 0.2f, 1e-5f);
    for (int step = 1; step <= 160; ++step) {
        const float x = baseReceiverX + static_cast<float>(step) * 10.0f;
        const float curT = SampleCloudSunTransmittance(stacked, kSide, kRows, m, Vec3{ x, 0.0f, baseReceiverZ });
        EXPECT_GE(curT + 1e-6f, prevT) << step;
        EXPECT_LT(curT - prevT, 0.03f) << step;
        prevT = curT;
    }
    EXPECT_NEAR(prevT, 0.8f, 1e-5f);
}

TEST(VolumetricCloudShadowNearTest, ShaderLookupAndShadowPassMatchCppConstants)
{
    std::ifstream in(std::filesystem::path(GTE_TEST_SHADERS_DIR) / "CloudShadowLookup.glsl", std::ios::binary);
    ASSERT_TRUE(in.good());
    const std::string lookup((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    EXPECT_NE(lookup.find("const float kCloudShadowNearScale = 8.0;"), std::string::npos);
    EXPECT_NE(lookup.find("const float kCloudShadowFarFade = 0.06;"), std::string::npos);
    EXPECT_NE(lookup.find("const float kCloudShadowNearFade = 0.15;"), std::string::npos);
    EXPECT_NE(lookup.find("CloudShadowRegionCubic"), std::string::npos);
    EXPECT_NE(lookup.find("CloudShadowRegionBilinear"), std::string::npos);
    for (std::size_t at = lookup.find("texture("); at != std::string::npos; at = lookup.find("texture(", at + 1)) {
        const bool part = at > 0 && (std::isalnum(static_cast<unsigned char>(lookup[at - 1])) || lookup[at - 1] == '_');
        EXPECT_TRUE(part) << "CloudShadowLookup.glsl uses texture() without an explicit level of detail";
    }

    const std::string march = ReadShaderText("VolumetricCloud.comp");
    EXPECT_NE(march.find("const int kShadowNearScale = 8;"), std::string::npos);
    EXPECT_NE(march.find("const float kNearShadowDetail = 0.15;"), std::string::npos);
    EXPECT_NE(march.find("CloudNearShadowDensity"), std::string::npos);
    EXPECT_NE(march.find("CloudCurve(shape) * factor * modulation"), std::string::npos);
    EXPECT_NE(march.find("float transmittance = exp(-opticalDepth);"), std::string::npos);
    EXPECT_NE(march.find("float along = (tapCount > 1) ? (0.125 + 0.25 * float(tap)) : 0.5;"), std::string::npos);
    EXPECT_NE(march.find("kShadowRgss"), std::string::npos);
    EXPECT_EQ(march.find("ShadowNoise"), std::string::npos);
    EXPECT_EQ(march.find("kNearShadowMsWeight"), std::string::npos);
    EXPECT_EQ(march.find("kNearShadowMsDecay"), std::string::npos);
}

void ExpectAllFinite(const VolumetricCloudSunShaftsParamsGpu& p)
{
    for (int i = 0; i < 4; ++i) {
        EXPECT_TRUE(std::isfinite(p.sunUvAndReachAndIntensity[i])) << i;
        EXPECT_TRUE(std::isfinite(p.decayTapsAnisoContrast[i])) << i;
        EXPECT_TRUE(std::isfinite(p.sunColorAndHazeMix[i])) << i;
        EXPECT_TRUE(std::isfinite(p.frameFadeAspectActive[i])) << i;
        EXPECT_TRUE(std::isfinite(p.depthParams[i])) << i;
    }
    EXPECT_GT(p.depthParams[0], 0.0f);
    EXPECT_GT(p.depthParams[1], p.depthParams[0]);
    EXPECT_FLOAT_EQ(p.depthParams[2], 0.0f);
    EXPECT_GE(p.depthParams[3], 200.0f);
    EXPECT_LE(p.depthParams[3], 20000.0f);
}

Mat4 MakeTestRelativeViewProjection(
    const Vec3& forward, const Vec3& up, float aspect, float nearZ = 0.1f, float farZ = 1000.0f)
{
    const Vec3 eye(1200.0f, 50.0f, -800.0f);
    const Mat4 view = Mat4::LookAtLH(eye, eye + Normalize(forward), Normalize(up));
    const Mat4 proj = Mat4::PerspectiveFovLH_ZO(1.0471976f, aspect, nearZ, farZ);
    return MakeEyeRelativeCamera(proj * view, eye).viewProjection;
}

TEST(VolumetricCloudSunShaftsTest, ParamsBlockLayoutAndSizeMatchTheShader)
{
    static_assert(sizeof(VolumetricCloudSunShaftsParamsGpu) == 80);
    static_assert(std::is_trivially_copyable_v<VolumetricCloudSunShaftsParamsGpu>);
    EXPECT_EQ(offsetof(VolumetricCloudSunShaftsParamsGpu, sunUvAndReachAndIntensity), 0u);
    EXPECT_EQ(offsetof(VolumetricCloudSunShaftsParamsGpu, decayTapsAnisoContrast), 16u);
    EXPECT_EQ(offsetof(VolumetricCloudSunShaftsParamsGpu, sunColorAndHazeMix), 32u);
    EXPECT_EQ(offsetof(VolumetricCloudSunShaftsParamsGpu, frameFadeAspectActive), 48u);
    EXPECT_EQ(offsetof(VolumetricCloudSunShaftsParamsGpu, depthParams), 64u);
    EXPECT_EQ(sizeof(VolumetricCloudSunShaftsParamsGpu), 80u);
}

TEST(VolumetricCloudSunShaftsTest, SettingsDefaultsAndSanitizerSnapTapsToStepsOfEight)
{
    const VolumetricCloudSettings defaults;
    EXPECT_FLOAT_EQ(defaults.sunRays, 0.55f);
    EXPECT_FLOAT_EQ(defaults.sunRaysDecay, 1.0f);
    EXPECT_FLOAT_EQ(defaults.sunRaysLength, 1.5f);
    EXPECT_FLOAT_EQ(defaults.sunRaysTaps, 48.0f);
    EXPECT_FLOAT_EQ(defaults.sunRaysAnisotropy, 0.75f);
    EXPECT_FLOAT_EQ(defaults.sunRaysContrast, 1.65f);
    EXPECT_FLOAT_EQ(defaults.sunRaysAirLengthM, 2000.0f);
    EXPECT_FLOAT_EQ(defaults.sunRaysSpoke, 0.22f);

    VolumetricCloudSettings low = defaults;
    low.sunRays = -1.0f;
    low.sunRaysDecay = -2.0f;
    low.sunRaysLength = 0.01f;
    low.sunRaysTaps = 1.0f;
    low.sunRaysAnisotropy = -0.5f;
    low.sunRaysContrast = 0.01f;
    low.sunRaysAirLengthM = 10.0f;
    low.sunRaysSpoke = -0.5f;
    low = SanitizeVolumetricCloudSettings(low);
    EXPECT_FLOAT_EQ(low.sunRays, 0.0f);
    EXPECT_FLOAT_EQ(low.sunRaysDecay, 0.0f);
    EXPECT_FLOAT_EQ(low.sunRaysLength, 0.2f);
    EXPECT_FLOAT_EQ(low.sunRaysTaps, 8.0f);
    EXPECT_FLOAT_EQ(low.sunRaysAnisotropy, 0.0f);
    EXPECT_FLOAT_EQ(low.sunRaysContrast, 0.2f);
    EXPECT_FLOAT_EQ(low.sunRaysAirLengthM, 200.0f);
    EXPECT_FLOAT_EQ(low.sunRaysSpoke, 0.0f);

    VolumetricCloudSettings high = defaults;
    high.sunRays = 9.0f;
    high.sunRaysDecay = 20.0f;
    high.sunRaysLength = 10.0f;
    high.sunRaysTaps = 99.0f;
    high.sunRaysAnisotropy = 2.0f;
    high.sunRaysContrast = 9.0f;
    high.sunRaysAirLengthM = 90000.0f;
    high.sunRaysSpoke = 9.0f;
    high = SanitizeVolumetricCloudSettings(high);
    EXPECT_FLOAT_EQ(high.sunRays, 3.0f);
    EXPECT_FLOAT_EQ(high.sunRaysDecay, 6.0f);
    EXPECT_FLOAT_EQ(high.sunRaysLength, 3.0f);
    EXPECT_FLOAT_EQ(high.sunRaysTaps, 48.0f);
    EXPECT_FLOAT_EQ(high.sunRaysAnisotropy, 0.95f);
    EXPECT_FLOAT_EQ(high.sunRaysContrast, 3.0f);
    EXPECT_FLOAT_EQ(high.sunRaysAirLengthM, 20000.0f);
    EXPECT_FLOAT_EQ(high.sunRaysSpoke, 4.0f);

    for (const auto [rawTaps, snapped] : { std::pair{ 9.0f, 8.0f }, std::pair{ 13.0f, 16.0f },
             std::pair{ 23.0f, 24.0f }, std::pair{ 37.0f, 40.0f }, std::pair{ 46.0f, 48.0f } }) {
        VolumetricCloudSettings s = defaults;
        s.sunRaysTaps = rawTaps;
        EXPECT_FLOAT_EQ(SanitizeVolumetricCloudSettings(s).sunRaysTaps, snapped) << rawTaps;
    }

    VolumetricCloudSettings nanSettings = defaults;
    nanSettings.sunRays = std::numeric_limits<float>::quiet_NaN();
    nanSettings.sunRaysTaps = std::numeric_limits<float>::infinity();
    nanSettings.sunRaysContrast = -std::numeric_limits<float>::infinity();
    nanSettings.sunRaysAirLengthM = std::numeric_limits<float>::quiet_NaN();
    nanSettings.sunRaysSpoke = std::numeric_limits<float>::quiet_NaN();
    const VolumetricCloudSettings recovered = SanitizeVolumetricCloudSettings(nanSettings);
    EXPECT_FLOAT_EQ(recovered.sunRays, defaults.sunRays);
    EXPECT_FLOAT_EQ(recovered.sunRaysTaps, defaults.sunRaysTaps);
    EXPECT_FLOAT_EQ(recovered.sunRaysContrast, defaults.sunRaysContrast);
    EXPECT_FLOAT_EQ(recovered.sunRaysAirLengthM, defaults.sunRaysAirLengthM);
    EXPECT_FLOAT_EQ(recovered.sunRaysSpoke, defaults.sunRaysSpoke);
}

TEST(VolumetricCloudSunShaftsTest, BuilderHandlesOverheadSunInView)
{
    const VolumetricCloudSettings s = SanitizeVolumetricCloudSettings(VolumetricCloudSettings{});
    VolumetricCloudSunShaftsFacts facts;
    facts.viewWidth = 1280;
    facts.viewHeight = 720;
    facts.frameIndex = 42;
    facts.sunDirection = Vec3(0.0f, 1.0f, 0.0f);
    facts.sunColor = Vec3(0.95f, 0.9f, 0.85f);
    facts.viewProjection = MakeTestRelativeViewProjection(Vec3(0.0f, 1.0f, 0.0f), Vec3(0.0f, 0.0f, -1.0f), 1280.0f / 720.0f);

    const VolumetricCloudSunShaftsParamsGpu p = MakeCloudSunShaftsParams(s, facts);
    ExpectAllFinite(p);
    EXPECT_NEAR(p.sunUvAndReachAndIntensity[0], 0.5f, 1e-4f);
    EXPECT_NEAR(p.sunUvAndReachAndIntensity[1], 0.5f, 1e-4f);
    EXPECT_FLOAT_EQ(p.sunUvAndReachAndIntensity[2], s.sunRaysLength);
    EXPECT_FLOAT_EQ(p.sunUvAndReachAndIntensity[3], s.sunRays * 0.35f);
    EXPECT_FLOAT_EQ(p.decayTapsAnisoContrast[0], s.sunRaysDecay * 1.80f);
    EXPECT_FLOAT_EQ(p.decayTapsAnisoContrast[1], s.sunRaysTaps);
    EXPECT_FLOAT_EQ(p.decayTapsAnisoContrast[2], s.sunRaysAnisotropy);
    EXPECT_FLOAT_EQ(p.decayTapsAnisoContrast[3], s.sunRaysContrast * 0.65f);
    EXPECT_FLOAT_EQ(p.sunColorAndHazeMix[0], 0.95f);
    EXPECT_FLOAT_EQ(p.sunColorAndHazeMix[1], 0.9f);
    EXPECT_FLOAT_EQ(p.sunColorAndHazeMix[2], 0.85f);
    EXPECT_FLOAT_EQ(p.sunColorAndHazeMix[3], kCloudSunShaftsHazeMix);
    EXPECT_FLOAT_EQ(p.frameFadeAspectActive[0], 42.0f);
    EXPECT_FLOAT_EQ(p.frameFadeAspectActive[1], 1.0f);
    EXPECT_NEAR(p.frameFadeAspectActive[2], 1280.0f / 720.0f, 1e-5f);
    EXPECT_FLOAT_EQ(p.frameFadeAspectActive[3], 1.0f);
    EXPECT_NEAR(p.depthParams[0], 0.1f, 1e-4f);
    EXPECT_NEAR(p.depthParams[1], 1000.0f, 1.0f);
    EXPECT_FLOAT_EQ(p.depthParams[2], 0.0f);
    EXPECT_FLOAT_EQ(p.depthParams[3], s.sunRaysAirLengthM);

    // Daytime sun elevations <= 55 deg keep full intensity, decay and contrast untouched.
    VolumetricCloudSunShaftsFacts day = facts;
    day.sunDirection = Normalize(Vec3(0.0f, 0.70f, 0.714143f));
    day.viewProjection = MakeTestRelativeViewProjection(day.sunDirection, Vec3::Up(), 1280.0f / 720.0f);
    const VolumetricCloudSunShaftsParamsGpu dayOut = MakeCloudSunShaftsParams(s, day);
    ExpectAllFinite(dayOut);
    EXPECT_FLOAT_EQ(dayOut.sunUvAndReachAndIntensity[3], s.sunRays);
    EXPECT_FLOAT_EQ(dayOut.decayTapsAnisoContrast[0], s.sunRaysDecay);
    EXPECT_FLOAT_EQ(dayOut.decayTapsAnisoContrast[3], s.sunRaysContrast);
}

TEST(VolumetricCloudSunShaftsTest, BuilderFadesOutsideFrameAndClampsExtremeUv)
{
    const VolumetricCloudSettings s = SanitizeVolumetricCloudSettings(VolumetricCloudSettings{});
    const float aspect = 16.0f / 9.0f;
    VolumetricCloudSunShaftsFacts facts;
    facts.viewWidth = 1600;
    facts.viewHeight = 900;
    facts.viewProjection = MakeTestRelativeViewProjection(Vec3(0.0f, 0.35f, 0.9367f), Vec3::Up(), aspect);

    // Sun moderately above the top of the 60-deg vertical FOV (< 1.5 view heights): UV outside 0..1, faded, still active.
    facts.sunDirection = Normalize(Vec3(0.0f, 0.94f, 0.34f));
    const VolumetricCloudSunShaftsParamsGpu nearEdge = MakeCloudSunShaftsParams(s, facts);
    ExpectAllFinite(nearEdge);
    EXPECT_TRUE(nearEdge.sunUvAndReachAndIntensity[1] < 0.0f || nearEdge.sunUvAndReachAndIntensity[1] > 1.0f);
    EXPECT_GT(nearEdge.frameFadeAspectActive[1], 0.0f);
    EXPECT_LT(nearEdge.frameFadeAspectActive[1], 1.0f);
    EXPECT_FLOAT_EQ(nearEdge.frameFadeAspectActive[3], 1.0f);

    // Sun nearly 89 degrees off the horizontal camera axis: raw UV far past +-4 is clamped and faded to inactive.
    facts.viewProjection = MakeTestRelativeViewProjection(Vec3(0.0f, 0.02f, 0.9998f), Vec3::Up(), aspect);
    facts.sunDirection = Vec3(0.0f, 1.0f, 0.0f);
    const VolumetricCloudSunShaftsParamsGpu farOutside = MakeCloudSunShaftsParams(s, facts);
    ExpectAllFinite(farOutside);
    EXPECT_GE(farOutside.sunUvAndReachAndIntensity[0], -kCloudSunShaftsUvClamp);
    EXPECT_LE(farOutside.sunUvAndReachAndIntensity[0], kCloudSunShaftsUvClamp);
    EXPECT_GE(farOutside.sunUvAndReachAndIntensity[1], -kCloudSunShaftsUvClamp);
    EXPECT_LE(farOutside.sunUvAndReachAndIntensity[1], kCloudSunShaftsUvClamp);
    EXPECT_FLOAT_EQ(farOutside.frameFadeAspectActive[1], 0.0f);
    EXPECT_FLOAT_EQ(farOutside.frameFadeAspectActive[3], 0.0f);
}

TEST(VolumetricCloudSunShaftsTest, BuilderMarksBehindCameraIntensityZeroNoHistoryAndNanInactive)
{
    const VolumetricCloudSettings s = SanitizeVolumetricCloudSettings(VolumetricCloudSettings{});
    VolumetricCloudSunShaftsFacts base;
    base.viewWidth = 1280;
    base.viewHeight = 720;
    base.viewProjection = MakeTestRelativeViewProjection(Vec3(0.0f, 0.2f, 0.9798f), Vec3::Up(), 1280.0f / 720.0f);
    base.sunDirection = Normalize(Vec3(0.0f, 0.3f, 0.9539f));
    ASSERT_FLOAT_EQ(MakeCloudSunShaftsParams(s, base).frameFadeAspectActive[3], 1.0f);

    VolumetricCloudSunShaftsFacts behind = base;
    behind.sunDirection = Normalize(Vec3(0.0f, 0.4f, -0.9165f));
    const VolumetricCloudSunShaftsParamsGpu behindOut = MakeCloudSunShaftsParams(s, behind);
    ExpectAllFinite(behindOut);
    EXPECT_FLOAT_EQ(behindOut.frameFadeAspectActive[3], 0.0f);

    VolumetricCloudSettings zeroIntensity = s;
    zeroIntensity.sunRays = 0.0f;
    const VolumetricCloudSunShaftsParamsGpu zeroOut = MakeCloudSunShaftsParams(zeroIntensity, base);
    ExpectAllFinite(zeroOut);
    EXPECT_FLOAT_EQ(zeroOut.sunUvAndReachAndIntensity[3], 0.0f);
    EXPECT_FLOAT_EQ(zeroOut.frameFadeAspectActive[3], 0.0f);

    VolumetricCloudSunShaftsFacts noHistory = base;
    noHistory.historyValid = false;
    const VolumetricCloudSunShaftsParamsGpu noHistoryOut = MakeCloudSunShaftsParams(s, noHistory);
    ExpectAllFinite(noHistoryOut);
    EXPECT_FLOAT_EQ(noHistoryOut.frameFadeAspectActive[3], 0.0f);

    const float nan = std::numeric_limits<float>::quiet_NaN();
    VolumetricCloudSunShaftsFacts nanDir = base;
    nanDir.sunDirection.x = nan;
    const VolumetricCloudSunShaftsParamsGpu nanDirOut = MakeCloudSunShaftsParams(s, nanDir);
    ExpectAllFinite(nanDirOut);
    EXPECT_FLOAT_EQ(nanDirOut.frameFadeAspectActive[3], 0.0f);

    VolumetricCloudSunShaftsFacts nanVp = base;
    nanVp.viewProjection(0, 0) = nan;
    const VolumetricCloudSunShaftsParamsGpu nanVpOut = MakeCloudSunShaftsParams(s, nanVp);
    ExpectAllFinite(nanVpOut);
    EXPECT_FLOAT_EQ(nanVpOut.frameFadeAspectActive[3], 0.0f);

    VolumetricCloudSettings nanSetting = s;
    nanSetting.sunRays = nan;
    const VolumetricCloudSunShaftsParamsGpu nanSetOut = MakeCloudSunShaftsParams(nanSetting, base);
    ExpectAllFinite(nanSetOut);
    EXPECT_FLOAT_EQ(nanSetOut.frameFadeAspectActive[3], 0.0f);
}

TEST(VolumetricCloudSunShaftsTest, BuilderExtractsCameraDepthPlanesAndFallsBackWhenInvalid)
{
    VolumetricCloudSettings s = SanitizeVolumetricCloudSettings(VolumetricCloudSettings{});
    s.sunRaysAirLengthM = 3500.0f;

    VolumetricCloudSunShaftsFacts aerial;
    aerial.viewWidth = 1280;
    aerial.viewHeight = 720;
    aerial.sunDirection = Normalize(Vec3(0.0f, 0.3f, 0.9539f));
    aerial.viewProjection = MakeTestRelativeViewProjection(
        Vec3(0.0f, 0.2f, 0.9798f), Vec3::Up(), 1280.0f / 720.0f, 3.0f, 25000.0f);
    const VolumetricCloudSunShaftsParamsGpu aerialOut = MakeCloudSunShaftsParams(s, aerial);
    ExpectAllFinite(aerialOut);
    EXPECT_NEAR(aerialOut.depthParams[0], 3.0f, 1e-3f);
    EXPECT_NEAR(aerialOut.depthParams[1], 25000.0f, 250.0f);
    EXPECT_FLOAT_EQ(aerialOut.depthParams[2], 0.0f);
    EXPECT_FLOAT_EQ(aerialOut.depthParams[3], 3500.0f);

    const Vec3 eye(500.0f, 2.0f, -300.0f);
    const Mat4 view = Mat4::LookAtLH(eye, eye + Normalize(Vec3(0.2f, 0.1f, 0.97f)), Vec3::Up());
    VolumetricCloudSunShaftsFacts flipped = aerial;
    flipped.viewProjection = MakeEyeRelativeCamera(
        Mat4::PerspectiveFovLH_ZO(1.0471976f, 16.0f / 9.0f, 0.15f, 10000.0f, /*flipY=*/true) * view, eye).viewProjection;
    const VolumetricCloudSunShaftsParamsGpu flippedOut = MakeCloudSunShaftsParams(s, flipped);
    ExpectAllFinite(flippedOut);
    EXPECT_NEAR(flippedOut.depthParams[0], 0.15f, 1e-4f);
    EXPECT_NEAR(flippedOut.depthParams[1], 10000.0f, 50.0f);

    for (const Mat4& badVp : {
             Mat4::Identity(),
             Mat4::Zero(),
             Mat4::OrthographicLH_ZO(-10.0f, 10.0f, -10.0f, 10.0f, 0.5f, 500.0f),
             Mat4::PerspectiveFovLH_ZO(1.0471976f, 16.0f / 9.0f, -0.5f, 1000.0f) * view,
             Mat4::PerspectiveFovLH_ZO(1.0471976f, 16.0f / 9.0f, 500.0f, 100.0f) * view,
         }) {
        VolumetricCloudSunShaftsFacts bad = aerial;
        bad.viewProjection = badVp;
        const VolumetricCloudSunShaftsParamsGpu out = MakeCloudSunShaftsParams(s, bad);
        ExpectAllFinite(out);
        EXPECT_FLOAT_EQ(out.depthParams[0], kCloudSunShaftsFallbackNearM);
        EXPECT_FLOAT_EQ(out.depthParams[1], kCloudSunShaftsFallbackFarM);
        EXPECT_FLOAT_EQ(out.depthParams[2], 0.0f);
        EXPECT_FLOAT_EQ(out.depthParams[3], 3500.0f);
    }

    VolumetricCloudSettings nanAir = s;
    nanAir.sunRaysAirLengthM = std::numeric_limits<float>::quiet_NaN();
    const VolumetricCloudSunShaftsParamsGpu nanAirOut = MakeCloudSunShaftsParams(nanAir, aerial);
    ExpectAllFinite(nanAirOut);
    EXPECT_FLOAT_EQ(nanAirOut.depthParams[3], VolumetricCloudSettings{}.sunRaysAirLengthM);
    EXPECT_FLOAT_EQ(nanAirOut.frameFadeAspectActive[3], 0.0f);
}

TEST(VolumetricCloudSunShaftsTest, EndpointAndSetupExposeSunShaftsKeysSettingsAndPassGate)
{
    VolumetricCloudFeature feature;
    fx::FeatureSetup setup;
    feature.Setup(setup);
    EXPECT_TRUE(setup.Errors().empty());
    bool foundGate = false;
    for (const fx::Registration& reg : setup.Registrations()) {
        if (reg.name == "VolumetricCloud" && reg.stage == fx::Stage::PostComposite) {
            foundGate = std::find(reg.gate.passNames.begin(), reg.gate.passNames.end(),
                            "VolumetricCloud.SunShafts.Dispatch")
                != reg.gate.passNames.end();
        }
    }
    EXPECT_TRUE(foundGate);

    fx::IFeatureDebugEndpoint* endpoint = feature.DebugEndpoint();
    ASSERT_NE(endpoint, nullptr);
    const auto findField = [](const fx::FeatureDebugResult& r, std::string_view key) -> const fx::FeatureDebugField* {
        for (const fx::FeatureDebugField& f : r.fields) {
            if (f.key == key) {
                return &f;
            }
        }
        return nullptr;
    };

    const fx::FeatureDebugResult state = endpoint->GetState();
    ASSERT_TRUE(state.ok);
    for (const char* key : { "sunRays", "sunRaysDecay", "sunRaysLength", "sunRaysTaps", "sunRaysAnisotropy",
             "sunRaysContrast", "sunRaysAirLengthM", "sunRaysSpoke", "game.sunShafts.sunUv", "game.sunShafts.reach",
             "game.sunShafts.active" }) {
        ASSERT_NE(findField(state, key), nullptr) << key;
    }
    EXPECT_NEAR(std::get<double>(findField(state, "sunRays")->value), 0.55, 1e-6);
    EXPECT_NEAR(std::get<double>(findField(state, "sunRaysDecay")->value), 1.0, 1e-6);
    EXPECT_NEAR(std::get<double>(findField(state, "sunRaysLength")->value), 1.5, 1e-6);
    EXPECT_DOUBLE_EQ(std::get<double>(findField(state, "sunRaysTaps")->value), 48.0);
    EXPECT_NEAR(std::get<double>(findField(state, "sunRaysAnisotropy")->value), 0.75, 1e-6);
    EXPECT_NEAR(std::get<double>(findField(state, "sunRaysContrast")->value), 1.65, 1e-6);
    EXPECT_DOUBLE_EQ(std::get<double>(findField(state, "sunRaysAirLengthM")->value), 2000.0);
    EXPECT_NEAR(std::get<double>(findField(state, "sunRaysSpoke")->value), 0.22, 1e-6);
    EXPECT_NE(std::get<std::string>(findField(state, "game.sunShafts.sunUv")->value).find(','), std::string::npos);
    EXPECT_DOUBLE_EQ(std::get<double>(findField(state, "game.sunShafts.reach")->value), 0.0);
    EXPECT_FALSE(std::get<bool>(findField(state, "game.sunShafts.active")->value));

    const std::vector<fx::FeatureDebugArg> args{
        { "sunRaysTaps", "21" }, { "sunRaysContrast", "1.8" }, { "sunRaysAirLengthM", "4500" }, { "sunRaysSpoke", "2.5" } };
    const fx::FeatureDebugResult updated = endpoint->Set(args);
    ASSERT_TRUE(updated.ok);
    EXPECT_DOUBLE_EQ(std::get<double>(findField(updated, "sunRaysTaps")->value), 24.0);
    EXPECT_NEAR(std::get<double>(findField(updated, "sunRaysContrast")->value), 1.8, 1e-6);
    EXPECT_DOUBLE_EQ(std::get<double>(findField(updated, "sunRaysAirLengthM")->value), 4500.0);
    EXPECT_DOUBLE_EQ(std::get<double>(findField(updated, "sunRaysSpoke")->value), 2.5);

    const std::vector<fx::FeatureDebugArg> clampSpoke{ { "sunRaysSpoke", "9.0" } };
    const fx::FeatureDebugResult clampedSpoke = endpoint->Set(clampSpoke);
    ASSERT_TRUE(clampedSpoke.ok);
    EXPECT_DOUBLE_EQ(std::get<double>(findField(clampedSpoke, "sunRaysSpoke")->value), 4.0);

    for (const char* roKey : { "game.sunShafts.sunUv", "game.sunShafts.reach", "game.sunShafts.active" }) {
        const std::vector<fx::FeatureDebugArg> roSet{ { roKey, "0" } };
        const fx::FeatureDebugResult bad = endpoint->Set(roSet);
        EXPECT_FALSE(bad.ok) << roKey;
        EXPECT_EQ(bad.badKey, roKey);
    }
}

TEST(VolumetricCloudSunShaftsTest, ResolveParamsReuseSunRayZForSpokeWithoutGrowingBlock)
{
    static_assert(sizeof(VolumetricCloudResolveParamsGpu) == 256);
    EXPECT_EQ(offsetof(VolumetricCloudResolveParamsGpu, sunRay), 224u);
    EXPECT_EQ(offsetof(VolumetricCloudResolveParamsGpu, sunRayColor), 240u);
    EXPECT_EQ(sizeof(VolumetricCloudResolveParamsGpu), 256u);

    VolumetricCloudSettings s = SanitizeVolumetricCloudSettings(VolumetricCloudSettings{});
    VolumetricCloudFrameFacts facts;
    facts.sunDirection = Normalize(Vec3(0.0f, 0.5f, 0.8660254f));
    VolumetricCloudResolveFacts resolve;
    resolve.sunUv[0] = 0.4f;
    resolve.sunUv[1] = 0.3f;
    resolve.sunRayFade = 1.0f;

    const VolumetricCloudResolveParamsGpu defaultsOut = MakeVolumetricCloudResolveParams(s, facts, resolve);
    EXPECT_FLOAT_EQ(defaultsOut.sunRay[0], 0.4f);
    EXPECT_FLOAT_EQ(defaultsOut.sunRay[1], 0.3f);
    EXPECT_FLOAT_EQ(defaultsOut.sunRay[2], 0.22f);

    // Sun elevations <= 48 deg stay completely unattenuated, while near-zenith overhead sun suppresses spokes to 0.
    facts.sunDirection = Normalize(Vec3(0.0f, 0.743145f, 0.669131f));
    EXPECT_FLOAT_EQ(MakeVolumetricCloudResolveParams(s, facts, resolve).sunRay[2], 0.22f);
    facts.sunDirection = Normalize(Vec3(0.0f, 0.999391f, 0.034899f));
    EXPECT_FLOAT_EQ(MakeVolumetricCloudResolveParams(s, facts, resolve).sunRay[2], 0.0f);

    facts.sunDirection = Normalize(Vec3(0.0f, 0.5f, 0.8660254f));
    s.sunRaysSpoke = 2.5f;
    resolve.sunRayFade = 0.5f;
    EXPECT_FLOAT_EQ(MakeVolumetricCloudResolveParams(s, facts, resolve).sunRay[2], 1.25f);

    s.sunRaysSpoke = 0.0f;
    EXPECT_FLOAT_EQ(MakeVolumetricCloudResolveParams(s, facts, resolve).sunRay[2], 0.0f);

    s.sunRaysSpoke = 2.0f;
    s.sunRays = 0.0f;
    EXPECT_FLOAT_EQ(MakeVolumetricCloudResolveParams(s, facts, resolve).sunRay[2], 0.0f);
}

TEST(VolumetricCloudLightingTest, ResolveSoftHdrShoulderPreservesChromaAndCapsSmoothly)
{
    EXPECT_FLOAT_EQ(kCloudResolveShoulderStart, 3.6f);
    EXPECT_FLOAT_EQ(kCloudResolveShoulderCeiling, 7.2f);

    const auto lumaOf = [](const Vec3& c) { return 0.2126f * c.x + 0.7152f * c.y + 0.0722f * c.z; };
    const Vec3 neg = CompressCloudResolveLuma(Vec3(-1.0f, 0.0f, -3.0f));
    EXPECT_FLOAT_EQ(neg.x, 0.0f);
    EXPECT_FLOAT_EQ(neg.y, 0.0f);
    EXPECT_FLOAT_EQ(neg.z, 0.0f);

    // Below the 3.6 knee, linear HDR radiance passes through untouched.
    const Vec3 below(3.4f, 3.0f, 2.4f);
    const Vec3 belowOut = CompressCloudResolveLuma(below);
    EXPECT_FLOAT_EQ(belowOut.x, below.x);
    EXPECT_FLOAT_EQ(belowOut.y, below.y);
    EXPECT_FLOAT_EQ(belowOut.z, below.z);

    // Continuous across the 3.6 knee.
    const Vec3 justBelow = CompressCloudResolveLuma(Vec3(3.599f, 3.599f, 3.599f));
    const Vec3 justAbove = CompressCloudResolveLuma(Vec3(3.601f, 3.601f, 3.601f));
    EXPECT_NEAR(lumaOf(justBelow), lumaOf(justAbove), 0.003f);

    // Chroma ratios stay identical while luma compresses smoothly below 7.2.
    const Vec3 brightWarm(12.0f, 6.0f, 3.0f);
    const Vec3 compressedWarm = CompressCloudResolveLuma(brightWarm);
    EXPECT_NEAR(compressedWarm.x / compressedWarm.y, 2.0f, 1e-5f);
    EXPECT_NEAR(compressedWarm.y / compressedWarm.z, 2.0f, 1e-5f);
    EXPECT_LT(lumaOf(compressedWarm), kCloudResolveShoulderCeiling);
    EXPECT_GT(lumaOf(compressedWarm), kCloudResolveShoulderStart);

    float prevLuma = 0.0f;
    for (int step = 1; step <= 200; ++step) {
        const float v = static_cast<float>(step) * 0.2f;
        const float curLuma = lumaOf(CompressCloudResolveLuma(Vec3(v, v, v)));
        EXPECT_GT(curLuma, prevLuma);
        EXPECT_LT(curLuma, kCloudResolveShoulderCeiling);
        prevLuma = curLuma;
    }
}

TEST(VolumetricCloudShadowNearTest, ShadowHistoryInvalidatesOnSunCoverageOrShadowDensityChange)
{
    const VolumetricCloudSettings defaults = SanitizeVolumetricCloudSettings(VolumetricCloudSettings{});
    EXPECT_FLOAT_EQ(defaults.shadowDensity, 2.20f);
    EXPECT_FLOAT_EQ(defaults.shadowSoftness, 0.48f);
    EXPECT_FLOAT_EQ(defaults.shadowUpdate, 1.0f);
    EXPECT_FLOAT_EQ(defaults.shadowSteps, 12.0f);
    EXPECT_DOUBLE_EQ(kCloudWindPeriodM, 350000.0);

    const Vec3 sunA = Normalize(Vec3(0.3f, 0.8f, -0.5f));
    EXPECT_FALSE(CloudShadowHistoryReusable(Vec3::Zero(), defaults.coverage, defaults.shadowDensity, sunA, defaults));
    EXPECT_TRUE(CloudShadowHistoryReusable(sunA, defaults.coverage, defaults.shadowDensity, sunA, defaults));

    // A sun rotation past cos(theta) = 0.9999 invalidates stale shadow tiles immediately.
    const Vec3 sunB = Normalize(Vec3(0.33f, 0.8f, -0.5f));
    ASSERT_LT(Dot(sunA, sunB), 0.9999f);
    EXPECT_FALSE(CloudShadowHistoryReusable(sunA, defaults.coverage, defaults.shadowDensity, sunB, defaults));

    VolumetricCloudSettings covChanged = defaults;
    covChanged.coverage = defaults.coverage - 0.05f;
    EXPECT_FALSE(CloudShadowHistoryReusable(sunA, defaults.coverage, defaults.shadowDensity, sunA, covChanged));

    VolumetricCloudSettings densChanged = defaults;
    densChanged.shadowDensity = defaults.shadowDensity + 0.05f;
    EXPECT_FALSE(CloudShadowHistoryReusable(sunA, defaults.coverage, defaults.shadowDensity, sunA, densChanged));
}

TEST(VolumetricCloudShaderTest, ResolveShaderHasKarisUpsampleDilatedReprojectionAndVarianceClamp)
{
    const std::string resolve = ReadShaderText("VolumetricCloudResolve.comp");
    EXPECT_NE(resolve.find("const float kResolveShoulderStart ="), std::string::npos);
    EXPECT_NE(resolve.find("const float kResolveShoulderCeiling ="), std::string::npos);
    EXPECT_NE(resolve.find("float TapFireflyWeight(vec4 tapColor)"), std::string::npos);
    EXPECT_NE(resolve.find("1.0 - tapColor.a > 0.01"), std::string::npos);
    EXPECT_NE(resolve.find("nearestCloudKm"), std::string::npos);
    EXPECT_NE(resolve.find("float edgeContrast ="), std::string::npos);
    EXPECT_NE(resolve.find("float clampWeight = smoothstep(0.15, 1.25, length(reprojectionShift));"), std::string::npos);
    EXPECT_NE(resolve.find("kept = mix(history, clamped, clampWeight);"), std::string::npos);
    EXPECT_NE(resolve.find("if (pixelEndKm < 50.0 && aux.y > pixelEndKm * 1.08 + 0.15)"), std::string::npos);
    EXPECT_NE(resolve.find("float silhouetteEdge = smoothstep("), std::string::npos);
    EXPECT_NE(resolve.find("cloud.rgb = CompressCloudShoulder(cloud.rgb);"), std::string::npos);
    EXPECT_NE(resolve.find("float shaftCloudWeight = mix(0.06, 1.0, cloud.a * cloud.a);"), std::string::npos);
}

TEST(VolumetricCloudShaderTest, MarchShaderRemovesSkinNormalAndSuppressesVerticalPillars)
{
    const std::string march = ReadShaderText("VolumetricCloud.comp");
    const std::string lighting = ReadShaderText("VolumetricCloudLighting.glsl");
    EXPECT_EQ(march.find("SkinNormal"), std::string::npos);
    EXPECT_EQ(march.find("fbm - 1.0"), std::string::npos);
    EXPECT_EQ(march.find("exp(-params.weather.w * 0.75)"), std::string::npos);
    EXPECT_EQ(march.find("0.5 * stepLength"), std::string::npos);
    EXPECT_NE(march.find("const float kCoverageGain = 1.18;"), std::string::npos);
    EXPECT_EQ(march.find("domeSampleA"), std::string::npos);
    EXPECT_EQ(march.find("domeSampleB"), std::string::npos);
    EXPECT_EQ(march.find("flB"), std::string::npos);
    EXPECT_NE(march.find("bool gPrimaryRay = true;"), std::string::npos);
    EXPECT_NE(march.find("weatherCoverage *= smoothstep(0.10, 0.25, massAverage);"), std::string::npos);
    EXPECT_NE(march.find("float coreTurret ="), std::string::npos);
    EXPECT_NE(march.find("float shoulderEnv ="), std::string::npos);
    EXPECT_NE(march.find("float hMax = clamp("), std::string::npos);
    EXPECT_NE(march.find("float peakFloor ="), std::string::npos);
    EXPECT_NE(march.find("if (heightFraction >= hMaxCeil)"), std::string::npos);
    EXPECT_NE(march.find("float localTop = clamp("), std::string::npos);
    EXPECT_NE(march.find("float hNorm = Saturate(heightFraction / max(localTop, 0.16));"), std::string::npos);
    EXPECT_LT(march.find("if (heightFraction >= localTop)"), march.find("vec4 shapeSample = textureLod(shapeVolume,"));
    EXPECT_NE(march.find("float nearLarge = clamp("), std::string::npos);
    EXPECT_NE(march.find("float largeShare = Saturate(nearLarge + params.structure2.w * farRamp);"), std::string::npos);
    EXPECT_NE(march.find("if (largeShare > 0.02)"), std::string::npos);
    EXPECT_LT(march.find("float largeShare = Saturate(nearLarge + params.structure2.w * farRamp);"),
        march.find("shapeUv / kLargeShapeScale + kLargeShapeOffset"));
    EXPECT_NE(march.find("float roundTop = 1.0 - smoothstep(localTop"), std::string::npos);
    EXPECT_NE(march.find("float basePedestal ="), std::string::npos);
    EXPECT_NE(march.find("float domeTaper = Saturate(1.0 - taperStrength * taperCurve);"), std::string::npos);
    EXPECT_NE(march.find("float rawEff = Saturate(flankCov * basePedestal * domeTaper);"), std::string::npos);
    EXPECT_NE(march.find("float LumpDome(float v, float invR2)"), std::string::npos);
    EXPECT_NE(march.find("float skinCarve ="), std::string::npos);
    EXPECT_NE(march.find("float puff ="), std::string::npos);
    EXPECT_NE(march.find("return Saturate(body);"), std::string::npos);
    EXPECT_EQ(march.find("floretCarve"), std::string::npos);
    EXPECT_EQ(march.find("flA"), std::string::npos);
    EXPECT_NE(march.find("float billowErosion ="), std::string::npos);
    EXPECT_NE(march.find("float tBack = max(t - lastCoarseStep, tStart);"), std::string::npos);
    EXPECT_NE(march.find("float tMid = max(t - 0.5 * lastCoarseStep, tStart);"), std::string::npos);
    EXPECT_NE(march.find("lastCoarseStep = gWeatherClear ? coarseLength * params.skip.x : coarseLength;"), std::string::npos);
    EXPECT_NE(march.find("detailAmount *= 1.0 - smoothstep(18000.0, 52000.0, fromEyeLength);"), std::string::npos);
    EXPECT_NE(lighting.find("(i <= 1u && params.lightMisc.w > 0.0)"), std::string::npos);
    EXPECT_NE(lighting.find("CloudDensityAt(samplePosition, params.lightMisc.w * (i == 0u ? 1.0 : 0.55))"), std::string::npos);
    EXPECT_NE(lighting.find("float silverMask ="), std::string::npos);
    EXPECT_NE(lighting.find("scatter = scatter / (1.0 + scatter / kScatterShoulder);"), std::string::npos);
    EXPECT_NE(lighting.find("smoothstep(0.0, kPowderDensityFadeEnd, s.density)"), std::string::npos);
    EXPECT_NE(lighting.find("EffectiveSunWarmth(params.lightTune2.x, s.sunDirection.y)"), std::string::npos);
}

TEST(VolumetricCloudLightingTest, LowSunWarmthCrownLiftAndOpticalDepthInterpolationBehaveSmoothly)
{
    EXPECT_FLOAT_EQ(CloudEffectiveSunWarmth(0.0f, 0.10f), 0.0f);
    EXPECT_FLOAT_EQ(CloudEffectiveSunWarmth(0.25f, 0.50f), 0.25f);
    EXPECT_FLOAT_EQ(CloudEffectiveSunWarmth(0.25f, 0.34f), 0.25f);
    const float warmth12Deg = CloudEffectiveSunWarmth(0.25f, 0.2079f);
    EXPECT_GT(warmth12Deg, 0.50f);
    EXPECT_LE(warmth12Deg, 1.0f);

    EXPECT_FLOAT_EQ(CloudCrownScatterLift(0.30f, 0.0f), 0.0f);
    EXPECT_NEAR(CloudCrownScatterLift(0.95f, 0.0f), 0.22f, 1e-6f);
    EXPECT_LT(CloudCrownScatterLift(0.95f, 3.0f), CloudCrownScatterLift(0.95f, 0.2f));

    EXPECT_NEAR(CloudInterpolateOpticalDepth(0.1f, 5.0f, 0.0f), 0.1f, 1e-5f);
    EXPECT_NEAR(CloudInterpolateOpticalDepth(0.1f, 5.0f, 1.0f), 5.0f, 1e-5f);
    EXPECT_NEAR(CloudInterpolateOpticalDepth(35.0f, 35.0f, 0.5f), 35.0f, 1e-4f);
    const float midTau = CloudInterpolateOpticalDepth(0.1f, 4.0f, 0.5f);
    EXPECT_NEAR(std::exp(-midTau), 0.5f * (std::exp(-0.1f) + std::exp(-4.0f)), 1e-5f);
}

TEST(VolumetricCloudLayerTest, ShapeValuePreservesConvexWorleyDomesAndCarvesCellWallCreases)
{
    const VolumetricCloudSettings defaults = SanitizeVolumetricCloudSettings(VolumetricCloudSettings{});
    EXPECT_FLOAT_EQ(kCloudShapeCreaseBias, 0.22f);
    const float domeCenter = CloudShapeValue(
        0.70f, 1.0f, 1.0f, 1.0f, 0.0f, defaults.shapeWorley, defaults.shapeLow, defaults.shapeHigh);
    const float cellWall = CloudShapeValue(
        0.70f, 0.0f, 0.0f, 0.0f, 0.0f, defaults.shapeWorley, defaults.shapeLow, defaults.shapeHigh);
    EXPECT_NEAR(domeCenter, 0.686248f, 1e-4f);
    EXPECT_NEAR(cellWall, 0.226496f, 1e-4f);
    EXPECT_GT(domeCenter, cellWall + 0.40f);

    float previous = 0.0f;
    for (int step = 0; step <= 10; ++step) {
        const float w = static_cast<float>(step) * 0.1f;
        const float current = CloudShapeValue(
            0.70f, w, w, w, 0.0f, defaults.shapeWorley, defaults.shapeLow, defaults.shapeHigh);
        EXPECT_GE(current + 1e-6f, previous) << step;
        previous = current;
    }

    // At far shape LOD, high-frequency Worley channels fade out cleanly so distant lobes never alias.
    EXPECT_FLOAT_EQ(
        CloudShapeValue(0.70f, 0.6f, 0.0f, 0.0f, 1.0f, defaults.shapeWorley, defaults.shapeLow, defaults.shapeHigh),
        CloudShapeValue(0.70f, 0.6f, 1.0f, 1.0f, 1.0f, defaults.shapeWorley, defaults.shapeLow, defaults.shapeHigh));
}

TEST(VolumetricCloudLayerTest, DomedEnvelopeAndHeightMaskedCrownSculptKeepFlatBasesSolid)
{
    const VolumetricCloudSettings defaults = SanitizeVolumetricCloudSettings(VolumetricCloudSettings{});
    EXPECT_FLOAT_EQ(CloudDomedRoofHeight(0.06f, 0.05f, 0.5f, defaults.peakMin), 0.14f);
    EXPECT_FLOAT_EQ(CloudDomedRoofHeight(0.80f, 0.90f, 0.90f, defaults.peakMin), 0.96f);

    // Outer shoulders where turret peak wm.b < 0.20 stay low and peakMin never lifts outer rims.
    const float outerShoulder = CloudDomedRoofHeight(0.25f, 0.10f, 0.75f, defaults.peakMin);
    EXPECT_GT(outerShoulder, 0.16f);
    EXPECT_LT(outerShoulder, 0.28f);
    EXPECT_FLOAT_EQ(CloudDomedRoofHeight(0.35f, 0.30f, 0.75f, 0.90f), CloudDomedRoofHeight(0.35f, 0.30f, 0.75f, 0.0f));

    float prevRoof = 0.0f;
    for (int step = 0; step <= 12; ++step) {
        const float wCov = 0.06f + static_cast<float>(step) * 0.06f;
        const float curRoof = CloudDomedRoofHeight(wCov, 0.75f, 0.75f, defaults.peakMin);
        EXPECT_GE(curRoof + 1e-6f, prevRoof) << step;
        prevRoof = curRoof;
    }

    float prevTurret = 0.0f;
    for (int step = 0; step <= 10; ++step) {
        const float peak = static_cast<float>(step) * 0.1f;
        const float curTurret = CloudDomedRoofHeight(0.75f, peak, 0.80f, defaults.peakMin);
        EXPECT_GE(curTurret + 1e-6f, prevTurret) << step;
        prevTurret = curTurret;
    }

    // Interior column floor prevents shadow pinholes while leaving LCL rims and upper crowns sculpted.
    EXPECT_FLOAT_EQ(CloudCoreFloor(0.20f, 0.25f), 0.0f);
    EXPECT_FLOAT_EQ(CloudCoreFloor(0.80f, 0.25f), 0.48f);
    EXPECT_FLOAT_EQ(CloudCoreFloor(0.80f, 0.75f), 0.0f);
    EXPECT_GT(CloudCoreFloor(0.40f, 0.25f), 0.20f);

    const float centralTurret = CloudTurretLocalTop(0.85f, 0.90f, 0.85f, 0.0f, 0.0f);
    const float shoulderPuff = CloudTurretLocalTop(0.85f, 0.05f, 0.05f, 0.0f, 0.0f);
    EXPECT_GT(centralTurret - shoulderPuff, 0.25f);
    EXPECT_LE(CloudTurretLocalTop(0.96f, 1.0f, 1.0f, 0.0f, 0.0f), 0.98f);
    EXPECT_GE(CloudTurretLocalTop(0.14f, 0.0f, 0.0f, 0.0f, 1.0f), 0.16f);

    // Floret weight stays zero at the flat LCL base, sculpts crowns at any cloud height via hNorm, and fades far away.
    EXPECT_FLOAT_EQ(CloudFloretWeight(0.05f, 0.50f, 3000.0f), 0.0f);
    EXPECT_GT(CloudFloretWeight(0.75f, 0.70f, 3000.0f), 0.95f);
    EXPECT_GT(CloudFloretWeight(0.30f, 0.30f, 3000.0f), 0.80f);
    EXPECT_FLOAT_EQ(CloudFloretWeight(0.75f, 0.50f, 55000.0f), 0.0f);

    // Undersides carry subtle mamma undulation, lower cores stay calm, and crowns and thin shells get full sculpting.
    EXPECT_FLOAT_EQ(CloudCrownShellMask(0.0f, 0.80f, defaults.bodyErosion), 0.16f);
    EXPECT_LT(CloudCrownShellMask(0.12f, 0.80f, defaults.bodyErosion), 0.05f);
    EXPECT_FLOAT_EQ(CloudCrownShellMask(0.60f, 0.80f, defaults.bodyErosion), defaults.bodyErosion);
    EXPECT_FLOAT_EQ(CloudCrownShellMask(0.12f, 0.06f, defaults.bodyErosion), 1.0f);
}

} // namespace
} // namespace gte
