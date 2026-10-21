// Tests for the Atmosphere /feature_debug endpoint. They need a headless Core and skip without one.

#include "Core/Core.h"
#include "Core/IHostServices.h"
#include "Features/Atmosphere/AtmosphereFeature.h"
#include "Features/Atmosphere/AtmosphereParameters.h"

#include "../../Fakes/HeadlessSurfaceProvider.h"

#include <gtest/gtest.h>

#include <exception>
#include <memory>
#include <string>
#include <variant>
#include <vector>

namespace gte {
namespace {

class NoopHostServices : public IHostServices {
public:
    void Log(LogLevel, std::string_view) override { }
};

const fx::FeatureDebugField* Find(const fx::FeatureDebugResult& result, const std::string& key)
{
    for (const fx::FeatureDebugField& field : result.fields) {
        if (field.key == key) {
            return &field;
        }
    }
    return nullptr;
}

bool FlagOf(const fx::FeatureDebugResult& result, const std::string& key)
{
    const fx::FeatureDebugField* field = Find(result, key);
    return field != nullptr && std::get<bool>(field->value);
}

class AtmosphereEndpointTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        try {
            core = std::make_unique<Core>(surface, services);
        } catch (const std::exception&) {
            GTEST_SKIP() << "Core construction needs headless Vulkan, which this machine lacks.";
        }
        host = std::make_unique<fx::FeatureHost>(*core);
        feature = std::make_unique<AtmosphereFeature>(*host);
        endpoint = feature->DebugEndpoint();
    }

    fx::FeatureDebugResult Set(std::vector<fx::FeatureDebugArg> args) { return endpoint->Set(args); }

    HeadlessSurfaceProvider surface;
    NoopHostServices services;
    std::unique_ptr<Core> core;
    std::unique_ptr<fx::FeatureHost> host;
    std::unique_ptr<AtmosphereFeature> feature;
    fx::IFeatureDebugEndpoint* endpoint = nullptr;
};

TEST_F(AtmosphereEndpointTest, SwitchIsOnByDefaultAndCanBeSet)
{
    EXPECT_TRUE(FlagOf(endpoint->GetState(), "sceneLightFollowsAtmosphere"));

    const fx::FeatureDebugResult result = Set({ { "sceneLightFollowsAtmosphere", "false" } });
    ASSERT_TRUE(result.ok);
    EXPECT_FALSE(FlagOf(result, "sceneLightFollowsAtmosphere"));
    EXPECT_FALSE(feature->Settings().sceneLightFollowsAtmosphere);
}

TEST_F(AtmosphereEndpointTest, StateReportsBothViews)
{
    const fx::FeatureDebugResult state = endpoint->GetState();
    for (const char* key : { "game.active", "game.sunTintR", "game.ambientScale", "game.frameIndex", "scene.active",
             "scene.sunTintB" }) {
        EXPECT_NE(Find(state, key), nullptr) << key;
    }
    EXPECT_FALSE(FlagOf(state, "game.active"));
}

TEST_F(AtmosphereEndpointTest, BadRequestsFailAndKeepTheState)
{
    const fx::FeatureDebugResult readOnly = Set({ { "game.active", "true" } });
    EXPECT_FALSE(readOnly.ok);
    EXPECT_EQ(readOnly.message, "key is read-only");

    EXPECT_FALSE(Set({ { "noSuchKey", "1" } }).ok);
    EXPECT_FALSE(Set({ { "sceneLightFollowsAtmosphere", "maybe" } }).ok);

    // One bad key voids the whole request.
    EXPECT_FALSE(Set({ { "sceneLightFollowsAtmosphere", "false" }, { "noSuchKey", "1" } }).ok);
    EXPECT_TRUE(feature->Settings().sceneLightFollowsAtmosphere);
}

TEST_F(AtmosphereEndpointTest, StateReportsTheAerialValuesOfBothViews)
{
    const fx::FeatureDebugResult state = endpoint->GetState();
    for (const char* prefix : { "game.", "scene." }) {
        for (const char* name : { "aerial.maxDistanceKm", "aerial.exaggeration", "aerial.shadowBlend",
                 "aerial.cascadeCount", "aerial.receiver", "aerial.scaleHeld", "aerial.frameIndex" }) {
            EXPECT_NE(Find(state, std::string(prefix) + name), nullptr) << prefix << name;
        }
    }
    const fx::FeatureDebugField* receiver = Find(state, "game.aerial.receiver");
    ASSERT_NE(receiver, nullptr);
    EXPECT_EQ(std::get<std::string>(receiver->value), "NotChecked");
}

TEST_F(AtmosphereEndpointTest, EveryWritableKeyReadsBackAfterClamping)
{
    const fx::FeatureDebugResult result = Set({ { "autoScale", "false" }, { "aerialPerspectiveStrength", "0.5" },
        { "aerialPerspectiveMaxDistanceKm", "12.5" }, { "aerialPerspectiveDepthExponent", "3" },
        { "aerialPerspectiveSamplesPerSlice", "12" }, { "aerialPerspectiveScatteringExaggeration", "2.5" } });
    ASSERT_TRUE(result.ok);
    const AtmosphereSettings& s = feature->Settings();
    EXPECT_FALSE(s.autoScale);
    EXPECT_FLOAT_EQ(s.aerialPerspectiveStrength, 0.5f);
    EXPECT_FLOAT_EQ(s.aerialPerspectiveMaxDistanceKm, 12.5f);
    EXPECT_FLOAT_EQ(s.aerialPerspectiveDepthExponent, 3.0f);
    EXPECT_EQ(s.aerialPerspectiveSamplesPerSlice, 12);
    EXPECT_FLOAT_EQ(s.aerialPerspectiveScatteringExaggeration, 2.5f);

    ASSERT_TRUE(Set({ { "aerialPerspectiveMaxDistanceKm", "999" }, { "aerialPerspectiveSamplesPerSlice", "500" } }).ok);
    EXPECT_FLOAT_EQ(feature->Settings().aerialPerspectiveMaxDistanceKm, 50.0f);
    EXPECT_EQ(feature->Settings().aerialPerspectiveSamplesPerSlice, 16);
}

TEST_F(AtmosphereEndpointTest, BadFloatTextIsRejectedOrClamped)
{
    for (const char* key : { "aerialPerspectiveStrength", "aerialPerspectiveMaxDistanceKm",
             "aerialPerspectiveDepthExponent", "aerialPerspectiveScatteringExaggeration" }) {
        for (const char* text : { "nan", "inf", "-inf", "abc", "", "-5", "1e30" }) {
            const fx::FeatureDebugResult result = Set({ { key, text } });
            if (!result.ok) {
                EXPECT_EQ(result.badKey, key) << text;
            }
            const AtmosphereSettings& s = feature->Settings();
            EXPECT_EQ(s.aerialPerspectiveStrength, SanitizeAtmosphereSettings(s).aerialPerspectiveStrength) << key << text;
            EXPECT_EQ(s.aerialPerspectiveMaxDistanceKm, SanitizeAtmosphereSettings(s).aerialPerspectiveMaxDistanceKm)
                << key << text;
            EXPECT_EQ(s.aerialPerspectiveDepthExponent, SanitizeAtmosphereSettings(s).aerialPerspectiveDepthExponent)
                << key << text;
            EXPECT_EQ(s.aerialPerspectiveScatteringExaggeration,
                SanitizeAtmosphereSettings(s).aerialPerspectiveScatteringExaggeration)
                << key << text;
        }
    }
}

TEST_F(AtmosphereEndpointTest, BadIntegerTextIsRejected)
{
    for (const char* text : { "abc", "", "-3", "1e30", "99999999999" }) {
        EXPECT_FALSE(Set({ { "aerialPerspectiveSamplesPerSlice", text } }).ok) << text;
    }
    EXPECT_EQ(feature->Settings().aerialPerspectiveSamplesPerSlice, 8);
}

TEST_F(AtmosphereEndpointTest, AerialStateKeysAreReadOnly)
{
    const fx::FeatureDebugResult result = Set({ { "game.aerial.maxDistanceKm", "1" } });
    EXPECT_FALSE(result.ok);
    EXPECT_EQ(result.message, "key is read-only");
    EXPECT_EQ(result.badKey, "game.aerial.maxDistanceKm");
}

TEST_F(AtmosphereEndpointTest, OneBadAerialKeyVoidsTheWholeRequest)
{
    EXPECT_FALSE(Set({ { "autoScale", "false" }, { "aerialPerspectiveStrength", "abc" } }).ok);
    EXPECT_TRUE(feature->Settings().autoScale);
}

TEST_F(AtmosphereEndpointTest, EveryActionFails)
{
    EXPECT_FALSE(endpoint->Invoke("reset_defaults", {}).ok);
    EXPECT_FALSE(endpoint->Invoke("", {}).ok);
}

} // namespace
} // namespace gte
