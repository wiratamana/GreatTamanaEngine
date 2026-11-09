#pragma once

#include <array>
#include <cstdint>
#include <type_traits>

namespace gte {

// Name the feature registers under.
inline constexpr const char* kVolumetricCloudFeatureName = "VolumetricCloud";

// Baked noise files, relative to the "assets/" folder next to the working directory.
inline constexpr const char* kCloudShapeAsset = "clouds/cloud_shape_128.gtvol";
inline constexpr const char* kCloudDetailAsset = "clouds/cloud_detail_32.gtvol";
inline constexpr const char* kCloudWeatherAsset = "clouds/cloud_weather_512.gtvol";
inline constexpr const char* kCloudBlueNoiseAsset = "clouds/blue_noise_128.gtvol";

// What the debug overlay shows. Off draws nothing.
enum class VolumetricCloudDebugView : std::uint8_t { Off, ShapeSlice, DetailSlice, WeatherMap, BlueNoise };

enum class VolumetricCloudFrameStatus : std::uint8_t {
    NotRendered,
    Off,
    Ok,
    Disabled,
    AssetMissing,
    ShadersNotReady,
    TargetFailed,
    PassDisabled,
};

// Channel to show: 0 R, 1 G, 2 B, 3 A, 4 RGB.
inline constexpr std::uint32_t kCloudDebugChannelRgb = 4;

// Tunables. Stored only through SanitizeVolumetricCloudSettings().
struct VolumetricCloudSettings {
    bool enabled = true;
    VolumetricCloudDebugView debugView = VolumetricCloudDebugView::Off;
    float slice = 0.5f;                            // Depth of the shown slice, 0..1.
    std::uint32_t channel = kCloudDebugChannelRgb; // 0..4.
    float tiling = 2.0f;                           // How often the texture repeats over the screen, 0.25..16.
};

struct VolumetricCloudViewSnapshot {
    VolumetricCloudFrameStatus status = VolumetricCloudFrameStatus::NotRendered;
    std::uint64_t frameIndex = 0;
};

// Push constants of the slice viewer shaders (16 bytes).
struct VolumetricCloudSlicePush {
    float slice = 0.0f;
    float tiling = 1.0f;
    std::uint32_t channel = 0;
    std::uint32_t reserved = 0;
};
static_assert(sizeof(VolumetricCloudSlicePush) == 16);
static_assert(std::is_trivially_copyable_v<VolumetricCloudSlicePush>);

// Clamps every field into its documented range; a non-finite number falls back to the default.
VolumetricCloudSettings SanitizeVolumetricCloudSettings(const VolumetricCloudSettings& candidate) noexcept;

VolumetricCloudSlicePush MakeVolumetricCloudSlicePush(const VolumetricCloudSettings& settings) noexcept;

} // namespace gte
