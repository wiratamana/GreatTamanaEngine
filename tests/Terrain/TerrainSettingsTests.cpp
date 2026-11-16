// Tier 1: terrain settings sanitizing, hashing and change classification.

#include "Terrain/TerrainSettings.h"

#include <gtest/gtest.h>

#include <cmath>
#include <limits>

namespace gte::terrain {

TEST(TerrainSettingsTests, DefaultsSurviveSanitizeUnchanged)
{
    const TerrainSettings d{};
    EXPECT_EQ(Sanitize(d), d);
}

TEST(TerrainSettingsTests, SanitizeClampsAndReplacesNaN)
{
    TerrainSettings s;
    s.heightScale = std::numeric_limits<float>::quiet_NaN();
    s.octaves = 99;
    s.gain = -3.0f;
    s.lodLevelCount = 40;
    s.chunkQuads = 50;
    s.worldHalfExtentChunks = 3;
    s.maxViewDistance = std::numeric_limits<float>::infinity();
    const TerrainSettings c = Sanitize(s);
    EXPECT_EQ(c.heightScale, TerrainSettings{}.heightScale);
    EXPECT_EQ(c.octaves, 10);
    EXPECT_GT(c.gain, 0.0f);
    EXPECT_EQ(c.lodLevelCount, 12);
    EXPECT_EQ(c.chunkQuads, 64);
    EXPECT_EQ(c.worldHalfExtentChunks % (1 << (c.lodLevelCount - 1)), 0);
    EXPECT_TRUE(std::isfinite(c.maxViewDistance));
    EXPECT_EQ(Sanitize(c), c);
}

TEST(TerrainSettingsTests, HashChangesWithEveryFieldGroup)
{
    const TerrainSettings base{};
    TerrainSettings a = base;
    a.seed += 1;
    TerrainSettings b = base;
    b.erosionRate += 0.1f;
    TerrainSettings c = base;
    c.hiZCulling = !c.hiZCulling;
    EXPECT_EQ(Hash(base), Hash(TerrainSettings{}));
    EXPECT_NE(Hash(base), Hash(a));
    EXPECT_NE(Hash(base), Hash(b));
    EXPECT_NE(Hash(base), Hash(c));
}

TEST(TerrainSettingsTests, ClassifyChangeSeparatesHeightMaterialAndOptimization)
{
    const TerrainSettings base{};

    EXPECT_EQ(ClassifyChange(base, base), SettingsChange::None);

    TerrainSettings seed = base;
    seed.seed = 5;
    EXPECT_TRUE(HasChange(ClassifyChange(base, seed), SettingsChange::Height));

    TerrainSettings erosion = base;
    erosion.erosionDropletCount = 10;
    const SettingsChange e = ClassifyChange(base, erosion);
    EXPECT_TRUE(HasChange(e, SettingsChange::Height));
    EXPECT_FALSE(HasChange(e, SettingsChange::Material));

    TerrainSettings material = base;
    material.snowMinHeightFraction = 0.4f;
    material.grassMaxSlopeDeg = 20.0f;
    EXPECT_EQ(ClassifyChange(base, material), SettingsChange::Material);

    TerrainSettings opt = base;
    opt.frustumCulling = false;
    opt.maxViewDistance = 500.0f;
    opt.lodDistanceFactor = 4.0f;
    EXPECT_EQ(ClassifyChange(base, opt), SettingsChange::Optimization);

    TerrainSettings geometry = base;
    geometry.lod0Spacing = 1.0f;
    const SettingsChange g = ClassifyChange(base, geometry);
    EXPECT_TRUE(HasChange(g, SettingsChange::Geometry));
    EXPECT_TRUE(HasChange(g, SettingsChange::Height));
}

TEST(TerrainSettingsTests, LayoutMatchesDefaultWorldSize)
{
    const TerrainLayout l = MakeLayout(TerrainSettings{});
    EXPECT_EQ(l.verticesPerSide, 65);
    EXPECT_DOUBLE_EQ(l.nodeSizeLevel0, 128.0);
    EXPECT_EQ(l.rootLevel, 7);
    EXPECT_DOUBLE_EQ(l.worldHalfExtent, 16384.0);
    EXPECT_DOUBLE_EQ(NodeSize(l, 7), 16384.0);
    EXPECT_DOUBLE_EQ(NodeSpacing(l, 3), 16.0);
}

} // namespace gte::terrain
