#include "Features/Terrain/TerrainSettingsKeys.h"

#include <gtest/gtest.h>

#include <set>
#include <string>

namespace gte::terrain {

TEST(TerrainSettingsKeysTest, SetsFloatIntUintAndBoolByName)
{
    TerrainSettings s;
    std::string error;
    EXPECT_TRUE(SetSettingByName(s, "heightScale", "900.5", &error));
    EXPECT_FLOAT_EQ(s.heightScale, 900.5f);
    EXPECT_TRUE(SetSettingByName(s, "octaves", "4", &error));
    EXPECT_EQ(s.octaves, 4);
    EXPECT_TRUE(SetSettingByName(s, "seed", "99", &error));
    EXPECT_EQ(s.seed, 99u);
    EXPECT_TRUE(SetSettingByName(s, "hiZCulling", "false", &error));
    EXPECT_FALSE(s.hiZCulling);
    EXPECT_TRUE(SetSettingByName(s, "hiZCulling", "1", &error));
    EXPECT_TRUE(s.hiZCulling);
}

TEST(TerrainSettingsKeysTest, RejectsUnknownNamesAndBadValues)
{
    TerrainSettings s;
    const TerrainSettings before = s;
    std::string error;
    EXPECT_FALSE(SetSettingByName(s, "nope", "1", &error));
    EXPECT_NE(error.find("unknown"), std::string::npos);
    EXPECT_FALSE(SetSettingByName(s, "heightScale", "abc", &error));
    EXPECT_FALSE(SetSettingByName(s, "heightScale", "nan", &error));
    EXPECT_FALSE(SetSettingByName(s, "seed", "-3", &error));
    EXPECT_FALSE(SetSettingByName(s, "frustumCulling", "maybe", &error));
    EXPECT_EQ(s, before);
}

TEST(TerrainSettingsKeysTest, NamesAreUniqueAndAppearInJson)
{
    const std::vector<std::string_view> names = SettingNames();
    EXPECT_EQ(names.size(), 42u);
    std::set<std::string_view> unique(names.begin(), names.end());
    EXPECT_EQ(unique.size(), names.size());
    const std::string json = SettingsToJson(TerrainSettings{});
    for (const std::string_view name : names) {
        EXPECT_NE(json.find("\"" + std::string(name) + "\":"), std::string::npos) << name;
    }
}

TEST(TerrainSettingsKeysTest, JsonValuesFollowTheSettings)
{
    TerrainSettings s;
    s.seed = 7;
    s.erosionEnabled = false;
    const std::string json = SettingsToJson(s);
    EXPECT_NE(json.find("\"seed\":7"), std::string::npos);
    EXPECT_NE(json.find("\"erosionEnabled\":false"), std::string::npos);
    EXPECT_EQ(json.front(), '{');
    EXPECT_EQ(json.back(), '}');
}

} // namespace gte::terrain
