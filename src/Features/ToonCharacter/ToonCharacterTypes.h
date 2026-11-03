#pragma once

#include <cstdint>

namespace gte {

// Name the feature registers under.
inline constexpr const char* kToonCharacterFeatureName = "ToonCharacter";

// Debug views of the toon shader: 0 off, 1 class colors, 2 band mask, 3 shadow amount, 4 outline only, 5 albedo only.
inline constexpr std::int32_t kToonDebugViewCount = 6;

// Tunables. Stored only through SanitizeToonCharacterSettings().
struct ToonCharacterSettings {
    bool enabled = true;

    // Light bands: half-Lambert thresholds and how much the sun shadow pushes a pixel toward shade.
    float bandThreshold = 0.50f; // 0.05..0.95
    float bandSoftness = 0.02f;  // 0.001..0.25
    float deepThreshold = 0.22f; // 0..bandThreshold
    float shadowStrength = 0.60f; // 0..1
    float shadeBlend = 1.0f;     // 0..1, strength of the coloured shade tint

    // Rim light, hair ring and glow.
    float rimStrength = 0.35f;    // 0..2
    float rimWidth = 0.60f;       // 0..1
    float hairHighlight = 0.8f;   // 0..4
    float emissiveBoost = 3.0f;   // 0..16
    bool gemHeuristic = true;     // Bright cyan cloth glows like a gem.
    bool goldGlint = true;        // Gold trim gets a small specular glint.

    float faceFlatten = 0.8f;     // 0..1, normal blend toward the head forward axis

    // Outline: width in pixels at 720 p height, scaled by the view height; darkness scales the albedo.
    float outlineWidthPx = 1.6f;  // 0..8
    float outlineDarkness = 0.35f; // 0..1

    std::int32_t debugView = 0;   // 0..kToonDebugViewCount - 1
};

enum class ToonCharacterFrameStatus : std::uint8_t {
    NotRendered, Ok, FeatureDisabled, NoToonParts, PassDisabled, CaptureArmed, DrawRefused
};

// What the endpoint reads, one per view scope. Counts are for the frame in `frameIndex`.
struct ToonCharacterViewSnapshot {
    ToonCharacterFrameStatus status = ToonCharacterFrameStatus::NotRendered;
    std::uint64_t frameIndex = 0;
    std::uint32_t candidatePartCount = 0; // Mesh parts under a toon root.
    std::uint32_t claimedEntityCount = 0; // Parts the surface pass took over from the main draw.
    std::uint32_t drawnPartCount = 0;     // Parts the surface pass drew.
};

// Shade and tint classes. Ids match the material class ids in the mesh catalog and the toon shaders.
enum class ToonShadeClass : std::uint32_t {
    Cloth = 0, Skin = 1, FaceSkin = 2, FaceLine = 3, Eye = 4, Hair = 5, HairOverlay = 6, Gem = 7, ClothBack = 8
};
inline constexpr std::uint32_t kToonShadeClassCount = 9;

} // namespace gte
