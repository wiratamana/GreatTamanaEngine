#include "TerrainMaterial.h"

#include <algorithm>
#include <cmath>

namespace gte::terrain {

namespace {

constexpr float kHalfPi = 1.5707963267948966f;
constexpr float kDegToRad = 0.017453292519943295f;

inline float Saturate(float v) noexcept { return std::clamp(v, 0.0f, 1.0f); }

inline float SmoothStep(float e0, float e1, float v) noexcept
{
    if (e1 <= e0) {
        return v >= e1 ? 1.0f : 0.0f;
    }
    const float t = Saturate((v - e0) / (e1 - e0));
    return t * t * (3.0f - 2.0f * t);
}

inline Rgb Mix(const Rgb& a, const Rgb& b, float t) noexcept
{
    return { a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t };
}

constexpr Rgb kSeabed{ 0.30f, 0.29f, 0.24f };
constexpr Rgb kSand{ 0.72f, 0.66f, 0.46f };
constexpr Rgb kGrassLow{ 0.17f, 0.29f, 0.10f };
constexpr Rgb kGrassHigh{ 0.30f, 0.31f, 0.16f };
constexpr Rgb kRock{ 0.40f, 0.36f, 0.32f };
constexpr Rgb kSnow{ 0.94f, 0.95f, 0.97f };

} // namespace

float Height01(float h, const TerrainSettings& s) noexcept
{
    const float lo = MinHeight(s);
    return Saturate((h - lo) / (MaxHeight(s) - lo));
}

float HeightFromHeight01(float t, const TerrainSettings& s) noexcept
{
    const float lo = MinHeight(s);
    return lo + t * (MaxHeight(s) - lo);
}

float Slope01(float slopeRadians) noexcept { return Saturate(slopeRadians / kHalfPi); }

float SlopeFromSlope01(float slope01) noexcept { return slope01 * kHalfPi; }

Rgb SurfaceColor(float h, float slope, const TerrainSettings& s) noexcept
{
    const float slopeDeg = slope / kDegToRad;
    const float heightFrac = Saturate(h / std::max(1.0f, s.heightScale));

    const Rgb grass = Mix(kGrassLow, kGrassHigh, SmoothStep(0.0f, 0.5f, heightFrac));
    const float sandW = 1.0f - SmoothStep(s.sandMaxHeight * 0.5f, std::max(s.sandMaxHeight, 0.01f), h);
    const float seabedW = SmoothStep(0.0f, 6.0f, -h);
    Rgb flat = Mix(grass, kSand, sandW);
    flat = Mix(flat, kSeabed, seabedW);

    const float rockW = SmoothStep(s.grassMaxSlopeDeg, s.rockMinSlopeDeg, slopeDeg);
    Rgb base = Mix(flat, kRock, rockW);

    const float line = s.snowMinHeightFraction * s.heightScale;
    const float band = 0.06f * s.heightScale;
    const float snowH = SmoothStep(line - band, line + band, h);
    const float snowSlope = 1.0f - SmoothStep(s.snowSlopeLimitDeg - 8.0f, s.snowSlopeLimitDeg, slopeDeg);
    base = Mix(base, kSnow, snowH * snowSlope);
    return base;
}

std::uint32_t PackRgba8(const Rgb& c) noexcept
{
    const auto q = [](float v) { return static_cast<std::uint32_t>(std::lround(Saturate(v) * 255.0f)); };
    return q(c.r) | (q(c.g) << 8) | (q(c.b) << 16) | 0xFF000000u;
}

std::vector<std::uint8_t> BuildRampImage(const TerrainSettings& s, int size)
{
    size = std::clamp(size, 2, 2048);
    std::vector<std::uint8_t> image(static_cast<std::size_t>(size) * static_cast<std::size_t>(size) * 4u);
    for (int y = 0; y < size; ++y) {
        const float slope = SlopeFromSlope01(static_cast<float>(y) / static_cast<float>(size - 1));
        for (int x = 0; x < size; ++x) {
            const float h = HeightFromHeight01(static_cast<float>(x) / static_cast<float>(size - 1), s);
            const std::uint32_t packed = PackRgba8(SurfaceColor(h, slope, s));
            std::uint8_t* px = &image[(static_cast<std::size_t>(y) * static_cast<std::size_t>(size) + static_cast<std::size_t>(x)) * 4u];
            px[0] = static_cast<std::uint8_t>(packed & 0xFFu);
            px[1] = static_cast<std::uint8_t>((packed >> 8) & 0xFFu);
            px[2] = static_cast<std::uint8_t>((packed >> 16) & 0xFFu);
            px[3] = 255;
        }
    }
    return image;
}

} // namespace gte::terrain
