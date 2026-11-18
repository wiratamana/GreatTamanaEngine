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
inline constexpr const char* kCloudDetailAsset = "clouds/cloud_detail_64.gtvol";
inline constexpr const char* kCloudCurlAsset = "clouds/cloud_curl_128.gtvol";
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
    float layerTopM = 4400.0f;
    float maxDistanceM = 250000.0f; // Longest ray. A ground camera sees the layer top at its horizon 237 km away.
    float fadeStart = 0.9f;         // Fraction of the max distance where the clouds start to thin out (the air hazes them before).

    // Density model.
    float coverage = 0.8f; // Sky share under cloud: 0.2 a few puffs, 0.65 scattered, 1 overcast.
    float density = 1.50f;
    float extinction = 0.048f; // Per metre at density 1.
    float weatherScaleM = 25000.0f;
    float shapeScaleM = 6250.0f;
    // Structure: how big and how leaning the clouds are.
    float shapeWorley = 0.70f;      // Weight of Worley cell-wall crease erosion in the base shape.
    float largeShapeMix = 0.45f;    // Share of the big lobe octave (2 x the shape tile) in the shape; 0 = only the mid lobes.
    float sizeVariation = 0.5f;     // How much the weather map's cloud type shifts that share: big tall clouds get big lobes, small ones keep mid lobes.
    float windShearM = 280.0f;      // How far the top of the layer is shifted downwind relative to its base; clouds lean.
    float bodyErosion = 0.28f;      // Share of fine crown erosion kept on upper cloud domes; flat bases stay solid.
    float verticalStretch = 0.70f;  // Vertical scale of noise lobes; 1 keeps cells isotropic.
    float baseVariation = 0.06f;    // How much the big octave gently shifts the cloud base; 0 = one flat base.
    float bodyThresholdMin = 0.10f; // Lowest cut level of the body: raises the threshold in big masses so a high coverage does not fill them solid; 0 = off.
    float bodyWidth = 0.38f;        // Width of the body ramp in shape units: small = crisp lobe surfaces, large = soft mist; 0 = full range above threshold.
    float baseRagged = 0.04f;       // How much the mid lobes undulate the cloud base; 0 = smooth condensation plane.
    float farSoftening = 0.7f;      // Extra share of the big octave at the far field start and beyond; far clouds lose their stacked fine sheets.
    float detailScaleM = 350.0f;
    float detailStrength = 0.68f;
    // Detail noise: billowy (rounded lobes) and wispy (fibres) channels, pushed around by a curl noise.
    float curlAmountM = 95.0f;  // How far the curl noise moves the detail lookup, metres.
    float curlScaleM = 3500.0f; // World size of one tile of the curl noise.
    float wispiness = 0.30f;    // Share of the wispy (fibre) detail near the base and thin fringes; 0 = only billows.
    float fineDetail = 0.15f;    // Weight of a second, finer fetch of the detail noise; 0 = off.
    float detailFadeDistM = 48000.0f;
    // Far field. Starts at farWeatherStartM and is full at twice that distance.
    float farWeatherStartM = 30000.0f; // Distance where the far field starts: second weather map, coverage lift, air shortening.
    float farWeatherMix = 0.5f;        // Share of a second, differently scaled weather map; 0 = off.
    float farCoverageLift = 0.1f;      // Added to the coverage; fills the far deck so it reads as a layer.
    float farAirScale = 0.6f;          // Share of the distance beyond the start that the air in front of a cloud counts (mode 1).
    float windSpeed = 4.0f;
    float windDirection = 45.0f;
    float peakMin = 0.32f; // Minimum peak height of established clouds as a share of the layer.
    float baseRoundness = 0.09f;
    float topRoundness = 0.2f;
    float densityRamp = 0.28f;
    float densityTopFade = 0.9f;
    float topFalloffStart = 0.5f; // Where the top roundness starts to fall, as a fraction of the cloud's peak height.
    float shapeLow = 0.26f;       // Shape noise value that maps to 0 (the noise is stretched between low and high).
    float shapeHigh = 0.98f;      // Shape noise value that maps to 1.
    float densityCurve = 0.95f;   // Exponent on the eroded body; > 1 keeps wispy translucent edges instead of inflated plateaus.

    // March.
    float stepBaseM = 15.0f;
    float maxStepDepth = 0.4f;  // Largest optical depth one dense step may have; above it the step is shortened. 20 = off.
    float stepGrowth = 0.007f;  // Smallest growth per metre; a ray that would run out of steps grows faster, see maxSteps.
    float coarseMultiplier = 1.0f;
    float maxSteps = 84.0f; // Every ray reaches the end of its segment within 85 percent of this by growing its steps.
    float emptyToCoarse = 3.0f;
    float clearSkip = 1.0f; // Stride multiplier over weather that holds no cloud; above 1 can miss small clouds.
    float minTransmittance = 0.012f;
    float jitter = 1.0f;

    // Lighting. Intensities scale the sun and sky radiance the engine hands over.
    float sunIntensity = 6.3f;
    float ambientIntensity = 0.40f;
    float sunSteps = 6.0f;          // Samples of the march toward the sun.
    float lightReach = 0.7f;        // Length of that march, as a fraction of the layer thickness.
    float msOctaves = 4.0f;         // Multiple scattering octaves.
    float msAttenuation = 0.52f;    // Per octave factor on the optical depth.
    float msContribution = 0.62f;   // Per octave factor on the light.
    float msEccentricity = 0.5f;    // Per octave factor on the phase eccentricity.
    float phaseModel = 0.0f;        // 0 dual lobe Henyey-Greenstein, 1 Henyey-Greenstein plus Draine.
    float forwardG = 0.65f;
    float backwardG = -0.40f;
    float backwardMix = 0.46f;
    float silverIntensity = 0.12f;  // Weight of the narrow forward lobe that lights thin edges facing the sun.
    float silverG = 0.85f;
    float powder = 0.34f;           // Soft depth shading effect when looking away from the sun.
    float ambientBottom = 0.42f;    // Sky light left at the layer base, relative to the top.
    float hazeDistanceM = 45000.0f; // Distance at which a cloud has faded to 1/e into the sky behind it.
    float inScatter = 0.48f;        // Nubis in-scatter chance: shades cavities and thin rims softly; 0 = off.
    float lightDetail = 1.0f;       // Share of the fine erosion the nearest sun sample uses.
    float lightStride = 2.0f;       // Lit samples between two sun marches; the light in between is blended.
    float skyOcclusion = 0.85f;     // How strongly cloud above a sample blocks the sky light; 0 = height ramp only.
    float skyOcclusionReachM = 600.0f; // Height above a sample that the occlusion looks at.
    float skyFloor = 0.32f;         // Share of the sky light that always reaches a sample.
    float groundBounce = 1.5f;      // Strength of the sunlight bounced up from the ground; 0 = none (mode 1).
    float groundWarmth = 0.7f;      // 0 grey ground light, 1 warm sunlit-ground light.
    float msBlueShift = 0.88f;      // Tints the deeper scattering octaves toward blue.
    float ambientSaturation = 1.22f; // Colour saturation of the sky light that reaches the cloud: 1 as the sky, 0 grey, >1 boosted.
    float msDiffuse = 0.5f;         // Weight of the diffusion term of the multiple scattering: thick cores and undersides keep some light.
    float msGlow = 0.30f;           // Weight of the inner glow of cloud seen toward the sun.
    float sunWarmth = 0.25f;        // Tints the sun light on the cloud toward warm; 0 = the colour the atmosphere gives it.

    // Resolution and temporal accumulation.
    float resolutionDivisor = 0.0f; // March at 1/n of the view size per axis, n = 1..6; 0 picks n so the march grid is about 632 texels wide.
    float temporal = 1.0f;          // 1 accumulates over frames; 0 shows the current march only.
    float historyBlend = 0.0f;      // Weight of the new samples in a pixel without an exact sample this frame.
    float freshBlend = 0.06f;       // Same for the pixel that was marched itself; with adaptiveBlend the smallest weight a new sample gets.
    float clampExpand = 0.85f;      // Sigma width of the 3x3 neighbourhood variance clamp on silhouettes; 0 = no clamp.
    float motionBlend = 0.10f;      // Extra weight of the new samples where the picture moves by a pixel or more.
    float historySharp = 1.0f;      // 1 resamples the history with Catmull-Rom (sharper in motion), 0 bilinear.
    float adaptiveBlend = 1.0f;     // 1 averages the first samples of a still pixel (1/n) down to the freshBlend floor.
    float subpixelJitter = 0.35f;   // Size of the sub-pixel shift of each march ray, in pixels; the temporal average antialiases the edges. 0 = pixel centre.

    // Atmosphere. 1 lights the clouds from the atmosphere's look-up tables and falls back to the standalone
    // look (0) while the atmosphere publishes none.
    float atmosphereMode = 1.0f;
    float airScale = 1.0f;          // Factor on the air's scattering between the eye and a cloud (mode 1).
    float skyAmbient = 3.5f;        // Factor on the sky light that reaches the clouds (mode 1).

    // Cloud shadows: a map of the sun light the layer lets through, read by the lit materials.
    float shadowStrength = 1.0f;  // 0 = no cloud shadows (the map is not made); 1 = a full cloud blocks the sun fully.
    float shadowRangeKm = 24.0f;  // Width of the map around the camera.
    float shadowSoftness = 0.48f; // Blur of the shadow edge in map texels.
    float shadowUpdate = 1.0f;    // Every texel is redone once per this many frames (1..4).
    float shadowDensity = 1.20f;  // Factor on the optical depth of the shadow ray; tuned for crisp cumulus footprints.
    float shadowSteps = 12.0f;    // Samples of the shadow ray through the layer.
    float shaftStrength = 1.0f;  // 0 = no light shafts; 1 = the air loses the sun light the clouds block (uses the shadow map).
    float shaftSharpness = 3.0f; // Exponent on the sun transmittance of the air; higher gives crisper beams and darker gaps.
    float shaftHaze = 0.6f;     // Density per km of the haze that carries the light curtains near the ground (its extinction is a quarter of it; 0 = none).
    float shaftFill = 0.45f;     // Sky light in the shadowed columns of the haze (0 = black, 1 = like the sky's multiple scattering).
    float shaftMist = 0.0f;      // Extinction per km of an extra near-field mist in the volumetric light (0 = none).
    float shaftHeightM = 2500.0f; // Scale height of that haze; it thins out above.
    float shaftHighSunKeep = 0.7f; // Share of low-sun shaft strength and beam haze kept when the sun is overhead.
    float shadowFill = 0.22f;     // Share of the blocked sun that reaches shadowed ground as diffuse light.
    float sunRays = 0.55f;          // Intensity of the quarter-res screen-space sun shafts through cloud gaps (0 = off).
    float sunRaysDecay = 1.0f;      // Exponential decay of sun-shaft taps away from the sun.
    float sunRaysLength = 1.5f;     // Reach of the sun shafts across the screen, in view heights.
    float sunRaysTaps = 48.0f;      // Radial sample count per pixel, snapped to 8..48 in steps of 8.
    float sunRaysAnisotropy = 0.75f; // Forward-scatter phase eccentricity g of the sun-shaft glow.
    float sunRaysContrast = 1.65f;  // Contrast gain on open cloud gaps along the sun line.
    float sunRaysAirLengthM = 2000.0f; // Distance scale (m) over which geometry accumulates sun-shaft air light.
    float sunRaysSpoke = 0.22f;     // Strength of the angular high-pass spoke term between cloud gaps (0 = off).
    float sunStar = 1.0f;           // Strength of the small starburst on the sun where it shows through a gap (0 = off).
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
    VolumetricCloudFloatSetting{ "shapeWorley", &VolumetricCloudSettings::shapeWorley, 0.0f, 1.5f },
    VolumetricCloudFloatSetting{ "largeShapeMix", &VolumetricCloudSettings::largeShapeMix, 0.0f, 1.0f },
    VolumetricCloudFloatSetting{ "sizeVariation", &VolumetricCloudSettings::sizeVariation, 0.0f, 1.0f },
    VolumetricCloudFloatSetting{ "windShearM", &VolumetricCloudSettings::windShearM, 0.0f, 10000.0f },
    VolumetricCloudFloatSetting{ "bodyErosion", &VolumetricCloudSettings::bodyErosion, 0.0f, 1.0f },
    VolumetricCloudFloatSetting{ "verticalStretch", &VolumetricCloudSettings::verticalStretch, 0.25f, 6.0f },
    VolumetricCloudFloatSetting{ "baseVariation", &VolumetricCloudSettings::baseVariation, 0.0f, 1.0f },
    VolumetricCloudFloatSetting{ "bodyThresholdMin", &VolumetricCloudSettings::bodyThresholdMin, 0.0f, 0.9f },
    VolumetricCloudFloatSetting{ "bodyWidth", &VolumetricCloudSettings::bodyWidth, 0.0f, 1.0f },
    VolumetricCloudFloatSetting{ "baseRagged", &VolumetricCloudSettings::baseRagged, 0.0f, 1.0f },
    VolumetricCloudFloatSetting{ "farSoftening", &VolumetricCloudSettings::farSoftening, 0.0f, 1.0f },
    VolumetricCloudFloatSetting{ "detailScaleM", &VolumetricCloudSettings::detailScaleM, 10.0f, 20000.0f },
    VolumetricCloudFloatSetting{ "detailStrength", &VolumetricCloudSettings::detailStrength, 0.0f, 2.0f },
    VolumetricCloudFloatSetting{ "curlAmountM", &VolumetricCloudSettings::curlAmountM, 0.0f, 2000.0f },
    VolumetricCloudFloatSetting{ "curlScaleM", &VolumetricCloudSettings::curlScaleM, 200.0f, 100000.0f },
    VolumetricCloudFloatSetting{ "wispiness", &VolumetricCloudSettings::wispiness, 0.0f, 1.0f },
    VolumetricCloudFloatSetting{ "fineDetail", &VolumetricCloudSettings::fineDetail, 0.0f, 1.0f },
    VolumetricCloudFloatSetting{ "detailFadeDistM", &VolumetricCloudSettings::detailFadeDistM, 100.0f, 500000.0f },
    VolumetricCloudFloatSetting{ "farWeatherStartM", &VolumetricCloudSettings::farWeatherStartM, 1000.0f, 500000.0f },
    VolumetricCloudFloatSetting{ "farWeatherMix", &VolumetricCloudSettings::farWeatherMix, 0.0f, 1.0f },
    VolumetricCloudFloatSetting{ "farCoverageLift", &VolumetricCloudSettings::farCoverageLift, 0.0f, 0.5f },
    VolumetricCloudFloatSetting{ "farAirScale", &VolumetricCloudSettings::farAirScale, 0.05f, 1.0f },
    VolumetricCloudFloatSetting{ "windSpeed", &VolumetricCloudSettings::windSpeed, 0.0f, 500.0f },
    VolumetricCloudFloatSetting{ "windDirection", &VolumetricCloudSettings::windDirection, -360.0f, 360.0f },
    VolumetricCloudFloatSetting{ "peakMin", &VolumetricCloudSettings::peakMin, 0.05f, 1.0f },
    VolumetricCloudFloatSetting{ "topFalloffStart", &VolumetricCloudSettings::topFalloffStart, 0.0f, 0.95f },
    VolumetricCloudFloatSetting{ "shapeLow", &VolumetricCloudSettings::shapeLow, 0.0f, 0.95f },
    VolumetricCloudFloatSetting{ "shapeHigh", &VolumetricCloudSettings::shapeHigh, 0.05f, 1.0f },
    VolumetricCloudFloatSetting{ "densityCurve", &VolumetricCloudSettings::densityCurve, 0.2f, 2.0f },
    VolumetricCloudFloatSetting{ "baseRoundness", &VolumetricCloudSettings::baseRoundness, 0.01f, 0.5f },
    VolumetricCloudFloatSetting{ "topRoundness", &VolumetricCloudSettings::topRoundness, 0.0f, 0.5f },
    VolumetricCloudFloatSetting{ "densityRamp", &VolumetricCloudSettings::densityRamp, 0.01f, 0.5f },
    VolumetricCloudFloatSetting{ "densityTopFade", &VolumetricCloudSettings::densityTopFade, 0.5f, 0.99f },
    VolumetricCloudFloatSetting{ "stepBaseM", &VolumetricCloudSettings::stepBaseM, 1.0f, 2000.0f },
    VolumetricCloudFloatSetting{ "maxStepDepth", &VolumetricCloudSettings::maxStepDepth, 0.1f, 20.0f },
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
    VolumetricCloudFloatSetting{ "inScatter", &VolumetricCloudSettings::inScatter, 0.0f, 1.0f },
    VolumetricCloudFloatSetting{ "lightDetail", &VolumetricCloudSettings::lightDetail, 0.0f, 1.0f },
    VolumetricCloudFloatSetting{ "lightStride", &VolumetricCloudSettings::lightStride, 1.0f, 8.0f },
    VolumetricCloudFloatSetting{ "skyOcclusion", &VolumetricCloudSettings::skyOcclusion, 0.0f, 4.0f },
    VolumetricCloudFloatSetting{ "skyOcclusionReachM", &VolumetricCloudSettings::skyOcclusionReachM, 10.0f, 5000.0f },
    VolumetricCloudFloatSetting{ "skyFloor", &VolumetricCloudSettings::skyFloor, 0.0f, 1.0f },
    VolumetricCloudFloatSetting{ "groundBounce", &VolumetricCloudSettings::groundBounce, 0.0f, 20.0f },
    VolumetricCloudFloatSetting{ "groundWarmth", &VolumetricCloudSettings::groundWarmth, 0.0f, 1.0f },
    VolumetricCloudFloatSetting{ "msBlueShift", &VolumetricCloudSettings::msBlueShift, 0.0f, 1.0f },
    VolumetricCloudFloatSetting{ "ambientSaturation", &VolumetricCloudSettings::ambientSaturation, 0.0f, 2.0f },
    VolumetricCloudFloatSetting{ "msDiffuse", &VolumetricCloudSettings::msDiffuse, 0.0f, 8.0f },
    VolumetricCloudFloatSetting{ "msGlow", &VolumetricCloudSettings::msGlow, 0.0f, 8.0f },
    VolumetricCloudFloatSetting{ "sunWarmth", &VolumetricCloudSettings::sunWarmth, 0.0f, 1.0f },
    VolumetricCloudFloatSetting{ "resolutionDivisor", &VolumetricCloudSettings::resolutionDivisor, 0.0f, 6.0f },
    VolumetricCloudFloatSetting{ "temporal", &VolumetricCloudSettings::temporal, 0.0f, 1.0f },
    VolumetricCloudFloatSetting{ "historyBlend", &VolumetricCloudSettings::historyBlend, 0.0f, 1.0f },
    VolumetricCloudFloatSetting{ "freshBlend", &VolumetricCloudSettings::freshBlend, 0.01f, 1.0f },
    VolumetricCloudFloatSetting{ "clampExpand", &VolumetricCloudSettings::clampExpand, 0.0f, 4.0f },
    VolumetricCloudFloatSetting{ "motionBlend", &VolumetricCloudSettings::motionBlend, 0.0f, 1.0f },
    VolumetricCloudFloatSetting{ "historySharp", &VolumetricCloudSettings::historySharp, 0.0f, 1.0f },
    VolumetricCloudFloatSetting{ "adaptiveBlend", &VolumetricCloudSettings::adaptiveBlend, 0.0f, 1.0f },
    VolumetricCloudFloatSetting{ "subpixelJitter", &VolumetricCloudSettings::subpixelJitter, 0.0f, 2.0f },
    VolumetricCloudFloatSetting{ "atmosphereMode", &VolumetricCloudSettings::atmosphereMode, 0.0f, 1.0f },
    VolumetricCloudFloatSetting{ "airScale", &VolumetricCloudSettings::airScale, 0.0f, 50.0f },
    VolumetricCloudFloatSetting{ "skyAmbient", &VolumetricCloudSettings::skyAmbient, 0.0f, 10.0f },
    VolumetricCloudFloatSetting{ "shadowStrength", &VolumetricCloudSettings::shadowStrength, 0.0f, 1.0f },
    VolumetricCloudFloatSetting{ "shadowRangeKm", &VolumetricCloudSettings::shadowRangeKm, 4.0f, 100.0f },
    VolumetricCloudFloatSetting{ "shadowSoftness", &VolumetricCloudSettings::shadowSoftness, 0.0f, 4.0f },
    VolumetricCloudFloatSetting{ "shadowUpdate", &VolumetricCloudSettings::shadowUpdate, 1.0f, 4.0f },
    VolumetricCloudFloatSetting{ "shadowDensity", &VolumetricCloudSettings::shadowDensity, 0.1f, 8.0f },
    VolumetricCloudFloatSetting{ "shadowSteps", &VolumetricCloudSettings::shadowSteps, 4.0f, 24.0f },
    VolumetricCloudFloatSetting{ "shaftStrength", &VolumetricCloudSettings::shaftStrength, 0.0f, 1.0f },
    VolumetricCloudFloatSetting{ "shaftSharpness", &VolumetricCloudSettings::shaftSharpness, 1.0f, 8.0f },
    VolumetricCloudFloatSetting{ "shaftHaze", &VolumetricCloudSettings::shaftHaze, 0.0f, 4.0f },
    VolumetricCloudFloatSetting{ "shaftFill", &VolumetricCloudSettings::shaftFill, 0.0f, 4.0f },
    VolumetricCloudFloatSetting{ "shaftMist", &VolumetricCloudSettings::shaftMist, 0.0f, 4.0f },
    VolumetricCloudFloatSetting{ "shaftHeightM", &VolumetricCloudSettings::shaftHeightM, 200.0f, 6000.0f },
    VolumetricCloudFloatSetting{ "shaftHighSunKeep", &VolumetricCloudSettings::shaftHighSunKeep, 0.0f, 1.0f },
    VolumetricCloudFloatSetting{ "shadowFill", &VolumetricCloudSettings::shadowFill, 0.0f, 1.0f },
    VolumetricCloudFloatSetting{ "sunRays", &VolumetricCloudSettings::sunRays, 0.0f, 3.0f },
    VolumetricCloudFloatSetting{ "sunRaysDecay", &VolumetricCloudSettings::sunRaysDecay, 0.0f, 6.0f },
    VolumetricCloudFloatSetting{ "sunRaysLength", &VolumetricCloudSettings::sunRaysLength, 0.2f, 3.0f },
    VolumetricCloudFloatSetting{ "sunRaysTaps", &VolumetricCloudSettings::sunRaysTaps, 8.0f, 48.0f },
    VolumetricCloudFloatSetting{ "sunRaysAnisotropy", &VolumetricCloudSettings::sunRaysAnisotropy, 0.0f, 0.95f },
    VolumetricCloudFloatSetting{ "sunRaysContrast", &VolumetricCloudSettings::sunRaysContrast, 0.2f, 3.0f },
    VolumetricCloudFloatSetting{ "sunRaysAirLengthM", &VolumetricCloudSettings::sunRaysAirLengthM, 200.0f, 20000.0f },
    VolumetricCloudFloatSetting{ "sunRaysSpoke", &VolumetricCloudSettings::sunRaysSpoke, 0.0f, 4.0f },
    VolumetricCloudFloatSetting{ "sunStar", &VolumetricCloudSettings::sunStar, 0.0f, 3.0f },
};

// Where the light of the clouds came from in the last frame.
enum class VolumetricCloudSkyLight : std::uint8_t {
    Standalone,  // The settings only (mode 0).
    Atmosphere,  // The atmosphere's look-up tables (mode 1).
    Unavailable, // Mode 1 was asked for but the atmosphere published no tables; the standalone look was used.
};

struct VolumetricCloudViewSnapshot {
    VolumetricCloudFrameStatus status = VolumetricCloudFrameStatus::NotRendered;
    VolumetricCloudSkyLight skyLight = VolumetricCloudSkyLight::Standalone;
    std::uint64_t frameIndex = 0;
    bool historyUsed = false;       // The resolve pass blended the history of the last frame.
    std::uint32_t divisor = 1;      // March resolution divisor of the last frame.
    float sunUp = 0.0f;             // Sine of the active sun elevation in the last frame.
    float shaftStrength = 0.0f;     // Published light-shaft strength of the last frame.
    float beamHazePerKm = 0.0f;     // Published beam-haze density per km of the last frame.
    float shaftHazePerM = 0.0f;     // Published volumetric-light mist extinction per m of the last frame.
    float shadowExtentM = 0.0f;     // Published far cloud-shadow map width in metres.
    float shadowTexelM = 0.0f;      // Published far cloud-shadow texel size in metres.
    float shadowNearExtentM = 0.0f; // Published nested near cloud-shadow map width in metres.
    float shadowNearTexelM = 0.0f;  // Published nested near cloud-shadow texel size in metres.
    bool shadowNearCovers = false;  // True when the published near cloud-shadow region covers the eye.
    float sunShaftsSunUv[2] = {};   // Projected sun UV of the sun-shafts pass.
    float sunShaftsReach = 0.0f;    // Reach of the sun-shafts pass in view heights.
    bool sunShaftsActive = false;   // True when the sun-shafts pass ran with active rays.
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
    float ambientColor[4] = {};         // rgb ambient radiance, w density curve exponent.
    float layer[4] = {};                // x base (m), y top (m), z max distance (m), w fade start fraction.
    float weather[4] = {};              // x weather tile (m), yz wind offset (m), w coverage.
    float noise[4] = {};                // x shape tile (m), y detail tile (m), z detail strength, w detail fade (m).
    float density[4] = {};              // x density factor, y extinction (1/m), z peak min, w jitter strength.
    float heightShape[4] = {};          // x base roundness, y top roundness, z density ramp, w density top fade.
    float march[4] = {};                // x base step (m), y step growth, z coarse multiplier, w min transmittance.
    std::uint32_t counts[4] = {};       // x max steps, y frame index, z empty samples to coarse, w sun march stride.
    float skip[4] = {};                 // x clear-weather stride multiplier, y top falloff start, zw shape stretch low, high.
    float lightMarch[4] = {};           // x sun steps, y reach fraction of the thickness, z octaves, w powder.
    float lightPhase[4] = {};           // x forward g, y backward g, z backward mix, w silver intensity.
    float lightScatter[4] = {};         // x silver g, y octave attenuation, z octave contribution, w octave eccentricity.
    float lightMisc[4] = {};            // x phase model, y ambient at the base, z haze distance (m), w light detail.
    float drainePhase[4] = {};          // x Henyey-Greenstein g, y Draine g, z Draine alpha, w Draine weight.
    std::uint32_t lowRes[4] = {};       // x divisor, yz sample offset inside the block, w sub-pixel jitter in percent of a pixel.
    std::uint32_t fullSize[4] = {};     // xy view size in pixels.
    float atmosphereMode[4] = {};       // x 1 = lit by the atmosphere, y sky intensity, z air scale, w sky ambient gain.
    float atmosphereCamera[4] = {};     // xyz camera position in atmosphere space (km).
    float skyShade[4] = {};             // x sky occlusion strength, y its reach (m), z sky light floor, w ground bounce.
    float skyShade2[4] = {};            // x ground bounce warmth, y blue shift of the scattering octaves, z far field start (m), w far weather mix.
    float farField[4] = {};             // x far coverage lift, y far air distance scale, z max step optical depth, w in-scatter strength.
    float detailShape[4] = {};          // x curl amount (m), y curl tile (m), z wispiness, w fine detail weight.
    float structure[4] = {};            // x large lobe mix, y size variation, zw wind shear vector at the layer top (m).
    float structure2[4] = {};           // x erosion kept in the body, y vertical stretch, z base variation, w far softening.
    float structure3[4] = {};           // x weight of the Worley cells in the shape, y body threshold floor, z ragged base, w body ramp width.
    float lightTune[4] = {};            // x sky light saturation, y diffusion gain, z inner glow gain, w reserved.
    float lightTune2[4] = {};           // x sun warmth, yzw reserved.
    float shadowGrid[4] = {};           // x map texel (m), yz map centre x, z (m), w 1 = write the cloud shadow map.
    float shadowShift[4] = {};          // xy history shift (m), z history blend, w optical depth factor.
    float shadowTune[4] = {};           // x edge blur (texels), y update interval (frames), z ray steps, w reserved.
};
static_assert(sizeof(VolumetricCloudParamsGpu) == 64 + 33 * 16);
static_assert(std::is_trivially_copyable_v<VolumetricCloudParamsGpu>);

// std430 block of VolumetricCloudResolve.comp; member order matches the shader.
struct VolumetricCloudResolveParamsGpu {
    Mat4 invViewProjection;
    Mat4 prevViewProjection;
    float eye[4] = {};                  // xyz eye (world).
    float windDelta[4] = {};            // xz: shift from a cloud position now to the same cloud last frame (m).
    float blend[4] = {};                // x history blend, y fresh blend, z clamp expand, w fallback cloud distance (m).
    float motion[4] = {};               // x extra blend at one pixel of motion, y Catmull-Rom history weight, z adaptive sample count (0/1).
    std::uint32_t counts[4] = {};       // x divisor, yz sample offset inside the block, w history valid.
    std::uint32_t sizes[4] = {};        // xy view size, zw march target size.
    float sunRay[4] = {};               // xy sun uv, z spoke strength (sunRaysSpoke, 0 = off), w starburst strength (0 = off).
    float sunRayColor[4] = {};          // rgb colour of the rays, w glow radius (view height units).
};
static_assert(sizeof(VolumetricCloudResolveParamsGpu) == 128 + 8 * 16);
static_assert(std::is_trivially_copyable_v<VolumetricCloudResolveParamsGpu>);

// Parameter block of VolumetricCloudSunShafts.comp (80 bytes); member order matches the shader.
struct VolumetricCloudSunShaftsParamsGpu {
    float sunUvAndReachAndIntensity[4] = {}; // xy sun uv, z reach (view heights), w intensity.
    float decayTapsAnisoContrast[4] = {};    // x decay, y taps, z anisotropy g, w contrast.
    float sunColorAndHazeMix[4] = {};        // rgb tinted sun colour, w haze mix.
    float frameFadeAspectActive[4] = {};     // x frame index, y fade (0..1), z aspect (w/h), w active (0/1).
    float depthParams[4] = {};               // x near plane (m), y far plane (m), z reserved, w air length scale (m).
};
static_assert(sizeof(VolumetricCloudSunShaftsParamsGpu) == 80);
static_assert(std::is_trivially_copyable_v<VolumetricCloudSunShaftsParamsGpu>);

// Clamps every field into its documented range; a non-finite number falls back to the default.
VolumetricCloudSettings SanitizeVolumetricCloudSettings(const VolumetricCloudSettings& candidate) noexcept;

// Sky looks that set only the coverage and density. Everything else is kept, `enabled` included.
enum class VolumetricCloudPreset : std::uint8_t { Clear, Scattered, Broken, Overcast };
inline constexpr std::array<const char*, 4> kVolumetricCloudPresetNames{ "clear", "scattered", "broken", "overcast" };

VolumetricCloudSettings ApplyVolumetricCloudPreset(
    const VolumetricCloudSettings& current, VolumetricCloudPreset preset) noexcept;

VolumetricCloudSlicePush MakeVolumetricCloudSlicePush(const VolumetricCloudSettings& settings) noexcept;

} // namespace gte
