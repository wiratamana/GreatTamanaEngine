#include "ToonCharacterMath.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace gte {

namespace {

float SanitizeFloat(float value, float lo, float hi, float fallback) noexcept
{
    return std::isfinite(value) ? std::clamp(value, lo, hi) : fallback;
}

constexpr ToonRgb kWhite{ 1.0f, 1.0f, 1.0f };

constexpr ToonClassTint kClothTint{ { 0.42f, 0.48f, 0.82f }, { 0.80f, 0.82f, 0.92f }, { 0.30f, 0.36f, 0.62f } };
constexpr ToonClassTint kHairTint{ { 0.62f, 0.70f, 0.93f }, { 0.85f, 0.88f, 0.95f }, { 0.45f, 0.55f, 0.78f } };
constexpr ToonClassTint kSkinTint{ { 0.92f, 0.74f, 0.72f }, kWhite, { 0.55f, 0.36f, 0.34f } };
constexpr ToonClassTint kFaceSkinTint{ { 0.97f, 0.86f, 0.84f }, kWhite, { 0.55f, 0.36f, 0.34f } };
constexpr ToonClassTint kNeutralTint{ kWhite, kWhite, { 0.30f, 0.36f, 0.62f } };

// Indexed by ToonShadeClass.
constexpr std::array<ToonClassTint, kToonShadeClassCount> kTints{
    kClothTint,   // Cloth
    kSkinTint,    // Skin
    kFaceSkinTint, // FaceSkin
    kNeutralTint, // FaceLine
    kNeutralTint, // Eye
    kHairTint,    // Hair
    kNeutralTint, // HairOverlay
    kNeutralTint, // Gem
    kClothTint,   // ClothBack
};

} // namespace

ToonCharacterSettings SanitizeToonCharacterSettings(const ToonCharacterSettings& in) noexcept
{
    const ToonCharacterSettings d{};
    ToonCharacterSettings s = in;
    s.bandThreshold = SanitizeFloat(in.bandThreshold, 0.05f, 0.95f, d.bandThreshold);
    s.bandSoftness = SanitizeFloat(in.bandSoftness, 0.001f, 0.25f, d.bandSoftness);
    s.deepThreshold = std::min(SanitizeFloat(in.deepThreshold, 0.0f, 0.95f, d.deepThreshold), s.bandThreshold);
    s.shadowStrength = SanitizeFloat(in.shadowStrength, 0.0f, 1.0f, d.shadowStrength);
    s.shadeBlend = SanitizeFloat(in.shadeBlend, 0.0f, 1.0f, d.shadeBlend);
    s.rimStrength = SanitizeFloat(in.rimStrength, 0.0f, 2.0f, d.rimStrength);
    s.rimWidth = SanitizeFloat(in.rimWidth, 0.0f, 1.0f, d.rimWidth);
    s.hairHighlight = SanitizeFloat(in.hairHighlight, 0.0f, 4.0f, d.hairHighlight);
    s.emissiveBoost = SanitizeFloat(in.emissiveBoost, 0.0f, 16.0f, d.emissiveBoost);
    s.faceFlatten = SanitizeFloat(in.faceFlatten, 0.0f, 1.0f, d.faceFlatten);
    s.outlineWidthPx = SanitizeFloat(in.outlineWidthPx, 0.0f, 8.0f, d.outlineWidthPx);
    s.outlineDarkness = SanitizeFloat(in.outlineDarkness, 0.0f, 1.0f, d.outlineDarkness);
    s.debugView = std::clamp(in.debugView, 0, kToonDebugViewCount - 1);
    return s;
}

ToonLook PackToonLook(const ToonCharacterSettings& s, Extent2D viewExtent) noexcept
{
    const bool hasPixels = viewExtent.width > 0 && viewExtent.height > 0;
    const float height = hasPixels ? static_cast<float>(viewExtent.height) : kToonOutlineReferenceHeightPx;
    const float aspect = hasPixels ? static_cast<float>(viewExtent.width) / height : 1.0f;
    const float widthPx = s.outlineWidthPx * height / kToonOutlineReferenceHeightPx;

    ToonLook look;
    look.v0[0] = s.bandThreshold;
    look.v0[1] = s.bandSoftness;
    look.v0[2] = s.deepThreshold;
    look.v0[3] = s.shadowStrength;
    look.v1[0] = s.rimStrength;
    look.v1[1] = s.rimWidth;
    look.v1[2] = s.hairHighlight;
    look.v1[3] = s.emissiveBoost;
    look.v2[0] = s.faceFlatten;
    look.v2[1] = widthPx * 2.0f / height;
    look.v2[2] = s.outlineDarkness;
    look.v2[3] = static_cast<float>(s.debugView);
    look.v3[0] = s.shadeBlend;
    look.v3[1] = s.gemHeuristic ? 1.0f : 0.0f;
    look.v3[2] = s.goldGlint ? 1.0f : 0.0f;
    look.v3[3] = aspect;
    return look;
}

const ToonClassTint& ToonTintOf(ToonShadeClass shadeClass) noexcept
{
    const std::uint32_t id = static_cast<std::uint32_t>(shadeClass);
    return id < kToonShadeClassCount ? kTints[id] : kClothTint;
}

bool ToonClassIsUnlit(ToonShadeClass shadeClass) noexcept
{
    return shadeClass == ToonShadeClass::FaceLine || shadeClass == ToonShadeClass::Eye;
}

bool ToonClassHasDeepBand(ToonShadeClass shadeClass) noexcept
{
    return shadeClass == ToonShadeClass::Cloth || shadeClass == ToonShadeClass::ClothBack
        || shadeClass == ToonShadeClass::Hair;
}

bool ToonClassDrawsOutline(ToonShadeClass shadeClass) noexcept
{
    switch (shadeClass) {
    case ToonShadeClass::Eye:
    case ToonShadeClass::FaceLine:
    case ToonShadeClass::HairOverlay:
    case ToonShadeClass::ClothBack:
        return false;
    default:
        return true;
    }
}

float ToonLuminance(const ToonRgb& c) noexcept
{
    return 0.2126f * c.r + 0.7152f * c.g + 0.0722f * c.b;
}

ToonRgb ToonShadeTone(ToonShadeClass shadeClass, bool deepBand) noexcept
{
    const ToonClassTint& tint = ToonTintOf(shadeClass);
    ToonRgb tone = tint.shade;
    if (deepBand && ToonClassHasDeepBand(shadeClass)) {
        tone = ToonRgb{ tone.r * tint.deep.r, tone.g * tint.deep.g, tone.b * tint.deep.b };
    }
    // Lift toward white until the luminance floor holds; channels stay at or below 1.
    const float luminance = ToonLuminance(tone);
    if (luminance < kToonMinShadeLuminance) {
        const float t = (kToonMinShadeLuminance - luminance) / (1.0f - luminance);
        tone = ToonRgb{ tone.r + (1.0f - tone.r) * t, tone.g + (1.0f - tone.g) * t, tone.b + (1.0f - tone.b) * t };
    }
    return tone;
}

} // namespace gte
