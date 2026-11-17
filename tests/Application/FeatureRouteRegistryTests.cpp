#include "Application/FeatureRouteRegistry.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <string>

namespace gte {
namespace {

ModuleRouteSpec Spec(std::string url, std::string prefix, std::vector<std::string> readOnly)
{
    return { std::move(url), std::move(prefix), std::move(readOnly) };
}

TEST(FeatureRouteRegistryTest, ReadOnlyIsDecidedOnTheNameAfterTheActionPrefix)
{
    const ModuleRouteSpec water = Spec("water", "water.", { "state", "map" });
    EXPECT_TRUE(water.IsReadOnly("water.state"));
    EXPECT_TRUE(water.IsReadOnly("water.map"));
    EXPECT_FALSE(water.IsReadOnly("water.add_lake"));
    EXPECT_FALSE(water.IsReadOnly("state")); // Wrong family.
    EXPECT_FALSE(water.IsReadOnly("wat"));
    EXPECT_FALSE(water.IsReadOnly(""));
}

TEST(FeatureRouteRegistryTest, AnEmptyActionPrefixMatchesPlainNames)
{
    const ModuleRouteSpec plain = Spec("things", "", { "state" });
    EXPECT_TRUE(plain.IsReadOnly("state"));
    EXPECT_FALSE(plain.IsReadOnly("open"));
}

TEST(FeatureRouteRegistryTest, ADuplicateUrlPrefixIsRefusedAndTheFirstWins)
{
    FeatureRouteRegistry registry;
    EXPECT_TRUE(registry.AddModuleRoutes(Spec("a", "", { "state" })));
    EXPECT_FALSE(registry.AddModuleRoutes(Spec("a", "x.", { "other" })));
    EXPECT_FALSE(registry.AddModuleRoutes(Spec("", "", {})));
    EXPECT_TRUE(registry.AddModuleRoutes(Spec("b", "b.", {})));
    const std::vector<ModuleRouteSpec> routes = registry.ModuleRoutes();
    ASSERT_EQ(routes.size(), 2u);
    EXPECT_EQ(routes[0].actionPrefix, "");
    EXPECT_EQ(routes[1].urlPrefix, "b");
}

TEST(FeatureRouteRegistryTest, AliasesNeedAllThreePartsAndAUniquePath)
{
    FeatureRouteRegistry registry;
    EXPECT_TRUE(registry.AddDebugAlias({ "/x/one", "Feat", "one" }));
    EXPECT_FALSE(registry.AddDebugAlias({ "/x/one", "Other", "two" }));
    EXPECT_FALSE(registry.AddDebugAlias({ "", "Feat", "one" }));
    EXPECT_FALSE(registry.AddDebugAlias({ "/x/two", "", "one" }));
    EXPECT_FALSE(registry.AddDebugAlias({ "/x/two", "Feat", "" }));
    ASSERT_EQ(registry.DebugAliases().size(), 1u);
    EXPECT_EQ(registry.DebugAliases()[0].featureName, "Feat");
}

// The landscape features register their routes when the editor library is linked in.
TEST(FeatureRouteRegistryTest, TheBuiltInFeaturesRegisteredTheirRoutes)
{
    const std::vector<ModuleRouteSpec> routes = FeatureRouteRegistry::Instance().ModuleRoutes();
    const auto find = [&routes](const std::string& url) {
        return std::find_if(routes.begin(), routes.end(), [&url](const ModuleRouteSpec& r) { return r.urlPrefix == url; });
    };
    const auto terrain = find("terrain");
    const auto water = find("water");
    ASSERT_NE(terrain, routes.end());
    ASSERT_NE(water, routes.end());
    EXPECT_EQ(terrain->actionPrefix, "");
    EXPECT_TRUE(terrain->IsReadOnly("state"));
    EXPECT_TRUE(terrain->IsReadOnly("map"));
    EXPECT_FALSE(terrain->IsReadOnly("open"));
    EXPECT_EQ(water->actionPrefix, "water.");
    EXPECT_TRUE(water->IsReadOnly("water.probe"));
    EXPECT_FALSE(water->IsReadOnly("water.add_lake"));

    const std::vector<DebugRouteAlias> aliases = FeatureRouteRegistry::Instance().DebugAliases();
    for (const char* path : { "/grass/tile", "/grass/probe", "/grass/probe_batch" }) {
        EXPECT_TRUE(std::any_of(aliases.begin(), aliases.end(), [path](const DebugRouteAlias& a) { return a.path == path; })) << path;
    }
}

} // namespace
} // namespace gte
