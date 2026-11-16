// Tests for the Water /feature_debug endpoint. They need a headless Core and skip without one.

#include "Core/Core.h"
#include "Core/IHostServices.h"
#include "Features/Water/WaterFeature.h"

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

std::string TextOf(const fx::FeatureDebugResult& result, const std::string& key)
{
    const fx::FeatureDebugField* field = Find(result, key);
    return field != nullptr && std::holds_alternative<std::string>(field->value) ? std::get<std::string>(field->value)
                                                                               : std::string{};
}

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

class WaterEndpointTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        try {
            core = std::make_unique<Core>(surface, services);
        } catch (const std::exception&) {
            GTEST_SKIP() << "Core construction needs headless Vulkan, which this machine lacks.";
        }
        host = std::make_unique<fx::FeatureHost>(*core);
        feature = std::make_unique<WaterFeature>(*host);
        endpoint = feature->DebugEndpoint();
    }

    fx::FeatureDebugResult Set(std::vector<fx::FeatureDebugArg> args) { return endpoint->Set(args); }
    fx::FeatureDebugResult Invoke(const char* action, std::vector<fx::FeatureDebugArg> args)
    {
        return endpoint->Invoke(action, args);
    }

    HeadlessSurfaceProvider surface;
    NoopHostServices services;
    std::unique_ptr<Core> core;
    std::unique_ptr<fx::FeatureHost> host;
    std::unique_ptr<WaterFeature> feature;
    fx::IFeatureDebugEndpoint* endpoint = nullptr;
};

TEST_F(WaterEndpointTest, StateListsSettingsAndBothViews)
{
    const fx::FeatureDebugResult state = endpoint->GetState();
    ASSERT_TRUE(state.ok);
    for (const char* key : { "enabled", "seaLevel", "waveCount", "waveHeight", "shallowColor", "ssr", "wetShore", "debugView",
             "bodies.local", "game.status", "scene.status", "game.bodyCount" }) {
        EXPECT_NE(Find(state, key), nullptr) << key;
    }
    EXPECT_EQ(TextOf(state, "game.status"), "NotRendered");
}

TEST_F(WaterEndpointTest, SetAppliesColoursEnumsAndRefusesBadValues)
{
    const fx::FeatureDebugResult ok = Set({ { "shallowColor", "0.2,0.4,0.6" }, { "debugView", "Foam" }, { "waveCount", "99" } });
    ASSERT_TRUE(ok.ok);
    EXPECT_EQ(TextOf(ok, "debugView"), "Foam");
    EXPECT_DOUBLE_EQ(NumberOf(ok, "waveCount"), static_cast<double>(kWaterMaxWaves));

    EXPECT_FALSE(Set({ { "shallowColor", "0.2,0.4" } }).ok);
    EXPECT_FALSE(Set({ { "debugView", "Nope" } }).ok);
    EXPECT_FALSE(Set({ { "game.status", "Ok" } }).ok);
    EXPECT_EQ(Set({ { "choppiness", "x" } }).badKey, "choppiness");
}

TEST_F(WaterEndpointTest, ActionsAddListAndRemoveBodies)
{
    ASSERT_TRUE(Invoke("add_ocean", { { "level", "3" } }).ok);
    ASSERT_TRUE(Invoke("add_lake", { { "minX", "0" }, { "minZ", "0" }, { "maxX", "50" }, { "maxZ", "80" }, { "level", "12" } }).ok);
    ASSERT_TRUE(Invoke("add_lake", { { "points", "0,0;10,0;10,10;0,10" }, { "level", "5" } }).ok);
    const fx::FeatureDebugResult river =
        Invoke("add_river", { { "points", "0,0,10,4;100,0,6,5;200,40,3,6" }, { "spline", "1" }, { "flow", "2" } });
    ASSERT_TRUE(river.ok);
    EXPECT_DOUBLE_EQ(NumberOf(river, "bodies.local"), 4.0);
    EXPECT_NE(Find(river, "bodies.3"), nullptr);

    EXPECT_FALSE(Invoke("add_lake", { { "points", "0,0;1,1" } }).ok);
    EXPECT_FALSE(Invoke("add_river", { { "points", "0,0" } }).ok);
    EXPECT_FALSE(Invoke("add_ocean", { { "bogus", "1" } }).ok);
    EXPECT_FALSE(Invoke("remove_body", { { "index", "9" } }).ok);

    EXPECT_DOUBLE_EQ(NumberOf(Invoke("remove_body", { { "index", "1" } }), "bodies.local"), 3.0);
    EXPECT_DOUBLE_EQ(NumberOf(Invoke("clear_bodies", {}), "bodies.local"), 0.0);
}

TEST_F(WaterEndpointTest, BodyLimitIsEnforced)
{
    for (std::uint32_t i = 0; i < kWaterMaxBodies; ++i) {
        ASSERT_TRUE(Invoke("add_ocean", {}).ok);
    }
    EXPECT_FALSE(Invoke("add_ocean", {}).ok);
}

} // namespace
} // namespace gte
