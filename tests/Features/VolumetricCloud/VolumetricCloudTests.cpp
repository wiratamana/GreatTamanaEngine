// Tier-1 tests for the VolumetricCloud settings, the layer math and the shader names.

#include "Core/FeatureKit/ScreenSpaceView.h"
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
    for (const char* asset : { kCloudShapeAsset, kCloudDetailAsset, kCloudWeatherAsset, kCloudBlueNoiseAsset }) {
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

TEST(VolumetricCloudLayerTest, WindMovesAlongTheDirection)
{
    double offset[2] = {};
    double step[2] = {};
    AdvanceCloudWind(offset, step, 10.0f, 0.0f, 2.0f, 0.0);
    EXPECT_NEAR(offset[0], 20.0, 1e-4);
    EXPECT_NEAR(offset[1], 0.0, 1e-4);
    EXPECT_NEAR(step[0], 20.0, 1e-4);
    AdvanceCloudWind(offset, step, 10.0f, 90.0f, 1.0f, 0.0);
    EXPECT_NEAR(offset[1], 10.0, 1e-4);
}

TEST(VolumetricCloudLayerTest, WindPeriodIsTheCommonRepeatOfTheNoiseTiles)
{
    const VolumetricCloudSettings defaults = SanitizeVolumetricCloudSettings(VolumetricCloudSettings{});
    EXPECT_DOUBLE_EQ(CloudWindPeriodM(defaults), 420000.0);
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
    EXPECT_NEAR(offset[0], std::fmod(unwrappedX, period), 1e-3);
    // One float step at the largest offset is far below a metre.
    const float value = static_cast<float>(period);
    EXPECT_LT(std::nextafter(value, 2.0f * value) - value, 0.1f);
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
    EXPECT_EQ(sizeof(VolumetricCloudParamsGpu), 400u);
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
    EXPECT_FLOAT_EQ(defaults.skyAmbient, 1.0f);
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
    EXPECT_GT(d.forwardG, 0.0f);
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
    for (const char* sampler : { "sourceDepth", "shapeVolume", "detailVolume", "weatherMap", "blueNoise",
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
    EXPECT_EQ(result.descriptorBindings.size(), 12u);
    EXPECT_FALSE(result.hasPushConstantRange);
}

TEST(VolumetricCloudShaderNamesTest, ResolveExposesBoundNames)
{
    const ShaderReflectionResult result = ReflectCompute(GTE_TEST_CLOUD_RESOLVE_SPV_PATH);
    const ReflectedDescriptorBinding* params = Find(result, "params");
    ASSERT_NE(params, nullptr);
    EXPECT_EQ(params->type, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
    EXPECT_EQ(params->access, ReflectedAccess::ReadOnly);
    for (const char* sampler : { "sourceColor", "sourceDepth", "lowColor", "lowAux", "historyColor", "historyAux" }) {
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
    EXPECT_EQ(result.descriptorBindings.size(), 10u);
    EXPECT_FALSE(result.hasPushConstantRange);
}

TEST(VolumetricCloudTemporalTest, ResolveBlockLayoutMatchesTheShader)
{
    EXPECT_EQ(offsetof(VolumetricCloudResolveParamsGpu, prevViewProjection), 64u);
    EXPECT_EQ(offsetof(VolumetricCloudResolveParamsGpu, eye), 128u);
    EXPECT_EQ(offsetof(VolumetricCloudResolveParamsGpu, windDelta), 144u);
    EXPECT_EQ(offsetof(VolumetricCloudResolveParamsGpu, blend), 160u);
    EXPECT_EQ(offsetof(VolumetricCloudResolveParamsGpu, motion), 176u);
    EXPECT_EQ(offsetof(VolumetricCloudResolveParamsGpu, counts), 192u);
    EXPECT_EQ(offsetof(VolumetricCloudResolveParamsGpu, sizes), 208u);
    EXPECT_EQ(sizeof(VolumetricCloudResolveParamsGpu), 224u);
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
    const CloudRelativeCamera camera = MakeCloudRelativeCamera(projection * view, eye);
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
                const Vec4 clip = camera.projection * Vec4(ray.x * distance, ray.y * distance, ray.z * distance, 1.0f);
                const double x = (clip.x / clip.w * 0.5f + 0.5f) * width - px;
                const double y = (clip.y / clip.w * 0.5f + 0.5f) * height - py;
                worst = std::max(worst, std::max(std::abs(x), std::abs(y)));
            }
        }
    }
    EXPECT_LT(worst, 0.02);
}

TEST(VolumetricCloudTemporalTest, DivisorRoundsToOneTwoOrFour)
{
    VolumetricCloudSettings s;
    const auto divisorOf = [&](float value) {
        s.resolutionDivisor = value;
        return CloudResolutionDivisor(SanitizeVolumetricCloudSettings(s));
    };
    EXPECT_EQ(divisorOf(1.0f), 1u);
    EXPECT_EQ(divisorOf(1.4f), 1u);
    EXPECT_EQ(divisorOf(2.0f), 2u);
    EXPECT_EQ(divisorOf(2.9f), 2u);
    EXPECT_EQ(divisorOf(3.0f), 4u);
    EXPECT_EQ(divisorOf(4.0f), 4u);
    EXPECT_EQ(divisorOf(99.0f), 4u);
}

TEST(VolumetricCloudTemporalTest, BayerOffsetsVisitEveryCellOncePerCycle)
{
    for (const std::uint32_t divisor : { 2u, 4u }) {
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
    // A cloud 3 km away is behind a mountain 0.5 km away.
    EXPECT_FALSE(CloudTapUsable(sky, 3.0f, 0.4f, 0.5f));
    // No cloud at all is fine for the mountain, a cloud in front of it too.
    EXPECT_TRUE(CloudTapUsable(sky, 0.0f, 1.0f, 0.5f));
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

    // Without accumulation the new samples replace the picture and no history is read.
    s.temporal = 0.0f;
    const VolumetricCloudResolveParamsGpu off = MakeVolumetricCloudResolveParams(s, facts, resolve);
    EXPECT_EQ(off.counts[3], 0u);
    EXPECT_FLOAT_EQ(off.blend[0], 1.0f);
    EXPECT_FLOAT_EQ(off.blend[1], 1.0f);

    // The march block carries the same offset as the resolve block.
    const VolumetricCloudParamsGpu march = MakeVolumetricCloudParams(s, facts);
    EXPECT_EQ(march.lowRes[0], 4u);
    EXPECT_EQ(march.lowRes[1], o.x);
    EXPECT_EQ(march.lowRes[2], o.y);
    EXPECT_EQ(march.fullSize[0], 884u);
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
        const CloudRelativeCamera camera = MakeCloudRelativeCamera(projection * view, eye);
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
            GTE_CLOUD_MEMBER(VolumetricCloudParamsGpu, atmosphereCamera) });
    ExpectBlock(ParseBlock(ReadShaderText("VolumetricCloudResolve.comp"), "VolumetricCloudResolveParams"),
        { GTE_CLOUD_MEMBER(VolumetricCloudResolveParamsGpu, invViewProjection),
            GTE_CLOUD_MEMBER(VolumetricCloudResolveParamsGpu, prevViewProjection),
            GTE_CLOUD_MEMBER(VolumetricCloudResolveParamsGpu, eye),
            GTE_CLOUD_MEMBER(VolumetricCloudResolveParamsGpu, windDelta),
            GTE_CLOUD_MEMBER(VolumetricCloudResolveParamsGpu, blend),
            GTE_CLOUD_MEMBER(VolumetricCloudResolveParamsGpu, motion),
            GTE_CLOUD_MEMBER(VolumetricCloudResolveParamsGpu, counts),
            GTE_CLOUD_MEMBER(VolumetricCloudResolveParamsGpu, sizes) });
#undef GTE_CLOUD_MEMBER
}

TEST(VolumetricCloudTemporalTest, ResolveShaderTapRuleUsesTheSameToleranceAsTheReference)
{
    const std::string source = ReadShaderText("VolumetricCloudResolve.comp");
    EXPECT_NE(source.find("const float kRelative = 0.03;"), std::string::npos);
    EXPECT_NE(source.find("const float kSlackKm = 0.02;"), std::string::npos);
    EXPECT_NE(source.find("const float kClearTransmittance = 0.999;"), std::string::npos);
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
