// Tests for the Atmosphere /feature_debug endpoint. They need a headless Core and skip without one.

#include "Core/Core.h"
#include "Core/IHostServices.h"
#include "Features/Atmosphere/AtmosphereFeature.h"

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

TEST_F(AtmosphereEndpointTest, EveryActionFails)
{
    EXPECT_FALSE(endpoint->Invoke("reset_defaults", {}).ok);
    EXPECT_FALSE(endpoint->Invoke("", {}).ok);
}

} // namespace
} // namespace gte
