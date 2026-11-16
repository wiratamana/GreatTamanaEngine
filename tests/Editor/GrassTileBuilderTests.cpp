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

} // namespace
} // namespace gte
