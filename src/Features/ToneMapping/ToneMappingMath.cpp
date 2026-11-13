#include "ToneMappingMath.h"

#include <algorithm>
#include <cmath>

namespace gte {

namespace {

float SanitizeFloat(float value, float lo, float hi, float fallback) noexcept
{
    return std::isfinite(value) ? std::clamp(value, lo, hi) : fallback;
}

} // namespace

ToneMappingSettings SanitizeToneMappingSettings(const ToneMappingSettings& settings) noexcept
{
    const ToneMappingSettings d{};
    ToneMappingSettings s = settings;
    s.exposure = SanitizeFloat(settings.exposure, 0.01f, 16.0f, d.exposure);
    s.saturation = SanitizeFloat(settings.saturation, 0.0f, 2.0f, d.saturation);
    s.dither = SanitizeFloat(settings.dither, 0.0f, 2.0f, d.dither);
    s.vignette = SanitizeFloat(settings.vignette, 0.0f, 1.0f, d.vignette);
    s.contrast = SanitizeFloat(settings.contrast, 0.5f, 2.0f, d.contrast);
    s.splitTone = SanitizeFloat(settings.splitTone, 0.0f, 1.0f, d.splitTone);
    s.grain = SanitizeFloat(settings.grain, 0.0f, 1.0f, d.grain);
    if (static_cast<std::uint8_t>(settings.mode) > static_cast<std::uint8_t>(tonemap::ToneMapMode::PbrNeutral)) {
        s.mode = d.mode;
    }
    return s;
}

ToneMappingPush MakeToneMappingPush(const ToneMappingSettings& safe, std::uint64_t frameIndex) noexcept
{
    ToneMappingPush p;
    p.exposureModeSaturationDither[0] = safe.exposure;
    p.exposureModeSaturationDither[1] = static_cast<float>(static_cast<std::uint8_t>(safe.mode));
    p.exposureModeSaturationDither[2] = safe.saturation;
    p.exposureModeSaturationDither[3] = safe.dither;
    p.frame[0] = static_cast<std::uint32_t>(frameIndex);
    p.vignetteContrastSplitGrain[0] = safe.vignette;
    p.vignetteContrastSplitGrain[1] = safe.contrast;
    p.vignetteContrastSplitGrain[2] = safe.splitTone;
    p.vignetteContrastSplitGrain[3] = safe.grain;
    return p;
}

} // namespace gte
