#pragma once

#include "../Core/PixelFormat.h"

#include <cstdint>

namespace gte::Encoding {

// Converts a tightly packed R8_UNorm readback into grayscale RGBA8 (R=G=B=value, A=255).
// Any other format returns false and leaves `outRgba8` untouched.
bool Convert1ChannelToGrayscaleRgba8(
    const std::uint8_t* raw, PixelFormat colorFormat, int width, int height, std::uint8_t* outRgba8);

} // namespace gte::Encoding
