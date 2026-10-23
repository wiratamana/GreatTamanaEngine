#pragma once

// CPU rasterizer: draws 2D overlay quads and segments into an RGBA8 image.

#include "ViewportProjection.h"

#include <cstdint>
#include <span>

namespace gte {

// Fills convex quads with alpha-over blending at pixel centres, clipped to the image.
// Same image contract as RasterizeSegments; draw quads first so lines stay on top.
void RasterizeQuads(std::span<std::uint8_t> rgba, int width, int height, std::span<const OverlayQuad2D> quads);

// Draws clipped 1 px lines with alpha-over blending. `rgba` holds width * height * 4 bytes, row 0 on top.
// An empty image, a too small span or a non-finite segment changes nothing.
void RasterizeSegments(std::span<std::uint8_t> rgba, int width, int height, std::span<const OverlaySegment2D> segments);

} // namespace gte
