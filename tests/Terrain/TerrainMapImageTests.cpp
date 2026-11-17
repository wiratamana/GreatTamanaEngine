#include "Features/Terrain/Debug/TerrainMapImage.h"

#include <gtest/gtest.h>

#include <vector>

namespace gte::terrain {

namespace {

TerrainNodeInfo Node(int x, int z, int level, double size, bool enabled = true, bool inView = true)
{
    TerrainNodeInfo node;
    node.key = NodeKey{ x, z, static_cast<std::uint8_t>(level) };
    node.minX = x * size;
    node.maxX = (x + 1) * size;
    node.minZ = z * size;
    node.maxZ = (z + 1) * size;
    node.minY = 0.0;
    node.maxY = 100.0;
    node.enabled = enabled;
    node.inView = inView;
    return node;
}

std::array<std::uint8_t, 3> PixelAt(const MapImage& image, int px, int py)
{
    const std::size_t i = (static_cast<std::size_t>(py) * image.width + px) * 4;
    return { image.rgba[i], image.rgba[i + 1], image.rgba[i + 2] };
}

// Pixel of a world position (camera at the origin).
std::array<std::uint8_t, 3> PixelOfWorld(const MapImage& image, double x, double z)
{
    return PixelAt(image, static_cast<int>((x - image.minX) / image.metresPerPixel), static_cast<int>((image.maxZ - z) / image.metresPerPixel));
}

MapOptions Options(int size = 256, double range = 1000.0)
{
    MapOptions options;
    options.size = size;
    options.range = range;
    options.drawFrustum = false;
    return options;
}

} // namespace

TEST(TerrainMapImageTest, SizeIsClampedAndBufferMatches)
{
    const MapCamera camera;
    const MapImage small = RenderTerrainMap({}, camera, Options(1));
    EXPECT_GE(small.width, 64);
    EXPECT_EQ(small.rgba.size(), static_cast<std::size_t>(small.width) * small.height * 4);
    const MapImage huge = RenderTerrainMap({}, camera, Options(100000));
    EXPECT_LE(huge.width, 2048);
}

TEST(TerrainMapImageTest, WorldRectangleIsCentredOnTheCamera)
{
    MapCamera camera;
    camera.x = 500.0;
    camera.z = -300.0;
    const MapImage image = RenderTerrainMap({}, camera, Options(200, 1000.0));
    EXPECT_DOUBLE_EQ(image.minX, -500.0);
    EXPECT_DOUBLE_EQ(image.maxX, 1500.0);
    EXPECT_DOUBLE_EQ(image.minZ, -1300.0);
    EXPECT_DOUBLE_EQ(image.maxZ, 700.0);
    EXPECT_DOUBLE_EQ(image.metresPerPixel, 10.0);
}

TEST(TerrainMapImageTest, LodModeColoursByLevelAndAlwaysOpaque)
{
    const std::vector<TerrainNodeInfo> nodes = { Node(0, 0, 0, 300.0), Node(-1, -1, 3, 300.0) };
    const MapImage image = RenderTerrainMap(nodes, MapCamera{}, Options());
    for (std::size_t i = 3; i < image.rgba.size(); i += 4) {
        ASSERT_EQ(image.rgba[i], 255);
    }
    // Inside each node, away from its border and the camera marker.
    const auto level0 = PixelOfWorld(image, 250.0, 250.0);
    const auto level3 = PixelOfWorld(image, -250.0, -250.0);
    EXPECT_NE(level0, level3);
    const auto background = PixelAt(image, 200, 20);
    EXPECT_NE(level0, background);
}

TEST(TerrainMapImageTest, StateModeSeparatesDrawnOutsideAndHidden)
{
    const std::vector<TerrainNodeInfo> nodes = { Node(0, 0, 0, 250.0, true, true), Node(1, 0, 0, 250.0, true, false),
        Node(0, 1, 0, 250.0, false, false) };
    MapOptions options = Options();
    options.mode = MapColorMode::State;
    const MapImage image = RenderTerrainMap(nodes, MapCamera{}, options);
    const auto drawn = PixelOfWorld(image, 120.0, 120.0);
    const auto outside = PixelOfWorld(image, 370.0, 120.0);
    const auto hidden = PixelOfWorld(image, 120.0, 370.0);
    EXPECT_NE(drawn, outside);
    EXPECT_NE(drawn, hidden);
    EXPECT_NE(outside, hidden);
    EXPECT_GT(drawn[1], drawn[0]); // Green dominates.
    EXPECT_GT(outside[0], outside[2]); // Amber dominates.
}

TEST(TerrainMapImageTest, FinerNodesAreDrawnOverCoarseOnes)
{
    // A coarse node covers the area, a fine node sits inside it.
    std::vector<TerrainNodeInfo> nodes = { Node(0, 0, 0, 100.0), Node(0, 0, 4, 600.0) };
    nodes[0].minX = 100.0;
    nodes[0].maxX = 300.0;
    nodes[0].minZ = 100.0;
    nodes[0].maxZ = 300.0;
    const MapImage image = RenderTerrainMap(nodes, MapCamera{}, Options());
    const auto inside = PixelOfWorld(image, 200.0, 200.0);
    const auto outsideFine = PixelOfWorld(image, 450.0, 450.0);
    EXPECT_NE(inside, outsideFine);
    EXPECT_GT(inside[1], inside[2]); // Level 0 is green; the level 4 colour is red.
}

TEST(TerrainMapImageTest, HigherNodesAreBrighter)
{
    std::vector<TerrainNodeInfo> nodes = { Node(0, 0, 0, 200.0), Node(1, 0, 0, 200.0) };
    nodes[0].minY = nodes[0].maxY = -250.0;
    nodes[1].minY = nodes[1].maxY = 1100.0;
    const MapImage image = RenderTerrainMap(nodes, MapCamera{}, Options());
    const auto low = PixelOfWorld(image, 100.0, 100.0);
    const auto high = PixelOfWorld(image, 300.0, 100.0);
    EXPECT_GT(static_cast<int>(high[1]), static_cast<int>(low[1]));
}

TEST(TerrainMapImageTest, CameraMarkerAndWedgeAreDrawn)
{
    MapCamera camera;
    camera.yawRadians = 0.0; // Looking along +Z, which is up in the picture.
    MapOptions options = Options(256, 1000.0);
    options.drawFrustum = true;
    const MapImage withWedge = RenderTerrainMap({}, camera, options);
    options.drawFrustum = false;
    const MapImage without = RenderTerrainMap({}, camera, options);
    EXPECT_NE(withWedge.rgba, without.rgba);
    // The marker is red at the image centre in both.
    const auto centre = PixelAt(without, 128, 128);
    EXPECT_GT(centre[0], 200);
    EXPECT_LT(centre[1], 100);
    // The wedge centre line runs up from the camera.
    const auto ahead = PixelAt(withWedge, 128, 100);
    EXPECT_EQ(ahead[0], 255);
    EXPECT_EQ(ahead[1], 255);
}

TEST(TerrainMapImageTest, NodesOutsideTheMapAreSkipped)
{
    const std::vector<TerrainNodeInfo> nodes = { Node(1000, 1000, 0, 128.0) };
    const MapImage a = RenderTerrainMap(nodes, MapCamera{}, Options());
    const MapImage b = RenderTerrainMap({}, MapCamera{}, Options());
    EXPECT_EQ(a.rgba, b.rgba);
}

TEST(TerrainMapImageTest, LodColoursAreDistinctAndClamped)
{
    for (int a = 0; a < 12; ++a) {
        for (int b = a + 1; b < 12; ++b) {
            EXPECT_NE(MapLodColor(a), MapLodColor(b));
        }
    }
    EXPECT_EQ(MapLodColor(-5), MapLodColor(0));
    EXPECT_EQ(MapLodColor(99), MapLodColor(11));
}

} // namespace gte::terrain
