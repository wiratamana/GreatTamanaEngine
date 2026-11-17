#pragma once

// Colour ramp texture for the GPU path, which only carries (height01, slope01) per vertex. The engine samples
// every material image with repeat addressing and mips, so the ramp gets a replicated border and the uv is
// mapped to stay inside it: no wrap-around colours at the edges, even in the coarse mips.

#include "../TerrainSettings.h"

#include <cstdint>
#include <vector>

namespace gte::terrain::gpu {

struct RampUvMap {
    float scale = 1.0f;
    float bias = 0.0f;
    float Apply(float u01) const noexcept { return bias + scale * u01; }
};

struct RampImage {
    int size = 0;                      // Width and height, border included.
    int border = 0;
    std::vector<std::uint8_t> rgba;    // size * size * 4.
    RampUvMap uvMap;                   // Maps Height01 and Slope01 into the inner area.
};

// `size` is clamped to 16..1024 and the border to 1..size/4. u = Height01 (x), v = Slope01 (y).
RampImage BuildPaddedRamp(const TerrainSettings& settings, int size = 128, int border = 4);

RampUvMap MakeRampUvMap(int size, int border) noexcept;

} // namespace gte::terrain::gpu
