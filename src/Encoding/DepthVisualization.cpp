#include "DepthVisualization.h"

#include <cstddef>
#include <cstring>

namespace gte::Encoding {

namespace {

std::uint8_t ClampToByte(float value)
{
    if (value <= 0.0f) return 0;
    if (value >= 1.0f) return 255;
    return static_cast<std::uint8_t>(value * 255.0f + 0.5f);
}

} // namespace

bool ConvertDepthToGrayscaleRgba8(
    const std::uint8_t* rawDepth, PixelFormat depthFormat, int width, int height, std::uint8_t* outRgba8)
{
    if (width <= 0 || height <= 0) {
        return true;
    }
    if (rawDepth == nullptr || outRgba8 == nullptr) {
        return false;
    }
    if (depthFormat != PixelFormat::D32_Float && depthFormat != PixelFormat::D32_Float_S8_UInt
        && depthFormat != PixelFormat::D24_UNorm_S8_UInt) {
        return false;
    }

    const std::size_t pixelCount = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
    for (std::size_t i = 0; i < pixelCount; ++i) {
        std::uint32_t word = 0;
        std::memcpy(&word, rawDepth + i * 4, 4);

        std::uint8_t gray = 0;
        if (depthFormat == PixelFormat::D24_UNorm_S8_UInt) {
            const std::uint32_t depth24 = word & 0x00FFFFFFu;
            gray = ClampToByte(static_cast<float>(depth24) / static_cast<float>(0x00FFFFFFu));
        } else {
            float depthFloat = 0.0f;
            std::memcpy(&depthFloat, &word, 4);
            gray = ClampToByte(depthFloat);
        }

        std::uint8_t* out = outRgba8 + i * 4;
        out[0] = gray;
        out[1] = gray;
        out[2] = gray;
        out[3] = 255;
    }
    return true;
}

} // namespace gte::Encoding
