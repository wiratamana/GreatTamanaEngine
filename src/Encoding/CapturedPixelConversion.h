#pragma once

#include "../Core/CapturedPixels.h"
#include "HdrColorVisualization.h"

#include <cstdint>
#include <vector>

namespace gte::Encoding {

// True for the BGRA channel order formats a capture can return. The one canonical copy.
bool IsBgraFormat(PixelFormat format) noexcept;

// Converts captured pixels to tightly packed RGBA8 (half-float via `hdrMode`).
// Returns an empty vector for an unrecognized format that needs conversion.
std::vector<std::uint8_t> ConvertCapturedPixelsToRgba8(
    const CapturedPixels& raw, HdrVisualization hdrMode = HdrVisualization::DebugScaled);

} // namespace gte::Encoding
