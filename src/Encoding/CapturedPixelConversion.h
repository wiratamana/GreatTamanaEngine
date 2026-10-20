#pragma once

#include "../Core/CapturedPixels.h"
#include "HdrColorVisualization.h"

#include <cstdint>
#include <vector>

namespace gte::Encoding {

// True for the BGRA channel order formats a capture can return. The one canonical copy.
bool IsBgraFormat(PixelFormat format) noexcept;

// Converts captured pixels into tightly packed RGBA8 (HDR half-float via `hdrMode`, one channel and BGRA sources).
// Returns a plain copy when the pixels are already RGBA8, and an empty vector when a format needed
// conversion but was not recognized.
std::vector<std::uint8_t> ConvertCapturedPixelsToRgba8(
    const CapturedPixels& raw, HdrVisualization hdrMode = HdrVisualization::DebugScaled);

} // namespace gte::Encoding
