#include "Game/Physics/PhysicsDebugSettings.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

namespace gte {
namespace {

struct RangeCase {
    const char* key;
    const char* atMin;
    const char* atMax;
    const char* belowMin;
    const char* aboveMax;
};

const std::vector<RangeCase>& NumericCases()
{
    static const std::vector<RangeCase> cases = {
        { "gravity_x", "-1000", "1000", "-1000.5", "1000.5" },
        { "gravity_y", "-1000", "1000", "-1000.5", "1000.5" },
        { "gravity_z", "-1000", "1000", "-1000.5", "1000.5" },
        { "fixed_step_hz", "30", "480", "29.5", "480.5" },
        { "max_steps_per_frame", "1", "32", "0", "33" },
        { "damping_multiplier", "0", "50", "-0.5", "50.5" },
        { "stiffness_multiplier", "0", "50", "-0.5", "50.5" },
        { "bend_limit_scale", "0", "20", "-0.5", "20.5" },
        { "bend_limit_min_degrees", "0", "180", "-0.5", "180.5" },
    };
    return cases;
}

bool Apply(GlobalPhysicsSettings& settings, const char* key, const char* value)
{
    std::string message;
    std::string errorKey;
    return ApplyPhysicsSettingPairs(settings, { { key, value } }, message, errorKey);
}

std::string ValueOf(const GlobalPhysicsSettings& settings, const std::string& key)
{
    for (const auto& [k, v] : DescribeSettings(settings)) {
        if (k == key) {
            return v;
        }
    }
    return "<missing>";
}

TEST(PhysicsDebugSettingsTest, NumericKeysAcceptMinAndMaxAndRejectJustOutside)
{
    for (const RangeCase& c : NumericCases()) {
        GlobalPhysicsSettings settings;
        EXPECT_TRUE(Apply(settings, c.key, c.atMin)) << c.key;
        EXPECT_TRUE(Apply(settings, c.key, c.atMax)) << c.key;

        const std::string before = ValueOf(settings, c.key);
        std::string message;
        std::string errorKey;
        EXPECT_FALSE(ApplyPhysicsSettingPairs(settings, { { c.key, c.belowMin } }, message, errorKey)) << c.key;
        EXPECT_EQ(errorKey, c.key);
        EXPECT_NE(message.find("must be between"), std::string::npos) << message;
        EXPECT_FALSE(Apply(settings, c.key, c.aboveMax)) << c.key;
        EXPECT_EQ(ValueOf(settings, c.key), before) << c.key;
    }
}

TEST(PhysicsDebugSettingsTest, BoolAndEnumKeysAreStrict)
{
    const char* boolKeys[] = { "collision_enabled", "collision_fit_enabled", "bend_limits_enabled", "interpolate_output" };
    for (const char* key : boolKeys) {
        GlobalPhysicsSettings settings;
        EXPECT_TRUE(Apply(settings, key, "false")) << key;
        EXPECT_EQ(ValueOf(settings, key), "false");
        EXPECT_TRUE(Apply(settings, key, "true")) << key;
        EXPECT_EQ(ValueOf(settings, key), "true");
        EXPECT_TRUE(Apply(settings, key, "0")) << key;
        EXPECT_EQ(ValueOf(settings, key), "false");
        EXPECT_TRUE(Apply(settings, key, "1")) << key;
        EXPECT_EQ(ValueOf(settings, key), "true");
        EXPECT_FALSE(Apply(settings, key, "True")) << key;
        EXPECT_FALSE(Apply(settings, key, "yes")) << key;
        EXPECT_FALSE(Apply(settings, key, "")) << key;
    }

    GlobalPhysicsSettings settings;
    EXPECT_TRUE(Apply(settings, "damping_mode", "raw_pmx"));
    EXPECT_TRUE(settings.useRawPmxDamping);
    EXPECT_TRUE(Apply(settings, "damping_mode", "mapped"));
    EXPECT_FALSE(settings.useRawPmxDamping);
    EXPECT_FALSE(Apply(settings, "damping_mode", "Mapped"));
    EXPECT_FALSE(Apply(settings, "damping_mode", "other"));
}

TEST(PhysicsDebugSettingsTest, MalformedNumbersAreRejected)
{
    const char* bad[] = { "12abc", "+1", "nan", "inf", "-inf", "", " 1", "1 ", "0x10", "1e999" };
    for (const char* text : bad) {
        GlobalPhysicsSettings settings;
        EXPECT_FALSE(Apply(settings, "gravity_y", text)) << text;
        EXPECT_FLOAT_EQ(settings.gravity.y, -98.0f) << text;
    }
    GlobalPhysicsSettings settings;
    EXPECT_FALSE(Apply(settings, "max_steps_per_frame", "2.5"));
    EXPECT_FALSE(Apply(settings, "max_steps_per_frame", "+2"));
    EXPECT_EQ(settings.maxStepsPerFrame, 10);
}

TEST(PhysicsDebugSettingsTest, UnknownAndDuplicateKeysNameTheKey)
{
    GlobalPhysicsSettings settings;
    std::string message;
    std::string errorKey;
    EXPECT_FALSE(ApplyPhysicsSettingPairs(settings, { { "no_such_key", "1" } }, message, errorKey));
    EXPECT_EQ(errorKey, "no_such_key");
    EXPECT_NE(message.find("no_such_key"), std::string::npos);

    errorKey.clear();
    EXPECT_FALSE(ApplyPhysicsSettingPairs(settings, { { "gravity_x", "1" }, { "gravity_x", "2" } }, message, errorKey));
    EXPECT_EQ(errorKey, "gravity_x");
    EXPECT_FLOAT_EQ(settings.gravity.x, 0.0f);

    EXPECT_FALSE(Apply(settings, "rig_enabled", "true"));
}

TEST(PhysicsDebugSettingsTest, OneBadPairChangesNothing)
{
    GlobalPhysicsSettings settings;
    const auto before = DescribeSettings(settings);
    std::string message;
    std::string errorKey;
    const bool ok = ApplyPhysicsSettingPairs(
        settings, { { "gravity_y", "-50" }, { "bend_limit_scale", "5" }, { "max_steps_per_frame", "99" } }, message, errorKey);
    EXPECT_FALSE(ok);
    EXPECT_EQ(errorKey, "max_steps_per_frame");
    EXPECT_EQ(DescribeSettings(settings), before);

    EXPECT_TRUE(ApplyPhysicsSettingPairs(settings, { { "gravity_y", "-50" }, { "bend_limit_scale", "5" } }, message, errorKey));
    EXPECT_FLOAT_EQ(settings.gravity.y, -50.0f);
    EXPECT_FLOAT_EQ(settings.bendLimitScale, 5.0f);
}

TEST(PhysicsDebugSettingsTest, FixedStepHzStoresTheStepLength)
{
    GlobalPhysicsSettings settings;
    EXPECT_TRUE(Apply(settings, "fixed_step_hz", "120"));
    EXPECT_NEAR(settings.fixedTimestepSeconds, 1.0 / 120.0, 1e-9);
    EXPECT_EQ(ValueOf(settings, "fixed_step_hz"), "120");
    EXPECT_TRUE(Apply(settings, "fixed_step_hz", "60"));
    EXPECT_EQ(ValueOf(settings, "fixed_step_hz"), "60");
}

TEST(PhysicsDebugSettingsTest, DefaultPresetMatchesTheStructDefaults)
{
    const GlobalPhysicsSettings defaults;
    EXPECT_EQ(DescribeSettings(defaults).size(), 14u);

    GlobalPhysicsSettings legacy;
    ASSERT_TRUE(ApplyPhysicsPreset(legacy, "legacy"));
    ASSERT_TRUE(ApplyPhysicsPreset(legacy, "default"));
    EXPECT_EQ(DescribeSettings(legacy), DescribeSettings(defaults));
    EXPECT_FLOAT_EQ(legacy.gravity.y, defaults.gravity.y);
    EXPECT_EQ(legacy.fixedTimestepSeconds, defaults.fixedTimestepSeconds); // bit exact, no 1/hz round trip.
    EXPECT_EQ(legacy.maxStepsPerFrame, defaults.maxStepsPerFrame);
}

TEST(PhysicsDebugSettingsTest, LegacyPresetSwitchesEveryNewBehaviorOff)
{
    GlobalPhysicsSettings settings;
    ASSERT_TRUE(ApplyPhysicsPreset(settings, "legacy"));
    EXPECT_FLOAT_EQ(settings.gravity.y, -9.8f);
    EXPECT_FLOAT_EQ(settings.gravity.x, 0.0f);
    EXPECT_FLOAT_EQ(settings.gravity.z, 0.0f);
    EXPECT_NEAR(settings.fixedTimestepSeconds, 1.0 / 60.0, 1e-9);
    EXPECT_EQ(settings.maxStepsPerFrame, 4);
    EXPECT_TRUE(settings.collisionEnabled);
    EXPECT_FLOAT_EQ(settings.dampingMultiplier, 1.0f);
    EXPECT_FLOAT_EQ(settings.stiffnessMultiplier, 1.0f);
    EXPECT_TRUE(settings.useRawPmxDamping);
    EXPECT_FALSE(settings.useCollisionFit);
    EXPECT_FALSE(settings.bendLimitsEnabled);
    EXPECT_FLOAT_EQ(settings.bendLimitScale, 1.0f);
    EXPECT_FLOAT_EQ(settings.bendLimitMinDegrees, 0.0f);
    EXPECT_FALSE(settings.interpolateOutput);
}

TEST(PhysicsDebugSettingsTest, UnknownPresetChangesNothing)
{
    GlobalPhysicsSettings settings;
    const auto before = DescribeSettings(settings);
    EXPECT_FALSE(ApplyPhysicsPreset(settings, "Legacy"));
    EXPECT_FALSE(ApplyPhysicsPreset(settings, ""));
    EXPECT_EQ(DescribeSettings(settings), before);
}

TEST(PhysicsDebugSettingsTest, DescribeRoundTripsThroughApply)
{
    GlobalPhysicsSettings legacy;
    ASSERT_TRUE(ApplyPhysicsPreset(legacy, "legacy"));
    const auto text = DescribeSettings(legacy);

    GlobalPhysicsSettings copy;
    std::string message;
    std::string errorKey;
    ASSERT_TRUE(ApplyPhysicsSettingPairs(copy, text, message, errorKey)) << message;
    EXPECT_EQ(DescribeSettings(copy), text);
}

TEST(PhysicsDebugSettingsTest, EntityKeysAreRecognized)
{
    EXPECT_TRUE(IsPhysicsEntitySettingKey("rig_enabled"));
    EXPECT_TRUE(IsPhysicsEntitySettingKey("rig_frozen"));
    EXPECT_FALSE(IsPhysicsEntitySettingKey("gravity_x"));
    EXPECT_FALSE(IsPhysicsEntitySettingKey(""));
}

} // namespace
} // namespace gte
