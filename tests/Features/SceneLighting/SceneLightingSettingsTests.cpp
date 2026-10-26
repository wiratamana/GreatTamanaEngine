// Settings sanitizing, uniform packing and the debug endpoint of the SceneLighting feature.

#include "Features/SceneLighting/SceneLightingDebugEndpoint.h"
#include "Features/SceneLighting/SceneLightingTypes.h"

#include <gtest/gtest.h>

#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <string>
#include <variant>
#include <vector>

namespace gte {
namespace {

using fx::FeatureDebugArg;
using fx::FeatureDebugResult;

double FieldValue(const FeatureDebugResult& result, const std::string& key)
{
    for (const fx::FeatureDebugField& field : result.fields) {
        if (field.key == key) {
            return std::get<double>(field.value);
        }
    }
    ADD_FAILURE() << "missing field " << key;
    return 0.0;
}

TEST(SceneLightingSettingsTest, SanitizeClampsAmbientIntensity)
{
    SceneLightingSettings s;
    s.ambientIntensity = -1.0f;
    EXPECT_FLOAT_EQ(SanitizeSceneLightingSettings(s).ambientIntensity, 0.0f);
    s.ambientIntensity = 100.0f;
    EXPECT_FLOAT_EQ(SanitizeSceneLightingSettings(s).ambientIntensity, 8.0f);
}

TEST(SceneLightingSettingsTest, SanitizeReplacesNonFiniteWithDefault)
{
    SceneLightingSettings s;
    s.skyAmbientR = std::numeric_limits<float>::quiet_NaN();
    s.ambientIntensity = std::numeric_limits<float>::infinity();
    const SceneLightingSettings out = SanitizeSceneLightingSettings(s);
    const SceneLightingSettings d;
    EXPECT_FLOAT_EQ(out.skyAmbientR, d.skyAmbientR);
    EXPECT_FLOAT_EQ(out.ambientIntensity, d.ambientIntensity);
}

const Vec3 kEye(1.0f, 2.0f, 3.0f);

TEST(SceneLightingSettingsTest, PackWritesAmbientAndLeavesDebugViewZero)
{
    SceneLightingSettings s;
    s.ambientIntensity = 2.0f;
    const SceneLightingUniformData data = PackSceneLightingUniformData(ResolvedDirectionalLight{}, s, kEye);
    EXPECT_FLOAT_EQ(data.groundAmbientAndMaterialDebugView[3], 0.0f);
    EXPECT_FLOAT_EQ(data.skyAmbientAndIntensity[3], 2.0f);
    EXPECT_FLOAT_EQ(data.skyAmbientAndIntensity[0], s.skyAmbientR);
    EXPECT_FLOAT_EQ(data.groundAmbientAndMaterialDebugView[2], s.groundAmbientB);
}

SunShadowReceiver MakeReceiver(std::uint32_t cascades)
{
    SunShadowReceiver r;
    r.cascadeCount = cascades;
    r.resolution = 2048;
    r.materialStrength = 0.75f;
    r.filterRadiusTexels = 1.5f;
    r.constBiasTexels = 2.0f;
    r.normalOffsetTexels = 3.0f;
    r.slopeBiasTexels = 4.0f;
    r.filterSamples = 8;
    r.cameraPosition = Vec3(1.0f, 2.0f, 3.0f);
    r.cameraForward = Vec3(0.0f, 0.0f, 1.0f);
    r.fadeStart = 90.0f;
    r.shadowDistance = 120.0f;
    r.splitFar = { 10.0f, 30.0f, 70.0f, 120.0f };
    r.blendWidth = { 1.0f, 2.0f, 3.0f, 4.0f };
    r.texelWorld = { 0.01f, 0.02f, 0.03f, 0.04f };
    r.depthRange = { 50.0f, 60.0f, 70.0f, 80.0f };
    r.viewProjection[0] = Mat4::Identity();
    r.viewProjection[2] = Mat4::Identity();
    r.viewProjection[2](1, 3) = 7.0f;
    return r;
}

TEST(SceneLightingSettingsTest, PackWithoutReceiverLeavesShadowFieldsZero)
{
    const SceneLightingUniformData none =
        PackSceneLightingUniformData(ResolvedDirectionalLight{}, SceneLightingSettings{}, kEye);
    EXPECT_FLOAT_EQ(none.shadowParams[0], 0.0f);
    EXPECT_FLOAT_EQ(none.shadowParams[1], 0.0f);
    EXPECT_FLOAT_EQ(none.cascadeSplitFar[2], 0.0f);
    EXPECT_FLOAT_EQ(none.cascadeViewProjection0.columns[0].x, 0.0f);
}

TEST(SceneLightingSettingsTest, PackCopiesTheReceiverCascades)
{
    const SunShadowReceiver receiver = MakeReceiver(3);
    const SceneLightingUniformData data =
        PackSceneLightingUniformData(ResolvedDirectionalLight{}, SceneLightingSettings{}, kEye, &receiver);
    EXPECT_FLOAT_EQ(data.shadowParams[0], 3.0f);
    EXPECT_FLOAT_EQ(data.shadowParams[1], 0.75f);
    EXPECT_FLOAT_EQ(data.shadowParams[2], 1.0f / 2048.0f);
    EXPECT_FLOAT_EQ(data.shadowParams[3], 1.5f);
    EXPECT_FLOAT_EQ(data.shadowBias[0], 2.0f);
    EXPECT_FLOAT_EQ(data.shadowBias[1], 3.0f);
    EXPECT_FLOAT_EQ(data.shadowBias[2], 4.0f);
    EXPECT_FLOAT_EQ(data.shadowBias[3], 8.0f);
    EXPECT_FLOAT_EQ(data.cameraPositionAndFadeStart[1], 2.0f);
    EXPECT_FLOAT_EQ(data.cameraPositionAndFadeStart[3], 90.0f);
    EXPECT_FLOAT_EQ(data.cameraForwardAndFadeEnd[2], 1.0f);
    EXPECT_FLOAT_EQ(data.cameraForwardAndFadeEnd[3], 120.0f);
    EXPECT_FLOAT_EQ(data.cascadeSplitFar[1], 30.0f);
    EXPECT_FLOAT_EQ(data.cascadeBlendWidth[2], 3.0f);
    EXPECT_FLOAT_EQ(data.cascadeTexelWorld[0], 0.01f);
    EXPECT_FLOAT_EQ(data.cascadeDepthRange[3], 80.0f);
    EXPECT_FLOAT_EQ(data.cascadeViewProjection0(0, 0), 1.0f);
    EXPECT_FLOAT_EQ(data.cascadeViewProjection2(1, 3), 7.0f);
}

TEST(SceneLightingSettingsTest, PackIgnoresAReceiverWithoutMaterialStrength)
{
    SunShadowReceiver receiver = MakeReceiver(3);
    receiver.materialStrength = 0.0f;
    const SceneLightingUniformData data =
        PackSceneLightingUniformData(ResolvedDirectionalLight{}, SceneLightingSettings{}, kEye, &receiver);
    EXPECT_FLOAT_EQ(data.shadowParams[0], 0.0f);
    EXPECT_FLOAT_EQ(data.cascadeSplitFar[1], 0.0f);
}

TEST(SceneLightingSettingsTest, PackClampsTheCascadeCount)
{
    const SunShadowReceiver receiver = MakeReceiver(9);
    const SceneLightingUniformData data =
        PackSceneLightingUniformData(ResolvedDirectionalLight{}, SceneLightingSettings{}, kEye, &receiver);
    EXPECT_FLOAT_EQ(data.shadowParams[0], 4.0f);
}

TEST(SceneLightingSettingsTest, PackWritesTheSunBytes)
{
    ResolvedDirectionalLight sun{};
    sun.directionTowardSun = Vec3(0.0f, 1.0f, 0.0f);
    sun.sunIlluminance = Vec3(1.0f, 2.0f, 3.0f);
    const SceneLightingUniformData a = PackSceneLightingUniformData(sun, SceneLightingSettings{}, kEye);
    EXPECT_FLOAT_EQ(a.sunDirectionAndValid[1], 1.0f);
    EXPECT_FLOAT_EQ(a.sunDirectionAndValid[3], 1.0f);
    EXPECT_FLOAT_EQ(a.sunIlluminance[2], 3.0f);
}

TEST(SceneLightingSettingsTest, DefaultsSurviveSanitizing)
{
    const SceneLightingSettings d;
    const SceneLightingSettings out = SanitizeSceneLightingSettings(d);
    EXPECT_FLOAT_EQ(out.skyAmbientB, d.skyAmbientB);
    EXPECT_FLOAT_EQ(out.groundAmbientR, d.groundAmbientR);
    EXPECT_EQ(out.materialDebugView, MaterialDebugView::Off);
}

TEST(SceneLightingSettingsTest, SanitizeClampsTheMaterialDebugView)
{
    SceneLightingSettings s;
    s.materialDebugView = static_cast<MaterialDebugView>(-5);
    EXPECT_EQ(SanitizeSceneLightingSettings(s).materialDebugView, MaterialDebugView::Off);
    s.materialDebugView = static_cast<MaterialDebugView>(99);
    EXPECT_EQ(SanitizeSceneLightingSettings(s).materialDebugView, MaterialDebugView::BaseColor);
    s.materialDebugView = MaterialDebugView::Tangent;
    EXPECT_EQ(SanitizeSceneLightingSettings(s).materialDebugView, MaterialDebugView::Tangent);
}

TEST(SceneLightingSettingsTest, PackWritesTheMaterialDebugView)
{
    SceneLightingSettings s;
    EXPECT_FLOAT_EQ(PackSceneLightingUniformData(ResolvedDirectionalLight{}, s, kEye)
                        .groundAmbientAndMaterialDebugView[3], 0.0f);
    s.materialDebugView = MaterialDebugView::Metallic;
    EXPECT_FLOAT_EQ(PackSceneLightingUniformData(ResolvedDirectionalLight{}, s, kEye)
                        .groundAmbientAndMaterialDebugView[3], 3.0f);
}

TEST(SceneLightingSettingsTest, PackWritesTheEyeWithoutAReceiver)
{
    const SceneLightingUniformData data =
        PackSceneLightingUniformData(ResolvedDirectionalLight{}, SceneLightingSettings{}, kEye);
    EXPECT_FLOAT_EQ(data.cameraPositionAndFadeStart[0], 1.0f);
    EXPECT_FLOAT_EQ(data.cameraPositionAndFadeStart[1], 2.0f);
    EXPECT_FLOAT_EQ(data.cameraPositionAndFadeStart[2], 3.0f);
    EXPECT_FLOAT_EQ(data.cameraPositionAndFadeStart[3], 0.0f);
}

TEST(SceneLightingSettingsTest, PackTakesTheEyeFromTheParameterNotTheReceiver)
{
    SunShadowReceiver receiver = MakeReceiver(3);
    receiver.cameraPosition = Vec3(9.0f, 9.0f, 9.0f);
    const SceneLightingUniformData data = PackSceneLightingUniformData(
        ResolvedDirectionalLight{}, SceneLightingSettings{}, Vec3(4.0f, 5.0f, 6.0f), &receiver);
    EXPECT_FLOAT_EQ(data.cameraPositionAndFadeStart[0], 4.0f);
    EXPECT_FLOAT_EQ(data.cameraPositionAndFadeStart[1], 5.0f);
    EXPECT_FLOAT_EQ(data.cameraPositionAndFadeStart[2], 6.0f);
    EXPECT_FLOAT_EQ(data.cameraPositionAndFadeStart[3], 90.0f);
}

TEST(SceneLightingSettingsTest, PackWritesZeroForANonFiniteEye)
{
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const SceneLightingUniformData data =
        PackSceneLightingUniformData(ResolvedDirectionalLight{}, SceneLightingSettings{}, Vec3(1.0f, nan, 3.0f));
    EXPECT_FLOAT_EQ(data.cameraPositionAndFadeStart[0], 0.0f);
    EXPECT_FLOAT_EQ(data.cameraPositionAndFadeStart[1], 0.0f);
    EXPECT_FLOAT_EQ(data.cameraPositionAndFadeStart[2], 0.0f);
}

TEST(SceneLightingDebugEndpointTest, SetAmbientIntensityIsReportedInState)
{
    SceneLightingSettings settings;
    SceneLightingDebugEndpoint endpoint(settings);
    const std::vector<FeatureDebugArg> args{ { "ambientIntensity", "0.5" } };
    const FeatureDebugResult result = endpoint.Set(args);
    ASSERT_TRUE(result.ok);
    EXPECT_DOUBLE_EQ(FieldValue(result, "ambientIntensity"), 0.5);
    EXPECT_FLOAT_EQ(settings.ambientIntensity, 0.5f);
}

TEST(SceneLightingDebugEndpointTest, StateHasNoExposureKey)
{
    SceneLightingSettings settings;
    SceneLightingDebugEndpoint endpoint(settings);
    for (const fx::FeatureDebugField& field : endpoint.GetState().fields) {
        EXPECT_NE(field.key, "exposure");
    }
}

TEST(SceneLightingDebugEndpointTest, BadValueFailsAndLeavesStateUnchanged)
{
    SceneLightingSettings settings;
    SceneLightingDebugEndpoint endpoint(settings);
    const std::vector<FeatureDebugArg> args{ { "skyAmbientR", "2" }, { "ambientIntensity", "nan" } };
    const FeatureDebugResult result = endpoint.Set(args);
    EXPECT_FALSE(result.ok);
    EXPECT_EQ(result.badKey, "ambientIntensity");
    EXPECT_FLOAT_EQ(settings.skyAmbientR, SceneLightingSettings{}.skyAmbientR); // All or none.
}

TEST(SceneLightingDebugEndpointTest, UnknownKeyFails)
{
    SceneLightingSettings settings;
    SceneLightingDebugEndpoint endpoint(settings);
    const std::vector<FeatureDebugArg> args{ { "exposure", "1" } };
    const FeatureDebugResult result = endpoint.Set(args);
    EXPECT_FALSE(result.ok);
    EXPECT_EQ(result.badKey, "exposure");
}

TEST(SceneLightingDebugEndpointTest, ResetDefaultsRestores)
{
    SceneLightingSettings settings;
    SceneLightingDebugEndpoint endpoint(settings);
    const std::vector<FeatureDebugArg> args{ { "ambientIntensity", "4" } };
    ASSERT_TRUE(endpoint.Set(args).ok);
    const FeatureDebugResult reset = endpoint.Invoke("reset_defaults", {});
    ASSERT_TRUE(reset.ok);
    EXPECT_FLOAT_EQ(settings.ambientIntensity, SceneLightingSettings{}.ambientIntensity);
    EXPECT_FALSE(endpoint.Invoke("bogus", {}).ok);
}

TEST(SceneLightingDebugEndpointTest, SetClampsOutOfRangeValues)
{
    SceneLightingSettings settings;
    SceneLightingDebugEndpoint endpoint(settings);
    const std::vector<FeatureDebugArg> args{ { "ambientIntensity", "100" } };
    ASSERT_TRUE(endpoint.Set(args).ok);
    EXPECT_FLOAT_EQ(settings.ambientIntensity, 8.0f);
}

TEST(SceneLightingDebugEndpointTest, SetMaterialDebugViewIsReportedAsInteger)
{
    SceneLightingSettings settings;
    SceneLightingDebugEndpoint endpoint(settings);
    const std::vector<FeatureDebugArg> args{ { "materialDebugView", "3" } };
    const FeatureDebugResult result = endpoint.Set(args);
    ASSERT_TRUE(result.ok);
    EXPECT_EQ(settings.materialDebugView, MaterialDebugView::Metallic);
    bool found = false;
    for (const fx::FeatureDebugField& field : result.fields) {
        if (field.key == "materialDebugView") {
            ASSERT_TRUE(std::holds_alternative<std::int64_t>(field.value));
            EXPECT_EQ(std::get<std::int64_t>(field.value), 3);
            found = true;
        }
    }
    EXPECT_TRUE(found);
}

TEST(SceneLightingDebugEndpointTest, BadMaterialDebugViewFailsAndLeavesStateUnchanged)
{
    for (const char* bad : { "9", "-1", "2.5", "x", "" }) {
        SceneLightingSettings settings;
        settings.materialDebugView = MaterialDebugView::Roughness;
        SceneLightingDebugEndpoint endpoint(settings);
        const std::vector<FeatureDebugArg> args{ { "skyAmbientR", "2" }, { "materialDebugView", bad } };
        const FeatureDebugResult result = endpoint.Set(args);
        EXPECT_FALSE(result.ok) << bad;
        EXPECT_EQ(result.badKey, "materialDebugView") << bad;
        EXPECT_EQ(settings.materialDebugView, MaterialDebugView::Roughness) << bad;
        EXPECT_FLOAT_EQ(settings.skyAmbientR, SceneLightingSettings{}.skyAmbientR) << bad;
    }
}

TEST(SceneLightingDebugEndpointTest, ResetDefaultsClearsTheDebugView)
{
    SceneLightingSettings settings;
    SceneLightingDebugEndpoint endpoint(settings);
    const std::vector<FeatureDebugArg> args{ { "materialDebugView", "8" } };
    ASSERT_TRUE(endpoint.Set(args).ok);
    ASSERT_TRUE(endpoint.Invoke("reset_defaults", {}).ok);
    EXPECT_EQ(settings.materialDebugView, MaterialDebugView::Off);
}

SceneLightingUniformData PackWithRecord(const SunLightModulation& record)
{
    ResolvedDirectionalLight sun{};
    sun.sunIlluminance = Vec3(2.0f, 2.0f, 2.0f);
    SceneLightingSettings s;
    s.ambientIntensity = 1.0f;
    return PackSceneLightingUniformData(sun, s, kEye, nullptr, &record);
}

TEST(SceneLightingSettingsTest, PackScalesTheSunAndAmbientByTheRecord)
{
    const SceneLightingUniformData data = PackWithRecord({ true, Vec3(0.5f, 0.25f, 0.0f), 0.4f });
    EXPECT_FLOAT_EQ(data.sunIlluminance[0], 1.0f);
    EXPECT_FLOAT_EQ(data.sunIlluminance[1], 0.5f);
    EXPECT_FLOAT_EQ(data.sunIlluminance[2], 0.0f);
    EXPECT_FLOAT_EQ(data.skyAmbientAndIntensity[3], 0.4f);
    const SceneLightingSettings d;
    EXPECT_FLOAT_EQ(data.skyAmbientAndIntensity[0], d.skyAmbientR);
    EXPECT_FLOAT_EQ(data.skyAmbientAndIntensity[2], d.skyAmbientB);
    EXPECT_FLOAT_EQ(data.groundAmbientAndMaterialDebugView[1], d.groundAmbientG);
    EXPECT_FLOAT_EQ(data.groundAmbientAndMaterialDebugView[3], 0.0f);
    EXPECT_EQ(sizeof(data), 448u);
}

TEST(SceneLightingSettingsTest, PackIgnoresAnInactiveRecord)
{
    const SceneLightingUniformData data = PackWithRecord({ false, Vec3(0.5f, 0.25f, 0.0f), 0.4f });
    EXPECT_FLOAT_EQ(data.sunIlluminance[0], 2.0f);
    EXPECT_FLOAT_EQ(data.sunIlluminance[1], 2.0f);
    EXPECT_FLOAT_EQ(data.sunIlluminance[2], 2.0f);
    EXPECT_FLOAT_EQ(data.skyAmbientAndIntensity[3], 1.0f);
}

TEST(SceneLightingSettingsTest, PackWritesBlackWhenTheSunIsBelowTheHorizon)
{
    const SceneLightingUniformData data = PackWithRecord({ true, Vec3::Zero(), 0.0f });
    EXPECT_FLOAT_EQ(data.sunIlluminance[0], 0.0f);
    EXPECT_FLOAT_EQ(data.sunIlluminance[1], 0.0f);
    EXPECT_FLOAT_EQ(data.sunIlluminance[2], 0.0f);
    EXPECT_FLOAT_EQ(data.skyAmbientAndIntensity[3], 0.0f);
}

TEST(SceneLightingSettingsTest, PackIgnoresANonFiniteRecord)
{
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const SceneLightingUniformData data = PackWithRecord({ true, Vec3(nan, 0.5f, 0.5f), 0.4f });
    EXPECT_FLOAT_EQ(data.sunIlluminance[0], 2.0f);
    EXPECT_FLOAT_EQ(data.sunIlluminance[1], 2.0f);
    EXPECT_FLOAT_EQ(data.skyAmbientAndIntensity[3], 1.0f);
}

TEST(SceneLightingSettingsTest, PackClampsAnOutOfRangeRecord)
{
    const SceneLightingUniformData data = PackWithRecord({ true, Vec3(3.0f, 3.0f, 3.0f), 5.0f });
    EXPECT_FLOAT_EQ(data.sunIlluminance[0], 2.0f);
    EXPECT_FLOAT_EQ(data.sunIlluminance[2], 2.0f);
    EXPECT_FLOAT_EQ(data.skyAmbientAndIntensity[3], 1.0f);
}

TEST(SceneLightingDebugEndpointTest, EndpointReportsTheAppliedRecord)
{
    SceneLightingSettings settings;
    fx::PerView<SunLightModulationSnapshot> applied;
    applied.For(rg::ViewScope::Primary) = { { true, Vec3(0.5f, 0.25f, 0.125f), 0.4f }, 42 };
    SceneLightingDebugEndpoint endpoint(settings, &applied);

    const FeatureDebugResult state = endpoint.GetState();
    EXPECT_DOUBLE_EQ(FieldValue(state, "game.sunTintR"), 0.5);
    EXPECT_DOUBLE_EQ(FieldValue(state, "game.sunTintG"), 0.25);
    EXPECT_DOUBLE_EQ(FieldValue(state, "game.sunTintB"), 0.125);
    EXPECT_NEAR(FieldValue(state, "game.ambientScale"), 0.4, 1e-6);
    EXPECT_DOUBLE_EQ(FieldValue(state, "scene.sunTintR"), 1.0);
    bool found = false;
    for (const fx::FeatureDebugField& field : state.fields) {
        if (field.key == "game.atmosphereLight") {
            EXPECT_TRUE(std::get<bool>(field.value));
            found = true;
        } else if (field.key == "game.frameIndex") {
            EXPECT_EQ(std::get<std::int64_t>(field.value), 42);
        }
    }
    EXPECT_TRUE(found);

    const std::vector<FeatureDebugArg> args{ { "game.ambientScale", "1" } };
    EXPECT_FALSE(endpoint.Set(args).ok);
}

TEST(SceneLightingDebugEndpointTest, EndpointWithoutTheRecordHasNoSuchKeys)
{
    SceneLightingSettings settings;
    SceneLightingDebugEndpoint endpoint(settings);
    for (const fx::FeatureDebugField& field : endpoint.GetState().fields) {
        EXPECT_FALSE(field.key.starts_with("game.") || field.key.starts_with("scene.")) << field.key;
    }
    const std::vector<FeatureDebugArg> args{ { "game.ambientScale", "1" } };
    const FeatureDebugResult result = endpoint.Set(args);
    EXPECT_FALSE(result.ok);
    EXPECT_EQ(result.message, "unknown key");
}

} // namespace
} // namespace gte
