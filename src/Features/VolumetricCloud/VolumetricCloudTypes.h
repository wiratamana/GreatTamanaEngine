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
    float coverage = 0.65f; // Sky share under cloud: 0.2 a few puffs, 0.65 scattered, 1 overcast.
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
    float maxSteps = 64.0f;
    float emptyToCoarse = 3.0f;
    float clearSkip = 1.0f; // Stride multiplier over weather that holds no cloud; above 1 can miss small clouds.
    float minTransmittance = 0.1f;
    float jitter = 1.0f;

    // Lighting. Intensities scale the sun and sky radiance the engine hands over.
    float sunIntensity = 8.0f;
    float ambientIntensity = 0.28f;
    float sunSteps = 3.0f;          // Samples of the march toward the sun.
    float lightReach = 0.5f;        // Length of that march, as a fraction of the layer thickness.
    float msOctaves = 3.0f;         // Multiple scattering octaves.
    float msAttenuation = 0.5f;     // Per octave factor on the optical depth.
    float msContribution = 0.5f;    // Per octave factor on the light.
    float msEccentricity = 0.5f;    // Per octave factor on the phase eccentricity.
    float phaseModel = 0.0f;        // 0 dual lobe Henyey-Greenstein, 1 Henyey-Greenstein plus Draine.
    float forwardG = 0.8f;
    float backwardG = -0.35f;
    float backwardMix = 0.3f;
    float silverIntensity = 0.2f;   // Weight of the narrow forward lobe that lights thin edges facing the sun.
    float silverG = 0.97f;
    float powder = 0.5f;            // Dark edge effect when looking away from the sun.
    float ambientBottom = 0.12f;    // Sky light left at the layer base, relative to the top.
    float hazeDistanceM = 45000.0f; // Distance at which a cloud has faded to 1/e into the sky behind it.
    float lightDetail = 0.0f;       // Share of the fine erosion the nearest sun sample uses.
    float lightStride = 2.0f;       // Lit samples that share one sun march.

    // Resolution and temporal accumulation.
    float resolutionDivisor = 4.0f; // March at 1/n of the view size per axis; 1, 2 or 4.
    float temporal = 1.0f;          // 1 accumulates over frames; 0 shows the current march only.
    float historyBlend = 0.0f;      // Weight of the new samples in a pixel without an exact sample this frame.
    float freshBlend = 0.2f;        // Same for the pixel that was marched itself.
    float clampExpand = 0.0f;       // Relative growth of the neighbourhood box the history is clamped to; 0 = no clamp.
    float motionBlend = 0.5f;       // Extra weight of the new samples where the picture moves by a pixel or more.
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
    VolumetricCloudFloatSetting{ "sunSteps", &VolumetricCloudSettings::sunSteps, 1.0f, 12.0f },
    VolumetricCloudFloatSetting{ "lightReach", &VolumetricCloudSettings::lightReach, 0.05f, 2.0f },
    VolumetricCloudFloatSetting{ "msOctaves", &VolumetricCloudSettings::msOctaves, 1.0f, 6.0f },
    VolumetricCloudFloatSetting{ "msAttenuation", &VolumetricCloudSettings::msAttenuation, 0.05f, 1.0f },
    VolumetricCloudFloatSetting{ "msContribution", &VolumetricCloudSettings::msContribution, 0.0f, 1.0f },
    VolumetricCloudFloatSetting{ "msEccentricity", &VolumetricCloudSettings::msEccentricity, 0.0f, 1.0f },
    VolumetricCloudFloatSetting{ "phaseModel", &VolumetricCloudSettings::phaseModel, 0.0f, 1.0f },
    VolumetricCloudFloatSetting{ "forwardG", &VolumetricCloudSettings::forwardG, 0.0f, 0.99f },
    VolumetricCloudFloatSetting{ "backwardG", &VolumetricCloudSettings::backwardG, -0.99f, 0.0f },
    VolumetricCloudFloatSetting{ "backwardMix", &VolumetricCloudSettings::backwardMix, 0.0f, 1.0f },
    VolumetricCloudFloatSetting{ "silverIntensity", &VolumetricCloudSettings::silverIntensity, 0.0f, 4.0f },
    VolumetricCloudFloatSetting{ "silverG", &VolumetricCloudSettings::silverG, 0.5f, 0.999f },
    VolumetricCloudFloatSetting{ "powder", &VolumetricCloudSettings::powder, 0.0f, 1.0f },
    VolumetricCloudFloatSetting{ "ambientBottom", &VolumetricCloudSettings::ambientBottom, 0.0f, 1.0f },
    VolumetricCloudFloatSetting{ "hazeDistanceM", &VolumetricCloudSettings::hazeDistanceM, 1000.0f, 10000000.0f },
    VolumetricCloudFloatSetting{ "lightDetail", &VolumetricCloudSettings::lightDetail, 0.0f, 1.0f },
    VolumetricCloudFloatSetting{ "lightStride", &VolumetricCloudSettings::lightStride, 1.0f, 8.0f },
    VolumetricCloudFloatSetting{ "resolutionDivisor", &VolumetricCloudSettings::resolutionDivisor, 1.0f, 4.0f },
    VolumetricCloudFloatSetting{ "temporal", &VolumetricCloudSettings::temporal, 0.0f, 1.0f },
    VolumetricCloudFloatSetting{ "historyBlend", &VolumetricCloudSettings::historyBlend, 0.0f, 1.0f },
    VolumetricCloudFloatSetting{ "freshBlend", &VolumetricCloudSettings::freshBlend, 0.01f, 1.0f },
    VolumetricCloudFloatSetting{ "clampExpand", &VolumetricCloudSettings::clampExpand, 0.0f, 4.0f },
    VolumetricCloudFloatSetting{ "motionBlend", &VolumetricCloudSettings::motionBlend, 0.0f, 1.0f },
};

struct VolumetricCloudViewSnapshot {
    VolumetricCloudFrameStatus status = VolumetricCloudFrameStatus::NotRendered;
    std::uint64_t frameIndex = 0;
    bool historyUsed = false;       // The resolve pass blended the history of the last frame.
    std::uint32_t divisor = 1;      // March resolution divisor of the last frame.
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
    std::uint32_t counts[4] = {};       // x max steps, y frame index, z empty samples to coarse, w sun march stride.
    float skip[4] = {};                 // x clear-weather stride multiplier.
    float lightMarch[4] = {};           // x sun steps, y reach fraction of the thickness, z octaves, w powder.
    float lightPhase[4] = {};           // x forward g, y backward g, z backward mix, w silver intensity.
    float lightScatter[4] = {};         // x silver g, y octave attenuation, z octave contribution, w octave eccentricity.
    float lightMisc[4] = {};            // x phase model, y ambient at the base, z haze distance (m), w light detail.
    float drainePhase[4] = {};          // x Henyey-Greenstein g, y Draine g, z Draine alpha, w Draine weight.
    std::uint32_t lowRes[4] = {};       // x divisor, yz sample offset inside the block, w unused.
    std::uint32_t fullSize[4] = {};     // xy view size in pixels.
};
static_assert(sizeof(VolumetricCloudParamsGpu) == 64 + 19 * 16);
static_assert(std::is_trivially_copyable_v<VolumetricCloudParamsGpu>);

// std430 block of VolumetricCloudResolve.comp; member order matches the shader.
struct VolumetricCloudResolveParamsGpu {
    Mat4 invViewProjection;
    Mat4 prevViewProjection;
    float eye[4] = {};                  // xyz eye (world).
    float windDelta[4] = {};            // xz: shift from a cloud position now to the same cloud last frame (m).
    float blend[4] = {};                // x history blend, y fresh blend, z clamp expand, w fallback cloud distance (m).
    float motion[4] = {};               // x extra blend at one pixel of motion.
    std::uint32_t counts[4] = {};       // x divisor, yz sample offset inside the block, w history valid.
    std::uint32_t sizes[4] = {};        // xy view size, zw march target size.
};
static_assert(sizeof(VolumetricCloudResolveParamsGpu) == 128 + 6 * 16);
static_assert(std::is_trivially_copyable_v<VolumetricCloudResolveParamsGpu>);

// Clamps every field into its documented range; a non-finite number falls back to the default.
VolumetricCloudSettings SanitizeVolumetricCloudSettings(const VolumetricCloudSettings& candidate) noexcept;

// Sky looks that set only the coverage and density. Everything else is kept, `enabled` included.
enum class VolumetricCloudPreset : std::uint8_t { Clear, Scattered, Broken, Overcast };
inline constexpr std::array<const char*, 4> kVolumetricCloudPresetNames{ "clear", "scattered", "broken", "overcast" };

VolumetricCloudSettings ApplyVolumetricCloudPreset(
    const VolumetricCloudSettings& current, VolumetricCloudPreset preset) noexcept;

VolumetricCloudSlicePush MakeVolumetricCloudSlicePush(const VolumetricCloudSettings& settings) noexcept;

} // namespace gte
