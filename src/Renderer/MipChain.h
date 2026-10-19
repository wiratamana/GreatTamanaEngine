#pragma once

#include <cstdint>
#include <vector>

namespace gte {

// Box-filtered mip chain down to 1x1. Level 0 is a copy of the input.
struct MipChainRgba8 {
    std::uint32_t levelCount = 0;
    std::vector<std::uint8_t> pixels;            // All levels, tightly packed, level 0 first.
    std::vector<std::uint64_t> levelByteOffsets; // One per level.
    std::vector<std::uint32_t> levelWidths;
    std::vector<std::uint32_t> levelHeights;
};

// Returns an empty chain (levelCount == 0) for a null input or a zero size.
MipChainRgba8 BuildMipChainRgba8(const std::uint8_t* rgba, std::uint32_t width, std::uint32_t height);

} // namespace gte
