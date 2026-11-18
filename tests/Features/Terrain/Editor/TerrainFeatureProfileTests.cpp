// Tier 1: which render features the terrain scene switches off, and the culling mode the settings ask for.

#include "Features/Terrain/Editor/TerrainFeatureProfile.h"

#include <gtest/gtest.h>

#include <algorithm>

namespace gte {

namespace {

RenderFeatureDebugEntry Feature(const char* name, bool enabled = true)
{
    RenderFeatureDebugEntry entry;
    entry.name = name;
    entry.enabled = enabled;
    return entry;
}

std::vector<RenderFeatureDebugEntry> AllFeatures()
{
    return { Feature("Atmosphere"), Feature("Shadow"), Feature("Gtao"), Feature("Ssgi"), Feature("VolumetricLight"),
        Feature("VolumetricCloud"), Feature("ToneMapping"), Feature("GpuDrivenScene"), Feature("Bloom") };
}

bool Has(const std::vector<std::string>& names, const char* name)
{
    return std::find(names.begin(), names.end(), name) != names.end();
}

} // namespace

TEST(TerrainFeatureProfileTests, FullProfileSwitchesNothingOff)
{
    EXPECT_TRUE(PlanTerrainFeatureSwitch(terrain::TerrainProfile::Full, AllFeatures()).empty());
}

TEST(TerrainFeatureProfileTests, ScenicKeepsVolumetricLightAndSwitchesOffSsgiAndGtao)
{
    const std::vector<std::string> off = PlanTerrainFeatureSwitch(terrain::TerrainProfile::Scenic, AllFeatures());
    EXPECT_TRUE(Has(off, "Ssgi"));
    EXPECT_TRUE(Has(off, "Gtao"));
    EXPECT_FALSE(Has(off, "VolumetricLight"));
    EXPECT_FALSE(Has(off, "Atmosphere"));
    EXPECT_FALSE(Has(off, "Shadow"));
    EXPECT_FALSE(Has(off, "ToneMapping"));
    EXPECT_FALSE(Has(off, "GpuDrivenScene"));

    const std::vector<RenderFeatureDebugEntry> dotted{ Feature("VolumetricLight.Volume"),
        Feature("VolumetricLight.Composite"), Feature("Ssgi.Trace"), Feature("Gtao.Occlusion") };
    const std::vector<std::string> dottedOff = PlanTerrainFeatureSwitch(terrain::TerrainProfile::Scenic, dotted);
    EXPECT_TRUE(Has(dottedOff, "Ssgi.Trace"));
    EXPECT_TRUE(Has(dottedOff, "Gtao.Occlusion"));
    EXPECT_FALSE(Has(dottedOff, "VolumetricLight.Volume"));
    EXPECT_FALSE(Has(dottedOff, "VolumetricLight.Composite"));
}

TEST(TerrainFeatureProfileTests, LeanSwitchesOffTheHeavyScreenSpaceAndVolumeFeaturesOnly)
{
    const std::vector<std::string> off = PlanTerrainFeatureSwitch(terrain::TerrainProfile::Lean, AllFeatures());
    EXPECT_TRUE(Has(off, "VolumetricLight"));
    EXPECT_TRUE(Has(off, "Ssgi"));
    EXPECT_TRUE(Has(off, "Gtao"));
    EXPECT_FALSE(Has(off, "Atmosphere"));
    EXPECT_FALSE(Has(off, "Shadow"));
    EXPECT_FALSE(Has(off, "ToneMapping"));
    EXPECT_FALSE(Has(off, "GpuDrivenScene")) << "the culling feature must stay on";
}

TEST(TerrainFeatureProfileTests, GeometryOnlyAlsoTurnsOffShadowAtmosphereAndToneMapping)
{
    const std::vector<std::string> off = PlanTerrainFeatureSwitch(terrain::TerrainProfile::GeometryOnly, AllFeatures());
    for (const char* name : { "Atmosphere", "Shadow", "Gtao", "Ssgi", "VolumetricLight", "ToneMapping" }) {
        EXPECT_TRUE(Has(off, name)) << name;
    }
    EXPECT_FALSE(Has(off, "GpuDrivenScene"));
    EXPECT_FALSE(Has(off, "Bloom"));
}

TEST(TerrainFeatureProfileTests, FeaturesThatAreAlreadyOffAreLeftAlone)
{
    std::vector<RenderFeatureDebugEntry> features = AllFeatures();
    for (RenderFeatureDebugEntry& entry : features) {
        if (entry.name == "Ssgi") {
            entry.enabled = false;
        }
    }
    const std::vector<std::string> off = PlanTerrainFeatureSwitch(terrain::TerrainProfile::Lean, features);
    EXPECT_FALSE(Has(off, "Ssgi")) << "a feature the user turned off must not be turned on again later";
    EXPECT_TRUE(Has(off, "Gtao"));
}

TEST(TerrainFeatureProfileTests, CullModeFollowsTheTwoToggles)
{
    terrain::TerrainSettings s;
    s.frustumCulling = true;
    s.hiZCulling = true;
    EXPECT_STREQ(TerrainCullModeName(s), "HiZ");
    s.hiZCulling = false;
    EXPECT_STREQ(TerrainCullModeName(s), "Frustum");
    s.frustumCulling = false;
    s.hiZCulling = true;
    EXPECT_STREQ(TerrainCullModeName(s), "NoCull") << "without the frustum toggle nothing is culled";
    s.hiZCulling = false;
    EXPECT_STREQ(TerrainCullModeName(s), "NoCull");
}

} // namespace gte
