#pragma once

// Top-down picture of the resident nodes, the camera and its view wedge. CPU only, no text:
// colours carry the meaning (see docs/debugging/terrain.md). +X is right, +Z is up.

#include "../TerrainControl.h"

#include <array>
#include <cstdint>
#include <span>
#include <vector>

namespace gte::terrain {

enum class MapColorMode : std::uint8_t {
    Lod,   // One colour per LOD level.
    State, // Green drawn in view, amber resident but outside the view, grey hidden by another node.
};

struct MapCamera {
    double x = 0.0;
    double z = 0.0;
    double yawRadians = 0.0; // 0 looks along +Z.
    double fovYDegrees = 60.0;
    double aspect = 16.0 / 9.0;
    double viewDistance = 12000.0;
};

struct MapOptions {
    int size = 768;            // Pixels per side (square).
    double range = 0.0;        // Half side in metres; 0 uses the view distance plus a margin.
    MapColorMode mode = MapColorMode::Lod;
    bool drawFrustum = true;
    double heightMin = -300.0; // Metres mapped to the darkest shade.
    double heightMax = 1200.0; // Metres mapped to the brightest shade.
};

struct MapImage {
    int width = 0;
    int height = 0;
    std::vector<std::uint8_t> rgba; // width * height * 4, row 0 is the top (largest Z).
    double metresPerPixel = 0.0;
    double minX = 0.0;
    double maxX = 0.0;
    double minZ = 0.0;
    double maxZ = 0.0;
};

// Colour of a LOD level in the Lod mode (also used for the legend strip).
std::array<std::uint8_t, 3> MapLodColor(int level) noexcept;

// The map is centred on the camera. Nodes are drawn coarse first so fine nodes stay on top.
MapImage RenderTerrainMap(std::span<const TerrainNodeInfo> nodes, const MapCamera& camera, const MapOptions& options);

} // namespace gte::terrain
