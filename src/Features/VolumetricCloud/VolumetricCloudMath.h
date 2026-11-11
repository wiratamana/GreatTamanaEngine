#pragma once

#include "VolumetricCloudTypes.h"
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

// Sun light reaching a sample: the sum over octaves of b^i * phase_i * exp(-tau * a^i), plus the silver lobe.
float CloudSunScatter(const VolumetricCloudSettings& safe, float cosTheta, float opticalDepth) noexcept;

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

// The march resolution divisor in use: the setting rounded to 1, 2 or 4.
std::uint32_t CloudResolutionDivisor(const VolumetricCloudSettings& safe) noexcept;

// Pixel inside an n x n block that the march samples on `frameIndex`; n*n frames visit every pixel once,
// in a Bayer order so that consecutive frames land far apart.
struct CloudSampleOffset {
    std::uint32_t x = 0;
    std::uint32_t y = 0;
};
CloudSampleOffset CloudBayerOffset(std::uint64_t frameIndex, std::uint32_t divisor) noexcept;

// How often the pixel marched on `frameIndex` was marched before. A pixel is marched once per divisor * divisor
// frames, so its own samples are numbered by this index, wrapped at 256.
std::uint32_t CloudOwnSampleIndex(std::uint64_t frameIndex, std::uint32_t divisor) noexcept;

// Start jitter in [0, 1) of a marched pixel: the blue noise value moved by the golden ratio per own sample.
float CloudMarchJitter(float blueNoise, std::uint64_t frameIndex, std::uint32_t divisor) noexcept;

// Previous-frame data of the resolve pass.
struct VolumetricCloudResolveFacts {
    Mat4 prevViewProjection = Mat4::Identity(); // Last frame's view-projection, eye-relative to THIS frame's eye.
    float windDelta[2] = {}; // Wind offset now minus last frame (m).
    bool historyValid = false;
    std::uint32_t lowWidth = 1;
    std::uint32_t lowHeight = 1;
};

VolumetricCloudResolveParamsGpu MakeVolumetricCloudResolveParams(const VolumetricCloudSettings& safe,
    const VolumetricCloudFrameFacts& facts, const VolumetricCloudResolveFacts& resolve) noexcept;

// CPU twin of the tap test in VolumetricCloudResolve.comp: may a march result be used at a pixel whose scene
// lies `pixelEndKm` away? `tapEndKm` is how far that march looked, `tapDistanceKm` where its cloud sits.
bool CloudTapUsable(float tapEndKm, float tapDistanceKm, float tapTransmittance, float pixelEndKm) noexcept;

// Final colour of a cloud pixel seen through air. `cloudLight` is the cloud's own light, `airTransmittance` and
// `airLight` the air between the eye and the cloud, `cloudTransmittance` what the cloud lets through of the
// scene behind it. A scene that already holds the air light of the same distance (the sky) comes back
// unchanged where the cloud lets everything through.
float CloudAerialComposite(float scene, float cloudTransmittance, float cloudLight, float airTransmittance,
    float airLight) noexcept;

// Distance that stands for "no surface", in km.
inline constexpr float kCloudNoSurfaceKm = 1000.0f;

} // namespace gte
