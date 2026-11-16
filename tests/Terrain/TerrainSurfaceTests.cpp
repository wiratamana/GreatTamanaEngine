// Tests for reading the drawn terrain surface from resident node meshes.

#include "Terrain/TerrainSurface.h"

#include <gtest/gtest.h>

#include <memory>

namespace gte::terrain {
namespace {

constexpr int kQuads = 4;

// A node whose grid vertices follow height(x, z); the skirt ring is left empty.
std::shared_ptr<const NodeMesh> MakeNode(NodeKey key, double originX, double originZ, float spacing, float (*height)(double, double))
{
    auto mesh = std::make_shared<NodeMesh>();
    mesh->key = key;
    mesh->originX = originX;
    mesh->originZ = originZ;
    mesh->spacing = spacing;
    mesh->size = spacing * kQuads;
    mesh->vertices.resize(NodeVertexCount(kQuads));
    for (int iz = 0; iz <= kQuads; ++iz) {
        for (int ix = 0; ix <= kQuads; ++ix) {
            TerrainVertex& v = mesh->vertices[static_cast<std::size_t>(iz * (kQuads + 1) + ix)];
            v.position[0] = static_cast<float>(ix) * spacing;
            v.position[2] = static_cast<float>(iz) * spacing;
            v.position[1] = height(originX + ix * spacing, originZ + iz * spacing);
        }
    }
    return mesh;
}

float Plane(double x, double z)
{
    return static_cast<float>(0.5 * x + 0.25 * z + 3.0);
}

float Flat10(double, double)
{
    return 10.0f;
}

float Flat20(double, double)
{
    return 20.0f;
}

SurfaceSnapshot Snapshot(std::vector<std::shared_ptr<const NodeMesh>> nodes)
{
    SurfaceSnapshot s;
    s.open = true;
    s.nodeSizeLevel0 = 8.0; // spacing 2 x 4 quads
    s.nodes = std::move(nodes);
    return s;
}

TEST(TerrainSurfaceTest, PlaneIsReproducedExactlyOnBothTriangles)
{
    const auto node = MakeNode({ 0, 0, 0 }, 0.0, 0.0, 2.0f, Plane);
    // One point per triangle of a quad (below and above the diagonal), plus a vertex and the far edge.
    for (const auto& p : { std::pair{ 1.5, 0.5 }, std::pair{ 0.5, 1.5 }, std::pair{ 2.0, 2.0 }, std::pair{ 8.0, 8.0 }, std::pair{ 7.9, 0.1 } }) {
        float h = 0.0f;
        ASSERT_TRUE(NodeHeightAt(*node, p.first, p.second, h));
        EXPECT_NEAR(h, Plane(p.first, p.second), 1e-4f) << p.first << "," << p.second;
    }
}

TEST(TerrainSurfaceTest, OutsideTheNodeIsRefused)
{
    const auto node = MakeNode({ 0, 0, 0 }, 0.0, 0.0, 2.0f, Plane);
    float h = 0.0f;
    EXPECT_FALSE(NodeHeightAt(*node, -0.01, 1.0, h));
    EXPECT_FALSE(NodeHeightAt(*node, 1.0, 8.01, h));
}

TEST(TerrainSurfaceTest, FinestResidentNodeWins)
{
    // Level 1 covers x,z in [0,16); level 0 only [0,8).
    const auto coarse = MakeNode({ 0, 0, 1 }, 0.0, 0.0, 4.0f, Flat10);
    const auto fine = MakeNode({ 0, 0, 0 }, 0.0, 0.0, 2.0f, Flat20);
    const SurfaceSampler sampler(Snapshot({ fine, coarse }));
    const NodeMesh* hint = nullptr;
    const SurfaceHit inFine = sampler.Sample(3.0, 3.0, hint);
    EXPECT_EQ(inFine.level, 0);
    EXPECT_FLOAT_EQ(inFine.height, 20.0f);
    const SurfaceHit inCoarse = sampler.Sample(12.0, 12.0, hint);
    EXPECT_EQ(inCoarse.level, 1);
    EXPECT_FLOAT_EQ(inCoarse.height, 10.0f);
}

TEST(TerrainSurfaceTest, HintDoesNotChangeTheAnswer)
{
    const auto fine = MakeNode({ 0, 0, 0 }, 0.0, 0.0, 2.0f, Plane);
    const auto next = MakeNode({ 1, 0, 0 }, 8.0, 0.0, 2.0f, Plane);
    const SurfaceSampler sampler(Snapshot({ fine, next }));
    const NodeMesh* hint = nullptr;
    const SurfaceHit a = sampler.Sample(7.0, 1.0, hint);
    const SurfaceHit b = sampler.Sample(9.0, 1.0, hint); // Leaves the hinted node.
    const NodeMesh* none = nullptr;
    EXPECT_FLOAT_EQ(b.height, sampler.Sample(9.0, 1.0, none).height);
    EXPECT_NEAR(a.height, Plane(7.0, 1.0), 1e-4f);
    EXPECT_NEAR(b.height, Plane(9.0, 1.0), 1e-4f);
}

TEST(TerrainSurfaceTest, NoNodeAndNoFieldGivesNoData)
{
    const SurfaceSampler sampler(Snapshot({}));
    const NodeMesh* hint = nullptr;
    EXPECT_EQ(sampler.Sample(1.0, 1.0, hint).level, -2);
}

TEST(TerrainSurfaceTest, NoNodeFallsBackToTheNoiseField)
{
    SurfaceSnapshot s = Snapshot({});
    s.field = std::make_shared<const TerrainHeightField>(Sanitize(TerrainSettings{}));
    const SurfaceSampler sampler(std::move(s));
    const NodeMesh* hint = nullptr;
    const SurfaceHit hit = sampler.Sample(100.0, 100.0, hint);
    EXPECT_EQ(hit.level, -1);
}

} // namespace
} // namespace gte::terrain
