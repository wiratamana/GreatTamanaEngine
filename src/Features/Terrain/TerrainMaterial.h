#pragma once

// Height and slope to colour: sand, grass, rock and snow. Pure CPU, used for vertex colours and the ramp texture.

#include "TerrainSettings.h"

#include <cstdint>
#include <vector>

namespace gte::terrain {

struct Rgb {
    float r = 0.0f;
    float g = 0.0f;
    float b = 0.0f;
};

// Height in metres to 0..1 over [MinHeight, MaxHeight]; the inverse restores the metres.
float Height01(float heightMetres, const TerrainSettings& s) noexcept;
float HeightFromHeight01(float height01, const TerrainSettings& s) noexcept;

// Slope angle (0 flat, pi/2 vertical) to 0..1.
float Slope01(float slopeRadians) noexcept;
float SlopeFromSlope01(float slope01) noexcept;

// Surface colour in linear 0..1.
Rgb SurfaceColor(float heightMetres, float slopeRadians, const TerrainSettings& s) noexcept;

// Packs to RGBA8 with alpha 255 (R in the lowest byte).
std::uint32_t PackRgba8(const Rgb& c) noexcept;

// RGBA8 image, `size` x `size`: u = Height01, v = Slope01. Row 0 is v = 0.
std::vector<std::uint8_t> BuildRampImage(const TerrainSettings& s, int size = 256);

} // namespace gte::terrain
