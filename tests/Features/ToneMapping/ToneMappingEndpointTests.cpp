// Tests for the ToneMapping /feature_debug endpoint. They need a headless Core and skip without one.

#include "Core/Core.h"
#include "Core/IHostServices.h"
#include "Features/ToneMapping/ToneMappingFeature.h"

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

class ToneMappingEndpointTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        try {
            core = std::make_unique<Core>(surface, services);
        } catch (const std::exception&) {
            GTEST_SKIP() << "Core construction needs headless Vulkan, which this machine lacks.";
        }
        host = std::make_unique<fx::FeatureHost>(*core);
        feature = std::make_unique<ToneMappingFeature>(*host);
        endpoint = feature->DebugEndpoint();
    }

    fx::FeatureDebugResult Set(std::vector<fx::FeatureDebugArg> args) { return endpoint->Set(args); }

    HeadlessSurfaceProvider surface;
    NoopHostServices services;
    std::unique_ptr<Core> core;
    std::unique_ptr<fx::FeatureHost> host;
    std::unique_ptr<ToneMappingFeature> feature;
    fx::IFeatureDebugEndpoint* endpoint = nullptr;
};

TEST_F(ToneMappingEndpointTest, GetStateListsSettingsAndBothViews)
{
    const fx::FeatureDebugResult state = endpoint->GetState();

    EXPECT_TRUE(state.ok);
    for (const char* key : { "exposure", "saturation", "dither", "mode", "game.status", "game.frameIndex",
             "scene.status", "scene.frameIndex" }) {
        EXPECT_NE(Find(state, key), nullptr) << key;
    }
    EXPECT_EQ(TextOf(state, "mode"), "Aces");
}

TEST_F(ToneMappingEndpointTest, SetAppliesAndSanitizesValues)
{
    const fx::FeatureDebugResult result = Set({ { "exposure", "0.4" }, { "dither", "9" }, { "mode", "PbrNeutral" } });

    ASSERT_TRUE(result.ok);
    EXPECT_DOUBLE_EQ(NumberOf(result, "exposure"), 0.4);
    EXPECT_DOUBLE_EQ(NumberOf(result, "dither"), 2.0);
    EXPECT_EQ(TextOf(result, "mode"), "PbrNeutral");
    EXPECT_DOUBLE_EQ(NumberOf(endpoint->GetState(), "exposure"), 0.4);
}

TEST_F(ToneMappingEndpointTest, BadRequestsFailAndKeepTheOldState)
{
    const double before = NumberOf(endpoint->GetState(), "exposure");

    const fx::FeatureDebugResult badValue = Set({ { "exposure", "abc" } });
    EXPECT_FALSE(badValue.ok);
    EXPECT_EQ(badValue.badKey, "exposure");

    const fx::FeatureDebugResult badMode = Set({ { "mode", "Bogus" } });
    EXPECT_FALSE(badMode.ok);
    EXPECT_EQ(badMode.badKey, "mode");

    const fx::FeatureDebugResult readOnly = Set({ { "game.status", "Ok" } });
    EXPECT_FALSE(readOnly.ok);
    EXPECT_EQ(readOnly.badKey, "game.status");

    EXPECT_FALSE(Set({ { "noSuchKey", "1" } }).ok);

    // One bad key voids the whole request.
    EXPECT_FALSE(Set({ { "exposure", "3" }, { "saturation", "x" } }).ok);
    EXPECT_DOUBLE_EQ(NumberOf(endpoint->GetState(), "exposure"), before);
}

TEST_F(ToneMappingEndpointTest, InvokeResetsDefaultsAndRefusesBadCalls)
{
    const double defaultExposure = NumberOf(endpoint->GetState(), "exposure");
    ASSERT_TRUE(Set({ { "exposure", "3" }, { "mode", "Clamp" } }).ok);

    const fx::FeatureDebugResult reset = endpoint->Invoke("reset_defaults", {});
    ASSERT_TRUE(reset.ok);
    EXPECT_DOUBLE_EQ(NumberOf(reset, "exposure"), defaultExposure);
    EXPECT_EQ(TextOf(reset, "mode"), "Aces");

    EXPECT_FALSE(endpoint->Invoke("nope", {}).ok);

    const std::vector<fx::FeatureDebugArg> extra{ { "exposure", "1" } };
    EXPECT_FALSE(endpoint->Invoke("reset_defaults", extra).ok);
}

} // namespace
} // namespace gte
