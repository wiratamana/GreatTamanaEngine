#include "PixelConversion.h"

#include <cstddef>
#include <utility>

namespace gte::Encoding {

void ConvertBgraToRgbaInPlace(std::uint8_t* pixels, int width, int height) {
    if (pixels == nullptr || width <= 0 || height <= 0) {
        return;
    }

    const std::size_t pixelCount = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
    for (std::size_t i = 0; i < pixelCount; ++i) {
        std::uint8_t* p = pixels + (i * 4);
        std::swap(p[0], p[2]);
    }
}

} // namespace gte::Encoding
