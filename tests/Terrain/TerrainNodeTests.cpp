// Tier 1: height field, erosion, node meshes: determinism, seams, bounds, winding.

#include "Terrain/TerrainHeightField.h"
#include "Terrain/TerrainMaterial.h"
#include "Terrain/TerrainNode.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstring>

namespace gte::terrain {

namespace {

TerrainSettings Settings(bool erosion)
{
    TerrainSettings s;
    s.erosionEnabled = erosion;
    return Sanitize(s);
}

// First node of `level` with real land (centre height above 150 m) in a search window.
NodeKey FindLandNode(const TerrainHeightField& field, int level)
{
    const double size = NodeSize(field.Layout(), level);
    for (int z = -6; z < 6; ++z) {
        for (int x = -6; x < 6; ++x) {
            const double h = field.Height((x + 0.5) * size, (z + 0.5) * size, NodeSpacing(field.Layout(), level));
            if (h > 150.0) {
                return NodeKey{ x, z, static_cast<std::uint8_t>(level) };
            }
        }
    }
    return NodeKey{ 0, 0, static_cast<std::uint8_t>(level) };
}

const TerrainVertex& V(const NodeMesh& m, int q, int ix, int iz)
{
    return m.vertices[static_cast<std::size_t>(iz) * static_cast<std::size_t>(q + 1) + static_cast<std::size_t>(ix)];
}

void ExpectSameEdge(const NodeMesh& a, const NodeMesh& b, int q, bool alongX)
{
    for (int k = 0; k <= q; ++k) {
        const TerrainVertex& va = alongX ? V(a, q, q, k) : V(a, q, k, q);
        const TerrainVertex& vb = alongX ? V(b, q, 0, k) : V(b, q, k, 0);
        EXPECT_EQ(va.position[1], vb.position[1]) << "height at " << k;
        EXPECT_NEAR(va.normal[0], vb.normal[0], 1.0e-5f);
        EXPECT_NEAR(va.normal[1], vb.normal[1], 1.0e-5f);
        EXPECT_NEAR(va.normal[2], vb.normal[2], 1.0e-5f);
    }
}

} // namespace

TEST(TerrainNodeTests, SameSeedSameHeightsDifferentSeedDiffers)
{
    const TerrainHeightField a(Settings(false));
    const TerrainHeightField b(Settings(false));
    TerrainSettings other = Settings(false);
    other.seed = 4242;
    const TerrainHeightField c(Sanitize(other));
    int differing = 0;
    for (int i = 0; i < 200; ++i) {
        const double x = i * 97.3 - 5000.0;
        const double z = i * 53.1 - 3000.0;
        EXPECT_EQ(a.Height(x, z, 2.0), b.Height(x, z, 2.0));
        differing += a.Height(x, z, 2.0) != c.Height(x, z, 2.0) ? 1 : 0;
    }
    EXPECT_GT(differing, 100);
}

TEST(TerrainNodeTests, HeightsStayInsideDeclaredRange)
{
    const TerrainSettings s = Settings(false);
    const TerrainHeightField f(s);
    for (int i = 0; i < 2000; ++i) {
        const float h = f.Height(i * 13.7 - 12000.0, i * 29.9 - 15000.0, 2.0);
        EXPECT_GE(h, MinHeight(s));
        EXPECT_LE(h, MaxHeight(s));
    }
}

TEST(TerrainNodeTests, BuildNodeIsDeterministicIncludingErosion)
{
    const TerrainHeightField f(Settings(true));
    const NodeKey key = FindLandNode(f, 0);
    const NodeMesh a = BuildNode(f, key);
    const NodeMesh b = BuildNode(f, key);
    ASSERT_EQ(a.vertices.size(), b.vertices.size());
    EXPECT_EQ(std::memcmp(a.vertices.data(), b.vertices.data(), a.vertices.size() * sizeof(TerrainVertex)), 0);
    EXPECT_EQ(a.colors, b.colors);
}

TEST(TerrainNodeTests, VertexAndIndexCountsAreConstantForEveryLevel)
{
    const TerrainHeightField f(Settings(false));
    const int q = f.Layout().quads;
    for (int level : { 0, 3, 7 }) {
        const NodeMesh m = BuildNode(f, NodeKey{ 1, -2, static_cast<std::uint8_t>(level) });
        EXPECT_EQ(m.vertices.size(), NodeVertexCount(q));
        EXPECT_EQ(m.colors.size(), m.vertices.size());
    }
    EXPECT_EQ(NodeVertexCount(64), 65u * 65u + 260u);
    EXPECT_EQ(BuildSharedIndices(q).size(), NodeIndexCount(q));
}

TEST(TerrainNodeTests, NeighbourNodesShareBorderHeightsAndNormals)
{
    for (const bool erosion : { false, true }) {
        const TerrainHeightField f(Settings(erosion));
        const int q = f.Layout().quads;
        const NodeKey k = FindLandNode(f, 0);
        const NodeMesh center = BuildNode(f, k);
        const NodeMesh east = BuildNode(f, NodeKey{ k.x + 1, k.z, 0 });
        const NodeMesh north = BuildNode(f, NodeKey{ k.x, k.z + 1, 0 });
        ExpectSameEdge(center, east, q, true);
        ExpectSameEdge(center, north, q, false);
    }
}

TEST(TerrainNodeTests, SeamHoldsOnCoarserLevelsToo)
{
    const TerrainHeightField f(Settings(true));
    const int q = f.Layout().quads;
    const NodeKey k = FindLandNode(f, 1);
    ExpectSameEdge(BuildNode(f, k), BuildNode(f, NodeKey{ k.x + 1, k.z, 1 }), q, true);
    const NodeKey far = FindLandNode(f, 4);
    ExpectSameEdge(BuildNode(f, far), BuildNode(f, NodeKey{ far.x, far.z + 1, 4 }), q, false);
}

TEST(TerrainNodeTests, ErosionChangesTheInteriorButNotTheBorder)
{
    const TerrainHeightField plain(Settings(false));
    const TerrainHeightField eroded(Settings(true));
    const NodeKey k = FindLandNode(plain, 0);
    const NodeMesh a = BuildNode(plain, k);
    const NodeMesh b = BuildNode(eroded, k);
    const int q = plain.Layout().quads;
    int changedInterior = 0;
    float maxDelta = 0.0f;
    for (int z = 0; z <= q; ++z) {
        for (int x = 0; x <= q; ++x) {
            const float d = std::fabs(V(a, q, x, z).position[1] - V(b, q, x, z).position[1]);
            const bool border = x <= 1 || z <= 1 || x >= q - 1 || z >= q - 1;
            if (border) {
                EXPECT_EQ(d, 0.0f);
            } else if (d > 1.0e-4f) {
                ++changedInterior;
                maxDelta = std::max(maxDelta, d);
            }
            EXPECT_TRUE(std::isfinite(V(b, q, x, z).position[1]));
        }
    }
    EXPECT_GT(changedInterior, 100);
    EXPECT_LT(maxDelta, 0.5f * plain.Settings().heightScale);
}

TEST(TerrainNodeTests, ErosionOnlyOnConfiguredLevels)
{
    TerrainSettings s = Settings(true);
    s.erosionMaxLevel = 1;
    EXPECT_TRUE(NodeUsesErosion(s, 0));
    EXPECT_TRUE(NodeUsesErosion(s, 1));
    EXPECT_FALSE(NodeUsesErosion(s, 2));
    s.erosionEnabled = false;
    EXPECT_FALSE(NodeUsesErosion(s, 0));
}

TEST(TerrainNodeTests, BoundsAreExactAndContainEveryVertex)
{
    const TerrainHeightField f(Settings(true));
    const int q = f.Layout().quads;
    const NodeMesh m = BuildNode(f, FindLandNode(f, 0));
    float minY = 1.0e30f;
    float maxY = -1.0e30f;
    float maxGridY = -1.0e30f;
    for (std::size_t i = 0; i < m.vertices.size(); ++i) {
        const TerrainVertex& v = m.vertices[i];
        EXPECT_GE(v.position[0], m.boundsMin.x);
        EXPECT_LE(v.position[0], m.boundsMax.x);
        EXPECT_GE(v.position[1], m.boundsMin.y);
        EXPECT_LE(v.position[1], m.boundsMax.y);
        EXPECT_GE(v.position[2], m.boundsMin.z);
        EXPECT_LE(v.position[2], m.boundsMax.z);
        minY = std::min(minY, v.position[1]);
        maxY = std::max(maxY, v.position[1]);
        if (i < GridVertexCount(q)) {
            maxGridY = std::max(maxGridY, v.position[1]);
        }
    }
    EXPECT_EQ(m.boundsMin.y, minY);
    EXPECT_EQ(m.boundsMax.y, maxY);
    EXPECT_EQ(maxGridY, maxY);
    EXPECT_EQ(m.boundsMin.x, 0.0f);
    EXPECT_EQ(m.boundsMax.x, static_cast<float>(q) * m.spacing);
    EXPECT_EQ(m.boundsMax.z, static_cast<float>(q) * m.spacing);
}

TEST(TerrainNodeTests, TrianglesFaceUpAndSkirtsFaceOutward)
{
    const TerrainHeightField f(Settings(false));
    const int q = f.Layout().quads;
    const NodeMesh m = BuildNode(f, NodeKey{ 3, 3, 0 });
    const std::vector<std::uint32_t> idx = BuildSharedIndices(q);
    const std::size_t gridIndices = static_cast<std::size_t>(q) * static_cast<std::size_t>(q) * 6u;
    const std::size_t perEdge = static_cast<std::size_t>(q) * 6u;
    // Front face normal: cross(c - a, b - a), as the engine draws them.
    for (std::size_t t = 0; t + 2 < idx.size(); t += 3) {
        ASSERT_LT(idx[t], m.vertices.size());
        ASSERT_LT(idx[t + 1], m.vertices.size());
        ASSERT_LT(idx[t + 2], m.vertices.size());
        const auto pos = [&](std::size_t i) {
            const TerrainVertex& v = m.vertices[idx[i]];
            return Vec3{ v.position[0], v.position[1], v.position[2] };
        };
        const Vec3 n = Cross(pos(t + 2) - pos(t), pos(t + 1) - pos(t));
        if (t < gridIndices) {
            EXPECT_GT(n.y, 0.0f) << "grid triangle " << t / 3;
        } else {
            const std::size_t edge = (t - gridIndices) / perEdge;
            const Vec3 outward = edge == 0 ? Vec3{ 0, 0, -1 } : edge == 1 ? Vec3{ 1, 0, 0 } : edge == 2 ? Vec3{ 0, 0, 1 } : Vec3{ -1, 0, 0 };
            EXPECT_GT(Dot(n, outward), 0.0f) << "skirt triangle " << t / 3;
        }
    }
}

TEST(TerrainNodeTests, NormalsAreUnitLengthAndUvAreInRange)
{
    const TerrainSettings s = Settings(false);
    const TerrainHeightField f(s);
    const NodeMesh m = BuildNode(f, FindLandNode(f, 2));
    for (const TerrainVertex& v : m.vertices) {
        const float len = std::sqrt(v.normal[0] * v.normal[0] + v.normal[1] * v.normal[1] + v.normal[2] * v.normal[2]);
        EXPECT_NEAR(len, 1.0f, 1.0e-4f);
        EXPECT_GE(v.uv[0], 0.0f);
        EXPECT_LE(v.uv[0], 1.0f);
        EXPECT_GE(v.uv[1], 0.0f);
        EXPECT_LE(v.uv[1], 1.0f);
    }
}

TEST(TerrainNodeTests, PositionsStayPreciseFarFromTheOrigin)
{
    const TerrainHeightField f(Settings(false));
    const NodeMesh m = BuildNode(f, NodeKey{ 127, -128, 0 });
    EXPECT_LT(m.boundsMax.x, 200.0f);
    EXPECT_DOUBLE_EQ(m.originX, 127.0 * 128.0);
    EXPECT_DOUBLE_EQ(m.originZ, -128.0 * 128.0);
}

TEST(TerrainNodeTests, RecolorMatchesFreshBuildForNewMaterials)
{
    const TerrainSettings base = Settings(false);
    TerrainSettings changed = base;
    changed.snowMinHeightFraction = 0.2f;
    changed.rockMinSlopeDeg = 50.0f;
    const TerrainHeightField fa(base);
    const TerrainHeightField fb(Sanitize(changed));
    const NodeKey k = FindLandNode(fa, 2);
    NodeMesh recolored = BuildNode(fa, k);
    RecolorNode(recolored, Sanitize(changed));
    const NodeMesh fresh = BuildNode(fb, k);
    int different = 0;
    for (std::size_t i = 0; i < fresh.colors.size(); ++i) {
        // uv quantisation allows a one-step difference per channel.
        for (int c = 0; c < 4; ++c) {
            const int a = static_cast<int>((recolored.colors[i] >> (8 * c)) & 0xFFu);
            const int b = static_cast<int>((fresh.colors[i] >> (8 * c)) & 0xFFu);
            different += std::abs(a - b) > 6 ? 1 : 0;
        }
    }
    EXPECT_LT(different, static_cast<int>(fresh.colors.size()) / 50);
}

TEST(TerrainMaterialTests, FlatLowIsGreenSteepIsRockHighIsSnow)
{
    const TerrainSettings s{};
    const Rgb grass = SurfaceColor(200.0f, 0.1f, s);
    const Rgb rock = SurfaceColor(600.0f, 1.2f, s);
    const Rgb snow = SurfaceColor(s.heightScale * 0.95f, 0.1f, s);
    const Rgb sand = SurfaceColor(1.0f, 0.05f, s);
    EXPECT_GT(grass.g, grass.r);
    EXPECT_GT(grass.g, grass.b);
    EXPECT_NEAR(rock.r, rock.g + 0.04f, 0.08f);
    EXPECT_GT(snow.r, 0.85f);
    EXPECT_GT(sand.r, sand.b + 0.15f);
    // Snow does not stick to a cliff.
    EXPECT_LT(SurfaceColor(s.heightScale * 0.95f, 1.4f, s).r, 0.7f);
}

TEST(TerrainMaterialTests, RampImageHasExpectedSizeAndOpaqueAlpha)
{
    const std::vector<std::uint8_t> img = BuildRampImage(TerrainSettings{}, 64);
    ASSERT_EQ(img.size(), 64u * 64u * 4u);
    for (std::size_t i = 3; i < img.size(); i += 4) {
        EXPECT_EQ(img[i], 255);
    }
}

TEST(TerrainMaterialTests, Height01RoundTrips)
{
    const TerrainSettings s{};
    for (float h : { -300.0f, 0.0f, 500.0f, 1700.0f }) {
        EXPECT_NEAR(HeightFromHeight01(Height01(h, s), s), h, 0.01f);
    }
}

} // namespace gte::terrain
