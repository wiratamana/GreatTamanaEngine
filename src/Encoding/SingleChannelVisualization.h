#pragma once
#include <cstdint>
#include <volk.h>

namespace gte::Encoding {

// Converts a tightly-packed single-channel 8-bit color readback into
// grayscale RGBA8 (R=G=B=value, A=255). Supports VK_FORMAT_R8_UNORM only;
// returns false (outRgba8 untouched) for any other format.
bool Convert1ChannelToGrayscaleRgba8(
    const std::uint8_t* raw, VkFormat colorFormat, int width, int height, std::uint8_t* outRgba8);

} // namespace gte::Encoding
