#pragma once

#include "../Core/PixelFormat.h"

#include <cstdint>
#include <string_view>

namespace gte::Encoding {

// How RGBA16_Float texels are mapped to 8 bits for display.
enum class HdrVisualization : std::uint8_t {
    DebugScaled,     // Fixed x400 exposure + Reinhard. For tiny-valued data such as LUTs.
    DisplayReferred, // Values are already display colors: clamp to 0..1, no curve.
    SceneReferred,   // Linear scene radiance: default tone mapper (exposure, curve, sRGB encode).
};

// Picks the visualization that fits a render graph texture name.
HdrVisualization HdrVisualizationForTextureName(std::string_view name) noexcept;

// Converts a tightly packed RGBA16_Float readback (8 bytes per texel) into RGBA8 using `mode`.
// Any other format returns false and leaves `outRgba8` untouched. `outRgba8` must hold width*height*4 bytes.
bool ConvertRgba16fToRgba8(const std::uint8_t* rawRgba16f, PixelFormat colorFormat, int width, int height,
    std::uint8_t* outRgba8, HdrVisualization mode);

// DebugScaled conversion: Reinhard after a fixed exposure so values above 1.0 stay distinguishable.
bool ConvertHdrRgba16fToRgba8(
    const std::uint8_t* rawRgba16f, PixelFormat colorFormat, int width, int height, std::uint8_t* outRgba8);

// IEEE 754 binary16 to binary32: zero, subnormals, normals, infinities and NaN, no F16C needed.
float DecodeHalfFloat(std::uint16_t half) noexcept;

} // namespace gte::Encoding
