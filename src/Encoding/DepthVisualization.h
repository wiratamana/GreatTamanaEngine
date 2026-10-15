#pragma once

#include "../Core/PixelFormat.h"

#include <cstdint>

namespace gte::Encoding {

// Converts a tightly packed depth-aspect readback (4 bytes per texel) into grayscale RGBA8.
// Supports D32_Float, D32_Float_S8_UInt and D24_UNorm_S8_UInt; any other format returns false
// and leaves `outRgba8` untouched. `outRgba8` must hold width*height*4 bytes; an empty size is a no-op.
bool ConvertDepthToGrayscaleRgba8(
    const std::uint8_t* rawDepth, PixelFormat depthFormat, int width, int height, std::uint8_t* outRgba8);

} // namespace gte::Encoding
