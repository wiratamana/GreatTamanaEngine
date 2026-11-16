#pragma once

// Global water settings: waves, colours, performance toggles. Flat struct, easy to hash and save.

#include "WaterTypes.h"

#include <cstdint>

namespace gte::water {

struct WaterSettings {
    float seaLevel = kDefaultSeaLevel;
    float waveHeight = 1.2f;     // Metres, crest to mean.
    float choppiness = 0.6f;     // 0 = round sine waves, 1 = sharp crests.
    float waveSpeed = 1.0f;
    float windDirectionDeg = 30.0f;
    float waveLength = 40.0f;    // Longest wave, metres.
    float shallowColor[3] = { 0.10f, 0.55f, 0.60f };
    float deepColor[3] = { 0.02f, 0.12f, 0.25f };
    float foamColor[3] = { 0.95f, 0.97f, 1.0f };
    float clarity = 8.0f;        // Metres of water until the bottom is hidden.
    float foamAmount = 0.7f;
    float wetBand = 1.5f;        // Height above the water the shore stays wet, metres.
    float wetDarken = 0.35f;     // How much the wet land gets darker, 0..1.
    bool ssrEnabled = false;
    std::int32_t waveCount = 4;  // 1..8 Gerstner waves.
    WaterQuality quality = WaterQuality::Medium;
    bool carveTerrain = true;    // Master switch: bodies reshape the terrain.
    bool hideOceanQuad = true;   // Hide the old flat ocean quad while water is active.
    bool cullDeepSeabed = true;  // Do not draw terrain nodes far below the ocean (hidden by deep water).
    float seabedCullDepth = 60.0f; // Metres below the ocean where a node counts as hidden, 5..500.
    bool halfRes = false;        // Shade the water at half resolution (cheaper, softer).
};

// Clamps every field to a safe range; NaN and infinity become the default.
WaterSettings Sanitize(const WaterSettings& in) noexcept;

// FNV-1a over the raw bits of every field.
std::uint64_t Hash(const WaterSettings& s) noexcept;

bool operator==(const WaterSettings& a, const WaterSettings& b) noexcept;

// Sets the fields that follow the quality level (wave count, SSR).
WaterSettings ApplyQuality(WaterSettings s, WaterQuality quality) noexcept;

} // namespace gte::water
