#pragma once

#include "../../Math/ToneMapCurves.h"

#include <cstdint>
#include <type_traits>

namespace gte {

// Name the feature registers under.
inline constexpr const char* kToneMappingFeatureName = "ToneMapping";

// Tunables. Stored only through SanitizeToneMappingSettings().
struct ToneMappingSettings {
    float exposure = tonemap::kDefaultExposure;     // Linear multiplier on scene color, 0.01..16.
    tonemap::ToneMapMode mode = tonemap::kDefaultMode;
    float saturation = tonemap::kDefaultSaturation; // 0..2, 1 leaves the color unchanged.
    float dither = 1.0f;                            // 8-bit steps of noise before quantization, 0..2.
    float vignette = 0.0f;                          // Corner darkening, 0..1, 0 is off.
    float contrast = 1.0f;                          // Power around mid grey on linear color, 0.5..2, 1 is off.
    float splitTone = 0.0f;                         // Cool shadows and warm highlights, 0..1, 0 is off.
    float grain = 0.0f;                             // Film grain amplitude, 0..1, 0 is off.
};

enum class ToneMappingFrameStatus : std::uint8_t { NotRendered, Ok, ShadersNotReady, TargetFailed, PassDisabled };

// What the endpoint reads, one per view scope.
struct ToneMappingViewSnapshot {
    ToneMappingFrameStatus status = ToneMappingFrameStatus::NotRendered;
    std::uint64_t frameIndex = 0;
};

// Push block of ToneMapping.comp; member names match the shader.
struct ToneMappingPush {
    float exposureModeSaturationDither[4] = {}; // x exposure, y mode, z saturation, w dither.
    std::uint32_t frame[4] = {};                // x frame index.
    float vignetteContrastSplitGrain[4] = {};   // x vignette, y contrast, z split tone, w grain.
};
static_assert(sizeof(ToneMappingPush) == 48);
static_assert(std::is_trivially_copyable_v<ToneMappingPush>);

} // namespace gte
