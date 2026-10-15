#include "HdrColorVisualization.h"

#include <cstddef>
#include <cstring>

namespace gte::Encoding {

// IEEE 754 binary16 to binary32 without hardware F16C; only used on rare debug-capture paths.
float DecodeHalfFloat(std::uint16_t half) noexcept
{
    const std::uint32_t sign = static_cast<std::uint32_t>(half & 0x8000u) << 16;
    std::uint32_t exponent = (half & 0x7C00u) >> 10;
    std::uint32_t mantissa = half & 0x03FFu;

    std::uint32_t bits;
    if (exponent == 0) {
        if (mantissa == 0) {
            bits = sign; // +/-0
        } else {
            // Subnormal half -> normalize into a normal float.
            exponent = 1;
            while ((mantissa & 0x0400u) == 0) {
                mantissa <<= 1;
                --exponent;
            }
            mantissa &= 0x03FFu;
            bits = sign | ((exponent + (127 - 15)) << 23) | (mantissa << 13);
        }
    } else if (exponent == 0x1Fu) {
        bits = sign | 0x7F800000u | (mantissa << 13); // +/-Inf or NaN
    } else {
        bits = sign | ((exponent + (127 - 15)) << 23) | (mantissa << 13);
    }

    float result = 0.0f;
    std::memcpy(&result, &bits, sizeof(result));
    return result;
}

namespace {

// Fixed debug exposure applied before the tonemap so small-magnitude values stay visible in 8 bits.
constexpr float kDebugVisualizationExposure = 400.0f;

// Reinhard tonemap and [0,255] quantization.
std::uint8_t TonemapToByte(float value) noexcept
{
    if (!(value > 0.0f)) {
        return 0; // Zero, negative and NaN all map to black.
    }
    const float exposed = value * kDebugVisualizationExposure;
    float tonemapped = exposed / (1.0f + exposed);
    if (tonemapped > 1.0f) {
        tonemapped = 1.0f;
    }
    return static_cast<std::uint8_t>(tonemapped * 255.0f + 0.5f);
}

} // namespace

bool ConvertHdrRgba16fToRgba8(
    const std::uint8_t* rawRgba16f, PixelFormat colorFormat, int width, int height, std::uint8_t* outRgba8)
{
    if (width <= 0 || height <= 0) {
        return true;
    }
    if (rawRgba16f == nullptr || outRgba8 == nullptr) {
        return false;
    }
    if (colorFormat != PixelFormat::RGBA16_Float) {
        return false;
    }

    const std::size_t pixelCount = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
    for (std::size_t i = 0; i < pixelCount; ++i) {
        std::uint16_t channels[4] = { 0, 0, 0, 0 };
        std::memcpy(channels, rawRgba16f + i * 8, 8);

        std::uint8_t rgba[4];
        rgba[0] = TonemapToByte(DecodeHalfFloat(channels[0]));
        rgba[1] = TonemapToByte(DecodeHalfFloat(channels[1]));
        rgba[2] = TonemapToByte(DecodeHalfFloat(channels[2]));
        rgba[3] = 255; // Alpha carries no data here; keep it opaque for display.

        std::memcpy(outRgba8 + i * 4, rgba, 4);
    }
    return true;
}

} // namespace gte::Encoding
