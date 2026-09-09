#pragma once

#include <cstdint>

namespace gte::Encoding {

// Swaps the R and B channels of a tightly-packed, row-major, 8-bit-per-
// channel BGRA buffer (width*height*4 bytes, no row padding) IN PLACE,
// turning it into RGBA - Green and Alpha are untouched. This exists because
// this engine's swapchain/RenderTexture color format is commonly
// VK_FORMAT_B8G8R8A8_UNORM (see RenderTexture.h's own default format,
// VulkanSwapchain.cpp's ChooseSurfaceFormat), while stb_image_write.h's
// stbi_write_png_to_func (see PngEncoder.h) unconditionally assumes its
// input is already in RGBA channel order - there is no "treat this as BGRA"
// flag anywhere in stb_image_write's own API to lean on instead.
//
// A caller whose source pixels are ALREADY in RGBA order (should this
// engine's negotiated swapchain format ever not be a *_B8G8R8A8_* variant on
// some future GPU/driver - see VulkanSwapchain.cpp's ChooseSurfaceFormat for
// exactly which formats it's willing to negotiate) must NOT call this
// function at all - a caller decides whether a conversion is actually
// needed based on the real VkFormat the captured RenderTexture/swapchain
// actually reports.
//
// width/height must be >= 0; a 0-sized buffer (width == 0 or height == 0) is
// a safe no-op. `pixels` may be nullptr only when the buffer is 0-sized.
void ConvertBgraToRgbaInPlace(std::uint8_t* pixels, int width, int height);

} // namespace gte::Encoding
