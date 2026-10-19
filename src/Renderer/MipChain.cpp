#include "MipChain.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>

namespace gte {

namespace {

constexpr int kEncodeSteps = 4096;

// sRGB byte <-> linear lookup tables. Encode is sampled at kEncodeSteps linear values.
struct SrgbTables {
    std::array<float, 256> decode{};
    std::array<std::uint8_t, kEncodeSteps> encode{};

    SrgbTables()
    {
        for (int i = 0; i < 256; ++i) {
            const double s = i / 255.0;
            decode[i] = static_cast<float>(s <= 0.04045 ? s / 12.92 : std::pow((s + 0.055) / 1.055, 2.4));
        }
        for (int i = 0; i < kEncodeSteps; ++i) {
            const double l = i / static_cast<double>(kEncodeSteps - 1);
            const double s = l <= 0.0031308 ? l * 12.92 : 1.055 * std::pow(l, 1.0 / 2.4) - 0.055;
            encode[i] = static_cast<std::uint8_t>(std::clamp(s * 255.0 + 0.5, 0.0, 255.0));
        }
    }
};

const SrgbTables& Tables()
{
    static const SrgbTables tables;
    return tables;
}

} // namespace

MipChainRgba8 BuildMipChainRgba8(const std::uint8_t* rgba, std::uint32_t width, std::uint32_t height,
    std::uint32_t maxLevels, MipColorSpace colorSpace)
{
    MipChainRgba8 chain;
    if (rgba == nullptr || width == 0 || height == 0 || maxLevels == 0) {
        return chain;
    }

    std::uint64_t total = 0;
    for (std::uint32_t w = width, h = height;; w = std::max(w / 2, 1u), h = std::max(h / 2, 1u)) {
        chain.levelWidths.push_back(w);
        chain.levelHeights.push_back(h);
        chain.levelByteOffsets.push_back(total);
        total += static_cast<std::uint64_t>(w) * h * 4;
        if ((w == 1 && h == 1) || chain.levelWidths.size() >= maxLevels) {
            break;
        }
    }
    chain.levelCount = static_cast<std::uint32_t>(chain.levelWidths.size());
    chain.pixels.resize(total);
    std::memcpy(chain.pixels.data(), rgba, static_cast<std::size_t>(width) * height * 4);

    const bool srgb = colorSpace == MipColorSpace::Srgb;
    const SrgbTables& tables = Tables();

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
                const std::uint8_t* t00 = src + (y0 * sw + x0) * 4;
                const std::uint8_t* t01 = src + (y0 * sw + x1) * 4;
                const std::uint8_t* t10 = src + (y1 * sw + x0) * 4;
                const std::uint8_t* t11 = src + (y1 * sw + x1) * 4;
                std::uint8_t* out = dst + (y * dw + x) * 4;
                for (std::uint32_t c = 0; c < 4; ++c) {
                    if (srgb && c < 3) {
                        const float average
                            = (tables.decode[t00[c]] + tables.decode[t01[c]] + tables.decode[t10[c]] + tables.decode[t11[c]])
                            * 0.25f;
                        out[c] = tables.encode[static_cast<int>(average * (kEncodeSteps - 1) + 0.5f)];
                    } else {
                        const std::uint32_t sum = t00[c] + t01[c] + t10[c] + t11[c];
                        out[c] = static_cast<std::uint8_t>((sum + 2) / 4);
                    }
                }
            }
        }
    }
    return chain;
}

} // namespace gte
