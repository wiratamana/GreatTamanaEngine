// Tier 1: colour ramp image with its replicated border, and node vertex packing for the pool and the preview.

#include "Features/Terrain/Gpu/TerrainNodePacking.h"
#include "Features/Terrain/Gpu/TerrainRampImage.h"
#include "Features/Terrain/TerrainHeightField.h"
#include "Features/Terrain/TerrainMaterial.h"
#include "Features/Terrain/TerrainNode.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstring>

namespace gte::terrain::gpu {

namespace {

std::uint32_t Texel(const RampImage& image, int x, int y)
{
    std::uint32_t value = 0;
    std::memcpy(&value, &image.rgba[(static_cast<std::size_t>(y) * static_cast<std::size_t>(image.size) + static_cast<std::size_t>(x)) * 4u], 4u);
    return value;
}

TerrainSettings Plain()
{
    TerrainSettings s;
    s.erosionEnabled = false;
    return Sanitize(s);
}

} // namespace

TEST(GpuRampImageTests, SizeAndBorderAreClamped)
{
    const RampImage tiny = BuildPaddedRamp(Plain(), 2, 100);
    EXPECT_EQ(tiny.size, 16);
    EXPECT_LE(tiny.border, 4);
    EXPECT_EQ(tiny.rgba.size(), 16u * 16u * 4u);

    const RampImage big = BuildPaddedRamp(Plain(), 100000, 0);
    EXPECT_EQ(big.size, 1024);
    EXPECT_GE(big.border, 1);
}

TEST(GpuRampImageTests, BorderReplicatesTheEdgeTexelsEverywhere)
{
    const RampImage image = BuildPaddedRamp(Plain(), 64, 4);
    const int b = image.border;
    const int last = image.size - 1;
    for (int i = 0; i < image.size; ++i) {
        const int ci = std::clamp(i, b, last - b);
        for (int k = 0; k < b; ++k) {
            EXPECT_EQ(Texel(image, k, i), Texel(image, b, ci)) << "left border " << k << "," << i;
            EXPECT_EQ(Texel(image, last - k, i), Texel(image, last - b, ci)) << "right border";
            EXPECT_EQ(Texel(image, i, k), Texel(image, ci, b)) << "top border";
            EXPECT_EQ(Texel(image, i, last - k), Texel(image, ci, last - b)) << "bottom border";
        }
    }
}

TEST(GpuRampImageTests, UvMapKeepsEveryVertexUvInsideTheInnerArea)
{
    for (const int size : { 32, 64, 128, 256 }) {
        const RampImage image = BuildPaddedRamp(Plain(), size, 4);
        const float lo = image.uvMap.Apply(0.0f);
        const float hi = image.uvMap.Apply(1.0f);
        const float texel = 1.0f / static_cast<float>(image.size);
        EXPECT_NEAR(lo, (image.border + 0.5f) * texel, 1e-6f);
        EXPECT_NEAR(hi, (image.size - image.border - 0.5f) * texel, 1e-6f);
        EXPECT_GT(lo, 0.0f);
        EXPECT_LT(hi, 1.0f);
        EXPECT_GE(lo, image.border * texel) << "the lowest sample must not reach into the border";
    }
}

TEST(GpuRampImageTests, SamplingAtTheMappedUvGivesTheSurfaceColour)
{
    const TerrainSettings s = Plain();
    const RampImage image = BuildPaddedRamp(s, 128, 4);
    const auto texelAt = [&](float height01, float slope01) {
        const int x = static_cast<int>(std::lround(image.uvMap.Apply(height01) * image.size - 0.5f));
        const int y = static_cast<int>(std::lround(image.uvMap.Apply(slope01) * image.size - 0.5f));
        return Texel(image, x, y);
    };
    struct Case { float h; float slope; };
    for (const Case c : { Case{ 0.3f, 0.0f }, Case{ 0.2f, 1.0f }, Case{ 0.95f, 0.1f }, Case{ 0.0f, 0.0f }, Case{ 1.0f, 1.0f } }) {
        const std::uint32_t expected = PackRgba8(SurfaceColor(HeightFromHeight01(c.h, s), SlopeFromSlope01(c.slope), s));
        const std::uint32_t got = texelAt(c.h, c.slope);
        for (int channel = 0; channel < 3; ++channel) {
            const int e = static_cast<int>((expected >> (8 * channel)) & 0xFFu);
            const int g = static_cast<int>((got >> (8 * channel)) & 0xFFu);
            EXPECT_NEAR(e, g, 12) << "h " << c.h << " slope " << c.slope << " channel " << channel;
        }
    }
}

TEST(GpuNodePackingTests, PositionsAreAbsoluteAndNeighboursShareBitIdenticalBorderVertices)
{
    const TerrainHeightField field(Plain());
    const int q = field.Layout().quads;
    const NodeKey a{ 60, -60, 0 };
    const NodeKey b{ 61, -60, 0 }; // East neighbour, 7.7 km from the origin.
    const NodeMesh ma = BuildNode(field, a);
    const NodeMesh mb = BuildNode(field, b);

    std::vector<TerrainVertex> pa(ma.vertices.size());
    std::vector<TerrainVertex> pb(mb.vertices.size());
    const RampUvMap uv = MakeRampUvMap(128, 4);
    PackNodeVertices(ma, uv, pa);
    PackNodeVertices(mb, uv, pb);

    const auto idx = [q](int ix, int iz) { return static_cast<std::size_t>(iz) * static_cast<std::size_t>(q + 1) + static_cast<std::size_t>(ix); };
    for (int k = 0; k <= q; ++k) {
        const TerrainVertex& left = pa[idx(q, k)];
        const TerrainVertex& right = pb[idx(0, k)];
        EXPECT_EQ(left.position[0], right.position[0]);
        EXPECT_EQ(left.position[1], right.position[1]);
        EXPECT_EQ(left.position[2], right.position[2]);
    }
    EXPECT_NEAR(pa[idx(0, 0)].position[0], static_cast<float>(ma.originX), 0.01f);
    EXPECT_NEAR(pa[idx(0, 0)].position[2], static_cast<float>(ma.originZ), 0.01f);
}

TEST(GpuNodePackingTests, UvStaysInsideTheRampInnerAreaAndNormalsAreUntouched)
{
    const TerrainHeightField field(Plain());
    const NodeMesh node = BuildNode(field, NodeKey{ 3, 4, 2 });
    std::vector<TerrainVertex> packed(node.vertices.size());
    const RampImage ramp = BuildPaddedRamp(Plain(), 128, 4);
    PackNodeVertices(node, ramp.uvMap, packed);
    const float lo = ramp.uvMap.Apply(0.0f);
    const float hi = ramp.uvMap.Apply(1.0f);
    for (std::size_t i = 0; i < packed.size(); ++i) {
        EXPECT_GE(packed[i].uv[0], lo);
        EXPECT_LE(packed[i].uv[0], hi);
        EXPECT_GE(packed[i].uv[1], lo);
        EXPECT_LE(packed[i].uv[1], hi);
        EXPECT_EQ(packed[i].normal[1], node.vertices[i].normal[1]);
        EXPECT_EQ(packed[i].position[1], node.vertices[i].position[1]);
    }
}

TEST(GpuNodePackingTests, PackIntoAShortSpanWritesOnlyWhatFits)
{
    const TerrainHeightField field(Plain());
    const NodeMesh node = BuildNode(field, NodeKey{ 0, 0, 3 });
    std::vector<TerrainVertex> packed(10, TerrainVertex{});
    EXPECT_NO_FATAL_FAILURE(PackNodeVertices(node, MakeRampUvMap(128, 4), packed));
    EXPECT_NE(packed[9].normal[1], 0.0f);
}

TEST(GpuNodePackingTests, PreviewVerticesCarryTheNodeColoursAsFloats)
{
    const TerrainHeightField field(Plain());
    const NodeMesh node = BuildNode(field, NodeKey{ 1, 1, 1 });
    std::vector<PreviewVertex> out(node.vertices.size());
    PackPreviewVertices(node, out);
    for (std::size_t i = 0; i < out.size(); i += 97) {
        const std::uint32_t packed = node.colors[i];
        EXPECT_NEAR(out[i].color[0], static_cast<float>(packed & 0xFFu) / 255.0f, 1e-6f);
        EXPECT_NEAR(out[i].color[1], static_cast<float>((packed >> 8) & 0xFFu) / 255.0f, 1e-6f);
        EXPECT_NEAR(out[i].color[2], static_cast<float>((packed >> 16) & 0xFFu) / 255.0f, 1e-6f);
        EXPECT_FLOAT_EQ(out[i].normal[1], node.vertices[i].normal[1]);
    }
}

TEST(GpuNodePackingTests, WorldBoxRoundsOutwardOnEveryAxis)
{
    NodeMesh node;
    node.originX = 16383.123456789;
    node.originZ = -16383.987654321;
    node.boundsMin = Vec3{ 0.0f, -12.5f, 0.0f };
    node.boundsMax = Vec3{ 128.0f, 80.25f, 128.0f };
    const BoxF box = NodeWorldBox(node);
    EXPECT_LE(static_cast<double>(box.min[0]), node.originX);
    EXPECT_GE(static_cast<double>(box.max[0]), node.originX + 128.0);
    EXPECT_LE(static_cast<double>(box.min[2]), node.originZ);
    EXPECT_GE(static_cast<double>(box.max[2]), node.originZ + 128.0);
    EXPECT_EQ(box.min[1], -12.5f);
    EXPECT_EQ(box.max[1], 80.25f);
}

TEST(TerrainPreviewGridTests, PreviewGridMatchesTheNodeBuilderForTheSameLayout)
{
    const TerrainHeightField field(Plain());
    const NodeKey key{ 5, -3, 1 };
    const NodeGeometry g = NodeGeometryOf(field.Layout(), key);
    GridBuildSpec spec;
    spec.key = key;
    spec.originX = g.originX;
    spec.originZ = g.originZ;
    spec.spacing = g.spacing;
    spec.quads = field.Layout().quads;
    spec.erode = false;
    const NodeMesh viaGrid = BuildGridMesh(field, spec);
    const NodeMesh viaNode = BuildNode(field, key);
    ASSERT_EQ(viaGrid.vertices.size(), viaNode.vertices.size());
    EXPECT_EQ(std::memcmp(viaGrid.vertices.data(), viaNode.vertices.data(), viaNode.vertices.size() * sizeof(TerrainVertex)), 0);
}

TEST(TerrainPreviewGridTests, A129VertexPreviewGridIsBuilt)
{
    TerrainSettings s;
    s.erosionEnabled = true;
    const TerrainHeightField field(Sanitize(s));
    GridBuildSpec spec;
    spec.originX = 1024.0;
    spec.originZ = -2048.0;
    spec.spacing = 4.0;
    spec.quads = 128;
    spec.erode = true;
    spec.erosionSeed = 99;
    const NodeMesh mesh = BuildGridMesh(field, spec);
    EXPECT_EQ(mesh.vertices.size(), NodeVertexCount(128));
    EXPECT_EQ(mesh.colors.size(), mesh.vertices.size());
    EXPECT_NEAR(mesh.size, 512.0f, 1e-3f);
    for (const TerrainVertex& v : mesh.vertices) {
        ASSERT_TRUE(std::isfinite(v.position[1]));
    }
}

} // namespace gte::terrain::gpu
