#pragma once

#include <cstdint>
#include <volk.h>

namespace gte::Encoding {

// network-impl-4 campaign, Phase 3
// (task_manager/network-impl-4/PHASE3_GENERIC_IMAGE_READBACK_AND_DEPTH_VISUALIZATION.md) -
// converts a tightly-packed, row-major depth-aspect-only readback buffer
// (width*height*4 bytes, no row padding - see Renderer::CaptureImagePixels()'s
// own doc comment for why every depth format this engine can ever pick is
// exactly 4 bytes/texel for a depth-aspect-only copy) into a plain grayscale
// RGBA8 image (R==G==B==the depth value remapped to [0,255], A==255) for
// PNG encoding - exactly the same "ready for Encoding::EncodeRgba8ToPng()"
// output shape ConvertBgraToRgbaInPlace() already produces for color.
//
// Supports EXACTLY the three concrete depth formats
// VulkanDevice::PickDepthFormat() (src/Renderer/Vulkan/VulkanDevice.cpp)
// can ever actually return - VK_FORMAT_D32_SFLOAT (pure float32, values
// already in [0,1] - just multiply by 255), VK_FORMAT_D32_SFLOAT_S8_UINT
// (identical to the above for a DEPTH-aspect-only copy - the stencil byte
// is a separate aspect, never present in this buffer at all), and
// VK_FORMAT_D24_UNORM_S8_UINT (a 24-bit UNORM value packed into the low 24
// bits of each 32-bit little-endian word - divide by 0x00FFFFFF). Returns
// `false` (leaving `outRgba8` UNTOUCHED) for any other format - the caller
// (Phase 4/5) must treat that as a clean, explicit failure ("depth format
// not recognized"), never silently emit a wrong/blank image. This is the
// exact "accepted narrow risk, explicitly documented, never a crash or a
// silently-wrong result" precedent Application.cpp's own IsBgraFormat()
// already established for the color/BGRA case.
//
// `rawDepth` is exactly width*height*4 bytes (see caller). `outRgba8` must
// already be sized to width*height*4 bytes before calling - this function
// only ever WRITES into it, never resizes it (mirrors
// ConvertBgraToRgbaInPlace()'s own "caller owns the buffer" convention,
// though this one is an OUT buffer, not in-place, since the source/dest
// pixel encodings are structurally different, not just channel-swapped).
// A 0-sized buffer (width <= 0 or height <= 0) is a safe no-op; a NULL
// `rawDepth`/`outRgba8` with a genuinely positive width/height is also a
// safe no-op returning `false` (mirrors ConvertBgraToRgbaInPlace()'s own
// `pixels == nullptr` guard - never dereferenced blindly).
bool ConvertDepthToGrayscaleRgba8(
    const std::uint8_t* rawDepth, VkFormat depthFormat, int width, int height, std::uint8_t* outRgba8);

} // namespace gte::Encoding
