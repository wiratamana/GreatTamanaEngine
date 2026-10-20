#include "HdrColorVisualization.h"

#include "../Math/ToneMapCurves.h"
#include "../Math/Vec3.h"

#include <cmath>
#include <cstddef>
#include <cstring>
#include <string_view>

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

std::uint8_t UnitToByte(float value) noexcept
{
    if (!(value > 0.0f)) {
        return 0; // Zero, negative and NaN all map to black.
    }
    return value >= 1.0f ? std::uint8_t{ 255 } : static_cast<std::uint8_t>(value * 255.0f + 0.5f);
}

// Reinhard tonemap and [0,255] quantization.
std::uint8_t TonemapToByte(float value) noexcept
{
    if (!(value > 0.0f)) {
        return 0;
    }
    const float exposed = value * kDebugVisualizationExposure;
    return std::isinf(exposed) ? std::uint8_t{ 255 } : UnitToByte(exposed / (1.0f + exposed));
}

// Calls `convert(r, g, b, out4)` for every texel. Alpha is always written opaque.
template <typename ConvertFn>
void ForEachTexel(const std::uint8_t* raw, int width, int height, std::uint8_t* out, ConvertFn&& convert) noexcept
{
    const std::size_t pixelCount = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
    for (std::size_t i = 0; i < pixelCount; ++i) {
        std::uint16_t channels[4] = { 0, 0, 0, 0 };
        std::memcpy(channels, raw + i * 8, 8);

        std::uint8_t rgba[4];
        convert(DecodeHalfFloat(channels[0]), DecodeHalfFloat(channels[1]), DecodeHalfFloat(channels[2]), rgba);
        rgba[3] = 255;

        std::memcpy(out + i * 4, rgba, 4);
    }
}

bool StartsWith(std::string_view text, std::string_view prefix) noexcept
{
    return text.size() >= prefix.size() && text.substr(0, prefix.size()) == prefix;
}

} // namespace

HdrVisualization HdrVisualizationForTextureName(std::string_view name) noexcept
{
    if (name == "GameView" || name == "SceneView") {
        return HdrVisualization::DisplayReferred;
    }
    if (name == "GameViewComposited" || name == "SceneViewComposited" || name == "GameViewVolumetricLight" ||
        name == "SceneViewVolumetricLight" || StartsWith(name, "FrameDebuggerReplayStep")) {
        return HdrVisualization::SceneReferred;
    }
    return HdrVisualization::DebugScaled;
}

bool ConvertRgba16fToRgba8(const std::uint8_t* rawRgba16f, PixelFormat colorFormat, int width, int height,
    std::uint8_t* outRgba8, HdrVisualization mode)
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

    switch (mode) {
    case HdrVisualization::DisplayReferred:
        ForEachTexel(rawRgba16f, width, height, outRgba8, [](float r, float g, float b, std::uint8_t* out) {
            out[0] = UnitToByte(r);
            out[1] = UnitToByte(g);
            out[2] = UnitToByte(b);
        });
        return true;
    case HdrVisualization::SceneReferred:
        ForEachTexel(rawRgba16f, width, height, outRgba8, [](float r, float g, float b, std::uint8_t* out) {
            const Vec3 display = tonemap::ToneMapLinearToDisplay(
                Vec3{ r, g, b }, tonemap::kDefaultExposure, tonemap::kDefaultMode, tonemap::kDefaultSaturation);
            out[0] = UnitToByte(display.x);
            out[1] = UnitToByte(display.y);
            out[2] = UnitToByte(display.z);
        });
        return true;
    case HdrVisualization::DebugScaled:
        break;
    }
    ForEachTexel(rawRgba16f, width, height, outRgba8, [](float r, float g, float b, std::uint8_t* out) {
        out[0] = TonemapToByte(r);
        out[1] = TonemapToByte(g);
        out[2] = TonemapToByte(b);
    });
    return true;
}

bool ConvertHdrRgba16fToRgba8(
    const std::uint8_t* rawRgba16f, PixelFormat colorFormat, int width, int height, std::uint8_t* outRgba8)
{
    return ConvertRgba16fToRgba8(rawRgba16f, colorFormat, width, height, outRgba8, HdrVisualization::DebugScaled);
}

} // namespace gte::Encoding
