#include "HdrColorVisualization.h"

#include <cstddef>
#include <cstring>

namespace gte::Encoding {

// atmosphere-scattering-2 campaign, Phase 5
// (task_manager/atmosphere-scattering-2/PHASE5_AERIAL_LUT_NUMERIC_VALIDATION_TOOL.md)
// - moved out of this file's own anonymous namespace (was a private
// `HalfToFloat()` helper) and renamed to `DecodeHalfFloat()`, now declared
// publicly in HdrColorVisualization.h - a PURE visibility change, this
// function's own body/behavior is completely unchanged.
//
// Plain IEEE 754 binary16 -> binary32 conversion (handles zero, subnormals,
// normals, infinities, and NaN) - no hardware F16C instruction dependency,
// since this is a rare, human/LLM-triggered debug-capture path, never a
// per-frame hot path (see AGENTS.md's own "Named Texture Capture" section
// on GET /get_texture's already-accepted vkDeviceWaitIdle() cost - this
// function's own cost is negligible in comparison).
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

// A multi-scattering "response" value is typically small in absolute terms
// (a fraction of the Transmittance LUT's own [0, 1] scale - scattering
// coefficients are ~0.006 km^-1 and phase functions are normalized over the
// full sphere, see AtmosphereMultiScatteringLut.comp) - a debug-only fixed
// exposure multiplier is applied BEFORE the Reinhard tonemap below purely so
// a genuinely spatially-varying-but-small-magnitude LUT is actually visible
// as a gradient in an 8-bit preview image, instead of quantizing to solid
// black. This constant affects ONLY this debug visualization path - it is
// never used by any real shading/compositing math anywhere in this
// campaign (Phase 7's real sky/aerial-perspective passes will have their
// own, separate, deliberately-designed exposure/tonemapping).
constexpr float kDebugVisualizationExposure = 400.0f;

// Reinhard tonemap + [0,255] quantization - see this file's own header
// comment for why a plain clamp would be actively misleading here.
std::uint8_t TonemapToByte(float value) noexcept
{
    if (!(value > 0.0f)) {
        // Handles exactly 0, negative (shouldn't occur for a physical
        // scattering quantity, but stay safe), and NaN (NaN > 0.0f is
        // false) uniformly - never propagate a NaN into the output image.
        return 0;
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
    const std::uint8_t* rawRgba16f, VkFormat colorFormat, int width, int height, std::uint8_t* outRgba8)
{
    if (width <= 0 || height <= 0) {
        return true; // Safe no-op - mirrors ConvertDepthToGrayscaleRgba8()'s own convention.
    }
    if (rawRgba16f == nullptr || outRgba8 == nullptr) {
        return false;
    }
    if (colorFormat != VK_FORMAT_R16G16B16A16_SFLOAT) {
        return false; // Unrecognized format - see this function's own header doc comment.
    }

    const std::size_t pixelCount = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
    for (std::size_t i = 0; i < pixelCount; ++i) {
        std::uint16_t channels[4] = { 0, 0, 0, 0 };
        std::memcpy(channels, rawRgba16f + i * 8, 8);

        std::uint8_t rgba[4];
        rgba[0] = TonemapToByte(DecodeHalfFloat(channels[0]));
        rgba[1] = TonemapToByte(DecodeHalfFloat(channels[1]));
        rgba[2] = TonemapToByte(DecodeHalfFloat(channels[2]));
        rgba[3] = 255; // Alpha carries no meaningful data for this LUT (imageStore()'s own alpha is always 0.0) - always fully opaque for display.

        std::memcpy(outRgba8 + i * 4, rgba, 4);
    }
    return true;
}

} // namespace gte::Encoding
