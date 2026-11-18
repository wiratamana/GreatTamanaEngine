// Checks FeaturePortAdapter state/snapshot/restore and the /bigworld HTTP open/state command handling.

#include "Features/BigWorld/Editor/BigWorldAdapters.h"
#include "Features/BigWorld/Editor/BigWorldModule.h"
#include "Core/FeatureKit/Feature.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <map>
#include <string>
#include <vector>

namespace gte {
namespace {

class FakeDebugFeature final : public fx::Feature, public fx::IFeatureDebugEndpoint {
public:
    explicit FakeDebugFeature(const char* name) : m_name(name) {}

    const char* Name() const override { return m_name; }
    void Setup(fx::FeatureSetup&) override {}
    fx::IFeatureDebugEndpoint* DebugEndpoint() noexcept override { return this; }

    fx::FeatureDebugResult GetState() const override
    {
        fx::FeatureDebugResult out;
        out.fields = fields;
        return out;
    }

    fx::FeatureDebugResult Set(std::span<const fx::FeatureDebugArg> args) override
    {
        for (const fx::FeatureDebugArg& arg : args) {
            const auto it = std::find_if(fields.begin(), fields.end(),
                [&](const fx::FeatureDebugField& f) { return f.key == arg.key; });
            if (it != fields.end()) {
                it->value = arg.value;
            } else {
                fields.push_back({ arg.key, arg.value });
            }
        }
        return GetState();
    }

    fx::FeatureDebugResult Invoke(std::string_view, std::span<const fx::FeatureDebugArg>) override
    {
        return GetState();
    }

    std::string ValueText(std::string_view key) const
    {
        const auto it = std::find_if(fields.begin(), fields.end(),
            [&](const fx::FeatureDebugField& f) { return f.key == key; });
        return it == fields.end() ? std::string{} : FeatureDebugValueText(it->value);
    }

    std::vector<fx::FeatureDebugField> fields;

private:
    const char* m_name;
};

class FakeModuleDirectory final : public IEditorModuleDirectory {
public:
    IEngineFeatureModule* FindModule(std::string_view) const override { return nullptr; }
    fx::Feature* FindFeature(std::string_view name) const override
    {
        const auto it = features.find(std::string(name));
        return it == features.end() ? nullptr : it->second;
    }

    std::map<std::string, fx::Feature*> features;
};

TEST(BigWorldModuleTest, FeatureAdapterReadsShadowCloudAndSunStateFromEndpoints)
{
    FakeDebugFeature grass("Grass");
    grass.fields = {
        { "game.status", std::string("Ok") },
        { "tile.present", true },
        { "tile.seaLevel", 0.0 },
        { "tile.revision", std::int64_t{ 4 } },
        { "game.stats.accepted0", std::int64_t{ 1200 } },
        { "game.stats.accepted1", std::int64_t{ 600 } },
        { "game.stats.accepted2", std::int64_t{ 300 } },
        { "game.shadow.cascades", std::int64_t{ 2 } },
        { "game.shadow.cloud", true },
        { "game.shadow.castCascades", std::int64_t{ 1 } },
        { "game.shadow.castBlades", std::int64_t{ 54321 } },
    };
    FakeDebugFeature clouds("VolumetricCloud");
    clouds.fields = { { "enabled", true }, { "coverage", 0.8 } };
    FakeDebugFeature shadow("Shadow");
    shadow.fields = { { "game.sunElevationDegrees", 35.0 } };

    FakeModuleDirectory dir;
    dir.features["Grass"] = &grass;
    dir.features["VolumetricCloud"] = &clouds;
    dir.features["Shadow"] = &shadow;

    FeaturePortAdapter adapter(dir);
    const bigworld::FeatureState s = adapter.GetState();
    EXPECT_TRUE(s.grass.available);
    EXPECT_EQ(s.grass.status, "Ok");
    EXPECT_TRUE(s.grass.tilePresent);
    EXPECT_EQ(s.grass.shadowCascades, 2);
    EXPECT_TRUE(s.grass.receivesSunShadow);
    EXPECT_TRUE(s.grass.receivesCloudShadow);
    EXPECT_EQ(s.grass.castCascades, 1);
    EXPECT_EQ(s.grass.castBlades, 54321);
    EXPECT_TRUE(s.cloudsAvailable);
    EXPECT_TRUE(s.cloudsEnabled);
    EXPECT_TRUE(s.sunAvailable);
    EXPECT_TRUE(s.sunAboveHorizon);
    EXPECT_DOUBLE_EQ(s.sunElevationDegrees, 35.0);

    shadow.fields[0].value = -4.0;
    const bigworld::FeatureState lowSun = adapter.GetState();
    EXPECT_TRUE(lowSun.sunAvailable);
    EXPECT_FALSE(lowSun.sunAboveHorizon);
}

TEST(BigWorldModuleTest, FeatureAdapterSnapshotsAndRestoresPlanTweaksRoundTrip)
{
    FakeDebugFeature clouds("VolumetricCloud");
    clouds.fields = {
        { "enabled", false },
        { "coverage", 0.8 },
        { "shadowRangeKm", 32.0 },
        { "shadowSoftness", 1.5 },
        { "shadowFill", 0.25 },
        { "sunRays", 0.8 },
        { "sunRaysContrast", 1.0 },
        { "sunRaysDecay", 1.5 },
        { "sunRaysLength", 1.2 },
        { "maxSteps", 96.0 },
        { "resolutionDivisor", 0.0 },
        { "shadowSteps", 10.0 },
        { "shadowUpdate", 3.0 },
    };
    FakeDebugFeature bloom("Bloom");
    bloom.fields = { { "enabled", true }, { "threshold", 1.0 }, { "intensity", 0.3 } };

    FakeModuleDirectory dir;
    dir.features["VolumetricCloud"] = &clouds;
    dir.features["Bloom"] = &bloom;

    FeaturePortAdapter adapter(dir);
    const bigworld::BigWorldPlan plan = bigworld::MakeBigWorldPlan(bigworld::Quality::Big);
    for (const bigworld::FeatureTweak& tweak : plan.tweaks) {
        if (tweak.feature == "Grass") {
            continue;
        }
        ASSERT_TRUE(adapter.Snapshot(tweak.feature, tweak.restoreKeys).ok);
        ASSERT_TRUE(adapter.Set(tweak.feature, tweak.values).ok);
    }

    EXPECT_EQ(clouds.ValueText("coverage"), "0.56");
    EXPECT_EQ(clouds.ValueText("shadowRangeKm"), "16");
    EXPECT_EQ(clouds.ValueText("shadowSoftness"), "0.5");
    EXPECT_EQ(clouds.ValueText("shadowFill"), "0.2");
    EXPECT_EQ(clouds.ValueText("sunRays"), "0.55");
    EXPECT_EQ(clouds.ValueText("sunRaysContrast"), "1.65");
    EXPECT_EQ(clouds.ValueText("sunRaysDecay"), "0.98");
    EXPECT_EQ(clouds.ValueText("sunRaysLength"), "1.25");
    EXPECT_EQ(clouds.ValueText("maxSteps"), "68");
    EXPECT_EQ(bloom.ValueText("threshold"), "2");
    EXPECT_EQ(bloom.ValueText("intensity"), "0.2");

    ASSERT_TRUE(adapter.RestoreAll().ok);
    // The snapshot keeps the text the endpoint printed, so compare numbers instead of spelling.
    const auto number = [](const FakeDebugFeature& f, std::string_view key) { return std::stod(f.ValueText(key)); };
    EXPECT_EQ(clouds.ValueText("enabled"), "false");
    EXPECT_DOUBLE_EQ(number(clouds, "coverage"), 0.8);
    EXPECT_DOUBLE_EQ(number(clouds, "shadowRangeKm"), 32.0);
    EXPECT_DOUBLE_EQ(number(clouds, "shadowSoftness"), 1.5);
    EXPECT_DOUBLE_EQ(number(clouds, "shadowFill"), 0.25);
    EXPECT_DOUBLE_EQ(number(clouds, "sunRays"), 0.8);
    EXPECT_DOUBLE_EQ(number(clouds, "sunRaysContrast"), 1.0);
    EXPECT_DOUBLE_EQ(number(clouds, "sunRaysDecay"), 1.5);
    EXPECT_DOUBLE_EQ(number(clouds, "sunRaysLength"), 1.2);
    EXPECT_DOUBLE_EQ(number(clouds, "maxSteps"), 96.0);
    EXPECT_DOUBLE_EQ(number(bloom, "threshold"), 1.0);
    EXPECT_DOUBLE_EQ(number(bloom, "intensity"), 0.3);
}

TEST(BigWorldModuleTest, OpenCommandParsesCoverageOverrideAndStateSerializesNewKeys)
{
    FakeDebugFeature grass("Grass");
    grass.fields = {
        { "game.status", std::string("Ok") },
        { "tile.present", true },
        { "tile.seaLevel", 0.0 },
        { "game.shadow.cascades", std::int64_t{ 2 } },
        { "game.shadow.cloud", true },
        { "game.shadow.castCascades", std::int64_t{ 1 } },
        { "game.shadow.castBlades", std::int64_t{ 12345 } },
    };
    FakeDebugFeature clouds("VolumetricCloud");
    clouds.fields = { { "enabled", false }, { "coverage", 0.8 } };
    FakeDebugFeature bloom("Bloom");
    bloom.fields = { { "enabled", true }, { "threshold", 1.0 }, { "intensity", 0.3 } };

    FakeModuleDirectory dir;
    dir.features["Grass"] = &grass;
    dir.features["VolumetricCloud"] = &clouds;
    dir.features["Bloom"] = &bloom;

    BigWorldModule module;
    module.OnAttach(EditorModuleAttach{ dir, nullptr, nullptr });

    const auto bad = module.OnCommand({ "bigworld.open", { { "coverage", "not_a_number" } } });
    ASSERT_TRUE(bad.has_value());
    EXPECT_EQ(bad->httpStatus, 400);

    // Without a TerrainModule attached, Open fails on terrain open (409) after sanitizing and storing the plan.
    const auto opened = module.OnCommand({ "bigworld.open", { { "quality", "big" }, { "coverage", "0.33" } } });
    ASSERT_TRUE(opened.has_value());
    EXPECT_EQ(opened->httpStatus, 409);
    ASSERT_NE(module.Demo(), nullptr);
    EXPECT_DOUBLE_EQ(module.Demo()->State().plan.cloudCoverage, 0.33);

    const auto state = module.OnCommand({ "bigworld.state", {} });
    ASSERT_TRUE(state.has_value());
    EXPECT_EQ(state->httpStatus, 200);
    EXPECT_NE(state->body.find("\"terrainProfile\":\"lean\""), std::string::npos);
    EXPECT_NE(state->body.find("\"cloudCoverage\":0.33"), std::string::npos);
    EXPECT_NE(state->body.find("\"cloudShadowRangeKm\":16"), std::string::npos);
    EXPECT_NE(state->body.find("\"cloudShadowSoftness\":0.5"), std::string::npos);
    EXPECT_NE(state->body.find("\"cloudShadowFill\":0.2"), std::string::npos);
    EXPECT_NE(state->body.find("\"cloudSunRays\":0.55"), std::string::npos);
    EXPECT_NE(state->body.find("\"cloudSunRaysContrast\":1.65"), std::string::npos);
    EXPECT_NE(state->body.find("\"cloudSunRaysDecay\":0.98"), std::string::npos);
    EXPECT_NE(state->body.find("\"cloudSunRaysLength\":1.25"), std::string::npos);
    EXPECT_NE(state->body.find("\"bloomThreshold\":2"), std::string::npos);
    EXPECT_NE(state->body.find("\"bloomIntensity\":0.2"), std::string::npos);
    EXPECT_EQ(state->body.find("volumetric"), std::string::npos);
    EXPECT_EQ(state->body.find("cloudShaftMist"), std::string::npos);
    EXPECT_NE(state->body.find("\"receivesSunShadow\":true"), std::string::npos);
    EXPECT_NE(state->body.find("\"receivesCloudShadow\":true"), std::string::npos);
    EXPECT_NE(state->body.find("\"castCascades\":1"), std::string::npos);
    EXPECT_NE(state->body.find("\"castBlades\":12345"), std::string::npos);

    module.OnShutdown();
}

} // namespace
} // namespace gte
