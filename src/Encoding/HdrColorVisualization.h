#pragma once

#include "../Core/PixelFormat.h"

#include <cstdint>

namespace gte::Encoding {

// Converts a tightly packed RGBA16_Float readback (8 bytes per texel) into RGBA8 with a Reinhard tonemap,
// so values above 1.0 stay distinguishable. Debug visualization only, never used by shading.
// Any other format returns false and leaves `outRgba8` untouched. `outRgba8` must hold width*height*4 bytes.
bool ConvertHdrRgba16fToRgba8(
    const std::uint8_t* rawRgba16f, PixelFormat colorFormat, int width, int height, std::uint8_t* outRgba8);

// IEEE 754 binary16 to binary32: zero, subnormals, normals, infinities and NaN, no F16C needed.
float DecodeHalfFloat(std::uint16_t half) noexcept;

} // namespace gte::Encoding
