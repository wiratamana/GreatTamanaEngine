#include "SingleChannelVisualization.h"
#include <cstddef>
#include <cstring>

namespace gte::Encoding {

namespace {

// Maps a float to 0..255 with clamping; NaN maps to 0.
std::uint8_t FloatToUnorm8(float value) noexcept
{
    if (!(value > 0.0f)) {
        return 0;
    }
    if (value >= 1.0f) {
        return 255;
    }
    return static_cast<std::uint8_t>(value * 255.0f + 0.5f);
}

} // namespace

bool Convert1ChannelToGrayscaleRgba8(
    const std::uint8_t* raw, PixelFormat colorFormat, int width, int height, std::uint8_t* outRgba8)
{
    if (width <= 0 || height <= 0) {
        return true;
    }
    if (raw == nullptr || outRgba8 == nullptr) {
        return false;
    }
    if (colorFormat != PixelFormat::R8_UNorm && colorFormat != PixelFormat::R32_Float) {
        return false;
    }

    const std::size_t pixelCount = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
    for (std::size_t i = 0; i < pixelCount; ++i) {
        std::uint8_t value = 0;
        if (colorFormat == PixelFormat::R32_Float) {
            float f = 0.0f;
            std::memcpy(&f, raw + i * sizeof(float), sizeof(float));
            value = FloatToUnorm8(f);
        } else {
            value = raw[i];
        }
        std::uint8_t* out = outRgba8 + i * 4;
        out[0] = value;
        out[1] = value;
        out[2] = value;
        out[3] = 255;
    }
    return true;
}

} // namespace gte::Encoding
