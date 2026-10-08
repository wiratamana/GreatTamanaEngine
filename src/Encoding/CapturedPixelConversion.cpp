#include "CapturedPixelConversion.h"

#include "HdrColorVisualization.h"
#include "PixelConversion.h"
#include "SingleChannelVisualization.h"

namespace gte::Encoding {

bool IsBgraFormat(VkFormat format) noexcept
{
    return format == VK_FORMAT_B8G8R8A8_UNORM || format == VK_FORMAT_B8G8R8A8_SRGB;
}

std::vector<std::uint8_t> ConvertCapturedPixelsToRgba8(const Renderer::CapturedRawPixels& raw)
{
    std::vector<std::uint8_t> rgba8(static_cast<std::size_t>(raw.width) * static_cast<std::size_t>(raw.height) * 4);

    if (raw.format == VK_FORMAT_R16G16B16A16_SFLOAT) {
        if (!ConvertHdrRgba16fToRgba8(raw.pixels.data(), raw.format, raw.width, raw.height, rgba8.data())) {
            return {};
        }
        return rgba8;
    }
    if (raw.format == VK_FORMAT_R8_UNORM) {
        if (!Convert1ChannelToGrayscaleRgba8(raw.pixels.data(), raw.format, raw.width, raw.height, rgba8.data())) {
            return {};
        }
        return rgba8;
    }
    if (IsBgraFormat(raw.format)) {
        rgba8 = raw.pixels; // Copy - caller's own `raw` stays untouched.
        ConvertBgraToRgbaInPlace(rgba8.data(), raw.width, raw.height);
        return rgba8;
    }
    rgba8 = raw.pixels; // Already tightly-packed RGBA8 - no conversion needed.
    return rgba8;
}

} // namespace gte::Encoding
