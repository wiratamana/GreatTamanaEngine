// Settings sanitizing, uniform packing and the debug endpoint of the SceneLighting feature.

#include "Features/SceneLighting/SceneLightingDebugEndpoint.h"
#include "Features/SceneLighting/SceneLightingTypes.h"

#include <gtest/gtest.h>

#include <cmath>
#include <cstring>
#include <limits>
#include <string>
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

TEST(SceneLightingSettingsTest, SanitizeClampsExposure)
{
    SceneLightingSettings s;
    s.exposure = 0.0f;
    EXPECT_FLOAT_EQ(SanitizeSceneLightingSettings(s).exposure, 0.01f);
    s.exposure = 100.0f;
    EXPECT_FLOAT_EQ(SanitizeSceneLightingSettings(s).exposure, 16.0f);
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

TEST(SceneLightingSettingsTest, PackWritesAmbientAndExposure)
{
    SceneLightingSettings s;
    s.exposure = 0.5f;
    s.ambientIntensity = 2.0f;
    const SceneLightingUniformData data = PackSceneLightingUniformData(ResolvedDirectionalLight{}, s);
    EXPECT_FLOAT_EQ(data.groundAmbientAndExposure[3], 0.5f);
    EXPECT_FLOAT_EQ(data.skyAmbientAndIntensity[3], 2.0f);
    EXPECT_FLOAT_EQ(data.skyAmbientAndIntensity[0], s.skyAmbientR);
    EXPECT_FLOAT_EQ(data.groundAmbientAndExposure[2], s.groundAmbientB);
}

TEST(SceneLightingSettingsTest, DefaultSettingsKeepTheSunBytes)
{
    ResolvedDirectionalLight sun{};
    sun.directionTowardSun = Vec3(0.0f, 1.0f, 0.0f);
    sun.sunIlluminance = Vec3(1.0f, 2.0f, 3.0f);
    const SceneLightingUniformData a = PackSceneLightingUniformData(sun);
    const SceneLightingUniformData b = PackSceneLightingUniformData(sun, SceneLightingSettings{});
    EXPECT_EQ(std::memcmp(&a, &b, sizeof(a)), 0);
    EXPECT_FLOAT_EQ(a.sunDirectionAndValid[1], 1.0f);
    EXPECT_FLOAT_EQ(a.sunDirectionAndValid[3], 1.0f);
    EXPECT_FLOAT_EQ(a.sunIlluminanceAndPad[2], 3.0f);
}

TEST(SceneLightingDebugEndpointTest, SetExposureIsReportedInState)
{
    SceneLightingSettings settings;
    SceneLightingDebugEndpoint endpoint(settings);
    const std::vector<FeatureDebugArg> args{ { "exposure", "0.5" } };
    const FeatureDebugResult result = endpoint.Set(args);
    ASSERT_TRUE(result.ok);
    EXPECT_DOUBLE_EQ(FieldValue(result, "exposure"), 0.5);
    EXPECT_FLOAT_EQ(settings.exposure, 0.5f);
}

TEST(SceneLightingDebugEndpointTest, BadValueFailsAndLeavesStateUnchanged)
{
    SceneLightingSettings settings;
    SceneLightingDebugEndpoint endpoint(settings);
    const std::vector<FeatureDebugArg> args{ { "ambientIntensity", "2" }, { "exposure", "nan" } };
    const FeatureDebugResult result = endpoint.Set(args);
    EXPECT_FALSE(result.ok);
    EXPECT_EQ(result.badKey, "exposure");
    EXPECT_FLOAT_EQ(settings.ambientIntensity, SceneLightingSettings{}.ambientIntensity); // All or none.
}

TEST(SceneLightingDebugEndpointTest, UnknownKeyFails)
{
    SceneLightingSettings settings;
    SceneLightingDebugEndpoint endpoint(settings);
    const std::vector<FeatureDebugArg> args{ { "nope", "1" } };
    const FeatureDebugResult result = endpoint.Set(args);
    EXPECT_FALSE(result.ok);
    EXPECT_EQ(result.badKey, "nope");
}

TEST(SceneLightingDebugEndpointTest, ResetDefaultsRestores)
{
    SceneLightingSettings settings;
    SceneLightingDebugEndpoint endpoint(settings);
    const std::vector<FeatureDebugArg> args{ { "exposure", "4" } };
    ASSERT_TRUE(endpoint.Set(args).ok);
    const FeatureDebugResult reset = endpoint.Invoke("reset_defaults", {});
    ASSERT_TRUE(reset.ok);
    EXPECT_FLOAT_EQ(settings.exposure, SceneLightingSettings{}.exposure);
    EXPECT_FALSE(endpoint.Invoke("bogus", {}).ok);
}

TEST(SceneLightingDebugEndpointTest, SetClampsOutOfRangeValues)
{
    SceneLightingSettings settings;
    SceneLightingDebugEndpoint endpoint(settings);
    const std::vector<FeatureDebugArg> args{ { "exposure", "100" } };
    ASSERT_TRUE(endpoint.Set(args).ok);
    EXPECT_FLOAT_EQ(settings.exposure, 16.0f);
}

} // namespace
} // namespace gte
