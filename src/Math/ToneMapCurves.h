#pragma once

#include "Vec3.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace gte::tonemap {

// CPU reference of ToneMapping.comp. Only ToneMapLinearToDisplay maps non-finite input to 0.

enum class ToneMapMode : std::uint8_t { Clamp, Aces, PbrNeutral };

inline constexpr float kDefaultExposure = 0.8f;
inline constexpr ToneMapMode kDefaultMode = ToneMapMode::Aces;
inline constexpr float kDefaultSaturation = 1.1f;

// ACES filmic fit (Narkowicz).
inline constexpr float kAcesA = 2.51f;
inline constexpr float kAcesB = 0.03f;
inline constexpr float kAcesC = 2.43f;
inline constexpr float kAcesD = 0.59f;
inline constexpr float kAcesE = 0.14f;

// Khronos PBR Neutral.
inline constexpr float kPbrStartCompression = 0.8f - 0.04f;
inline constexpr float kPbrDesaturation = 0.15f;

// Linear to sRGB-encoded, clamped to 0..1.
inline float SrgbEncode(float linear) noexcept
{
    if (!(linear > 0.0f)) {
        return 0.0f;
    }
    if (linear >= 1.0f) {
        return 1.0f;
    }
    return linear <= 0.0031308f ? linear * 12.92f : 1.055f * std::pow(linear, 1.0f / 2.4f) - 0.055f;
}

// sRGB-encoded to linear, clamped to 0..1.
inline float SrgbDecode(float encoded) noexcept
{
    if (!(encoded > 0.0f)) {
        return 0.0f;
    }
    if (encoded >= 1.0f) {
        return 1.0f;
    }
    return encoded <= 0.04045f ? encoded / 12.92f : std::pow((encoded + 0.055f) / 1.055f, 2.4f);
}

inline float AcesFilm(float x) noexcept
{
    const float mapped = (x * (kAcesA * x + kAcesB)) / (x * (kAcesC * x + kAcesD) + kAcesE);
    return std::clamp(mapped, 0.0f, 1.0f);
}

inline Vec3 AcesFilm(const Vec3& x) noexcept
{
    return { AcesFilm(x.x), AcesFilm(x.y), AcesFilm(x.z) };
}

inline Vec3 PbrNeutral(Vec3 color) noexcept
{
    const float x = std::min(color.x, std::min(color.y, color.z));
    const float offset = x < 0.08f ? x - 6.25f * x * x : 0.04f;
    color -= Vec3{ offset, offset, offset };

    const float peak = std::max(color.x, std::max(color.y, color.z));
    if (peak < kPbrStartCompression) {
        return color;
    }
    const float d = 1.0f - kPbrStartCompression;
    const float newPeak = 1.0f - d * d / (peak + d - kPbrStartCompression);
    color *= newPeak / peak;
    const float g = 1.0f - 1.0f / (kPbrDesaturation * (peak - newPeak) + 1.0f);
    return Lerp(color, Vec3{ newPeak, newPeak, newPeak }, g);
}

// Scales the color away from (above 1) or toward (below 1) its luma. The result is never negative.
inline Vec3 ApplySaturation(const Vec3& c, float saturation) noexcept
{
    const float luma = 0.2126f * c.x + 0.7152f * c.y + 0.0722f * c.z;
    const Vec3 grey{ luma, luma, luma };
    const Vec3 mixed = Lerp(grey, c, saturation);
    return { std::max(mixed.x, 0.0f), std::max(mixed.y, 0.0f), std::max(mixed.z, 0.0f) };
}

// Exposure, saturation, curve, sRGB encode. Returns display values in 0..1.
inline Vec3 ToneMapLinearToDisplay(const Vec3& sceneLinear, float exposure, ToneMapMode mode, float saturation) noexcept
{
    if (!std::isfinite(sceneLinear.x) || !std::isfinite(sceneLinear.y) || !std::isfinite(sceneLinear.z)) {
        return Vec3::Zero();
    }
    Vec3 c = ApplySaturation(sceneLinear * exposure, saturation);
    if (mode == ToneMapMode::PbrNeutral) {
        c = PbrNeutral(c);
    } else if (mode == ToneMapMode::Aces) {
        c = AcesFilm(c);
    }
    return { SrgbEncode(c.x), SrgbEncode(c.y), SrgbEncode(c.z) };
}

} // namespace gte::tonemap
