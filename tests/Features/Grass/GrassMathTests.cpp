// Tests for the Grass settings, grid, planes, params packing and the made-up landscape.

#include "Features/Grass/GrassFeature.h"
#include "Features/Grass/GrassMath.h"
#include "Game/Lighting/WindState.h"

#include <gtest/gtest.h>

#include <bit>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <limits>
#include <string>
#include <variant>
#include <vector>

namespace gte {
namespace {

TEST(GrassMathTest, ParamsBlockLayoutIsPinned)
{
    EXPECT_EQ(sizeof(GrassParamsGpu), 656u);
    EXPECT_EQ(offsetof(GrassParamsGpu, densityLog), 640u);
    EXPECT_EQ(offsetof(GrassParamsGpu, look), 608u);
    EXPECT_EQ(offsetof(GrassParamsGpu, extra), 624u);
    EXPECT_EQ(offsetof(GrassParamsGpu, planes), 128u);
    EXPECT_EQ(offsetof(GrassParamsGpu, instanceBase), 528u);
    EXPECT_EQ(offsetof(GrassParamsGpu, depthMap), 592u);
    EXPECT_EQ(sizeof(GrassInstanceGpu), 32u);
    EXPECT_EQ(sizeof(GrassPushGpu), 16u);
    EXPECT_EQ(sizeof(GrassStatsGpu), 128u);
    EXPECT_EQ(sizeof(GrassDrawCommand), 16u);
    EXPECT_EQ(sizeof(GrassShadowParamsGpu), 448u);
    EXPECT_EQ(offsetof(GrassShadowParamsGpu, cascadeSplitFar), 256u);
    EXPECT_EQ(offsetof(GrassShadowParamsGpu, eyeAndFadeStart), 320u);
    EXPECT_EQ(offsetof(GrassShadowParamsGpu, shadowParams), 352u);
    EXPECT_EQ(offsetof(GrassShadowParamsGpu, counts), 384u);
    EXPECT_EQ(offsetof(GrassShadowParamsGpu, cloudGrid), 400u);
    EXPECT_EQ(offsetof(GrassShadowParamsGpu, cloudSun), 432u);
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
    EXPECT_EQ(s.receiveSunShadow, d.receiveSunShadow);
    EXPECT_EQ(s.receiveCloudShadow, d.receiveCloudShadow);
    EXPECT_FLOAT_EQ(s.shadowStrength, d.shadowStrength);
    EXPECT_FLOAT_EQ(s.cloudFill, d.cloudFill);
    EXPECT_FLOAT_EQ(MakeGrassParams(s, GrassFrameInputs{}).skyAmbient[3], d.cloudFill);
    GrassSettings debug = d;
    debug.debugView = GrassDebugView::SunVis;
    EXPECT_EQ(SanitizeGrassSettings(debug).debugView, GrassDebugView::SunVis);
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
    const auto at = [&](int i, int j, int channel) {
        const float value = tile.data[(static_cast<std::size_t>(j) * tile.size + i) * kGrassTileStride + channel];
        return channel == 1 ? (GrassTileSubmerged(value) ? 1.0f : 0.0f) : value;
    };
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
    tile.data.assign(4 * 4 * kGrassTileStride, 0.0f);
    EXPECT_TRUE(tile.Valid());
    tile.data.pop_back();
    EXPECT_FALSE(tile.Valid());
}

TEST(GrassMathTest, DensityFallsSmoothlyFromLodToLod)
{
    const GrassSettings s = SanitizeGrassSettings(GrassSettings{});
    // Full density up close, the LOD1 / LOD2 densities at their distances, nothing above the full density.
    EXPECT_NEAR(GrassDensityAtDistance(s, 1.0f), s.densityLod0, 1e-3f);
    EXPECT_NEAR(GrassDensityAtDistance(s, s.lod0Distance), s.densityLod1, 1e-3f);
    EXPECT_NEAR(GrassDensityAtDistance(s, s.lod1Distance), s.densityLod2, 1e-3f);
    EXPECT_NEAR(GrassDensityAtDistance(s, s.lod2Distance), s.densityLod2, 1e-3f);
    // Monotone, and no step bigger than 10 % per half metre (the old hard step was a factor 6).
    float previous = GrassDensityAtDistance(s, 0.0f);
    for (float d = 0.5f; d < s.lod2Distance; d += 0.5f) {
        const float now = GrassDensityAtDistance(s, d);
        EXPECT_LE(now, previous * 1.0001f) << d;
        EXPECT_GE(now, previous * 0.9f) << d;
        previous = now;
    }
}

TEST(GrassMathTest, DensityBlendSetsTheSharpnessOfTheFall)
{
    GrassSettings s;
    s.densityBlend = 0.05f;
    const GrassSettings sharp = SanitizeGrassSettings(s);
    s.densityBlend = 1.0f;
    const GrassSettings soft = SanitizeGrassSettings(s);
    // Halfway to the LOD0 distance the soft fall has started, the sharp one has not.
    const float probe = 0.9f * sharp.lod0Distance;
    EXPECT_NEAR(GrassDensityAtDistance(sharp, probe), sharp.densityLod0, sharp.densityLod0 * 0.01f);
    EXPECT_LT(GrassDensityAtDistance(soft, probe), soft.densityLod0 * 0.9f);
}

TEST(GrassMathTest, ZeroDensityMeansNoBlades)
{
    GrassSettings s;
    s.densityLod0 = 0.0f;
    EXPECT_FLOAT_EQ(GrassDensityAtDistance(SanitizeGrassSettings(s), 5.0f), 0.0f);
}

TEST(GrassMathTest, UserValuesSurviveATemporaryConflict)
{
    GrassSettings in;
    in.lod1Distance = 80.0f;
    in.lod2Distance = 40.0f; // Conflicts with LOD1 for now.
    const GrassSettings user = SanitizeGrassSettings(in, false);
    EXPECT_FLOAT_EQ(user.lod1Distance, 80.0f);
    EXPECT_FLOAT_EQ(user.lod2Distance, 40.0f);
    const GrassSettings effective = SanitizeGrassSettings(user);
    EXPECT_LT(effective.lod1Distance, effective.lod2Distance);
    // The user raises LOD2 again: the old LOD1 value is back.
    GrassSettings raised = user;
    raised.lod2Distance = 150.0f;
    EXPECT_FLOAT_EQ(SanitizeGrassSettings(raised).lod1Distance, 80.0f);
}

TEST(GrassMathTest, PresetsAreValidAndGrowInReach)
{
    float lastReach = 0.0f;
    std::uint64_t lastBytes = 0;
    for (std::uint32_t i = 0; i < static_cast<std::uint32_t>(GrassPreset::Count); ++i) {
        const GrassSettings s = MakeGrassPreset(static_cast<GrassPreset>(i));
        EXPECT_GE(s.densityLod0, s.densityLod1);
        EXPECT_GE(s.densityLod1, s.densityLod2);
        EXPECT_LT(s.lod0Distance, s.lod1Distance);
        EXPECT_LT(s.lod1Distance, s.lod2Distance);
        EXPECT_GE(s.gridCells, MinGrassGridCells(s.lod2Distance));
        EXPECT_LE(s.heightTileTexels, kGrassMaxTileTexels);
        EXPECT_GE(s.heightTileTexels * s.heightTileTexelSize, 2.0f * (s.lod2Distance + 2.0f * kGrassChunkSizeM) - 1.0f);
        EXPECT_GE(s.lod2Distance, lastReach) << i;
        lastReach = s.lod2Distance;
        lastBytes = GrassInstanceBytes(s);
        EXPECT_LE(lastBytes, 128ull * 1024 * 1024);
    }
}

TEST(GrassMathTest, ApplyPresetKeepsTheLook)
{
    GrassSettings s;
    s.bladeHeight = 1.1f;
    ApplyGrassPreset(s, GrassPreset::Big);
    EXPECT_FLOAT_EQ(s.bladeHeight, 1.1f);
    EXPECT_FLOAT_EQ(s.lod2Distance, MakeGrassPreset(GrassPreset::Big).lod2Distance);
}

TEST(GrassMathTest, TileColourPacksIntoThreeBytes)
{
    const std::uint32_t bits = GrassPackTerrainColor(1.0f, 0.5f, 0.0f);
    EXPECT_EQ(bits & 255u, 255u);
    EXPECT_NEAR(static_cast<float>((bits >> 8) & 255u), 128.0f, 1.0f);
    EXPECT_EQ((bits >> 16) & 255u, 0u);
    EXPECT_EQ(bits >> 24, 0u);
    const float packed = GrassTileFlagsColor(true, bits);
    EXPECT_TRUE(GrassTileSubmerged(packed));
    EXPECT_FALSE(GrassTileSubmerged(GrassTileFlagsColor(false, bits)));
    EXPECT_EQ(std::bit_cast<std::uint32_t>(packed) & 0x00FFFFFFu, bits);
    // Out-of-range input clamps.
    EXPECT_EQ(GrassPackTerrainColor(-3.0f, 9.0f, 0.0f) & 0xFFFFu, 255u << 8);
}

TEST(GrassMathTest, DenseChunksGetMoreWorkgroups)
{
    EXPECT_EQ(GrassSlicesPerChunk(0.0f), 1u);
    EXPECT_EQ(GrassSlicesPerChunk(2.0f), 1u);   // 512 candidates
    EXPECT_EQ(GrassSlicesPerChunk(32.0f), 4u);  // 8192 candidates
    EXPECT_EQ(GrassSlicesPerChunk(500.0f), 16u);
    const GrassParamsGpu p = MakeGrassParams(SanitizeGrassSettings(GrassSettings{}), GrassFrameInputs{});
    EXPECT_FLOAT_EQ(p.extra[3], 5.0f); // 40 blades per m2 -> 10240 candidates
}

TEST(GrassMathTest, WidthAndSegmentsAreClamped)
{
    GrassSettings in;
    in.lod0Segments = 99;
    EXPECT_EQ(SanitizeGrassSettings(in).lod0Segments, kGrassMaxLod0Segments);
    in.lod0Segments = 0;
    EXPECT_EQ(SanitizeGrassSettings(in).lod0Segments, kGrassMinLod0Segments);
    EXPECT_EQ(GrassVertexCount(0, 4), 24u);
    EXPECT_EQ(GrassVertexCount(1, 4), 12u);
    EXPECT_EQ(GrassVertexCount(2, 4), 3u);
    const GrassParamsGpu p = MakeGrassParams(SanitizeGrassSettings(GrassSettings{}), GrassFrameInputs{});
    EXPECT_FLOAT_EQ(p.look[3], 4.0f);
    EXPECT_NEAR(p.densityLog[0], std::log(40.0f), 1e-5f);
    EXPECT_FLOAT_EQ(p.densityLog[3], 1.0f);
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

SunShadowReceiver MakeTestReceiver(std::uint32_t count)
{
    SunShadowReceiver r;
    r.cascadeCount = count;
    r.resolution = 2048;
    for (std::uint32_t c = 0; c < std::min(count, kSunShadowReceiverMaxCascades); ++c) {
        r.maps[c] = fx::Texture{ rg::TextureHandle{ c, 1 }, 3 };
        r.viewProjection[c] = Mat4::Translation(Vec3{ static_cast<float>(c + 1), 0.0f, 0.0f });
        r.splitFar[c] = 25.0f * static_cast<float>(c + 1);
        r.blendWidth[c] = c + 1 < count ? 3.0f : 0.0f;
        r.texelWorld[c] = 0.04f * static_cast<float>(c + 1);
        r.depthRange[c] = 120.0f * static_cast<float>(c + 1);
    }
    r.cameraPosition = Vec3{ 4.0f, 5.0f, 6.0f };
    r.cameraForward = Vec3::Forward();
    r.directionTowardSun = Vec3::Up();
    r.shadowDistance = 100.0f;
    r.fadeStart = 85.0f;
    r.materialStrength = 0.9f;
    r.constBiasTexels = 1.5f;
    r.normalOffsetTexels = 1.2f;
    r.slopeBiasTexels = 2.0f;
    r.filterRadiusTexels = 1.75f;
    r.filterSamples = 16;
    return r;
}

CloudShadowMap MakeTestCloudMap()
{
    CloudShadowMap m;
    m.map = fx::Texture{ rg::TextureHandle{ 9, 1 }, 3 };
    m.sunDirection = Normalize(Vec3{ 0.2f, 0.9f, -0.1f });
    m.centerX = 1200.0f;
    m.centerZ = -400.0f;
    m.extentM = 16000.0f;
    m.layerBaseM = 1500.0f;
    m.layerTopM = 4500.0f;
    m.strength = 0.8f;
    m.skyFill = 0.35f;
    return m;
}

TEST(GrassShadowParamsTest, MissingOrUnusableDataDisablesCascadesAndCloud)
{
    const GrassSettings s = SanitizeGrassSettings(GrassSettings{});
    const GrassShadowParamsGpu empty = MakeGrassShadowParams(nullptr, nullptr, Vec3::Up(), s);
    EXPECT_EQ(empty.counts[0], 0u);
    EXPECT_FLOAT_EQ(empty.shadowParams[0], 0.0f);
    EXPECT_FLOAT_EQ(empty.cloudGrid[3], 0.0f);

    SunShadowReceiver broken = MakeTestReceiver(3);
    broken.maps[1] = fx::Texture{};
    EXPECT_EQ(MakeGrassShadowParams(&broken, nullptr, Vec3::Up(), s).counts[0], 0u);

    // applyInMaterials off sets materialStrength to 0: grass stays unshadowed.
    SunShadowReceiver noMaterial = MakeTestReceiver(3);
    noMaterial.materialStrength = 0.0f;
    EXPECT_EQ(MakeGrassShadowParams(&noMaterial, nullptr, Vec3::Up(), s).counts[0], 0u);

    CloudShadowMap badCloud = MakeTestCloudMap();
    badCloud.map = fx::Texture{};
    EXPECT_FLOAT_EQ(MakeGrassShadowParams(nullptr, &badCloud, Vec3::Up(), s).cloudGrid[3], 0.0f);

    CloudShadowMap shaftsOnly = MakeTestCloudMap();
    shaftsOnly.strength = 0.0f;
    shaftsOnly.shaftStrength = 0.5f;
    EXPECT_FLOAT_EQ(MakeGrassShadowParams(nullptr, &shaftsOnly, Vec3::Up(), s).cloudGrid[3], 0.0f);
}

TEST(GrassShadowParamsTest, SunDirectionMismatchRejectsReceiver)
{
    const GrassSettings s = SanitizeGrassSettings(GrassSettings{});
    const SunShadowReceiver r = MakeTestReceiver(3);
    const CloudShadowMap cloud = MakeTestCloudMap();

    const GrassShadowParamsGpu mismatched =
        MakeGrassShadowParams(&r, &cloud, Normalize(Vec3{ 0.1f, 1.0f, 0.0f }), s);
    EXPECT_EQ(mismatched.counts[0], 0u);
    EXPECT_FLOAT_EQ(mismatched.shadowParams[0], 0.0f);
    EXPECT_FLOAT_EQ(mismatched.cloudGrid[3], 0.8f);

    const Vec3 nanSun{ std::numeric_limits<float>::quiet_NaN(), 1.0f, 0.0f };
    EXPECT_EQ(MakeGrassShadowParams(&r, nullptr, nanSun, s).counts[0], 0u);
}

TEST(GrassShadowParamsTest, PacksCascadesBiasesAndCloudMap)
{
    GrassSettings s = SanitizeGrassSettings(GrassSettings{});
    s.shadowStrength = 0.5f;
    const SunShadowReceiver r = MakeTestReceiver(3);
    const CloudShadowMap cloud = MakeTestCloudMap();
    const GrassShadowParamsGpu p = MakeGrassShadowParams(&r, &cloud, Vec3::Up(), s);

    EXPECT_EQ(p.counts[0], 3u);
    EXPECT_EQ(p.counts[1], 8u); // Spiral kernel caps at 8 taps.
    for (std::uint32_t c = 0; c < 3; ++c) {
        EXPECT_EQ(std::memcmp(p.cascadeViewProjection[c], r.viewProjection[c].Data(), sizeof(float) * 16), 0) << c;
        EXPECT_FLOAT_EQ(p.cascadeSplitFar[c], r.splitFar[c]) << c;
        EXPECT_FLOAT_EQ(p.cascadeBlendWidth[c], r.blendWidth[c]) << c;
        EXPECT_FLOAT_EQ(p.cascadeTexelWorld[c], r.texelWorld[c]) << c;
        EXPECT_FLOAT_EQ(p.cascadeDepthRange[c], r.depthRange[c]) << c;
    }
    EXPECT_FLOAT_EQ(p.cascadeSplitFar[3], r.splitFar[2]);
    EXPECT_FLOAT_EQ(p.eyeAndFadeStart[0], 4.0f);
    EXPECT_FLOAT_EQ(p.eyeAndFadeStart[3], 85.0f);
    EXPECT_FLOAT_EQ(p.forwardAndFadeEnd[2], 1.0f);
    EXPECT_FLOAT_EQ(p.forwardAndFadeEnd[3], 100.0f);
    EXPECT_FLOAT_EQ(p.shadowParams[0], 0.45f);
    EXPECT_FLOAT_EQ(p.shadowParams[1], 1.5f);
    EXPECT_FLOAT_EQ(p.shadowParams[2], 1.75f);
    EXPECT_FLOAT_EQ(p.shadowParams[3], 1.0f / 2048.0f);
    EXPECT_FLOAT_EQ(p.biasExtra[0], 2.0f);
    EXPECT_FLOAT_EQ(p.biasExtra[1], 1.2f);
    EXPECT_FLOAT_EQ(p.biasExtra[2], 1.0f);
    EXPECT_FLOAT_EQ(p.cloudGrid[0], 1200.0f);
    EXPECT_FLOAT_EQ(p.cloudGrid[1], -400.0f);
    EXPECT_FLOAT_EQ(p.cloudGrid[2], 1.0f / 16000.0f);
    EXPECT_FLOAT_EQ(p.cloudGrid[3], 0.4f);
    EXPECT_FLOAT_EQ(p.cloudLayer[0], 1500.0f);
    EXPECT_FLOAT_EQ(p.cloudLayer[1], 4500.0f);
    EXPECT_FLOAT_EQ(p.cloudLayer[2], 1.0f);
    EXPECT_FLOAT_EQ(p.cloudLayer[3], static_cast<float>(kCloudShadowMapSize));
    EXPECT_NEAR(p.cloudSun[1], cloud.sunDirection.y, 1e-5f);
}

TEST(GrassShadowParamsTest, SettingsTogglesDisableIndividualShadowSources)
{
    const SunShadowReceiver r = MakeTestReceiver(2);
    const CloudShadowMap cloud = MakeTestCloudMap();

    GrassSettings noSun = SanitizeGrassSettings(GrassSettings{});
    noSun.receiveSunShadow = false;
    const GrassShadowParamsGpu pNoSun = MakeGrassShadowParams(&r, &cloud, Vec3::Up(), noSun);
    EXPECT_EQ(pNoSun.counts[0], 0u);
    EXPECT_FLOAT_EQ(pNoSun.cloudGrid[3], 0.8f);

    GrassSettings noCloud = SanitizeGrassSettings(GrassSettings{});
    noCloud.receiveCloudShadow = false;
    const GrassShadowParamsGpu pNoCloud = MakeGrassShadowParams(&r, &cloud, Vec3::Up(), noCloud);
    EXPECT_EQ(pNoCloud.counts[0], 2u);
    EXPECT_FLOAT_EQ(pNoCloud.cloudGrid[3], 0.0f);

    GrassSettings zeroStrength = SanitizeGrassSettings(GrassSettings{});
    zeroStrength.shadowStrength = 0.0f;
    const GrassShadowParamsGpu pZero = MakeGrassShadowParams(&r, &cloud, Vec3::Up(), zeroStrength);
    EXPECT_EQ(pZero.counts[0], 0u);
    EXPECT_FLOAT_EQ(pZero.cloudGrid[3], 0.0f);
}

const fx::FeatureDebugField* FindField(const fx::FeatureDebugResult& result, std::string_view key)
{
    for (const fx::FeatureDebugField& field : result.fields) {
        if (field.key == key) {
            return &field;
        }
    }
    return nullptr;
}

TEST(GrassEndpointTest, SetupDeclaresEmptyViewShadowChannelReader)
{
    GrassFeature feature;
    fx::FeatureSetup setup;
    feature.Setup(setup);
    ASSERT_TRUE(setup.Errors().empty());
    ASSERT_EQ(setup.Registrations().size(), 2u);
    EXPECT_EQ(setup.Registrations()[1].name, "Grass.Draw");
    EXPECT_EQ(setup.Registrations()[1].consumedChannel, "Shadow.Receiver");
}

TEST(GrassEndpointTest, ExposesShadowSettingsSunVisAndReadOnlyShadowState)
{
    GrassFeature feature;
    fx::IFeatureDebugEndpoint* endpoint = feature.DebugEndpoint();
    ASSERT_NE(endpoint, nullptr);

    const fx::FeatureDebugResult initial = endpoint->GetState();
    ASSERT_TRUE(initial.ok);
    for (const char* key : { "receiveSunShadow", "receiveCloudShadow", "shadowStrength", "cloudFill",
             "game.shadow.cascades", "game.shadow.cloud", "scene.shadow.cascades", "scene.shadow.cloud" }) {
        ASSERT_NE(FindField(initial, key), nullptr) << key;
    }
    EXPECT_TRUE(std::get<bool>(FindField(initial, "receiveSunShadow")->value));
    EXPECT_TRUE(std::get<bool>(FindField(initial, "receiveCloudShadow")->value));
    EXPECT_DOUBLE_EQ(std::get<double>(FindField(initial, "shadowStrength")->value), 1.0);
    EXPECT_DOUBLE_EQ(std::get<double>(FindField(initial, "cloudFill")->value), 0.35);
    EXPECT_EQ(std::get<std::int64_t>(FindField(initial, "game.shadow.cascades")->value), 0);
    EXPECT_FALSE(std::get<bool>(FindField(initial, "game.shadow.cloud")->value));

    const auto set = [&](std::vector<fx::FeatureDebugArg> args) { return endpoint->Set(args); };
    const fx::FeatureDebugResult updated = set({
        { "receiveSunShadow", "false" },
        { "receiveCloudShadow", "false" },
        { "shadowStrength", "0.6" },
        { "cloudFill", "0.2" },
        { "debugView", "SunVis" },
    });
    ASSERT_TRUE(updated.ok);
    EXPECT_FALSE(std::get<bool>(FindField(updated, "receiveSunShadow")->value));
    EXPECT_FALSE(std::get<bool>(FindField(updated, "receiveCloudShadow")->value));
    EXPECT_DOUBLE_EQ(std::get<double>(FindField(updated, "shadowStrength")->value), 0.6);
    EXPECT_DOUBLE_EQ(std::get<double>(FindField(updated, "cloudFill")->value), 0.2);
    EXPECT_EQ(std::get<std::string>(FindField(updated, "debugView")->value), "SunVis");

    EXPECT_FALSE(set({ { "debugView", "UnknownMode" } }).ok);
    EXPECT_FALSE(set({ { "game.shadow.cascades", "4" } }).ok);
    EXPECT_FALSE(set({ { "game.shadow.cloud", "true" } }).ok);

    const fx::FeatureDebugResult reset = endpoint->Invoke("reset_defaults", {});
    ASSERT_TRUE(reset.ok);
    EXPECT_TRUE(std::get<bool>(FindField(reset, "receiveSunShadow")->value));
    EXPECT_EQ(std::get<std::string>(FindField(reset, "debugView")->value), "Normal");
}

} // namespace
} // namespace gte
