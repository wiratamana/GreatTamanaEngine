#pragma once

#include "PixelFormat.h"

#include <cstdint>
#include <vector>

namespace gte {

// Tightly packed, row-major pixels read back from a GPU image.
struct CapturedPixels {
    std::vector<std::uint8_t> pixels;
    int width = 0;
    int height = 0;
    PixelFormat format = PixelFormat::Undefined;
};

} // namespace gte
