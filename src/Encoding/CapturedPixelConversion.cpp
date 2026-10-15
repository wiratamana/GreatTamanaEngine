#include "CapturedPixelConversion.h"

#include "HdrColorVisualization.h"
#include "PixelConversion.h"
#include "SingleChannelVisualization.h"

namespace gte::Encoding {

bool IsBgraFormat(PixelFormat format) noexcept
{
    return format == PixelFormat::BGRA8_UNorm;
}

std::vector<std::uint8_t> ConvertCapturedPixelsToRgba8(const CapturedPixels& raw)
{
    std::vector<std::uint8_t> rgba8(static_cast<std::size_t>(raw.width) * static_cast<std::size_t>(raw.height) * 4);

    if (raw.format == PixelFormat::RGBA16_Float) {
        if (!ConvertHdrRgba16fToRgba8(raw.pixels.data(), raw.format, raw.width, raw.height, rgba8.data())) {
            return {};
        }
        return rgba8;
    }
    if (raw.format == PixelFormat::R8_UNorm) {
        if (!Convert1ChannelToGrayscaleRgba8(raw.pixels.data(), raw.format, raw.width, raw.height, rgba8.data())) {
            return {};
        }
        return rgba8;
    }
    if (IsBgraFormat(raw.format)) {
        rgba8 = raw.pixels;
        ConvertBgraToRgbaInPlace(rgba8.data(), raw.width, raw.height);
        return rgba8;
    }
    rgba8 = raw.pixels; // Already tightly packed RGBA8.
    return rgba8;
}

} // namespace gte::Encoding
