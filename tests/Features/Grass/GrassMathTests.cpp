// Tests for the Grass settings, grid, planes, params packing and the made-up landscape.

#include "Features/Grass/GrassMath.h"
#include "Game/Lighting/WindState.h"

#include <gtest/gtest.h>

#include <cmath>
#include <cstddef>
#include <limits>

namespace gte {
namespace {

TEST(GrassMathTest, ParamsBlockLayoutIsPinned)
{
    EXPECT_EQ(sizeof(GrassParamsGpu), 608u);
    EXPECT_EQ(offsetof(GrassParamsGpu, planes), 128u);
    EXPECT_EQ(offsetof(GrassParamsGpu, instanceBase), 528u);
    EXPECT_EQ(offsetof(GrassParamsGpu, depthMap), 592u);
    EXPECT_EQ(sizeof(GrassInstanceGpu), 32u);
    EXPECT_EQ(sizeof(GrassPushGpu), 16u);
    EXPECT_EQ(sizeof(GrassStatsGpu), 128u);
    EXPECT_EQ(sizeof(GrassDrawCommand), 16u);
}

TEST(GrassMathTest, DefaultsSurviveSanitizing)
{
    const GrassSettings d;
    const GrassSettings s = SanitizeGrassSettings(d);
    EXPECT_FLOAT_EQ(s.densityLod0, d.densityLod0);
    EXPECT_FLOAT_EQ(s.lod2Distance, d.lod2Distance);
    EXPECT_EQ(s.gridCells, d.gridCells);
    EXPECT_EQ(s.heightTileTexels, d.heightTileTexels);
    EXPECT_EQ(s.instanceCapLod0, d.instanceCapLod0);
}

TEST(GrassMathTest, DensitiesNeverIncreaseWithDistance)
{
    GrassSettings in;
    in.densityLod0 = 4.0f;
    in.densityLod1 = 40.0f;
    in.densityLod2 = 90.0f;
    const GrassSettings s = SanitizeGrassSettings(in);
    EXPECT_FLOAT_EQ(s.densityLod0, 4.0f);
    EXPECT_FLOAT_EQ(s.densityLod1, 4.0f);
    EXPECT_FLOAT_EQ(s.densityLod2, 4.0f);
}

TEST(GrassMathTest, LodDistancesStayOrdered)
{
    GrassSettings in;
    in.lod0Distance = 200.0f;
    in.lod1Distance = 50.0f;
    in.lod2Distance = 20.0f;
    const GrassSettings s = SanitizeGrassSettings(in);
    EXPECT_LT(s.lod0Distance, s.lod1Distance);
    EXPECT_LT(s.lod1Distance, s.lod2Distance);
    EXPECT_GE(s.lod0Distance, 2.0f);
}

TEST(GrassMathTest, NonFiniteValuesFallBackToDefaults)
{
    GrassSettings in;
    in.bladeHeight = std::numeric_limits<float>::quiet_NaN();
    in.maxSlopeDeg = std::numeric_limits<float>::infinity();
    in.colorTip[1] = std::numeric_limits<float>::quiet_NaN();
    const GrassSettings d;
    const GrassSettings s = SanitizeGrassSettings(in);
    EXPECT_FLOAT_EQ(s.bladeHeight, d.bladeHeight);
    EXPECT_FLOAT_EQ(s.colorTip[1], d.colorTip[1]);
    EXPECT_LE(s.maxSlopeDeg, 89.0f);
}

TEST(GrassMathTest, GridCoversTheFarthestLod)
{
    GrassSettings in;
    in.lod2Distance = 300.0f;
    in.gridCells = 4;
    const GrassSettings s = SanitizeGrassSettings(in);
    EXPECT_GE(s.gridCells, MinGrassGridCells(s.lod2Distance));
    EXPECT_LE(s.gridCells, kGrassMaxGridCells);
    EXPECT_GE(s.heightTileTexels * s.heightTileTexelSize, 2.0f * (s.lod2Distance + 2.0f * kGrassChunkSizeM) - 1.0f);
    EXPECT_LE(s.heightTileTexels, kGrassMaxTileTexels);
}

TEST(GrassMathTest, InstanceRegionsStayUnderTheBufferLimit)
{
    GrassSettings in;
    in.instanceCapLod0 = 2000000;
    in.instanceCapLod1 = 2000000;
    in.instanceCapLod2 = 2000000;
    const GrassSettings s = SanitizeGrassSettings(in);
    EXPECT_LE(GrassInstanceBytes(s), 128ull * 1024 * 1024);
}

TEST(GrassMathTest, GridOriginMovesOnlyAtChunkBorders)
{
    const GrassGrid a = MakeGrassGrid(100.0, -40.0, 22);
    const GrassGrid b = MakeGrassGrid(100.0 + 15.0, -40.0, 22);
    EXPECT_EQ(a.originChunkX, 6 - 11);
    EXPECT_EQ(b.originChunkX, a.originChunkX + 1); // 100 m is in chunk 6, 115 m in chunk 7.
    EXPECT_EQ(a.originChunkZ, -3 - 11);
    EXPECT_EQ(a.cells, 22u);
    // A camera far from the origin keeps small grid-relative numbers.
    GrassSettings s;
    GrassFrameInputs in;
    in.eyeX = 15000.5;
    in.eyeZ = -12000.25;
    const GrassParamsGpu p = MakeGrassParams(s, in);
    EXPECT_GE(p.camRel[0], 0.0f);
    EXPECT_LT(p.camRel[0], 16.0f * 22.0f);
    EXPECT_GE(p.camRel[2], 0.0f);
    EXPECT_LT(p.camRel[2], 16.0f * 22.0f);
}

TEST(GrassMathTest, FrustumPlanesSeparateInsideFromOutside)
{
    // Eye at the origin looking down +Z (left-handed, zero-to-one depth).
    const Mat4 vp = Mat4::PerspectiveFovLH_ZO(1.2f, 1.6f, 0.5f, 400.0f);
    float planes[6][4];
    ExtractGrassFrustumPlanes(vp, planes);
    const auto inside = [&](float x, float y, float z) {
        for (const auto& plane : planes) {
            if (plane[0] * x + plane[1] * y + plane[2] * z + plane[3] < 0.0f) {
                return false;
            }
        }
        return true;
    };
    EXPECT_TRUE(inside(0.0f, 0.0f, 10.0f));
    EXPECT_TRUE(inside(5.0f, 1.0f, 20.0f));
    EXPECT_FALSE(inside(0.0f, 0.0f, -10.0f));
    EXPECT_FALSE(inside(0.0f, 0.0f, 0.1f));
    EXPECT_FALSE(inside(0.0f, 0.0f, 500.0f));
    EXPECT_FALSE(inside(100.0f, 0.0f, 10.0f));
    EXPECT_FALSE(inside(0.0f, -100.0f, 10.0f));
    for (const auto& plane : planes) {
        EXPECT_NEAR(plane[0] * plane[0] + plane[1] * plane[1] + plane[2] * plane[2], 1.0f, 1e-4f);
    }
}

TEST(GrassMathTest, ParamsPackTileGridAndRegions)
{
    GrassSettings s = SanitizeGrassSettings(GrassSettings{});
    s.collectStats = true;
    s.hizCulling = true;
    const GrassHeightTile tile = MakeSyntheticGrassTile(GrassSyntheticSpec{}, 10.0, 20.0, 128, 1.0f, 7);
    GrassFrameInputs in;
    in.eyeX = 10.0;
    in.eyeY = 15.0;
    in.eyeZ = 20.0;
    in.tile = &tile;
    in.hizUsable = true;
    const GrassParamsGpu p = MakeGrassParams(s, in);

    const GrassGrid grid = MakeGrassGrid(10.0, 20.0, s.gridCells);
    EXPECT_EQ(p.grid[0], grid.originChunkX);
    EXPECT_EQ(p.grid[2], static_cast<std::int32_t>(s.gridCells));
    EXPECT_NE(p.grid[3] & kGrassFlagTriangleList, 0);
    EXPECT_NE(p.grid[3] & kGrassFlagHiz, 0);
    EXPECT_NE(p.grid[3] & kGrassFlagStats, 0);
    // Texel (0, 0) in grid space plus the grid corner is the tile origin in world space.
    EXPECT_NEAR(p.tile[0] + grid.originChunkX * kGrassChunkSizeM, static_cast<float>(tile.originX), 1e-3f);
    EXPECT_NEAR(p.tile[1] + grid.originChunkZ * kGrassChunkSizeM, static_cast<float>(tile.originZ), 1e-3f);
    EXPECT_FLOAT_EQ(p.tile[3], 128.0f);
    EXPECT_EQ(p.instanceBase[0], 0u);
    EXPECT_EQ(p.instanceBase[1], s.instanceCapLod0);
    EXPECT_EQ(p.instanceBase[2], s.instanceCapLod0 + s.instanceCapLod1);
    EXPECT_EQ(p.instanceCap[2], s.instanceCapLod2);

    s.hizCulling = false;
    EXPECT_EQ(MakeGrassParams(s, in).grid[3] & kGrassFlagHiz, 0);
}

TEST(GrassMathTest, NoTileGivesAnEmptyTileRange)
{
    GrassFrameInputs in;
    const GrassParamsGpu p = MakeGrassParams(GrassSettings{}, in);
    EXPECT_FLOAT_EQ(p.tile[3], 0.0f);
}

TEST(GrassMathTest, CandidateCountRoundsUp)
{
    EXPECT_EQ(GrassCandidatesPerChunk(32.0f), 8192u);
    EXPECT_EQ(GrassCandidatesPerChunk(5.0f), 1280u);
    EXPECT_EQ(GrassCandidatesPerChunk(0.6f), 154u);
    EXPECT_EQ(GrassCandidatesPerChunk(0.0f), 0u);
}

TEST(GrassMathTest, SyntheticTileHasHeightsAndALake)
{
    GrassSyntheticSpec spec;
    spec.kind = GrassSyntheticKind::Flat;
    spec.baseHeight = 12.0f;
    spec.lake = true;
    spec.lakeX = 0.0f;
    spec.lakeZ = 0.0f;
    spec.lakeRadius = 20.0f;
    spec.lakeLevel = 5.0f;
    const GrassHeightTile tile = MakeSyntheticGrassTile(spec, 0.0, 0.0, 128, 1.0f, 3);
    ASSERT_TRUE(tile.Valid());
    const auto at = [&](int i, int j, int channel) { return tile.data[(static_cast<std::size_t>(j) * tile.size + i) * 2u + channel]; };
    // The texel nearest the lake centre is wet, a corner is dry flat ground.
    const int cx = static_cast<int>(-tile.originX);
    const int cz = static_cast<int>(-tile.originZ);
    EXPECT_FLOAT_EQ(at(cx, cz, 1), 1.0f);
    EXPECT_FLOAT_EQ(at(0, 0, 1), 0.0f);
    EXPECT_FLOAT_EQ(at(0, 0, 0), 12.0f);
    EXPECT_LT(at(cx, cz, 0), 5.0f);
    EXPECT_EQ(tile.revision, 3u);
}

TEST(GrassMathTest, TileValidityChecksTheSize)
{
    GrassHeightTile tile;
    EXPECT_FALSE(tile.Valid());
    tile.size = 4;
    tile.data.assign(4 * 4 * 2, 0.0f);
    EXPECT_TRUE(tile.Valid());
    tile.data.pop_back();
    EXPECT_FALSE(tile.Valid());
}

TEST(WindStateTest, SanitizeNormalisesAndClamps)
{
    WindState w;
    w.directionX = 3.0f;
    w.directionZ = 4.0f;
    w.speed = 900.0f;
    const WindState s = SanitizeWindState(w);
    EXPECT_NEAR(s.directionX, 0.6f, 1e-5f);
    EXPECT_NEAR(s.directionZ, 0.8f, 1e-5f);
    EXPECT_FLOAT_EQ(s.speed, 500.0f);

    WindState bad;
    bad.directionX = 0.0f;
    bad.directionZ = 0.0f;
    const WindState fallback = SanitizeWindState(bad);
    EXPECT_NEAR(fallback.directionX, WindState{}.directionX, 1e-6f);
}

} // namespace
} // namespace gte
