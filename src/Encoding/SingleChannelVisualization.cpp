#include "SingleChannelVisualization.h"
#include <cstddef>

namespace gte::Encoding {

bool Convert1ChannelToGrayscaleRgba8(
    const std::uint8_t* raw, VkFormat colorFormat, int width, int height, std::uint8_t* outRgba8)
{
    if (width <= 0 || height <= 0) {
        return true;
    }
    if (raw == nullptr || outRgba8 == nullptr) {
        return false;
    }
    if (colorFormat != VK_FORMAT_R8_UNORM) {
        return false;
    }

    const std::size_t pixelCount = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
    for (std::size_t i = 0; i < pixelCount; ++i) {
        const std::uint8_t value = raw[i];
        std::uint8_t* out = outRgba8 + i * 4;
        out[0] = value;
        out[1] = value;
        out[2] = value;
        out[3] = 255;
    }
    return true;
}

} // namespace gte::Encoding
