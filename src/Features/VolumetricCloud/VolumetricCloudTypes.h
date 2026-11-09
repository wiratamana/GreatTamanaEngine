#pragma once

#include "../../Math/Mat4.h"

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

// Off renders the clouds; the other values replace the picture with one noise texture.
enum class VolumetricCloudDebugView : std::uint8_t { Off, ShapeSlice, DetailSlice, WeatherMap, BlueNoise };

enum class VolumetricCloudFrameStatus : std::uint8_t {
    NotRendered,
    Ok,
    Disabled,
    AssetMissing,
    ShadersNotReady,
    TargetFailed,
    PassDisabled,
    NoValidCamera,
};

// Channel to show: 0 R, 1 G, 2 B, 3 A, 4 RGB.
inline constexpr std::uint32_t kCloudDebugChannelRgb = 4;

// Tunables. Stored only through SanitizeVolumetricCloudSettings(). Lengths are metres, angles degrees.
struct VolumetricCloudSettings {
    bool enabled = false;
    VolumetricCloudDebugView debugView = VolumetricCloudDebugView::Off;
    std::uint32_t channel = kCloudDebugChannelRgb; // 0..4.
    float slice = 0.5f;                            // Depth of the shown slice, 0..1.
    float tiling = 2.0f;                           // How often the shown texture repeats over the screen.

    // Layer shell. The planet is centred below the camera, like the sky model.
    float planetRadiusKm = 6371.0f;
    float layerBaseM = 1500.0f;
    float layerTopM = 4000.0f;
    float maxDistanceM = 60000.0f;
    float fadeStart = 0.5f; // Fraction of the max distance where the clouds start to thin out.

    // Density model.
    float coverage = 0.65f;
    float density = 1.0f;
    float extinction = 0.03f; // Per metre at density 1.
    float weatherScaleM = 12000.0f;
    float shapeScaleM = 5000.0f;
    float detailScaleM = 700.0f;
    float detailStrength = 0.35f;
    float detailFadeDistM = 10000.0f;
    float windSpeed = 4.0f;
    float windDirection = 45.0f;
    float peakMin = 0.3f;
    float baseRoundness = 0.07f;
    float topRoundness = 0.12f;
    float densityRamp = 0.2f;
    float densityTopFade = 0.9f;

    // March.
    float stepBaseM = 40.0f;
    float stepGrowth = 0.025f;
    float coarseMultiplier = 3.0f;
    float maxSteps = 128.0f;
    float emptyToCoarse = 3.0f;
    float clearSkip = 1.0f; // Stride multiplier over weather that holds no cloud; above 1 can miss small clouds.
    float minTransmittance = 0.05f;
    float jitter = 1.0f;

    // Placeholder light, replaced by the lighting stage.
    float sunIntensity = 0.6f;
    float ambientIntensity = 0.35f;
};

// One tunable float: its endpoint key, field and range.
struct VolumetricCloudFloatSetting {
    const char* name;
    float VolumetricCloudSettings::* member;
    float low;
    float high;
};

inline constexpr std::array kVolumetricCloudFloatSettings{
    VolumetricCloudFloatSetting{ "slice", &VolumetricCloudSettings::slice, 0.0f, 1.0f },
    VolumetricCloudFloatSetting{ "tiling", &VolumetricCloudSettings::tiling, 0.25f, 16.0f },
    VolumetricCloudFloatSetting{ "planetRadiusKm", &VolumetricCloudSettings::planetRadiusKm, 1000.0f, 20000.0f },
    VolumetricCloudFloatSetting{ "layerBaseM", &VolumetricCloudSettings::layerBaseM, 0.0f, 20000.0f },
    VolumetricCloudFloatSetting{ "layerTopM", &VolumetricCloudSettings::layerTopM, 10.0f, 30000.0f },
    VolumetricCloudFloatSetting{ "maxDistanceM", &VolumetricCloudSettings::maxDistanceM, 1000.0f, 500000.0f },
    VolumetricCloudFloatSetting{ "fadeStart", &VolumetricCloudSettings::fadeStart, 0.0f, 0.99f },
    VolumetricCloudFloatSetting{ "coverage", &VolumetricCloudSettings::coverage, 0.0f, 1.0f },
    VolumetricCloudFloatSetting{ "density", &VolumetricCloudSettings::density, 0.0f, 4.0f },
    VolumetricCloudFloatSetting{ "extinction", &VolumetricCloudSettings::extinction, 0.0005f, 0.5f },
    VolumetricCloudFloatSetting{ "weatherScaleM", &VolumetricCloudSettings::weatherScaleM, 1000.0f, 500000.0f },
    VolumetricCloudFloatSetting{ "shapeScaleM", &VolumetricCloudSettings::shapeScaleM, 100.0f, 100000.0f },
    VolumetricCloudFloatSetting{ "detailScaleM", &VolumetricCloudSettings::detailScaleM, 10.0f, 20000.0f },
    VolumetricCloudFloatSetting{ "detailStrength", &VolumetricCloudSettings::detailStrength, 0.0f, 1.0f },
    VolumetricCloudFloatSetting{ "detailFadeDistM", &VolumetricCloudSettings::detailFadeDistM, 100.0f, 500000.0f },
    VolumetricCloudFloatSetting{ "windSpeed", &VolumetricCloudSettings::windSpeed, 0.0f, 500.0f },
    VolumetricCloudFloatSetting{ "windDirection", &VolumetricCloudSettings::windDirection, -360.0f, 360.0f },
    VolumetricCloudFloatSetting{ "peakMin", &VolumetricCloudSettings::peakMin, 0.05f, 1.0f },
    VolumetricCloudFloatSetting{ "baseRoundness", &VolumetricCloudSettings::baseRoundness, 0.01f, 0.5f },
    VolumetricCloudFloatSetting{ "topRoundness", &VolumetricCloudSettings::topRoundness, 0.0f, 0.5f },
    VolumetricCloudFloatSetting{ "densityRamp", &VolumetricCloudSettings::densityRamp, 0.01f, 0.5f },
    VolumetricCloudFloatSetting{ "densityTopFade", &VolumetricCloudSettings::densityTopFade, 0.5f, 0.99f },
    VolumetricCloudFloatSetting{ "stepBaseM", &VolumetricCloudSettings::stepBaseM, 1.0f, 2000.0f },
    VolumetricCloudFloatSetting{ "stepGrowth", &VolumetricCloudSettings::stepGrowth, 0.0f, 0.5f },
    VolumetricCloudFloatSetting{ "coarseMultiplier", &VolumetricCloudSettings::coarseMultiplier, 1.0f, 16.0f },
    VolumetricCloudFloatSetting{ "maxSteps", &VolumetricCloudSettings::maxSteps, 8.0f, 512.0f },
    VolumetricCloudFloatSetting{ "emptyToCoarse", &VolumetricCloudSettings::emptyToCoarse, 1.0f, 16.0f },
    VolumetricCloudFloatSetting{ "clearSkip", &VolumetricCloudSettings::clearSkip, 1.0f, 32.0f },
    VolumetricCloudFloatSetting{ "minTransmittance", &VolumetricCloudSettings::minTransmittance, 0.0f, 0.5f },
    VolumetricCloudFloatSetting{ "jitter", &VolumetricCloudSettings::jitter, 0.0f, 1.0f },
    VolumetricCloudFloatSetting{ "sunIntensity", &VolumetricCloudSettings::sunIntensity, 0.0f, 100.0f },
    VolumetricCloudFloatSetting{ "ambientIntensity", &VolumetricCloudSettings::ambientIntensity, 0.0f, 100.0f },
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

// std430 block of VolumetricCloud.comp; member order matches the shader.
struct VolumetricCloudParamsGpu {
    Mat4 invViewProjection;
    float eyeAndPlanetRadius[4] = {};   // xyz eye (world), w planet radius (m).
    float sunDirectionAndIntensity[4] = {}; // xyz toward the sun, w sun intensity factor.
    float sunColorAndAmbient[4] = {};   // rgb sun radiance, w ambient intensity factor.
    float ambientColor[4] = {};         // rgb ambient radiance, w unused.
    float layer[4] = {};                // x base (m), y top (m), z max distance (m), w fade start fraction.
    float weather[4] = {};              // x weather tile (m), yz wind offset (m), w coverage.
    float noise[4] = {};                // x shape tile (m), y detail tile (m), z detail strength, w detail fade (m).
    float density[4] = {};              // x density factor, y extinction (1/m), z peak min, w jitter strength.
    float heightShape[4] = {};          // x base roundness, y top roundness, z density ramp, w density top fade.
    float march[4] = {};                // x base step (m), y step growth, z coarse multiplier, w min transmittance.
    std::uint32_t counts[4] = {};       // x max steps, y frame index, z empty samples to coarse, w unused.
    float skip[4] = {};                 // x clear-weather stride multiplier.
};
static_assert(sizeof(VolumetricCloudParamsGpu) == 64 + 12 * 16);
static_assert(std::is_trivially_copyable_v<VolumetricCloudParamsGpu>);

// Clamps every field into its documented range; a non-finite number falls back to the default.
VolumetricCloudSettings SanitizeVolumetricCloudSettings(const VolumetricCloudSettings& candidate) noexcept;

VolumetricCloudSlicePush MakeVolumetricCloudSlicePush(const VolumetricCloudSettings& settings) noexcept;

} // namespace gte
