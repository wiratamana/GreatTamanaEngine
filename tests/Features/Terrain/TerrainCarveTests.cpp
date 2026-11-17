// Tier 1: terrain nodes with a carve hook: seams, determinism, no-op equality.

#include "Features/Terrain/TerrainHeightField.h"
#include "Features/Terrain/TerrainNode.h"
#include "Features/Water/World/WaterCarve.h"

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

NodeKey FindLandNode(const TerrainHeightField& field, int level)
{
    const double size = NodeSize(field.Layout(), level);
    for (int z = -6; z < 6; ++z) {
        for (int x = -6; x < 6; ++x) {
            if (field.Height((x + 0.5) * size, (z + 0.5) * size, NodeSpacing(field.Layout(), level)) > 150.0) {
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

bool SameVertices(const NodeMesh& a, const NodeMesh& b)
{
    return a.vertices.size() == b.vertices.size()
        && std::memcmp(a.vertices.data(), b.vertices.data(), a.vertices.size() * sizeof(TerrainVertex)) == 0
        && a.colors == b.colors;
}

std::shared_ptr<const water::WaterCarve> LakeAt(double cx, double cz, double size, float level, float depth = 6.0f)
{
    water::WaterBody b;
    b.name = "Lake";
    b.kind = water::WaterBodyKind::Lake;
    b.level = level;
    b.depth = depth;
    b.polygon = water::MakeBoxPolygon(cx, cz, size, size);
    return water::WaterCarve::Build(*water::WaterBodySet::Build({ b }, {}));
}

// A lake sitting on the corner shared by the four nodes around (cornerX, cornerZ).
std::shared_ptr<const water::WaterCarve> LakeOnCorner(const TerrainHeightField& field, double cornerX, double cornerZ, double size)
{
    const float level = field.BaseHeight(cornerX, cornerZ, 2.0) - 4.0f;
    return LakeAt(cornerX, cornerZ, size, level);
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

TEST(TerrainCarveTests, FarAwayCarveLeavesNodesBitIdentical)
{
    const TerrainSettings s = Settings(true);
    const TerrainHeightField plain(s);
    const TerrainHeightField withCarve(s, LakeAt(900000.0, 900000.0, 200.0, 10.0f));
    const NodeKey key = FindLandNode(plain, 0);
    NodeBuildTiming timing;
    const NodeMesh a = BuildNode(plain, key);
    const NodeMesh b = BuildNode(withCarve, key, &timing);
    EXPECT_TRUE(SameVertices(a, b));
    EXPECT_EQ(timing.carveMs >= 0.0, true);
}

TEST(TerrainCarveTests, NullCarveEqualsNoCarve)
{
    const TerrainSettings s = Settings(false);
    const TerrainHeightField plain(s);
    const TerrainHeightField nullCarve(s, nullptr);
    const NodeKey key = FindLandNode(plain, 1);
    EXPECT_TRUE(SameVertices(BuildNode(plain, key), BuildNode(nullCarve, key)));
    EXPECT_EQ(nullCarve.CarveHash(), 0u);
}

TEST(TerrainCarveTests, SeamsStayBitEqualWithCarveAndErosion)
{
    const TerrainSettings s = Settings(true);
    const TerrainHeightField plain(s);
    for (int level = 0; level <= 2; ++level) {
        const NodeKey a = FindLandNode(plain, level);
        const double size = NodeSize(plain.Layout(), level);
        const double cornerX = (a.x + 1) * size;
        const double cornerZ = (a.z + 1) * size;
        const TerrainHeightField field(s, LakeOnCorner(plain, cornerX, cornerZ, size * 0.8));
        const int q = field.Layout().quads;
        const NodeMesh na = BuildNode(field, a);
        const NodeMesh east = BuildNode(field, NodeKey{ a.x + 1, a.z, a.level });
        const NodeMesh south = BuildNode(field, NodeKey{ a.x, a.z + 1, a.level });
        ExpectSameEdge(na, east, q, true);
        ExpectSameEdge(na, south, q, false);
        // The carve really changed something on the shared border.
        const NodeMesh plainA = BuildNode(plain, a);
        EXPECT_FALSE(SameVertices(na, plainA)) << "level " << level;
    }
}

TEST(TerrainCarveTests, BuildIsDeterministicAndHashChangesOutput)
{
    const TerrainSettings s = Settings(true);
    const TerrainHeightField plain(s);
    const NodeKey key = FindLandNode(plain, 0);
    const double size = NodeSize(plain.Layout(), 0);
    const double cx = (key.x + 0.5) * size;
    const double cz = (key.z + 0.5) * size;
    const TerrainHeightField a(s, LakeOnCorner(plain, cx, cz, 80.0));
    const TerrainHeightField b(s, LakeOnCorner(plain, cx, cz, 80.0));
    const TerrainHeightField c(s, LakeAt(cx, cz, 80.0, plain.BaseHeight(cx, cz, 2.0) - 4.0f, 9.0f));
    EXPECT_EQ(a.CarveHash(), b.CarveHash());
    EXPECT_NE(a.CarveHash(), c.CarveHash());
    EXPECT_TRUE(SameVertices(BuildNode(a, key), BuildNode(b, key)));
    EXPECT_FALSE(SameVertices(BuildNode(a, key), BuildNode(c, key)));
}

TEST(TerrainCarveTests, MeshAgreesWithPointHeightAndBoundsHoldEveryVertex)
{
    const TerrainSettings s = Settings(false);
    const TerrainHeightField plain(s);
    const NodeKey key = FindLandNode(plain, 0);
    const double size = NodeSize(plain.Layout(), 0);
    const double cx = (key.x + 0.5) * size;
    const double cz = (key.z + 0.5) * size;
    const TerrainHeightField field(s, LakeOnCorner(plain, cx, cz, 90.0));
    const NodeMesh mesh = BuildNode(field, key);
    const NodeGeometry g = NodeGeometryOf(field.Layout(), key);
    const int q = field.Layout().quads;
    int lowered = 0;
    for (int iz = 0; iz <= q; iz += 4) {
        for (int ix = 0; ix <= q; ix += 4) {
            const float mh = V(mesh, q, ix, iz).position[1];
            const float ph = field.Height(g.originX + ix * g.spacing, g.originZ + iz * g.spacing, g.spacing);
            EXPECT_NEAR(mh, ph, 0.6f);
            EXPECT_GE(mh, mesh.boundsMin.y);
            EXPECT_LE(mh, mesh.boundsMax.y);
            lowered += mh < plain.BaseHeight(g.originX + ix * g.spacing, g.originZ + iz * g.spacing, g.spacing) - 0.5f ? 1 : 0;
        }
    }
    EXPECT_GT(lowered, 20);
}

TEST(TerrainCarveTests, CarveCostPerNodeIsMeasured)
{
    const TerrainSettings s = Settings(false);
    const TerrainHeightField plain(s);
    const NodeKey key = FindLandNode(plain, 0);
    const double size = NodeSize(plain.Layout(), 0);
    const TerrainHeightField field(s, LakeOnCorner(plain, (key.x + 0.5) * size, (key.z + 0.5) * size, 100.0));
    NodeBuildTiming t;
    double carve = 0.0;
    double total = 0.0;
    for (int i = 0; i < 20; ++i) {
        t = {};
        (void)BuildNode(field, key, &t);
        carve += t.carveMs;
        total += t.heightMs + t.carveMs + t.meshMs;
    }
    std::printf("[terrain] node build with lake: carve %.3f ms of %.3f ms (avg of 20)\n", carve / 20, total / 20);
    EXPECT_LT(carve / 20, 5.0);
}

} // namespace gte::terrain
