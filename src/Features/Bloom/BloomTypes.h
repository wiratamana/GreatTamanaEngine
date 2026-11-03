#pragma once

#include <cstdint>
#include <type_traits>

namespace gte {

// Name the feature registers under.
inline constexpr const char* kBloomFeatureName = "Bloom";

inline constexpr std::uint32_t kBloomMinLevels = 2;
inline constexpr std::uint32_t kBloomMaxLevels = 6;
inline constexpr std::uint32_t kBloomMinMipSize = 2;  // Smallest side the last mip may have.
inline constexpr float kBloomMaxRadiance = 64.0f;     // Input clamp, so a sun disc cannot flood the frame.

// Tunables. Stored only through SanitizeBloomSettings().
struct BloomSettings {
    bool enabled = true;
    float threshold = 1.0f;   // Linear radiance where glow starts (1 = white), 0..16.
    float knee = 0.5f;        // Soft transition width as a fraction of the threshold, 0..1.
    float intensity = 0.30f;  // Linear multiplier on the glow added to the picture, 0..4.
    std::uint32_t levels = 5; // Mip chain length, kBloomMinLevels..kBloomMaxLevels.
};

enum class BloomFrameStatus : std::uint8_t { NotRendered, Ok, Disabled, PassDisabled, ShadersNotReady, TargetFailed, ExtentTooSmall };

// What the endpoint reads, one per view scope.
struct BloomViewSnapshot {
    BloomFrameStatus status = BloomFrameStatus::NotRendered;
    std::uint64_t frameIndex = 0;
    std::uint32_t activeLevels = 0; // Levels the last rendered frame used.
};

// Push block of BloomPrefilter.comp; member names match the shader.
struct BloomPrefilterPush {
    float thresholdKneeMax[4] = {}; // x threshold, y absolute knee, z max radiance, w reserved.
};
static_assert(sizeof(BloomPrefilterPush) == 16);
static_assert(std::is_trivially_copyable_v<BloomPrefilterPush>);

// Push block of BloomUp.comp; member names match the shader.
struct BloomUpPush {
    float scaleAndPad[4] = {}; // x output scale, yzw reserved.
};
static_assert(sizeof(BloomUpPush) == 16);
static_assert(std::is_trivially_copyable_v<BloomUpPush>);

} // namespace gte
