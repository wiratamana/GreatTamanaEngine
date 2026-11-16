// Tests for the Grass height tile builder: snapping, node sampling, water flags and coverage.

#include "Editor/Grass/GrassTileBuilder.h"
#include "Features/Grass/GrassMath.h"
#include "Water/WaterEdit.h"

#include <gtest/gtest.h>

#include <cmath>
#include <memory>

namespace gte {
namespace {

constexpr int kQuads = 4;
constexpr float kSpacing = 2.0f;

// A flat level-0 node of 8 m at the given node coordinates.
std::shared_ptr<const terrain::NodeMesh> FlatNode(int nodeX, int nodeZ, float height)
{
    auto mesh = std::make_shared<terrain::NodeMesh>();
    mesh->key = { nodeX, nodeZ, 0 };
    mesh->originX = nodeX * 8.0;
    mesh->originZ = nodeZ * 8.0;
    mesh->spacing = kSpacing;
    mesh->size = kSpacing * kQuads;
    mesh->vertices.resize(terrain::NodeVertexCount(kQuads));
    for (auto& v : mesh->vertices) {
        v.position[1] = height;
    }
    return mesh;
}

GrassTileJob MakeJob(float nodeHeight, bool withNodes)
{
    GrassTileJob job;
    job.size = 16;
    job.texelSize = 1.0f;
    job.originX = 0.0;
    job.originZ = 0.0;
    job.centerX = 8.0;
    job.centerZ = 8.0;
    job.reach = 8.0;
    job.surface.open = true;
    job.surface.nodeSizeLevel0 = 8.0;
    job.surface.field = std::make_shared<const terrain::TerrainHeightField>(terrain::Sanitize(terrain::TerrainSettings{}));
    if (withNodes) {
        for (int z = 0; z < 2; ++z) {
            for (int x = 0; x < 2; ++x) {
                job.surface.nodes.push_back(FlatNode(x, z, nodeHeight));
            }
        }
    }
    return job;
}

TEST(GrassTileBuilderTest, SnapKeepsTexelsOnTheWorldGrid)
{
    // Moving the camera a little must give the same origin; the origin is a multiple of 8 texels.
    const double a = SnapGrassTileOrigin(1000.3, 384, 1.5f);
    const double b = SnapGrassTileOrigin(1003.9, 384, 1.5f);
    EXPECT_DOUBLE_EQ(a, b);
    EXPECT_DOUBLE_EQ(std::fmod(a, 12.0), 0.0);
    // The tile still surrounds the camera.
    EXPECT_LE(a, 1000.3 - 0.5 * 384 * 1.5 + 12.0);
    EXPECT_GE(a + 384 * 1.5, 1000.3 + 0.5 * 384 * 1.5 - 12.0);
}

TEST(GrassTileBuilderTest, SamplesTheResidentNodes)
{
    const GrassTileBuild build = BuildGrassTile(MakeJob(25.0f, true));
    ASSERT_NE(build.tile, nullptr);
    EXPECT_TRUE(build.tile->Valid());
    for (std::size_t i = 0; i < build.tile->data.size(); i += 2) {
        EXPECT_FLOAT_EQ(build.tile->data[i], 25.0f);
    }
    EXPECT_FLOAT_EQ(build.coverageLevel0, 1.0f);
    EXPECT_FLOAT_EQ(build.coverageNodes, 1.0f);
}

TEST(GrassTileBuilderTest, NoNodesMeansNoCoverageNotZeroHeights)
{
    const GrassTileBuild build = BuildGrassTile(MakeJob(0.0f, false));
    ASSERT_NE(build.tile, nullptr);
    EXPECT_FLOAT_EQ(build.coverageNodes, 0.0f);
    EXPECT_FLOAT_EQ(build.coverageLevel0, 0.0f);
}

TEST(GrassTileBuilderTest, MissingFieldBuildsNothing)
{
    GrassTileJob job = MakeJob(5.0f, true);
    job.surface.field.reset();
    EXPECT_EQ(BuildGrassTile(job).tile, nullptr);
}

TEST(GrassTileBuilderTest, OceanFlagsLowGroundOnly)
{
    GrassTileJob job = MakeJob(0.3f, true);
    job.waterMargin = 0.6f;
    job.water = water::WaterBodySet::Build({ water::MakeOceanBody(0.0f) }, water::WaterSettings{});
    const GrassTileBuild low = BuildGrassTile(job);
    ASSERT_NE(low.tile, nullptr);
    EXPECT_FLOAT_EQ(low.tile->seaLevel, 0.0f);
    EXPECT_FLOAT_EQ(low.submergedShare, 1.0f); // 0.3 m is inside the 0.6 m margin.

    GrassTileJob dry = MakeJob(2.0f, true);
    dry.water = job.water;
    EXPECT_FLOAT_EQ(BuildGrassTile(dry).submergedShare, 0.0f);
}

TEST(GrassTileBuilderTest, LakeFlagsOnlyInsideItsOutline)
{
    GrassTileJob job = MakeJob(12.0f, true); // Ground at 12 m everywhere.
    water::WaterBody lake;
    lake.kind = water::WaterBodyKind::Lake;
    lake.level = 13.0f;
    lake.polygon = water::MakeBoxPolygon(4.0, 4.0, 6.0, 6.0); // x,z in [1,7]
    job.water = water::WaterBodySet::Build({ water::MakeOceanBody(-100.0f), lake }, water::WaterSettings{});
    job.waterMargin = 0.0f;
    const GrassTileBuild build = BuildGrassTile(job);
    ASSERT_NE(build.tile, nullptr);
    float h = 0.0f;
    float flagInside = 0.0f;
    float flagOutside = 0.0f;
    ASSERT_TRUE(SampleGrassTile(*build.tile, 4.0, 4.0, h, flagInside));
    ASSERT_TRUE(SampleGrassTile(*build.tile, 12.0, 12.0, h, flagOutside));
    EXPECT_FLOAT_EQ(flagInside, 1.0f);
    EXPECT_FLOAT_EQ(flagOutside, 0.0f);
}

TEST(GrassTileBuilderTest, NoWaterSetMeansNoOceanAndNoFlags)
{
    const GrassTileBuild build = BuildGrassTile(MakeJob(-50.0f, true));
    ASSERT_NE(build.tile, nullptr);
    EXPECT_LT(build.tile->seaLevel, -1.0e5f);
    EXPECT_FLOAT_EQ(build.submergedShare, 0.0f);
}

TEST(GrassTileBuilderTest, SampleGrassTileIsBilinearAndBounded)
{
    GrassHeightTile tile;
    tile.size = 2;
    tile.texelSize = 2.0f;
    tile.originX = 10.0;
    tile.originZ = 20.0;
    tile.data = { 0.0f, 0.0f, 4.0f, 1.0f, 8.0f, 0.0f, 12.0f, 1.0f }; // (0,0) (1,0) (0,1) (1,1)
    ASSERT_TRUE(tile.Valid());
    float h = 0.0f;
    float f = 0.0f;
    ASSERT_TRUE(SampleGrassTile(tile, 11.0, 21.0, h, f));
    EXPECT_FLOAT_EQ(h, 6.0f);
    EXPECT_FLOAT_EQ(f, 0.5f);
    EXPECT_FALSE(SampleGrassTile(tile, 9.9, 21.0, h, f));
    EXPECT_FALSE(SampleGrassTile(tile, 11.0, 22.1, h, f));
}


// --- Rebuild policy ---

GrassTileView ViewWithTile()
{
    GrassTileView v;
    v.haveTile = true;
    v.originX = 0.0;
    v.originZ = 0.0;
    v.extent = 576.0;  // Half 288; reach 182 + 8 safety leaves 98 m of drift.
    v.reach = 182.0;
    v.cameraX = 288.0;
    v.cameraZ = 288.0;
    v.now = 100.0;
    return v;
}

GrassTileSeen Seen()
{
    GrassTileSeen s;
    s.settingsKey = 1;
    s.waterHash = 2;
    s.carveHash = 3;
    s.terrainHash = 4;
    s.streamRevision = 5;
    s.fineKeys = { terrain::NodeKey{ 0, 0, 0 }, terrain::NodeKey{ 1, 0, 0 } };
    return s;
}

TEST(GrassTilePolicyTest, NothingChangedMeansNoRebuild)
{
    EXPECT_FALSE(DecideGrassTileRebuild(ViewWithTile(), Seen(), Seen()).has_value());
}

TEST(GrassTilePolicyTest, NoTileMeansInitial)
{
    GrassTileView v = ViewWithTile();
    v.haveTile = false;
    EXPECT_EQ(DecideGrassTileRebuild(v, Seen(), Seen()), GrassTileReason::Initial);
}

TEST(GrassTilePolicyTest, DriftInsideTheSlackKeepsTheTileAndOutsideRebuilds)
{
    GrassTileView v = ViewWithTile();
    v.cameraX += 90.0;
    EXPECT_FALSE(DecideGrassTileRebuild(v, Seen(), Seen()).has_value());
    v.cameraX += 20.0; // 110 m of drift, more than the 98 m slack.
    EXPECT_EQ(DecideGrassTileRebuild(v, Seen(), Seen()), GrassTileReason::Moved);
}

TEST(GrassTilePolicyTest, SmallTileStillAllowsSomeDrift)
{
    GrassTileView v = ViewWithTile();
    v.extent = 300.0; // Smaller than the reach: slack falls back to 8 m.
    v.cameraX = 150.0 + 7.0;
    v.cameraZ = 150.0;
    EXPECT_FALSE(DecideGrassTileRebuild(v, Seen(), Seen()).has_value());
    v.cameraX = 150.0 + 9.0;
    EXPECT_EQ(DecideGrassTileRebuild(v, Seen(), Seen()), GrassTileReason::Moved);
}

TEST(GrassTilePolicyTest, EachInputHasItsOwnReason)
{
    const GrassTileView v = ViewWithTile();
    GrassTileSeen now = Seen();
    now.settingsKey = 9;
    EXPECT_EQ(DecideGrassTileRebuild(v, Seen(), now), GrassTileReason::Settings);
    now = Seen();
    now.waterHash = 9;
    EXPECT_EQ(DecideGrassTileRebuild(v, Seen(), now), GrassTileReason::Water);
    now = Seen();
    now.carveHash = 9;
    EXPECT_EQ(DecideGrassTileRebuild(v, Seen(), now), GrassTileReason::Carve);
    now = Seen();
    now.terrainHash = 9;
    EXPECT_EQ(DecideGrassTileRebuild(v, Seen(), now), GrassTileReason::Terrain);
}

TEST(GrassTilePolicyTest, CarveRebuildFinishingTriggersOnce)
{
    GrassTileSeen last = Seen();
    last.rebuilding = 3;
    GrassTileSeen now = Seen();
    now.rebuilding = 2; // Still running.
    EXPECT_FALSE(DecideGrassTileRebuild(ViewWithTile(), last, now).has_value());
    now.rebuilding = 0;
    EXPECT_EQ(DecideGrassTileRebuild(ViewWithTile(), last, now), GrassTileReason::Carve);
}

TEST(GrassTilePolicyTest, OnlyNewFineNodesTriggerAStreamRebuild)
{
    const GrassTileView v = ViewWithTile();
    GrassTileSeen now = Seen();
    now.streamRevision = 99; // Churn far away: nodes built and evicted, nothing new inside.
    now.fineKeys.erase(terrain::NodeKey{ 1, 0, 0 });
    EXPECT_FALSE(DecideGrassTileRebuild(v, Seen(), now).has_value());
    now.fineKeys.insert(terrain::NodeKey{ 5, 5, 0 });
    EXPECT_EQ(DecideGrassTileRebuild(v, Seen(), now), GrassTileReason::Stream);
}

TEST(GrassTilePolicyTest, SettlingAfterATerrainEditRebuildsOnAnyRebuiltNode)
{
    GrassTileView v = ViewWithTile();
    GrassTileSeen now = Seen();
    now.streamRevision = 6; // Same keys, new content.
    EXPECT_FALSE(DecideGrassTileRebuild(v, Seen(), now).has_value());
    v.settleUntil = v.now + 1.0;
    EXPECT_EQ(DecideGrassTileRebuild(v, Seen(), now), GrassTileReason::Stream);
    v.settleUntil = v.now - 1.0;
    EXPECT_FALSE(DecideGrassTileRebuild(v, Seen(), now).has_value());
}

} // namespace
} // namespace gte
