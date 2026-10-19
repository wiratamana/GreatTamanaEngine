#include "MipChain.h"

#include <algorithm>
#include <cstring>

namespace gte {

MipChainRgba8 BuildMipChainRgba8(const std::uint8_t* rgba, std::uint32_t width, std::uint32_t height)
{
    MipChainRgba8 chain;
    if (rgba == nullptr || width == 0 || height == 0) {
        return chain;
    }

    std::uint64_t total = 0;
    for (std::uint32_t w = width, h = height;; w = std::max(w / 2, 1u), h = std::max(h / 2, 1u)) {
        chain.levelWidths.push_back(w);
        chain.levelHeights.push_back(h);
        chain.levelByteOffsets.push_back(total);
        total += static_cast<std::uint64_t>(w) * h * 4;
        if (w == 1 && h == 1) {
            break;
        }
    }
    chain.levelCount = static_cast<std::uint32_t>(chain.levelWidths.size());
    chain.pixels.resize(total);
    std::memcpy(chain.pixels.data(), rgba, static_cast<std::size_t>(width) * height * 4);

    for (std::uint32_t level = 1; level < chain.levelCount; ++level) {
        const std::uint8_t* src = chain.pixels.data() + chain.levelByteOffsets[level - 1];
        std::uint8_t* dst = chain.pixels.data() + chain.levelByteOffsets[level];
        const std::uint32_t sw = chain.levelWidths[level - 1];
        const std::uint32_t sh = chain.levelHeights[level - 1];
        const std::uint32_t dw = chain.levelWidths[level];
        const std::uint32_t dh = chain.levelHeights[level];
        for (std::uint32_t y = 0; y < dh; ++y) {
            const std::uint32_t y0 = std::min(2 * y, sh - 1);
            const std::uint32_t y1 = std::min(2 * y + 1, sh - 1);
            for (std::uint32_t x = 0; x < dw; ++x) {
                const std::uint32_t x0 = std::min(2 * x, sw - 1);
                const std::uint32_t x1 = std::min(2 * x + 1, sw - 1);
                for (std::uint32_t c = 0; c < 4; ++c) {
                    const std::uint32_t sum = src[(y0 * sw + x0) * 4 + c] + src[(y0 * sw + x1) * 4 + c]
                        + src[(y1 * sw + x0) * 4 + c] + src[(y1 * sw + x1) * 4 + c];
                    dst[(y * dw + x) * 4 + c] = static_cast<std::uint8_t>((sum + 2) / 4);
                }
            }
        }
    }
    return chain;
}

} // namespace gte
