// Tests for the TemporalAa /feature_debug endpoint. They need a headless Core and skip without one.

#include "Core/Core.h"
#include "Core/IHostServices.h"
#include "Features/TemporalAa/TemporalAaFeature.h"

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

// Reads an integer or float field as a double; -1 when the key is missing or has another type.
double NumberOf(const fx::FeatureDebugResult& result, const std::string& key)
{
    const fx::FeatureDebugField* field = Find(result, key);
    if (field == nullptr) {
        return -1.0;
    }
    if (const double* number = std::get_if<double>(&field->value)) {
        return *number;
    }
    if (const std::int64_t* integer = std::get_if<std::int64_t>(&field->value)) {
        return static_cast<double>(*integer);
    }
    return -1.0;
}

std::string TextOf(const fx::FeatureDebugResult& result, const std::string& key)
{
    const fx::FeatureDebugField* field = Find(result, key);
    return field != nullptr ? std::get<std::string>(field->value) : std::string{};
}

bool BoolOf(const fx::FeatureDebugResult& result, const std::string& key)
{
    const fx::FeatureDebugField* field = Find(result, key);
    return field != nullptr && std::get<bool>(field->value);
}

class TemporalAaEndpointTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        try {
            core = std::make_unique<Core>(surface, services);
        } catch (const std::exception&) {
            GTEST_SKIP() << "Core construction needs headless Vulkan, which this machine lacks.";
        }
        host = std::make_unique<fx::FeatureHost>(*core);
        feature = std::make_unique<TemporalAaFeature>(*host);
        endpoint = feature->DebugEndpoint();
    }

    fx::FeatureDebugResult Set(std::vector<fx::FeatureDebugArg> args) { return endpoint->Set(args); }

    HeadlessSurfaceProvider surface;
    NoopHostServices services;
    std::unique_ptr<Core> core;
    std::unique_ptr<fx::FeatureHost> host;
    std::unique_ptr<TemporalAaFeature> feature;
    fx::IFeatureDebugEndpoint* endpoint = nullptr;
};

TEST_F(TemporalAaEndpointTest, GetStateListsSettingsAndBothViews)
{
    const fx::FeatureDebugResult state = endpoint->GetState();

    EXPECT_TRUE(state.ok);
    for (const char* key : { "enabled", "jitterSampleCount", "jitterScale", "feedbackMin", "feedbackMax", "clipGamma",
             "depthTolerance", "debugView" }) {
        EXPECT_NE(Find(state, key), nullptr) << key;
    }
    for (const char* prefix : { "game.", "scene." }) {
        for (const char* name : { "status", "frameIndex", "jitterDecision", "jitterFrameIndex", "jitterApplied",
                 "jitterPixelX", "jitterPixelY", "historyUsed", "stableFrames", "extentWidth", "extentHeight",
                 "textureBytes" }) {
            EXPECT_NE(Find(state, std::string(prefix) + name), nullptr) << prefix << name;
        }
    }
    EXPECT_EQ(TextOf(state, "debugView"), "Off");
    EXPECT_EQ(TextOf(state, "game.status"), "NotRendered");
    EXPECT_FALSE(BoolOf(state, "game.jitterApplied"));
}

TEST_F(TemporalAaEndpointTest, SetAppliesAndSanitizesValues)
{
    const fx::FeatureDebugResult result = Set({ { "jitterScale", "99" }, { "feedbackMin", "0.9" },
        { "feedbackMax", "0.5" }, { "debugView", "JitterOnly" }, { "enabled", "false" } });

    ASSERT_TRUE(result.ok);
    EXPECT_DOUBLE_EQ(NumberOf(result, "jitterScale"), 8.0);
    EXPECT_DOUBLE_EQ(NumberOf(result, "feedbackMin"), 0.9);
    EXPECT_DOUBLE_EQ(NumberOf(result, "feedbackMax"), 0.9);
    EXPECT_EQ(TextOf(result, "debugView"), "JitterOnly");
    EXPECT_FALSE(BoolOf(result, "enabled"));
    EXPECT_DOUBLE_EQ(NumberOf(endpoint->GetState(), "jitterScale"), 8.0);
}

TEST_F(TemporalAaEndpointTest, BadRequestsFailAndKeepTheOldState)
{
    const double before = NumberOf(endpoint->GetState(), "jitterScale");

    const fx::FeatureDebugResult badValue = Set({ { "jitterScale", "abc" } });
    EXPECT_FALSE(badValue.ok);
    EXPECT_EQ(badValue.badKey, "jitterScale");

    const fx::FeatureDebugResult badBool = Set({ { "enabled", "maybe" } });
    EXPECT_FALSE(badBool.ok);
    EXPECT_EQ(badBool.badKey, "enabled");

    const fx::FeatureDebugResult badEnum = Set({ { "debugView", "Bogus" } });
    EXPECT_FALSE(badEnum.ok);
    EXPECT_EQ(badEnum.badKey, "debugView");

    const fx::FeatureDebugResult readOnly = Set({ { "game.status", "Ok" } });
    EXPECT_FALSE(readOnly.ok);
    EXPECT_EQ(readOnly.badKey, "game.status");

    const fx::FeatureDebugResult sceneReadOnly = Set({ { "scene.jitterApplied", "true" } });
    EXPECT_FALSE(sceneReadOnly.ok);
    EXPECT_EQ(sceneReadOnly.badKey, "scene.jitterApplied");

    const fx::FeatureDebugResult unknown = Set({ { "noSuchKey", "1" } });
    EXPECT_FALSE(unknown.ok);
    EXPECT_EQ(unknown.badKey, "noSuchKey");

    // One bad key voids the whole request.
    const fx::FeatureDebugResult mixed = Set({ { "jitterScale", "3" }, { "clipGamma", "x" } });
    EXPECT_FALSE(mixed.ok);
    EXPECT_EQ(mixed.badKey, "clipGamma");
    EXPECT_DOUBLE_EQ(NumberOf(endpoint->GetState(), "jitterScale"), before);
}

TEST_F(TemporalAaEndpointTest, InvokeResetsDefaultsAndRefusesBadCalls)
{
    ASSERT_TRUE(Set({ { "jitterScale", "3" }, { "debugView", "JitterOnly" }, { "jitterSampleCount", "4" } }).ok);

    const fx::FeatureDebugResult reset = endpoint->Invoke("reset_defaults", {});
    ASSERT_TRUE(reset.ok);
    const TemporalAaSettings defaults{};
    EXPECT_DOUBLE_EQ(NumberOf(reset, "jitterScale"), defaults.jitterScale);
    EXPECT_DOUBLE_EQ(NumberOf(reset, "jitterSampleCount"), static_cast<double>(defaults.jitterSampleCount));
    EXPECT_EQ(TextOf(reset, "debugView"), "Off");
    EXPECT_TRUE(BoolOf(reset, "enabled"));

    EXPECT_FALSE(endpoint->Invoke("nope", {}).ok);

    const std::vector<fx::FeatureDebugArg> extra{ { "jitterScale", "1" } };
    EXPECT_FALSE(endpoint->Invoke("reset_defaults", extra).ok);
}

} // namespace
} // namespace gte
