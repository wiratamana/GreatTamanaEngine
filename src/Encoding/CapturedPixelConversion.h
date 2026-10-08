#pragma once

#include "../Renderer/Renderer.h"

#include <cstdint>
#include <vector>

namespace gte::Encoding {

// True for the BGRA-channel-order 8-bit formats this engine's swapchain/
// RenderTexture can actually negotiate. The ONE canonical copy - do not
// re-add a local one anywhere else.
bool IsBgraFormat(VkFormat format) noexcept;

// Converts a captured raw pixel buffer into tightly-packed RGBA8, handling
// HDR half-float, 1-channel, and BGRA source formats. Returns the already-
// converted buffer, or a plain copy of `raw.pixels` if no conversion was
// needed (already tightly-packed RGBA8). Returns an empty vector if the
// format needed conversion but was not recognized.
std::vector<std::uint8_t> ConvertCapturedPixelsToRgba8(const Renderer::CapturedRawPixels& raw);

} // namespace gte::Encoding
