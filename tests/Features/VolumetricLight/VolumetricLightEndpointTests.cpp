// Tests for the VolumetricLight /feature_debug endpoint. They need a headless Core and skip without one.

#include "Core/Core.h"
#include "Core/IHostServices.h"
#include "Features/VolumetricLight/VolumetricLightFeature.h"

#include "../../Fakes/HeadlessSurfaceProvider.h"

#include <gtest/gtest.h>

#include <cstdint>
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

double NumberOf(const fx::FeatureDebugResult& result, const std::string& key)
{
    const fx::FeatureDebugField* field = Find(result, key);
    return field != nullptr ? std::get<double>(field->value) : -1.0;
}

std::string TextOf(const fx::FeatureDebugResult& result, const std::string& key)
{
    const fx::FeatureDebugField* field = Find(result, key);
    return field != nullptr ? std::get<std::string>(field->value) : std::string{};
}

class VolumetricLightEndpointTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        try {
            core = std::make_unique<Core>(surface, services);
        } catch (const std::exception&) {
            GTEST_SKIP() << "Core construction needs headless Vulkan, which this machine lacks.";
        }
        host = std::make_unique<fx::FeatureHost>(*core);
        feature = std::make_unique<VolumetricLightFeature>(*host);
        endpoint = feature->DebugEndpoint();
    }

    fx::FeatureDebugResult Set(std::vector<fx::FeatureDebugArg> args) { return endpoint->Set(args); }

    HeadlessSurfaceProvider surface;
    NoopHostServices services;
    std::unique_ptr<Core> core;
    std::unique_ptr<fx::FeatureHost> host;
    std::unique_ptr<VolumetricLightFeature> feature;
    fx::IFeatureDebugEndpoint* endpoint = nullptr;
};

TEST_F(VolumetricLightEndpointTest, GetStateListsSettingsAndBothViews)
{
    const fx::FeatureDebugResult state = endpoint->GetState();

    EXPECT_TRUE(state.ok);
    for (const char* key : { "enabled", "density", "maxDistance", "shadowSamples", "debugView", "game.status",
             "game.receiver", "scene.status", "scene.receiver" }) {
        EXPECT_NE(Find(state, key), nullptr) << key;
    }
    EXPECT_EQ(TextOf(state, "game.grid"), "160x90x64");
}

TEST_F(VolumetricLightEndpointTest, SetAppliesAValue)
{
    const fx::FeatureDebugResult result = Set({ { "density", "0.03" } });

    ASSERT_TRUE(result.ok);
    EXPECT_DOUBLE_EQ(NumberOf(result, "density"), 0.03);
    EXPECT_DOUBLE_EQ(NumberOf(endpoint->GetState(), "density"), 0.03);
}

TEST_F(VolumetricLightEndpointTest, BadRequestsFailAndKeepTheOldState)
{
    const double before = NumberOf(endpoint->GetState(), "density");

    const fx::FeatureDebugResult badValue = Set({ { "density", "abc" } });
    EXPECT_FALSE(badValue.ok);
    EXPECT_EQ(badValue.badKey, "density");

    const fx::FeatureDebugResult readOnly = Set({ { "game.status", "Ok" } });
    EXPECT_FALSE(readOnly.ok);
    EXPECT_EQ(readOnly.badKey, "game.status");

    EXPECT_FALSE(Set({ { "noSuchKey", "1" } }).ok);

    // One bad key voids the whole request.
    EXPECT_FALSE(Set({ { "density", "0.5" }, { "albedo", "x" } }).ok);
    EXPECT_DOUBLE_EQ(NumberOf(endpoint->GetState(), "density"), before);
}

TEST_F(VolumetricLightEndpointTest, SetSanitizesTheValues)
{
    const fx::FeatureDebugResult result = Set({ { "shadowSamples", "3" }, { "density", "5" } });

    ASSERT_TRUE(result.ok);
    EXPECT_EQ(std::get<std::int64_t>(Find(result, "shadowSamples")->value), 4);
    EXPECT_DOUBLE_EQ(NumberOf(result, "density"), 1.0);
}

TEST_F(VolumetricLightEndpointTest, DebugViewRoundTripsAndRejectsUnknownNames)
{
    const fx::FeatureDebugResult result = Set({ { "debugView", "InScatterOnly" } });
    ASSERT_TRUE(result.ok);
    EXPECT_EQ(TextOf(result, "debugView"), "InScatterOnly");

    const fx::FeatureDebugResult bad = Set({ { "debugView", "Bogus" } });
    EXPECT_FALSE(bad.ok);
    EXPECT_EQ(bad.badKey, "debugView");
    EXPECT_EQ(TextOf(endpoint->GetState(), "debugView"), "InScatterOnly");
}

TEST_F(VolumetricLightEndpointTest, InvokeResetsDefaultsAndRefusesBadCalls)
{
    const double defaultDensity = NumberOf(endpoint->GetState(), "density");
    ASSERT_TRUE(Set({ { "density", "0.5" }, { "debugView", "Transmittance" } }).ok);

    const fx::FeatureDebugResult reset = endpoint->Invoke("reset_defaults", {});
    ASSERT_TRUE(reset.ok);
    EXPECT_DOUBLE_EQ(NumberOf(reset, "density"), defaultDensity);
    EXPECT_EQ(TextOf(reset, "debugView"), "Off");

    EXPECT_FALSE(endpoint->Invoke("nope", {}).ok);

    const std::vector<fx::FeatureDebugArg> extra{ { "density", "1" } };
    EXPECT_FALSE(endpoint->Invoke("reset_defaults", extra).ok);
}

} // namespace
} // namespace gte
