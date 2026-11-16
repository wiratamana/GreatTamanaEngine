#pragma once

// Top-down debug picture of the terrain and every water body, drawn on the CPU (no GPU readback needed).

#include "WaterBodySet.h"
#include "WaterEdit.h"

#include <cstdint>
#include <vector>

namespace gte::water {

struct WaterMapView {
    double centerX = 0.0;
    double centerZ = 0.0;
    double halfRange = 1000.0; // Metres from the centre to each edge.
    int size = 512;            // Pixels per side, 64..2048. +Z is up in the picture, +X is right.
};

struct WaterMapOptions {
    bool outlines = true;  // Lake outlines, river centre lines and control points.
    bool shoreBand = true; // Land that stays wet.
};

struct WaterMapImage {
    int size = 0;
    std::vector<std::uint8_t> rgba;
    int waterPixels = 0;
    int landPixels = 0;
    int shorePixels = 0;
};

WaterMapImage RenderWaterMap(const WaterBodySet& set, const HeightFn& ground, const WaterMapView& view, const WaterMapOptions& options);

// Pixel position of a world point in the picture (may lie outside).
bool WorldToMapPixel(const WaterMapView& view, double x, double z, double& px, double& py) noexcept;

} // namespace gte::water
