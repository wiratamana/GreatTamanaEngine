#pragma once

#include "VolumetricCloudTypes.h"
#include "../../Game/Lighting/CloudShadowMap.h"
#include "../../Math/Mat4.h"
#include "../../Math/Vec3.h"

#include <cstdint>

namespace gte {

// CPU twins of the pure functions in VolumetricCloud.comp; the tests pin both sides to the same numbers.

// Linear remap of `v` from [lo0, hi0] to [lo1, hi1], not clamped. A zero-width source range maps like a step.
float CloudRemap(float v, float lo0, float hi0, float lo1, float hi1) noexcept;

// Height above sea level (m) of a point `horizontalSquared` (m^2) away from the planet axis at world height `y`.
// Exact where `y` and the offset are small against the radius, unlike the length of a point on a 6371 km sphere.
float CloudAltitudeM(float horizontalSquared, float y, float radiusM) noexcept;

// Position inside the layer: 0 at the base, 1 at the top.
float CloudHeightFraction(float altitudeM, float baseM, float topM) noexcept;

// Rounds the bottom and the top of a cloud whose peak sits at `peak` (0..1 of the layer). The top falls from
// `topFalloffStart` * stop to the stop height.
float CloudShapeGradient(float heightFraction, float peak, float baseRoundness, float topRoundness,
    float topFalloffStart = 0.2f) noexcept;

// Stretches the shape noise value so [low, high] maps to [0, 1] (clamped).
float CloudStretchShape(float shape, float low, float high) noexcept;

// Share of the sky light that reaches a sample with `upwardOpticalDepth` of cloud above it (SkyVisibility()).
float CloudSkyVisibility(float upwardOpticalDepth, float strength, float floor) noexcept;

// Share of the bounced ground light that reaches a sample at `heightFraction` of the layer.
float CloudGroundShare(float heightFraction) noexcept;

// Per octave tint of the multiple scattering: white at 0 and `fullShiftTint` at 1 (SunScatter()).
Vec3 CloudOctaveTint(float blueShift, const Vec3& fullShiftTint) noexcept;

// Density ramp over the layer: thin at the base, thin again under the top.
float CloudDensityGradient(float heightFraction, float ramp, float topFade) noexcept;

// Part of one view ray that lies inside the layer shell, as distances along the ray.
struct CloudShellSegment {
    bool hit = false;
    float tStart = 0.0f;
    float tEnd = 0.0f;
};

// `cameraY` is the eye height above sea level and `dirY` the Y of the unit ray direction; the shell is centred
// below the eye, so nothing else matters. A ray that reaches the ground before the layer misses.
CloudShellSegment ComputeCloudShellSegment(
    float cameraY, float dirY, float radiusM, float baseM, float topM, float maxDistanceM) noexcept;

// Wind drift of the noise lookups: moves `offset` (x, z in metres, double precision) by `dt` seconds and
// returns the unwrapped step in `step`. The offset wraps at `periodM`, 0 meaning never.
void AdvanceCloudWind(double (&offset)[2], double (&step)[2], float speed, float directionDegrees, float dt,
    double periodM) noexcept;

// The far weather map is the weather map at this multiple of its tile size (VolumetricCloud.comp kFarWeatherTileScale).
inline constexpr float kCloudFarWeatherTileScale = 2.8f;

// The big lobe octave reads the shape volume at this multiple of the shape tile (VolumetricCloud.comp kLargeShapeScale).
inline constexpr float kCloudLargeShapeScale = 1.75f;

// Distance (m) the resolve pass assumes for a pixel that has no fresh cloud distance. Fixed: it is not tied to the
// march range, which reaches the horizon (250 km and more).
inline constexpr float kCloudHistoryFallbackDistanceM = 15000.0f;

// Default wind wrap period (m) matching the LCM of the shipped weather, shape and detail tiles.
inline constexpr double kCloudWindPeriodM = 350000.0;

// Length after which the weather, far weather, shape and detail lookups all repeat: the least common multiple of the
// tile sizes in whole metres, or 0 when they have none that stays small.
double CloudWindPeriodM(const VolumetricCloudSettings& safe) noexcept;

// Phase functions, normalised so they integrate to 1 over the sphere. `cosTheta` is the cosine between the
// view ray and the direction toward the sun, so +1 looks into the sun.
float CloudHenyeyGreenstein(float cosTheta, float g) noexcept;
float CloudDraine(float cosTheta, float g, float alpha) noexcept;

// Draine constants for 10 micrometre droplets, mixed with a very forward Henyey-Greenstein lobe.
inline constexpr float kCloudDraineHgG = 0.9881f;
inline constexpr float kCloudDraineG = 0.5567f;
inline constexpr float kCloudDraineAlpha = 21.9955f;
inline constexpr float kCloudDraineWeight = 0.4824f;

// Phase of one scattering octave; `eccentricity` scales every g (1 = the full model, 0 = isotropic).
float CloudPhase(const VolumetricCloudSettings& safe, float cosTheta, float eccentricity) noexcept;

// Beer-Lambert transmittance for an optical depth.
float CloudBeer(float opticalDepth) noexcept;

// Soft ceiling, thin-optical-depth gate and phase shoulder on sun scattering (VolumetricCloudLighting.glsl).
inline constexpr float kCloudSilverPhaseCap = 4.0f;
inline constexpr float kCloudSilverThinDepthScale = 0.90f;
inline constexpr float kCloudSunScatterShoulder = 1.85f;

// Smooth luminance ceiling on a cloud sample (VolumetricCloudLighting.glsl kMaxSourceLuma).
inline constexpr float kCloudMaxSourceLuma = 12.0f;

// Sun light reaching a sample: sum over octaves plus the thin-optical-depth gated silver lobe.
float CloudSunScatter(const VolumetricCloudSettings& safe, float cosTheta, float opticalDepth) noexcept;

// Smoothly bounds the luminance of `radiance` below `maxLuma` while preserving RGB hue.
Vec3 BoundCloudLuma(const Vec3& radiance, float maxLuma = kCloudMaxSourceLuma) noexcept;

// One energy-conserving march step: the fraction of the light behind that the step absorbs.
float CloudStepOpacity(float extinction, float stepLength) noexcept;

// Adds one step to a front-to-back accumulation. `source` is the radiance per unit of absorbed light.
void CloudAccumulateStep(float& light, float& transmittance, float source, float opacity) noexcept;

// What the per-frame parameter block needs besides the settings.
struct VolumetricCloudFrameFacts {
    Mat4 invViewProjection = Mat4::Identity(); // Eye-relative: fx::View::relative.inverse.
    Vec3 eye;
    Vec3 sunDirection = Vec3::Up();
    Vec3 sunIlluminance = Vec3::One();
    Vec3 sunTint = Vec3::One();
    float ambientScale = 1.0f;
    float windOffset[2] = {};
    std::uint64_t frameIndex = 0;
    std::uint32_t viewWidth = 1;
    std::uint32_t viewHeight = 1;

    // Set when the atmosphere's tables light the clouds: the sun is then passed as plain illuminance, because
    // the transmittance and the planet's shadow come from the tables.
    bool atmosphereLit = false;
    float skyIntensity = 1.0f;
    Vec3 atmosphereCameraKm = Vec3::Zero(); // Camera in atmosphere space.
    float atmospherePlanetRadiusKm = 0.0f;  // The atmosphere's planet radius; used when atmosphereLit.
};

// Planet radius of the cloud shell: the atmosphere's own when it lights the clouds, so the two never differ.
float CloudPlanetRadiusKm(const VolumetricCloudSettings& safe, const VolumetricCloudFrameFacts& facts) noexcept;

VolumetricCloudParamsGpu MakeVolumetricCloudParams(
    const VolumetricCloudSettings& safe, const VolumetricCloudFrameFacts& facts) noexcept;

// Where the cloud shadow map sits: a square around the point where the eye's sun ray crosses the layer, whose centre
// is a multiple of the texel size, so the texel lattice is fixed in the world and the map does not shimmer.
struct CloudShadowGrid {
    float texelM = 1.0f;
    float nearTexelM = 1.0f / static_cast<float>(kCloudShadowNearScale);
    double centerX = 0.0;
    double centerZ = 0.0;
};

CloudShadowGrid MakeCloudShadowGrid(
    const VolumetricCloudSettings& safe, const Vec3& eye, const Vec3& sunDirection, std::uint32_t mapSize) noexcept;

// History of the shadow map: how far the clouds moved since the last map, and how much of the new samples to take.
struct VolumetricCloudShadowFacts {
    CloudShadowGrid grid;
    double shiftX = 0.0; // Where the same cloud sat last time, relative to its place now (m): wind step + grid move.
    double shiftZ = 0.0;
    float historyBlend = 1.0f; // 1 = no history.
};

// Lets the march shader read `map` for the sun light of the air in front of a cloud (shaft strength 0 = off).
void SetCloudShaftLookup(VolumetricCloudParamsGpu& params, const CloudShadowMap& map) noexcept;

// True when the previous shadow map remains compatible with the active sun direction, coverage and shadow density.
bool CloudShadowHistoryReusable(const Vec3& lastSunDir, float lastCoverage, float lastShadowDensity,
    const Vec3& sunDir, const VolumetricCloudSettings& safe) noexcept;

// The parameter block of the dispatch that writes the shadow map (the march shader in its map mode).
VolumetricCloudParamsGpu MakeVolumetricCloudShadowParams(const VolumetricCloudSettings& safe,
    const VolumetricCloudFrameFacts& facts, const VolumetricCloudShadowFacts& shadow) noexcept;

// Sun height (sine of elevation) where cloud shadows fade in near the horizon.
inline constexpr float kCloudShadowFadeSunLow = 0.04f;
inline constexpr float kCloudShadowFadeSunHigh = 0.10f;

// Sun height range over which shaft strength and beam haze transition from 1 to shaftHighSunKeep.
inline constexpr float kCloudShaftFadeSunLow = 0.30f;
inline constexpr float kCloudShaftFadeSunHigh = 0.95f;

// High-sun scale on light shafts and beam haze: 1 at low sun, smoothly reaching highSunKeep overhead.
float CloudShaftSunFade(float sunUp, float highSunKeep) noexcept;

struct CloudShadowStrengths {
    float strength = 0.0f;
    float shaftStrength = 0.0f;
    float beamHazePerKm = 0.0f;
    float shaftHazePerM = 0.0f;
};

// Published cloud shadow and shaft terms for `sunUp` (sine of sun elevation), including horizon and high-sun fades.
CloudShadowStrengths ComputeCloudShadowStrengths(const VolumetricCloudSettings& safe, float sunUp) noexcept;

// Width in texels of the march grid when the divisor is automatic (setting 0).
inline constexpr float kCloudAutoMarchTexels = 632.0f;
inline constexpr std::uint32_t kCloudMaxResolutionDivisor = 6;

// The march resolution divisor in use: the setting rounded to 1..6; 0 picks it from `viewWidth` (4 when it is unknown).
std::uint32_t CloudResolutionDivisor(const VolumetricCloudSettings& safe, std::uint32_t viewWidth = 0) noexcept;

// Pixel inside an n x n block that the march samples on `frameIndex`; n*n frames visit every pixel once,
// in a Bayer order so that consecutive frames land far apart.
struct CloudSampleOffset {
    std::uint32_t x = 0;
    std::uint32_t y = 0;
};
CloudSampleOffset CloudBayerOffset(std::uint64_t frameIndex, std::uint32_t divisor) noexcept;

// How often the pixel marched on `frameIndex` was marched before, wrapped at 256.
std::uint32_t CloudOwnSampleIndex(std::uint64_t frameIndex, std::uint32_t divisor) noexcept;

// Start jitter in [0, 1) of a marched pixel: full-res pixel blue noise stepped once per Bayer cycle by the golden ratio.
float CloudMarchJitter(float blueNoise, std::uint64_t frameIndex, std::uint32_t divisor = 1) noexcept;

// Screen-space sun rays: how far toward the sun a pixel looks (share of the way) and the radius of the sun glow.
inline constexpr float kCloudSunRayGlowRadius = 0.18f;

// Sun height and off-screen limits for screen-space sun rays and starburst.
inline constexpr float kCloudSunRayOutsideView = 0.2f;
inline constexpr float kCloudSunRayFadeSunLow = 0.85f;
inline constexpr float kCloudSunRayFadeSunHigh = 1.05f;

// Screen-space sun-ray and starburst fade for `sunUp`, projected `sunUv` (0..1) and view `aspect` (width / height).
float CloudSunRayFade(float sunUp, const float (&sunUv)[2], float aspect) noexcept;

// Previous-frame data of the resolve pass.
struct VolumetricCloudResolveFacts {
    Mat4 prevViewProjection = Mat4::Identity(); // Last frame's view-projection, eye-relative to THIS frame's eye.
    float windDelta[2] = {}; // Wind offset now minus last frame (m).
    bool historyValid = false;
    std::uint32_t lowWidth = 1;
    std::uint32_t lowHeight = 1;
    float sunUv[2] = {};      // Sun position in the view (0..1); only used when sunRayFade > 0.
    float sunRayFade = 0.0f;  // 0 = the sun is behind the camera or too high for rays.
    float sunColor[3] = { 1.0f, 1.0f, 1.0f };
};

VolumetricCloudResolveParamsGpu MakeVolumetricCloudResolveParams(const VolumetricCloudSettings& safe,
    const VolumetricCloudFrameFacts& facts, const VolumetricCloudResolveFacts& resolve) noexcept;

// CPU twin of the tap test in VolumetricCloudResolve.comp: may a march result be used at a pixel whose scene
// lies `pixelEndKm` away? `tapEndKm` is how far that march looked, `tapDistanceKm` where its cloud sits.
bool CloudTapUsable(float tapEndKm, float tapDistanceKm, float tapTransmittance, float pixelEndKm) noexcept;

// Final colour of a cloud pixel seen through air; preserves sky pixels unchanged where cloud transmittance is 1.
float CloudAerialComposite(float scene, float cloudTransmittance, float cloudLight, float airTransmittance,
    float airLight) noexcept;

// Scene radiance knee above which cloud transmittance applies to the fourth power (VolumetricCloudResolve.comp).
inline constexpr float kCloudSceneHdrKnee = 64.0f;

// Soft HDR luminance shoulder applied to resolved cloud radiance before compositing (VolumetricCloudResolve.comp).
inline constexpr float kCloudResolveShoulderStart = 4.2f;
inline constexpr float kCloudResolveShoulderCeiling = 8.5f;

// CPU twin of CompressCloudShoulder() in VolumetricCloudResolve.comp: chroma-preserving HDR shoulder above 4.2.
Vec3 CompressCloudResolveLuma(const Vec3& radiance) noexcept;

// CPU twin of SceneBehindCloud() in VolumetricCloudResolve.comp: steepens occlusion above kCloudSceneHdrKnee.
Vec3 CloudSceneBehind(const Vec3& scene, float cloudTransmittance) noexcept;

// Distance that stands for "no surface", in km.
inline constexpr float kCloudNoSurfaceKm = 1000.0f;

// Off-screen fade distance (in view heights), UV clamp, sky-haze tint and fallback depth planes of sun shafts.
inline constexpr float kCloudSunShaftsOutsideView = 1.5f;
inline constexpr float kCloudSunShaftsUvClamp = 4.0f;
inline constexpr float kCloudSunShaftsHazeMix = 0.3f;
inline constexpr float kCloudSunShaftsFallbackNearM = 0.1f;
inline constexpr float kCloudSunShaftsFallbackFarM = 10000.0f;

// Horizon and off-screen fade (0..1) of the quarter-res sun-shafts pass; stays full for an overhead sun.
float CloudSunShaftsFade(float sunUp, const float (&sunUv)[2], float aspect) noexcept;

// Per-frame inputs of the quarter-resolution sun-shafts pass.
struct VolumetricCloudSunShaftsFacts {
    Mat4 viewProjection = Mat4::Identity(); // Eye-relative: fx::View::relative.viewProjection.
    Vec3 sunDirection = Vec3::Up();
    Vec3 sunColor = Vec3::One();
    std::uint64_t frameIndex = 0;
    std::uint32_t viewWidth = 1;
    std::uint32_t viewHeight = 1;
    bool historyValid = true;
};

// Builds the 80-byte parameter block of VolumetricCloudSunShafts.comp; outputs are always finite.
VolumetricCloudSunShaftsParamsGpu MakeCloudSunShaftsParams(
    const VolumetricCloudSettings& settings, const VolumetricCloudSunShaftsFacts& facts) noexcept;

} // namespace gte
